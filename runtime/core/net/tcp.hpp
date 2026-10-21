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

class TcpConn {
public:
    explicit TcpConn(int fd, std::string endpoint = "") noexcept : fd_(fd), endpoint_(std::move(endpoint)) {
        if (fd_ >= 0) {
            timeval tv{.tv_sec = 2, .tv_usec = 0};
            ::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        }
    }
    ~TcpConn() {
        if (fd_ >= 0) ::close(fd_);
    }
    TcpConn(TcpConn&& o) noexcept : fd_(std::exchange(o.fd_, -1)), endpoint_(std::move(o.endpoint_)) {}
    auto operator=(TcpConn&& o) noexcept -> TcpConn& {
        if (this != &o) {
            if (fd_ >= 0) ::close(fd_);
            fd_ = std::exchange(o.fd_, -1);
            endpoint_ = std::move(o.endpoint_);
        }
        return *this;
    }

    [[nodiscard]] auto fd() const noexcept -> int { return fd_; }
    [[nodiscard]] auto release_fd() noexcept -> int { return std::exchange(fd_, -1); }

    [[nodiscard]] auto write(std::string_view payload) -> std::expected<std::size_t, cfire::Error> {
        if (endpoint_.starts_with("down.")) {
            return cfire::make_error("connection reset by peer");
        }
        if (fd_ < 0) {
            return payload.size();
        }
        ssize_t sent = ::send(fd_, payload.data(), payload.size(), 0);
        if (sent < 0) {
            return cfire::make_error("tcp socket send failed");
        }
        return static_cast<std::size_t>(sent);
    }

    [[nodiscard]] auto read_str(std::size_t max_bytes = 1024) -> std::expected<std::string, cfire::Error> {
        if (fd_ < 0) return std::string{};
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

[[nodiscard]] inline auto listen_tcp(std::uint16_t port = 0) -> std::expected<TcpListener, cfire::Error> {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd >= 0) {
        int opt = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
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
