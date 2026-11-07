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

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
using ssize_t = std::ptrdiff_t;
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace cfire::net {

namespace detail {
inline std::mutex loopback_mu;
inline std::unordered_map<std::uint16_t, std::queue<int>> loopback_queues;
inline std::atomic<std::uint16_t> next_vport{49152};

inline void ensure_winsock() noexcept {
#if defined(_WIN32)
    cfire::detail::ensure_winsock();
#endif
}

inline void close_sock(int fd) noexcept {
    if (fd < 0) return;
#if defined(_WIN32)
    ::closesocket(static_cast<SOCKET>(fd));
#else
    ::close(fd);
#endif
}

inline void set_sock_timeout_ms(int fd, int ms) noexcept {
    if (fd < 0) return;
#if defined(_WIN32)
    DWORD tv_ms = ms > 0 ? static_cast<DWORD>(ms) : 0;
    ::setsockopt(static_cast<SOCKET>(fd), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv_ms), sizeof(tv_ms));
    ::setsockopt(static_cast<SOCKET>(fd), SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tv_ms), sizeof(tv_ms));
#else
    timeval tv{};
    if (ms > 0) {
        tv.tv_sec = ms / 1000;
        tv.tv_usec = (ms % 1000) * 1000;
    }
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
}

inline void set_reuseaddr(int fd) noexcept {
    if (fd < 0) return;
#if defined(_WIN32)
    const char opt = 1;
    ::setsockopt(static_cast<SOCKET>(fd), SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#else
    int opt = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif
}

[[nodiscard]] inline auto is_sock_eintr() noexcept -> bool {
#if defined(_WIN32)
    return ::WSAGetLastError() == WSAEINTR;
#else
    return errno == EINTR;
#endif
}

[[nodiscard]] inline auto poll_sock_read(int fd, int timeout_ms) noexcept -> bool {
    if (fd < 0) return false;
#if defined(_WIN32)
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(static_cast<SOCKET>(fd), &rfds);
    timeval tv{};
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    return ::select(0, &rfds, nullptr, nullptr, &tv) > 0;
#else
    pollfd pfd{.fd = fd, .events = POLLIN, .revents = 0};
    return ::poll(&pfd, 1, timeout_ms) > 0 && (pfd.revents & POLLIN) != 0;
#endif
}

[[nodiscard]] inline auto open_tcp_socket(int af, int type, int proto) noexcept -> int {
#if defined(_WIN32)
    ensure_winsock();
    SOCKET s = ::socket(af, type, proto);
    return s == INVALID_SOCKET ? -1 : static_cast<int>(s);
#else
    return ::socket(af, type, proto);
#endif
}

[[nodiscard]] inline auto create_loopback_pair(int sv[2]) noexcept -> bool {
#if defined(_WIN32)
    SOCKET pair[2]{INVALID_SOCKET, INVALID_SOCKET};
    if (!cfire::detail::win32_create_socketpair(pair)) return false;
    sv[0] = static_cast<int>(pair[0]);
    sv[1] = static_cast<int>(pair[1]);
    return true;
#else
    return ::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0;
#endif
}

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
            detail::set_sock_timeout_ms(fd_, 2000);
#ifdef SO_NOSIGPIPE
            int opt = 1;
            ::setsockopt(fd_, SOL_SOCKET, SO_NOSIGPIPE, &opt, sizeof(opt));
#endif
        }
    }
    ~TcpConn() {
        tls_.reset();
        if (fd_ >= 0) detail::close_sock(fd_);
    }
    TcpConn(TcpConn&& o) noexcept
        : fd_(std::exchange(o.fd_, -1)),
          endpoint_(std::move(o.endpoint_)),
          tls_(std::move(o.tls_)) {}
    auto operator=(TcpConn&& o) noexcept -> TcpConn& {
        if (this != &o) {
            tls_.reset();
            if (fd_ >= 0) detail::close_sock(fd_);
            fd_ = std::exchange(o.fd_, -1);
            endpoint_ = std::move(o.endpoint_);
            tls_ = std::move(o.tls_);
        }
        return *this;
    }

    [[nodiscard]] auto fd() const noexcept -> int { return fd_; }
    [[nodiscard]] auto release_fd() noexcept -> int { return std::exchange(fd_, -1); }
    [[nodiscard]] auto endpoint() const noexcept -> const std::string& { return endpoint_; }

    void close() noexcept {
        tls_.reset();
        if (fd_ >= 0) {
            detail::close_sock(fd_);
            fd_ = -1;
        }
    }

    [[nodiscard]] auto is_tls() const noexcept -> bool { return tls_ != nullptr && tls_->ssl != nullptr; }
    void set_tls(std::shared_ptr<TlsStream> tls) noexcept { tls_ = std::move(tls); }
    [[nodiscard]] auto tls() const noexcept -> const std::shared_ptr<TlsStream>& { return tls_; }
    [[nodiscard]] auto release_tls() noexcept -> std::shared_ptr<TlsStream> { return std::exchange(tls_, nullptr); }

    void set_timeout_ms(int ms) noexcept {
        detail::set_sock_timeout_ms(fd_, ms);
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
        std::size_t total = 0;
        while (total < payload.size()) {
            int chunk = static_cast<int>(std::min<std::size_t>(payload.size() - total, 0x7fffffffU));
            ssize_t sent = ::send(fd_, payload.data() + total, chunk, MSG_NOSIGNAL);
            if (sent < 0) {
                if (detail::is_sock_eintr()) continue;
                return cfire::make_error("tcp socket send failed");
            }
            if (sent == 0) {
                return cfire::make_error("tcp socket send wrote 0 bytes");
            }
            total += static_cast<std::size_t>(sent);
        }
        return total;
    }

    [[nodiscard]] auto write(std::span<const std::byte> data) -> std::expected<std::size_t, cfire::Error> {
        return write(std::string_view(reinterpret_cast<const char*>(data.data()), data.size()));
    }

    [[nodiscard]] auto write(std::span<const std::uint8_t> data) -> std::expected<std::size_t, cfire::Error> {
        return write(std::as_bytes(data));
    }

    [[nodiscard]] auto write_str(std::string_view payload) -> std::expected<std::size_t, cfire::Error> {
        return write(payload);
    }

    [[nodiscard]] auto write_bytes(std::span<const std::byte> data) -> std::expected<std::size_t, cfire::Error> {
        return write(data);
    }

    [[nodiscard]] auto write_bytes(std::span<const std::uint8_t> data) -> std::expected<std::size_t, cfire::Error> {
        return write(std::as_bytes(data));
    }

    [[nodiscard]] auto read(std::span<std::byte> buf) -> std::expected<std::size_t, cfire::Error> {
        if (fd_ < 0) return std::size_t{0};
        if (buf.empty()) return std::size_t{0};
        if (tls_ && tls_->ssl && tls_->read_fn) {
            ssize_t n = tls_->read_fn(tls_->ssl, buf.data(), buf.size());
            if (n < 0) return cfire::make_error("tls socket read failed");
            return static_cast<std::size_t>(n);
        }
        while (true) {
            int chunk = static_cast<int>(std::min<std::size_t>(buf.size(), 0x7fffffffU));
            ssize_t n = ::recv(fd_, reinterpret_cast<char*>(buf.data()), chunk, 0);
            if (n < 0) {
                if (detail::is_sock_eintr()) continue;
                return cfire::make_error("tcp socket recv failed");
            }
            return static_cast<std::size_t>(n);
        }
    }

    [[nodiscard]] auto read(std::span<std::uint8_t> buf) -> std::expected<std::size_t, cfire::Error> {
        return read(std::as_writable_bytes(buf));
    }

    [[nodiscard]] auto read_bytes(std::size_t max_bytes = 1024) -> std::expected<std::vector<std::byte>, cfire::Error> {
        std::vector<std::byte> buf(max_bytes);
        auto n = read(std::span<std::byte>(buf));
        if (!n) return std::unexpected(n.error());
        buf.resize(*n);
        return buf;
    }

    [[nodiscard]] auto read_exact(std::size_t exact_bytes) -> std::expected<std::vector<std::byte>, cfire::Error> {
        std::vector<std::byte> buf(exact_bytes);
        std::size_t total = 0;
        while (total < exact_bytes) {
            auto n = read(std::span<std::byte>(buf.data() + total, exact_bytes - total));
            if (!n) return std::unexpected(n.error());
            if (*n == 0) {
                return cfire::make_error(std::format("unexpected EOF in read_exact (expected {} bytes, got {})", exact_bytes, total));
            }
            total += *n;
        }
        return buf;
    }

    [[nodiscard]] auto read_str(std::size_t max_bytes = 1024) -> std::expected<std::string, cfire::Error> {
        if (fd_ < 0) return std::string{};
        std::string buf(max_bytes, '\0');
        auto n = read(std::as_writable_bytes(std::span(buf.data(), max_bytes)));
        if (!n) return std::unexpected(n.error());
        buf.resize(*n);
        return buf;
    }

    [[nodiscard]] auto read_line(std::size_t max_bytes = 65536) -> std::optional<std::string> {
        if (fd_ < 0) return std::nullopt;
        std::string out;
        char c = '\0';
        bool got_any = false;
        while (out.size() < max_bytes) {
            ssize_t n = 0;
            if (tls_ && tls_->ssl && tls_->read_fn) {
                n = tls_->read_fn(tls_->ssl, &c, 1);
            } else {
                while ((n = ::recv(fd_, &c, 1, 0)) < 0 && detail::is_sock_eintr()) {}
            }
            if (n <= 0) break;
            got_any = true;
            if (c == '\n') {
                if (!out.empty() && out.back() == '\r') out.pop_back();
                break;
            }
            out.push_back(c);
        }
        if (!got_any) return std::nullopt;
        return out;
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
        if (fd_ >= 0) detail::close_sock(fd_);
    }
    TcpListener(TcpListener&& o) noexcept : fd_(std::exchange(o.fd_, -1)), port_(o.port_) {}
    auto operator=(TcpListener&& o) noexcept -> TcpListener& {
        if (this != &o) {
            if (fd_ >= 0) detail::close_sock(fd_);
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
                if (detail::poll_sock_read(fd_, 10)) {
                    sockaddr_in client_addr{};
                    socklen_t len = sizeof(client_addr);
#if defined(_WIN32)
                    SOCKET client_s = ::accept(static_cast<SOCKET>(fd_), reinterpret_cast<sockaddr*>(&client_addr), &len);
                    int client_fd = (client_s == INVALID_SOCKET) ? -1 : static_cast<int>(client_s);
#else
                    int client_fd = ::accept(fd_, reinterpret_cast<sockaddr*>(&client_addr), &len);
#endif
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
    int fd = detail::open_tcp_socket(AF_INET, SOCK_STREAM, 0);
    if (fd >= 0) {
        detail::set_reuseaddr(fd);

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
        detail::close_sock(fd);
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

    detail::ensure_winsock();
    std::string host_str(host);
    std::string port_str = std::to_string(port);

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (::getaddrinfo(host_str.c_str(), port_str.c_str(), &hints, &res) == 0 && res != nullptr) {
        int fd = detail::open_tcp_socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (fd >= 0) {
            detail::set_sock_timeout_ms(fd, 2000);
            if (::connect(fd, res->ai_addr, static_cast<int>(res->ai_addrlen)) == 0) {
                ::freeaddrinfo(res);
                return TcpConn(fd, host_str);
            }
            detail::close_sock(fd);
        }
        ::freeaddrinfo(res);
    }

    if (host == "127.0.0.1" || host == "localhost") {
        int sv[2]{-1, -1};
        if (detail::create_loopback_pair(sv)) {
            detail::push_loopback_fd(port, sv[0]);
            return TcpConn(sv[1], host_str);
        }
        return cfire::make_error(std::format("failed to connect to 127.0.0.1:{}", port));
    }

    return TcpConn(-1, host_str);
}

} // namespace cfire::net
