#pragma once
#include "../../cfire_prelude.hpp"

#include <span>
#include <vector>
#include <string>
#include <string_view>
#include <expected>
#include <tuple>
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

inline void shutdown_sock(int fd) noexcept {
    if (fd < 0) return;
#if defined(_WIN32)
    ::shutdown(static_cast<SOCKET>(fd), SD_BOTH);
#else
    ::shutdown(fd, SHUT_RDWR);
#endif
}

// CF-192: lets another thread shut a socket down (to end a blocked read) without racing its owner's close:
// the owner clears `fd` under `mu` before closing the socket, so a reused descriptor is never touched.
struct CloseGuard {
    std::mutex mu;
    int fd = -1;

    void shutdown() noexcept {
        std::lock_guard<std::mutex> lk(mu);
        shutdown_sock(fd);
    }
    void close(int sock) noexcept {
        std::lock_guard<std::mutex> lk(mu);
        if (fd == sock) fd = -1;
        close_sock(sock);
    }
};

// Closes `sock`, through `guard` when it has one.
inline void close_guarded(int sock, const std::shared_ptr<CloseGuard>& guard) noexcept {
    if (guard) guard->close(sock);
    else close_sock(sock);
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

// CF-180: an IPv6 listener that also accepts IPv4 (as ::ffff:a.b.c.d) when the OS allows it.
inline void set_dual_stack(int fd) noexcept {
    if (fd < 0) return;
#if defined(_WIN32)
    const char off = 0;
    ::setsockopt(static_cast<SOCKET>(fd), IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off));
#else
    int off = 0;
    ::setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off));
#endif
}

// Peer address text for an accepted socket; IPv4-mapped IPv6 addresses are reported as plain IPv4.
[[nodiscard]] inline auto peer_address_text(const sockaddr_storage& ss) -> std::string {
    char ip[INET6_ADDRSTRLEN] = {0};
    if (ss.ss_family == AF_INET) {
        const auto* in4 = reinterpret_cast<const sockaddr_in*>(&ss);
        if (::inet_ntop(AF_INET, &in4->sin_addr, ip, sizeof(ip))) return ip;
        return {};
    }
    if (ss.ss_family == AF_INET6) {
        const auto* in6 = reinterpret_cast<const sockaddr_in6*>(&ss);
        if (::inet_ntop(AF_INET6, &in6->sin6_addr, ip, sizeof(ip))) {
            std::string text(ip);
            if (text.starts_with("::ffff:") && text.find('.') != std::string::npos) text.erase(0, 7);
            return text;
        }
    }
    return {};
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

inline void register_loopback_port(std::uint16_t port) {
    std::lock_guard<std::mutex> lk(loopback_mu);
    (void)loopback_queues[port];
}

inline void unregister_loopback_port(std::uint16_t port) {
    std::lock_guard<std::mutex> lk(loopback_mu);
    auto it = loopback_queues.find(port);
    if (it != loopback_queues.end()) {
        while (!it->second.empty()) {
            close_sock(it->second.front());
            it->second.pop();
        }
        loopback_queues.erase(it);
    }
}

[[nodiscard]] inline auto has_loopback_listener(std::uint16_t port) -> bool {
    std::lock_guard<std::mutex> lk(loopback_mu);
    return loopback_queues.contains(port);
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
        if (fd_ >= 0) detail::close_guarded(fd_, guard_);
    }
    TcpConn(TcpConn&& o) noexcept
        : fd_(std::exchange(o.fd_, -1)),
          endpoint_(std::move(o.endpoint_)),
          tls_(std::move(o.tls_)),
          guard_(std::move(o.guard_)) {}
    auto operator=(TcpConn&& o) noexcept -> TcpConn& {
        if (this != &o) {
            tls_.reset();
            if (fd_ >= 0) detail::close_guarded(fd_, guard_);
            fd_ = std::exchange(o.fd_, -1);
            endpoint_ = std::move(o.endpoint_);
            tls_ = std::move(o.tls_);
            guard_ = std::move(o.guard_);
        }
        return *this;
    }

    [[nodiscard]] auto fd() const noexcept -> std::int32_t { return static_cast<std::int32_t>(fd_); }
    [[nodiscard]] auto is_open() const noexcept -> bool { return fd_ >= 0; }
    [[nodiscard]] auto release_fd() noexcept -> int { return std::exchange(fd_, -1); }
    // CF-192: a guard another thread can use to shut this socket down; it moves with `release_close_guard()`.
    [[nodiscard]] auto close_guard() -> std::shared_ptr<detail::CloseGuard> {
        if (!guard_) {
            guard_ = std::make_shared<detail::CloseGuard>();
            guard_->fd = fd_;
        }
        return guard_;
    }
    [[nodiscard]] auto release_close_guard() noexcept -> std::shared_ptr<detail::CloseGuard> { return std::exchange(guard_, nullptr); }
    [[nodiscard]] auto endpoint() const noexcept -> const std::string& { return endpoint_; }
    // CF-180: the peer's address (IPv4 dotted or IPv6 text) for accepted connections, the dialed host otherwise.
    [[nodiscard]] auto remote_addr() const -> std::string { return endpoint_; }

    [[nodiscard]] auto poll(std::int64_t timeout_ms = 0) const noexcept -> bool {
        return detail::poll_sock_read(fd_, static_cast<int>(timeout_ms));
    }

    void close() noexcept {
        tls_.reset();
        if (fd_ >= 0) {
            detail::close_guarded(fd_, guard_);
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

    [[nodiscard]] auto write_bytes(std::span<const std::uint8_t> data) -> std::expected<std::size_t, cfire::Error> {
        if (fd_ < 0) {
            return cfire::make_error("socket is closed");
        }
        if (data.empty()) return std::size_t{0};
        const auto* ptr = reinterpret_cast<const char*>(data.data());
        if (tls_ && tls_->ssl && tls_->write_fn) {
            std::size_t total = 0;
            while (total < data.size()) {
                ssize_t n = tls_->write_fn(tls_->ssl, ptr + total, data.size() - total);
                if (n <= 0) {
                    return cfire::make_error("tls socket write failed");
                }
                total += static_cast<std::size_t>(n);
            }
            return total;
        }
        std::size_t total = 0;
        while (total < data.size()) {
            int chunk = static_cast<int>(std::min<std::size_t>(data.size() - total, 0x7fffffffU));
            ssize_t sent = ::send(fd_, ptr + total, chunk, MSG_NOSIGNAL);
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


    [[nodiscard]] auto write_text(std::string_view payload) -> std::expected<std::size_t, cfire::Error> {
        return write_bytes(cfire::as_u8(std::span(payload.data(), payload.size())));
    }

    [[nodiscard]] auto read_into(std::span<std::uint8_t> buf) -> std::expected<std::size_t, cfire::Error> {
        if (fd_ < 0) return cfire::make_error("socket is closed");
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


    [[nodiscard]] auto read_bytes(std::size_t max_bytes = 4096) -> std::expected<std::vector<std::uint8_t>, cfire::Error> {
        if (fd_ < 0) return cfire::make_error("socket is closed");
        std::vector<std::uint8_t> buf(max_bytes);
        auto n = read_into(std::span<std::uint8_t>(buf));
        if (!n) return std::unexpected(n.error());
        buf.resize(*n);
        return buf;
    }

    [[nodiscard]] auto read_exact(std::size_t exact_bytes) -> std::expected<std::vector<std::uint8_t>, cfire::Error> {
        if (fd_ < 0) return cfire::make_error("socket is closed");
        std::vector<std::uint8_t> buf(exact_bytes);
        std::size_t total = 0;
        while (total < exact_bytes) {
            auto n = read_into(std::span<std::uint8_t>(buf.data() + total, exact_bytes - total));
            if (!n) return std::unexpected(n.error());
            if (*n == 0) {
                return cfire::make_error(std::format("unexpected EOF in read_exact (expected {} bytes, got {})", exact_bytes, total));
            }
            total += *n;
        }
        return buf;
    }

    [[nodiscard]] auto read_text(std::size_t max_bytes = 4096) -> std::expected<std::string, cfire::Error> {
        if (fd_ < 0) return cfire::make_error("socket is closed");
        std::string buf(max_bytes, '\0');
        auto n = read_into(cfire::as_writable_u8(std::span(buf.data(), max_bytes)));
        if (!n) return std::unexpected(n.error());
        buf.resize(*n);
        return buf;
    }

    [[nodiscard]] auto read_bytes_until(std::uint8_t delim, std::size_t max_bytes = 65536)
        -> std::expected<std::vector<std::uint8_t>, cfire::Error>
    {
        if (fd_ < 0) return cfire::make_error("socket is closed");
        std::vector<std::uint8_t> out;
        std::uint8_t b{0};
        while (out.size() < max_bytes) {
            ssize_t n = 0;
            if (tls_ && tls_->ssl && tls_->read_fn) {
                n = tls_->read_fn(tls_->ssl, &b, 1);
            } else {
                while ((n = ::recv(fd_, reinterpret_cast<char*>(&b), 1, 0)) < 0 && detail::is_sock_eintr()) {}
            }
            if (n < 0) return cfire::make_error("tcp socket recv failed");
            if (n == 0) break;
            out.push_back(b);
            if (b == delim) break;
        }
        return out;
    }


    [[nodiscard]] auto read_text_until(std::string_view delim, std::size_t max_bytes = 65536)
        -> std::expected<std::string, cfire::Error>
    {
        if (fd_ < 0) return cfire::make_error("socket is closed");
        if (delim.empty()) return cfire::make_error("read_text_until requires non-empty delimiter");
        std::string out;
        std::uint8_t b{0};
        while (out.size() < max_bytes) {
            ssize_t n = 0;
            if (tls_ && tls_->ssl && tls_->read_fn) {
                n = tls_->read_fn(tls_->ssl, &b, 1);
            } else {
                while ((n = ::recv(fd_, reinterpret_cast<char*>(&b), 1, 0)) < 0 && detail::is_sock_eintr()) {}
            }
            if (n < 0) return cfire::make_error("tcp socket recv failed");
            if (n == 0) break;
            out.push_back(static_cast<char>(b));
            if (out.ends_with(delim)) break;
        }
        return out;
    }

    [[nodiscard]] auto read_line(std::size_t max_bytes = 65536) -> std::optional<std::string> {
        auto b = read_bytes_until(static_cast<std::uint8_t>('\n'), max_bytes);
        if (!b || b->empty()) return std::nullopt;
        std::string out(reinterpret_cast<const char*>(b->data()), b->size());
        if (!out.empty() && out.back() == '\n') {
            out.pop_back();
            if (!out.empty() && out.back() == '\r') out.pop_back();
        }
        return out;
    }

private:
    int fd_ = -1;
    std::string endpoint_;
    std::shared_ptr<TlsStream> tls_{nullptr};
    std::shared_ptr<detail::CloseGuard> guard_{nullptr};
};

class TcpListener {
public:
    TcpListener(int fd, std::uint16_t port) noexcept : fd_(fd), port_(port), active_(true) {
        if (port_ != 0) {
            detail::register_loopback_port(port_);
        }
    }
    ~TcpListener() {
        if (active_ && port_ != 0) {
            detail::unregister_loopback_port(port_);
        }
        if (fd_ >= 0) detail::close_sock(fd_);
    }
    TcpListener(TcpListener&& o) noexcept
        : fd_(std::exchange(o.fd_, -1)),
          port_(o.port_),
          active_(std::exchange(o.active_, false)) {}
    auto operator=(TcpListener&& o) noexcept -> TcpListener& {
        if (this != &o) {
            if (active_ && port_ != 0) {
                detail::unregister_loopback_port(port_);
            }
            if (fd_ >= 0) detail::close_sock(fd_);
            fd_ = std::exchange(o.fd_, -1);
            port_ = o.port_;
            active_ = std::exchange(o.active_, false);
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
                    sockaddr_storage client_addr{};
                    socklen_t len = sizeof(client_addr);
#if defined(_WIN32)
                    SOCKET client_s = ::accept(static_cast<SOCKET>(fd_), reinterpret_cast<sockaddr*>(&client_addr), &len);
                    int client_fd = (client_s == INVALID_SOCKET) ? -1 : static_cast<int>(client_s);
#else
                    int client_fd = ::accept(fd_, reinterpret_cast<sockaddr*>(&client_addr), &len);
#endif
                    if (client_fd >= 0) {
                        // Report the real peer address (CF-150); every client used to appear as 127.0.0.1.
                        return TcpConn(client_fd, detail::peer_address_text(client_addr));
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
    bool active_ = false;
};

[[nodiscard]] inline auto listen_tcp_addr(std::string_view host, std::uint16_t port = 0)
    -> std::expected<TcpListener, cfire::Error>
{
    // CF-180: the wildcard host listens dual-stack (IPv6 with IPv4-mapped clients) and falls back to IPv4;
    // `::1` / any IPv6 literal binds IPv6; an address that is neither is an error instead of a silent
    // bind to every interface.
    const bool wildcard = host.empty() || host == "0.0.0.0" || host == "*" || host == "::";
    const bool v6_literal = !wildcard && host.find(':') != std::string_view::npos;
    auto bound = [&](int fd, sockaddr* addr, socklen_t addr_len) -> std::optional<TcpListener> {
        if (::bind(fd, addr, addr_len) == 0 && ::listen(fd, 16) == 0) {
            sockaddr_storage got{};
            socklen_t len = sizeof(got);
            ::getsockname(fd, reinterpret_cast<sockaddr*>(&got), &len);
            std::uint16_t p = got.ss_family == AF_INET6 ? ntohs(reinterpret_cast<sockaddr_in6*>(&got)->sin6_port)
                                                        : ntohs(reinterpret_cast<sockaddr_in*>(&got)->sin_port);
            return TcpListener(fd, p);
        }
        detail::close_sock(fd);
        return std::nullopt;
    };
    if (wildcard || v6_literal) {
        sockaddr_in6 addr6{};
        addr6.sin6_family = AF_INET6;
        addr6.sin6_port = htons(port);
        addr6.sin6_addr = in6addr_any;
        if (v6_literal) {
            std::string host_str(host);
            if (host_str.size() > 2 && host_str.front() == '[' && host_str.back() == ']') host_str = host_str.substr(1, host_str.size() - 2);
            if (::inet_pton(AF_INET6, host_str.c_str(), &addr6.sin6_addr) != 1) {
                return cfire::make_error(std::format("invalid listen address '{}'", host));
            }
        }
        int fd6 = detail::open_tcp_socket(AF_INET6, SOCK_STREAM, 0);
        if (fd6 >= 0) {
            detail::set_reuseaddr(fd6);
            if (wildcard) detail::set_dual_stack(fd6);
            if (auto l = bound(fd6, reinterpret_cast<sockaddr*>(&addr6), sizeof(addr6))) return std::move(*l);
        }
        if (v6_literal) return cfire::make_error(std::format("failed to listen on [{}]:{}", host, port));
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (wildcard) {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (host == "localhost") {
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    } else {
        std::string host_str(host);
        if (::inet_pton(AF_INET, host_str.c_str(), &addr.sin_addr) != 1) {
            return cfire::make_error(std::format("invalid listen address '{}' (use an IPv4 or IPv6 literal, 'localhost', or '0.0.0.0')", host));
        }
    }
    int fd = detail::open_tcp_socket(AF_INET, SOCK_STREAM, 0);
    if (fd >= 0) {
        detail::set_reuseaddr(fd);
        if (auto l = bound(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr))) return std::move(*l);
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
    if (host.empty() || port == 0) {
        return cfire::make_error(std::format("failed to connect to {}:{}", host, port));
    }

    detail::ensure_winsock();
    std::string host_str(host);
    std::string port_str = std::to_string(port);

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;  // CF-180: IPv6 hosts and literals (`::1`) as well as IPv4
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (host_str.size() > 2 && host_str.front() == '[' && host_str.back() == ']') host_str = host_str.substr(1, host_str.size() - 2);
    if (::getaddrinfo(host_str.c_str(), port_str.c_str(), &hints, &res) == 0 && res != nullptr) {
        for (addrinfo* ai = res; ai != nullptr; ai = ai->ai_next) {
            int fd = detail::open_tcp_socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
            if (fd < 0) continue;
            detail::set_sock_timeout_ms(fd, 2000);
            if (::connect(fd, ai->ai_addr, static_cast<int>(ai->ai_addrlen)) == 0) {
                ::freeaddrinfo(res);
                return TcpConn(fd, host_str);
            }
            detail::close_sock(fd);
        }
        ::freeaddrinfo(res);
    }

    if ((host == "127.0.0.1" || host == "localhost") && detail::has_loopback_listener(port)) {
        int sv[2]{-1, -1};
        if (detail::create_loopback_pair(sv)) {
            detail::push_loopback_fd(port, sv[0]);
            return TcpConn(sv[1], host_str);
        }
    }

    return cfire::make_error(std::format("failed to connect to {}:{}", host, port));
}

[[nodiscard]] inline auto socketpair() -> std::expected<std::tuple<TcpConn, TcpConn>, cfire::Error> {
    int sv[2]{-1, -1};
    if (!detail::create_loopback_pair(sv)) {
        return cfire::make_error("socketpair failed");
    }
#if !defined(_WIN32)
    (void)::fcntl(sv[0], F_SETFD, FD_CLOEXEC);
    (void)::fcntl(sv[1], F_SETFD, FD_CLOEXEC);
#endif
    return std::tuple<TcpConn, TcpConn>{
        TcpConn(sv[0], "socketpair"),
        TcpConn(sv[1], "socketpair")
    };
}

// Q29: one verb per direction across `net`, `tls`, `websocket` and `ssh`.
[[nodiscard]] inline auto connect(std::string_view host, std::uint16_t port) -> std::expected<TcpConn, cfire::Error> {
    return connect_tcp(host, port);
}
[[nodiscard]] inline auto listen(std::uint16_t port = 0, std::string_view host = "0.0.0.0") -> std::expected<TcpListener, cfire::Error> {
    return listen_tcp_addr(host, port);
}

} // namespace cfire::net
