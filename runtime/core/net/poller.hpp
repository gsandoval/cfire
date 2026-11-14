#pragma once
// CF-193: readiness polling for the HTTP server's event loop: kqueue on macOS, epoll on Linux, WSAPoll on
// Windows (`poll()` on other targets such as Emscripten), all level-triggered. One thread owns a Poller (add / modify / remove / wait); `wake()` may be
// called from any thread and makes a pending `wait` return.
#include "tcp.hpp"

#include <cstdint>
#include <vector>

#if defined(__APPLE__)
#include <sys/event.h>
#elif defined(__linux__)
#include <sys/epoll.h>
#elif !defined(_WIN32)
#include <poll.h>
#endif

namespace cfire::net::detail {

#if !defined(__APPLE__) && !defined(__linux__)
#if defined(_WIN32)
using PollFd = WSAPOLLFD;
inline auto poll_fds(PollFd* fds, std::size_t n, int timeout_ms) -> int { return ::WSAPoll(fds, static_cast<ULONG>(n), timeout_ms); }
#else
using PollFd = pollfd;
using SOCKET = int;
using SHORT = short;
inline auto poll_fds(PollFd* fds, std::size_t n, int timeout_ms) -> int { return ::poll(fds, static_cast<nfds_t>(n), timeout_ms); }
#endif
#endif

struct PollEvent {
    std::uint64_t token = 0;
    bool readable = false;
    bool writable = false;
};

class Poller {
public:
    Poller() {
        int sv[2]{-1, -1};
        if (!create_loopback_pair(sv)) return;
        wake_rd_ = sv[0];
        wake_wr_ = sv[1];
        set_nonblocking(wake_rd_, true);
        set_nonblocking(wake_wr_, true);
#if defined(__APPLE__)
        backend_ = ::kqueue();
#elif defined(__linux__)
        backend_ = ::epoll_create1(EPOLL_CLOEXEC);
#else
        backend_ = 0;
#endif
        if (backend_ >= 0) add(wake_rd_, kWakeToken, false);
    }
    ~Poller() {
#if defined(__APPLE__) || defined(__linux__)
        if (backend_ >= 0) ::close(backend_);
#endif
        close_sock(wake_rd_);
        close_sock(wake_wr_);
    }
    Poller(const Poller&) = delete;
    auto operator=(const Poller&) -> Poller& = delete;

    [[nodiscard]] auto valid() const noexcept -> bool { return backend_ >= 0 && wake_rd_ >= 0; }

    // Watches `fd` for reading, or for writing when `want_write`.
    void add(int fd, std::uint64_t token, bool want_write) {
#if defined(__APPLE__)
        struct kevent ev{};
        EV_SET(&ev, static_cast<uintptr_t>(fd), want_write ? EVFILT_WRITE : EVFILT_READ, EV_ADD, 0, 0,
               reinterpret_cast<void*>(static_cast<uintptr_t>(token)));
        (void)::kevent(backend_, &ev, 1, nullptr, 0, nullptr);
        filters_[fd] = want_write;
#elif defined(__linux__)
        epoll_event ev{};
        ev.events = want_write ? EPOLLOUT : EPOLLIN;
        ev.data.u64 = token;
        (void)::epoll_ctl(backend_, EPOLL_CTL_ADD, fd, &ev);
#else
        index_[fd] = fds_.size();
        fds_.push_back(PollFd{static_cast<SOCKET>(fd), static_cast<SHORT>(want_write ? POLLWRNORM : POLLRDNORM), 0});
        tokens_.push_back(token);
#endif
    }

    void modify(int fd, std::uint64_t token, bool want_write) {
#if defined(__APPLE__)
        auto it = filters_.find(fd);
        if (it != filters_.end() && it->second == want_write) return;
        remove(fd);
        add(fd, token, want_write);
#elif defined(__linux__)
        epoll_event ev{};
        ev.events = want_write ? EPOLLOUT : EPOLLIN;
        ev.data.u64 = token;
        (void)::epoll_ctl(backend_, EPOLL_CTL_MOD, fd, &ev);
#else
        auto it = index_.find(fd);
        if (it == index_.end()) return;
        fds_[it->second].events = static_cast<SHORT>(want_write ? POLLWRNORM : POLLRDNORM);
        tokens_[it->second] = token;
#endif
    }

    // Stops watching `fd`; call it before the socket is closed or handed to another thread.
    void remove(int fd) {
#if defined(__APPLE__)
        auto it = filters_.find(fd);
        if (it == filters_.end()) return;
        struct kevent ev{};
        EV_SET(&ev, static_cast<uintptr_t>(fd), it->second ? EVFILT_WRITE : EVFILT_READ, EV_DELETE, 0, 0, nullptr);
        (void)::kevent(backend_, &ev, 1, nullptr, 0, nullptr);
        filters_.erase(it);
#elif defined(__linux__)
        (void)::epoll_ctl(backend_, EPOLL_CTL_DEL, fd, nullptr);
#else
        auto it = index_.find(fd);
        if (it == index_.end()) return;
        const std::size_t i = it->second;
        index_.erase(it);
        if (i + 1 != fds_.size()) {
            fds_[i] = fds_.back();
            tokens_[i] = tokens_.back();
            index_[static_cast<int>(fds_[i].fd)] = i;
        }
        fds_.pop_back();
        tokens_.pop_back();
#endif
    }

    // Waits up to `timeout_ms` (-1: until something is ready or `wake()`) and returns the ready sockets.
    [[nodiscard]] auto wait(int timeout_ms) -> const std::vector<PollEvent>& {
        ready_.clear();
#if defined(__APPLE__)
        struct kevent evs[256];
        timespec ts{timeout_ms / 1000, static_cast<long>(timeout_ms % 1000) * 1000000L};
        int n = ::kevent(backend_, nullptr, 0, evs, 256, timeout_ms < 0 ? nullptr : &ts);
        for (int i = 0; i < n; ++i) {
            const auto token = static_cast<std::uint64_t>(reinterpret_cast<uintptr_t>(evs[i].udata));
            push(token, evs[i].filter == EVFILT_READ, evs[i].filter == EVFILT_WRITE);
        }
#elif defined(__linux__)
        epoll_event evs[256];
        int n = ::epoll_wait(backend_, evs, 256, timeout_ms);
        for (int i = 0; i < n; ++i) {
            const bool err = (evs[i].events & (EPOLLERR | EPOLLHUP)) != 0;
            push(evs[i].data.u64, err || (evs[i].events & EPOLLIN) != 0, err || (evs[i].events & EPOLLOUT) != 0);
        }
#else
        int n = poll_fds(fds_.data(), fds_.size(), timeout_ms);
        for (std::size_t i = 0; n > 0 && i < fds_.size(); ++i) {
            const SHORT re = fds_[i].revents;
            if (re == 0) continue;
            const bool err = (re & (POLLERR | POLLHUP | POLLNVAL)) != 0;
            push(tokens_[i], err || (re & POLLRDNORM) != 0, err || (re & POLLWRNORM) != 0);
        }
#endif
        return ready_;
    }

    void wake() noexcept {
        const char b = 1;
        (void)::send(wake_wr_, &b, 1, MSG_NOSIGNAL);
    }

private:
    static constexpr std::uint64_t kWakeToken = ~std::uint64_t{0};

    void push(std::uint64_t token, bool readable, bool writable) {
        if (token == kWakeToken) {
            char buf[64];
            while (::recv(wake_rd_, buf, sizeof(buf), 0) > 0) {}
            return;
        }
        ready_.push_back(PollEvent{token, readable, writable});
    }

    int backend_ = -1;
    int wake_rd_ = -1;
    int wake_wr_ = -1;
    std::vector<PollEvent> ready_;
#if defined(__APPLE__)
    std::unordered_map<int, bool> filters_;  // fd -> watching for writes
#elif !defined(__linux__)
    std::vector<PollFd> fds_;
    std::vector<std::uint64_t> tokens_;
    std::unordered_map<int, std::size_t> index_;
#endif
};

} // namespace cfire::net::detail
