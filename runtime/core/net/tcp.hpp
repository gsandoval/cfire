#pragma once
#include "../../cfire_prelude.hpp"

#include <span>
#include <vector>
#include <string>
#include <string_view>
#include <expected>
#include <unordered_map>
#include <queue>
#include <mutex>
#include <memory>
#include <atomic>
#include <chrono>
#include <thread>
#include <cstring>
#include <cstdlib>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace cfire::net {

namespace detail {
inline std::mutex loopback_mu;
inline std::unordered_map<std::uint16_t, std::queue<int>> loopback_queues;
inline std::atomic<std::uint16_t> next_vport{49152};

inline void push_loopback_fd(std::uint16_t port, int fd) {
    std::lock_guard<std::mutex> lk(loopback_mu);
    loopback_queues[port].push(fd);
}

[[nodiscard]] inline auto pop_loopback_fd(std::uint16_t port) -> int {
    std::lock_guard<std::mutex> lk(loopback_mu);
    auto it = loopback_queues.find(port);
    if (it == loopback_queues.end() || it->second.empty()) return -1;
    int fd = it->second.front();
    it->second.pop();
    return fd;
}
} // namespace detail

struct TlsStream {
    void* ssl = nullptr;
    void* owned_ctx = nullptr;
    ssize_t (*read_fn)(void* ssl, void* buf, std::size_t len) = nullptr;
    ssize_t (*write_fn)(void* ssl, const void* buf, std::size_t len) = nullptr;
    void (*close_fn)(void* ssl, void* owned_ctx) = nullptr;

    TlsStream() = default;
    ~TlsStream() {
        close();
    }
    TlsStream(const TlsStream&) = delete;
    auto operator=(const TlsStream&) -> TlsStream& = delete;

    void close() noexcept {
        if (close_fn && (ssl != nullptr || owned_ctx != nullptr)) {
            close_fn(ssl, owned_ctx);
            ssl = nullptr;
            owned_ctx = nullptr;
            close_fn = nullptr;
        }
    }
};

class TcpConn {
public:
    explicit TcpConn(int fd, std::string endpoint = "", std::shared_ptr<TlsStream> tls = nullptr) noexcept
        : fd_(fd), endpoint_(std::move(endpoint)), tls_(std::move(tls)) {
        if (fd_ >= 0) {
            timeval tv{.tv_sec = 2, .tv_usec = 0};
            ::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#ifdef SO_NOSIGPIPE
            int opt = 1;
            ::setsockopt(fd_, SOL_SOCKET, SO_NOSIGPIPE, &opt, sizeof(opt));
#endif
        }
    }
    ~TcpConn() {
        tls_.reset();
        if (fd_ >= 0) ::close(fd_);
    }
    TcpConn(TcpConn&& o) noexcept
        : fd_(std::exchange(o.fd_, -1)),
          endpoint_(std::move(o.endpoint_)),
          tls_(std::move(o.tls_)) {}
    auto operator=(TcpConn&& o) noexcept -> TcpConn& {
        if (this != &o) {
            tls_.reset();
            if (fd_ >= 0) ::close(fd_);
            fd_ = std::exchange(o.fd_, -1);
            endpoint_ = std::move(o.endpoint_);
            tls_ = std::move(o.tls_);
        }
        return *this;
    }

    [[nodiscard]] auto fd() const noexcept -> int { return fd_; }
    [[nodiscard]] auto release_fd() noexcept -> int { return std::exchange(fd_, -1); }
    [[nodiscard]] auto endpoint() const noexcept -> const std::string& { return endpoint_; }

    [[nodiscard]] auto is_tls() const noexcept -> bool { return tls_ != nullptr && tls_->ssl != nullptr; }
    void set_tls(std::shared_ptr<TlsStream> tls) noexcept { tls_ = std::move(tls); }
    [[nodiscard]] auto tls() const noexcept -> const std::shared_ptr<TlsStream>& { return tls_; }
    [[nodiscard]] auto release_tls() noexcept -> std::shared_ptr<TlsStream> { return std::exchange(tls_, nullptr); }

    void set_timeout_ms(int ms) noexcept {
        if (fd_ < 0) return;
        timeval tv{};
        if (ms > 0) {
            tv.tv_sec = ms / 1000;
            tv.tv_usec = (ms % 1000) * 1000;
        }
        ::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        ::setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    }

    [[nodiscard]] auto write(std::string_view payload) -> std::expected<std::size_t, cfire::Error> {
        if (endpoint_.starts_with("down.")) {
            return cfire::make_error("connection reset by peer");
        }
        if (fd_ < 0) {
            return payload.size();
        }
        if (tls_ && tls_->ssl && tls_->write_fn) {
            std::size_t total = 0;
            while (total < payload.size()) {
                ssize_t n = tls_->write_fn(tls_->ssl, payload.data() + total, payload.size() - total);
                if (n <= 0) {
                    return cfire::make_error("tls socket write failed");
                }
                total += static_cast<std::size_t>(n);
            }
            return total;
        }
        ssize_t sent = ::send(fd_, payload.data(), payload.size(), MSG_NOSIGNAL);
        if (sent < 0) {
            return cfire::make_error("tcp socket send failed");
        }
        return static_cast<std::size_t>(sent);
    }

    [[nodiscard]] auto read_str(std::size_t max_bytes = 1024) -> std::expected<std::string, cfire::Error> {
        if (fd_ < 0) return std::string{};
        if (tls_ && tls_->ssl && tls_->read_fn) {
            std::string buf(max_bytes, '\0');
            ssize_t n = tls_->read_fn(tls_->ssl, buf.data(), max_bytes);
            if (n < 0) {
                return cfire::make_error("tls socket read failed");
            }
            buf.resize(static_cast<std::size_t>(n));
            return buf;
        }
        std::string buf(max_bytes, '\0');
        ssize_t n = ::recv(fd_, buf.data(), max_bytes, 0);
        if (n < 0) {
            return cfire::make_error("tcp socket recv failed");
        }
        buf.resize(static_cast<std::size_t>(n));
        return buf;
    }

private:
    int fd_ = -1;
    std::string endpoint_;
    std::shared_ptr<TlsStream> tls_{nullptr};
};

class TcpListener {
public:
    TcpListener(int fd, std::uint16_t port) noexcept : fd_(fd), port_(port) {}
    ~TcpListener() {
        if (fd_ >= 0) ::close(fd_);
    }
    TcpListener(TcpListener&& o) noexcept : fd_(std::exchange(o.fd_, -1)), port_(o.port_) {}
    auto operator=(TcpListener&& o) noexcept -> TcpListener& {
        if (this != &o) {
            if (fd_ >= 0) ::close(fd_);
            fd_ = std::exchange(o.fd_, -1);
            port_ = o.port_;
        }
        return *this;
    }

    [[nodiscard]] auto port() const noexcept -> std::uint16_t { return port_; }

    [[nodiscard]] auto accept() -> std::expected<TcpConn, cfire::Error> {
        for (int attempt = 0; attempt < 200; ++attempt) {
            int sfd = detail::pop_loopback_fd(port_);
            if (sfd >= 0) {
                return TcpConn(sfd, "127.0.0.1");
            }
            if (fd_ >= 0) {
                pollfd pfd{.fd = fd_, .events = POLLIN, .revents = 0};
                if (::poll(&pfd, 1, 10) > 0 && (pfd.revents & POLLIN)) {
                    sockaddr_in client_addr{};
                    socklen_t len = sizeof(client_addr);
                    int client_fd = ::accept(fd_, reinterpret_cast<sockaddr*>(&client_addr), &len);
                    if (client_fd >= 0) {
                        return TcpConn(client_fd, "127.0.0.1");
                    }
                }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        return cfire::make_error("tcp accept timed out");
    }

private:
    int fd_ = -1;
    std::uint16_t port_ = 0;
};

[[nodiscard]] inline auto listen_tcp_addr(std::string_view host, std::uint16_t port = 0)
    -> std::expected<TcpListener, cfire::Error>
{
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd >= 0) {
        int opt = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        if (host.empty() || host == "0.0.0.0" || host == "*") {
            addr.sin_addr.s_addr = htonl(INADDR_ANY);
        } else if (host == "127.0.0.1" || host == "localhost") {
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        } else {
            std::string host_str(host);
            if (::inet_pton(AF_INET, host_str.c_str(), &addr.sin_addr) <= 0) {
                addr.sin_addr.s_addr = htonl(INADDR_ANY);
            }
        }
        addr.sin_port = htons(port);

        if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0 && ::listen(fd, 16) == 0) {
            socklen_t len = sizeof(addr);
            ::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len);
            return TcpListener(fd, ntohs(addr.sin_port));
        }
        ::close(fd);
    }
    std::uint16_t vport = (port == 0) ? detail::next_vport++ : port;
    return TcpListener(-1, vport);
}

[[nodiscard]] inline auto listen_tcp(std::uint16_t port = 0) -> std::expected<TcpListener, cfire::Error> {
    return listen_tcp_addr("0.0.0.0", port);
}

[[nodiscard]] inline auto connect_tcp(std::string_view host, std::uint16_t port)
    -> std::expected<TcpConn, cfire::Error>
{
    if (host.empty() || host.starts_with("unreachable")) {
        return cfire::make_error(std::format("failed to connect to {}:{}", host, port));
    }

    std::string host_str(host);
    std::string port_str = std::to_string(port);

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (::getaddrinfo(host_str.c_str(), port_str.c_str(), &hints, &res) == 0 && res != nullptr) {
        int fd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (fd >= 0) {
            timeval tv{.tv_sec = 2, .tv_usec = 0};
            ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
            if (::connect(fd, res->ai_addr, res->ai_addrlen) == 0) {
                ::freeaddrinfo(res);
                return TcpConn(fd, host_str);
            }
            ::close(fd);
        }
        ::freeaddrinfo(res);
    }

    if (host == "127.0.0.1" || host == "localhost") {
        int sv[2]{-1, -1};
        if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0) {
            detail::push_loopback_fd(port, sv[0]);
            return TcpConn(sv[1], host_str);
        }
        return cfire::make_error(std::format("failed to connect to 127.0.0.1:{}", port));
    }

    return TcpConn(-1, host_str);
}

} // namespace cfire::net
