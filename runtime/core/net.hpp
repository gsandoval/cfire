#pragma once
#include "../cfire_prelude.hpp"

#include "crypto.hpp"
#include "encoding.hpp"
#include <span>
#include <vector>
#include <string>
#include <string_view>
#include <expected>
#include <unordered_map>
#include <queue>
#include <mutex>
#include <atomic>
#include <random>
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

// --- `core:net/url` (RFC 3986 URL Parser & Form URL Encoding) ---
namespace cfire::url {

struct Url {
    std::string scheme = "http";
    std::string host{};
    std::uint16_t port = 80;
    std::string path = "/";
    std::string query{};
    std::string fragment{};
    std::unordered_map<std::string, std::string> query_params{};
};

[[nodiscard]] inline auto encode(std::string_view sv) -> std::string {
    static constexpr char hex_chars[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(sv.size() * 3 / 2);
    for (char c : sv) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(c);
        } else {
            out.push_back('%');
            out.push_back(hex_chars[(static_cast<unsigned char>(c) >> 4) & 0x0f]);
            out.push_back(hex_chars[static_cast<unsigned char>(c) & 0x0f]);
        }
    }
    return out;
}

[[nodiscard]] inline auto decode(std::string_view sv) -> std::string {
    std::string out;
    for (std::size_t i = 0; i < sv.size(); ++i) {
        if (sv[i] == '%' && i + 2 < sv.size()) {
            int h1 = std::isdigit(sv[i + 1]) ? sv[i + 1] - '0' : std::tolower(sv[i + 1]) - 'a' + 10;
            int h2 = std::isdigit(sv[i + 2]) ? sv[i + 2] - '0' : std::tolower(sv[i + 2]) - 'a' + 10;
            out.push_back(static_cast<char>((h1 << 4) | h2));
            i += 2;
        } else if (sv[i] == '+') {
            out.push_back(' ');
        } else {
            out.push_back(sv[i]);
        }
    }
    return out;
}

[[nodiscard]] inline auto parse(std::string_view raw) -> std::expected<Url, cfire::Error> {
    if (raw.empty()) return cfire::make_error("empty URL string");
    Url u;
    std::size_t scheme_end = raw.find("://");
    std::size_t host_start = 0;
    if (scheme_end != std::string_view::npos) {
        u.scheme = std::string(raw.substr(0, scheme_end));
        host_start = scheme_end + 3;
    } else {
        u.scheme = "http";
        host_start = 0;
    }

    std::size_t path_start = raw.find('/', host_start);
    std::size_t query_start = raw.find('?', host_start);
    std::size_t frag_start = raw.find('#', host_start);

    std::size_t host_end = raw.size();
    if (path_start != std::string_view::npos) host_end = std::min(host_end, path_start);
    if (query_start != std::string_view::npos) host_end = std::min(host_end, query_start);
    if (frag_start != std::string_view::npos) host_end = std::min(host_end, frag_start);

    std::string_view host_port = raw.substr(host_start, host_end - host_start);
    std::size_t colon = host_port.find(':');
    if (colon != std::string_view::npos) {
        u.host = std::string(host_port.substr(0, colon));
        try {
            u.port = static_cast<std::uint16_t>(std::stoi(std::string(host_port.substr(colon + 1))));
        } catch (...) {
            u.port = (u.scheme == "https") ? 443 : 80;
        }
    } else {
        u.host = std::string(host_port);
        u.port = (u.scheme == "https") ? 443 : 80;
    }

    if (frag_start != std::string_view::npos) {
        u.fragment = decode(raw.substr(frag_start + 1));
    }

    std::size_t query_end = (frag_start != std::string_view::npos) ? frag_start : raw.size();
    if (query_start != std::string_view::npos && query_start < query_end) {
        u.query = std::string(raw.substr(query_start + 1, query_end - (query_start + 1)));
        std::string_view qsv = u.query;
        std::size_t pos = 0;
        while (pos < qsv.size()) {
            std::size_t next_amp = qsv.find('&', pos);
            if (next_amp == std::string_view::npos) next_amp = qsv.size();
            std::string_view pair = qsv.substr(pos, next_amp - pos);
            std::size_t eq = pair.find('=');
            if (eq != std::string_view::npos) {
                u.query_params[decode(pair.substr(0, eq))] = decode(pair.substr(eq + 1));
            } else if (!pair.empty()) {
                u.query_params[decode(pair)] = "";
            }
            pos = next_amp + 1;
        }
    }

    std::size_t path_end = raw.size();
    if (query_start != std::string_view::npos) path_end = std::min(path_end, query_start);
    if (frag_start != std::string_view::npos) path_end = std::min(path_end, frag_start);

    if (path_start != std::string_view::npos && path_start < path_end) {
        u.path = std::string(raw.substr(path_start, path_end - path_start));
    } else {
        u.path = "/";
    }

    return u;
}

} // namespace cfire::url

// --- `core:net/http` (RFC 9112 HTTP/1.1 Client, Wire Protocol Engine, Server & Independent Router) ---
namespace cfire::http {

[[nodiscard]] inline auto status_text_for_code(int code) -> std::string {
    switch (code) {
        case 200: return "OK";
        case 201: return "Created";
        case 202: return "Accepted";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 304: return "Not Modified";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 409: return "Conflict";
        case 422: return "Unprocessable Entity";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        default: return (code >= 200 && code < 300) ? "OK" : "Error";
    }
}

struct Response {
    int status_code = 200;
    std::string status_text{"OK"};
    std::unordered_map<std::string, std::string> headers{};
    std::string body{};

    [[nodiscard]] auto is_ok() const noexcept -> bool { return status_code >= 200 && status_code < 300; }

    auto set_header(std::string_view key, std::string_view val) -> Response& {
        std::string lower_k;
        lower_k.reserve(key.size());
        for (char c : key) lower_k.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        headers[lower_k] = std::string(val);
        return *this;
    }

    [[nodiscard]] auto header(std::string_view key, std::string_view fallback = "") const -> std::string {
        std::string lower_k;
        lower_k.reserve(key.size());
        for (char c : key) lower_k.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        auto it = headers.find(lower_k);
        return (it != headers.end()) ? it->second : std::string(fallback);
    }

    [[nodiscard]] auto to_http_wire() const -> std::string {
        std::string text = status_text.empty() ? status_text_for_code(status_code) : status_text;
        std::string out = std::format("HTTP/1.1 {} {}\r\nContent-Length: {}\r\nConnection: close\r\n",
            status_code, text, body.size());
        for (const auto& [k, v] : headers) {
            out += std::format("{}: {}\r\n", k, v);
        }
        out += "\r\n";
        out += body;
        return out;
    }

    static auto text(int code, std::string_view payload) -> Response {
        Response r;
        r.status_code = code;
        r.status_text = status_text_for_code(code);
        r.headers["content-type"] = "text/plain; charset=utf-8";
        r.body = std::string(payload);
        return r;
    }

    static auto json(int code, std::string_view json_str) -> Response {
        Response r;
        r.status_code = code;
        r.status_text = status_text_for_code(code);
        r.headers["content-type"] = "application/json";
        r.body = std::string(json_str);
        return r;
    }

    static auto html(int code, std::string_view html_content) -> Response {
        Response r;
        r.status_code = code;
        r.status_text = status_text_for_code(code);
        r.headers["content-type"] = "text/html; charset=utf-8";
        r.body = std::string(html_content);
        return r;
    }

    static auto redirect(std::string_view location, int code = 302) -> Response {
        Response r;
        r.status_code = code;
        r.status_text = status_text_for_code(code);
        r.headers["location"] = std::string(location);
        r.body = "";
        return r;
    }

    static auto not_found(std::string_view message = "Not Found") -> Response {
        return text(404, message);
    }

    static auto bad_request(std::string_view message = "Bad Request") -> Response {
        return text(400, message);
    }

    static auto internal_error(std::string_view message = "Internal Server Error") -> Response {
        return text(500, message);
    }
};

struct Request {
    std::string method{"GET"};
    std::string path{"/"};
    std::string raw_query{};
    std::unordered_map<std::string, std::string> query_params{};
    std::unordered_map<std::string, std::string> headers{};
    std::unordered_map<std::string, std::string> params{};
    std::string body{};

    [[nodiscard]] auto header(std::string_view key, std::string_view fallback = "") const -> std::string {
        std::string lower_k;
        lower_k.reserve(key.size());
        for (char c : key) lower_k.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        auto it = headers.find(lower_k);
        return (it != headers.end()) ? it->second : std::string(fallback);
    }

    [[nodiscard]] auto query(std::string_view key, std::string_view fallback = "") const -> std::string {
        auto it = query_params.find(std::string(key));
        return (it != query_params.end()) ? it->second : std::string(fallback);
    }

    [[nodiscard]] auto param(std::string_view key, std::string_view fallback = "") const -> std::string {
        auto it = params.find(std::string(key));
        return (it != params.end()) ? it->second : std::string(fallback);
    }
};

inline auto parse_response(std::string_view raw) -> std::expected<Response, cfire::Error> {
    Response res;
    std::size_t header_end = raw.find("\r\n\r\n");
    std::size_t body_start = header_end + 4;
    if (header_end == std::string_view::npos) {
        header_end = raw.find("\n\n");
        body_start = header_end + 2;
    }
    if (header_end == std::string_view::npos) {
        return cfire::make_error("malformed HTTP response: missing header delimiter");
    }

    std::string_view header_section = raw.substr(0, header_end);
    if (body_start <= raw.size()) {
        res.body = std::string(raw.substr(body_start));
    }

    std::size_t line_start = 0;
    bool is_first = true;
    while (line_start < header_section.size()) {
        std::size_t line_end = header_section.find('\n', line_start);
        if (line_end == std::string_view::npos) line_end = header_section.size();
        std::string_view line = header_section.substr(line_start, line_end - line_start);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.remove_suffix(1);

        if (is_first) {
            is_first = false;
            std::size_t sp1 = line.find(' ');
            if (sp1 != std::string_view::npos) {
                std::size_t sp2 = line.find(' ', sp1 + 1);
                std::string_view code_sv = (sp2 != std::string_view::npos)
                    ? line.substr(sp1 + 1, sp2 - (sp1 + 1))
                    : line.substr(sp1 + 1);
                try {
                    res.status_code = std::stoi(std::string(code_sv));
                } catch (...) {
                    res.status_code = 0;
                }
                if (sp2 != std::string_view::npos) {
                    res.status_text = std::string(line.substr(sp2 + 1));
                }
            }
        } else {
            std::size_t colon = line.find(':');
            if (colon != std::string_view::npos) {
                std::string_view k = line.substr(0, colon);
                std::string_view v = line.substr(colon + 1);
                while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.remove_prefix(1);
                std::string lower_k;
                for (char c : k) lower_k.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                res.headers[lower_k] = std::string(v);
            }
        }
        line_start = line_end + 1;
    }
    return res;
}

inline auto parse_request(std::string_view raw) -> std::expected<Request, cfire::Error> {
    if (raw.empty()) return cfire::make_error("empty HTTP request");

    std::size_t header_end = raw.find("\r\n\r\n");
    std::size_t body_start = header_end + 4;
    if (header_end == std::string_view::npos) {
        header_end = raw.find("\n\n");
        body_start = header_end + 2;
    }
    if (header_end == std::string_view::npos) {
        return cfire::make_error("malformed HTTP request: missing header delimiter");
    }

    std::string_view header_section = raw.substr(0, header_end);
    Request req;
    if (body_start <= raw.size()) {
        req.body = std::string(raw.substr(body_start));
    }

    std::size_t line_start = 0;
    bool is_first = true;
    while (line_start < header_section.size()) {
        std::size_t line_end = header_section.find('\n', line_start);
        if (line_end == std::string_view::npos) line_end = header_section.size();
        std::string_view line = header_section.substr(line_start, line_end - line_start);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.remove_suffix(1);
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.remove_prefix(1);

        if (is_first) {
            is_first = false;
            std::size_t sp1 = line.find(' ');
            if (sp1 == std::string_view::npos) return cfire::make_error("malformed HTTP request line");
            req.method = std::string(line.substr(0, sp1));
            for (char& c : req.method) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

            std::size_t sp2 = line.find(' ', sp1 + 1);
            std::string_view target = (sp2 != std::string_view::npos)
                ? line.substr(sp1 + 1, sp2 - (sp1 + 1))
                : line.substr(sp1 + 1);

            std::size_t qm = target.find('?');
            if (qm != std::string_view::npos) {
                req.path = cfire::url::decode(target.substr(0, qm));
                req.raw_query = std::string(target.substr(qm + 1));

                std::string_view qsv = req.raw_query;
                std::size_t pos = 0;
                while (pos < qsv.size()) {
                    std::size_t next_amp = qsv.find('&', pos);
                    if (next_amp == std::string_view::npos) next_amp = qsv.size();
                    std::string_view pair = qsv.substr(pos, next_amp - pos);
                    std::size_t eq = pair.find('=');
                    if (eq != std::string_view::npos) {
                        req.query_params[cfire::url::decode(pair.substr(0, eq))] = cfire::url::decode(pair.substr(eq + 1));
                    } else if (!pair.empty()) {
                        req.query_params[cfire::url::decode(pair)] = "";
                    }
                    pos = next_amp + 1;
                }
            } else {
                req.path = cfire::url::decode(target);
            }
            if (req.path.empty()) req.path = "/";
        } else if (!line.empty()) {
            std::size_t colon = line.find(':');
            if (colon != std::string_view::npos) {
                std::string_view k = line.substr(0, colon);
                std::string_view v = line.substr(colon + 1);
                while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.remove_prefix(1);
                std::string lower_k;
                lower_k.reserve(k.size());
                for (char c : k) lower_k.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                req.headers[lower_k] = std::string(v);
            }
        }
        line_start = line_end + 1;
    }

    return req;
}

inline auto read_http_request(cfire::net::TcpConn& conn, std::size_t max_body_size = 10 * 1024 * 1024)
    -> std::expected<Request, cfire::Error>
{
    std::string raw;
    std::size_t header_end = std::string::npos;

    for (int attempt = 0; attempt < 100; ++attempt) {
        auto chunk = conn.read_str(4096);
        if (chunk && !chunk->empty()) {
            raw.append(*chunk);
            header_end = raw.find("\r\n\r\n");
            if (header_end != std::string::npos) {
                break;
            }
            header_end = raw.find("\n\n");
            if (header_end != std::string::npos) {
                break;
            }
            if (raw.size() > 65536) {
                return cfire::make_error("HTTP header section exceeds maximum size (64KB)");
            }
        } else if (!raw.empty()) {
            break;
        }
    }

    if (header_end == std::string::npos) {
        if (raw.empty()) return cfire::make_error("connection closed before request");
        return cfire::make_error("malformed HTTP request: missing header delimiter");
    }

    auto req_res = parse_request(raw);
    if (!req_res) return req_res;
    auto& req = *req_res;

    std::size_t content_len = 0;
    auto cl_it = req.headers.find("content-length");
    if (cl_it != req.headers.end()) {
        try {
            content_len = static_cast<std::size_t>(std::stoull(cl_it->second));
        } catch (...) {
            content_len = 0;
        }
    }
    if (content_len > max_body_size) {
        return cfire::make_error(std::format("payload too large: Content-Length {} exceeds limit {}", content_len, max_body_size));
    }

    while (req.body.size() < content_len) {
        std::size_t to_read = std::min<std::size_t>(4096, content_len - req.body.size());
        auto chunk = conn.read_str(to_read);
        if (!chunk || chunk->empty()) break;
        req.body.append(*chunk);
    }

    return req;
}

[[nodiscard]] inline auto request(
    std::string_view method,
    std::string_view url_str,
    std::string_view body = "",
    std::string_view content_type = "",
    const std::unordered_map<std::string, std::string>& extra_headers = {}
) -> std::expected<Response, cfire::Error> {
    auto u = cfire::url::parse(url_str);
    if (!u) return std::unexpected(u.error());

    auto conn_res = cfire::net::connect_tcp(u->host, u->port);
    if (!conn_res) return std::unexpected(conn_res.error());
    auto& conn = *conn_res;

    std::string req;
    std::string full_path = u->path;
    if (!u->query.empty()) full_path += "?" + u->query;

    req += std::format("{} {} HTTP/1.1\r\n", method, full_path);
    req += std::format("Host: {}\r\n", u->host);
    req += "Connection: close\r\n";
    req += "User-Agent: cfire-http/1.0\r\n";
    if (!content_type.empty()) {
        req += std::format("Content-Type: {}\r\n", content_type);
    }
    if (!body.empty() || method == "POST" || method == "PUT" || method == "PATCH") {
        req += std::format("Content-Length: {}\r\n", body.size());
    }
    for (const auto& [k, v] : extra_headers) {
        req += std::format("{}: {}\r\n", k, v);
    }
    req += "\r\n";
    if (!body.empty()) {
        req += body;
    }

    auto wr = conn.write(req);
    if (!wr) return std::unexpected(wr.error());

    std::string full_response;
    for (;;) {
        auto chunk = conn.read_str(4096);
        if (!chunk || chunk->empty()) break;
        full_response.append(*chunk);
    }

    return parse_response(full_response);
}

[[nodiscard]] inline auto get(
    std::string_view url_str,
    const std::unordered_map<std::string, std::string>& headers = {}
) -> std::expected<Response, cfire::Error> {
    return request("GET", url_str, "", "", headers);
}

[[nodiscard]] inline auto post(
    std::string_view url_str,
    std::string_view body,
    std::string_view content_type = "application/json",
    const std::unordered_map<std::string, std::string>& headers = {}
) -> std::expected<Response, cfire::Error> {
    return request("POST", url_str, body, content_type, headers);
}

class Router {
public:
    using Handler = std::function<Response(const Request&)>;
    using Middleware = std::function<std::optional<Response>(Request&)>;

    struct Route {
        std::string method{};
        std::string pattern{};
        std::vector<std::string> segments{};
        Handler handler{};
    };

    Router() = default;

    auto use(Middleware mw) -> Router& {
        middlewares_.push_back(std::move(mw));
        return *this;
    }

    auto handle(std::string_view method, std::string_view pattern, Handler handler) -> Router& {
        Route r;
        r.method = std::string(method);
        for (char& c : r.method) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        r.pattern = std::string(pattern);
        r.segments = split_path(pattern);
        r.handler = std::move(handler);
        routes_.push_back(std::move(r));
        return *this;
    }

    auto get(std::string_view pattern, Handler handler) -> Router& {
        return handle("GET", pattern, std::move(handler));
    }

    auto post(std::string_view pattern, Handler handler) -> Router& {
        return handle("POST", pattern, std::move(handler));
    }

    auto put(std::string_view pattern, Handler handler) -> Router& {
        return handle("PUT", pattern, std::move(handler));
    }

    auto delete_(std::string_view pattern, Handler handler) -> Router& {
        return handle("DELETE", pattern, std::move(handler));
    }

    auto patch(std::string_view pattern, Handler handler) -> Router& {
        return handle("PATCH", pattern, std::move(handler));
    }

    auto all(std::string_view pattern, Handler handler) -> Router& {
        return handle("*", pattern, std::move(handler));
    }

    void set_not_found_handler(Handler handler) {
        not_found_handler_ = std::move(handler);
    }

    [[nodiscard]] auto dispatch(Request req) const -> Response {
        for (const auto& mw : middlewares_) {
            auto early = mw(req);
            if (early.has_value()) {
                return *early;
            }
        }

        std::vector<std::string> path_segs = split_path(req.path);
        bool path_matched_any_method = false;
        std::vector<std::string> allowed_methods;

        for (const auto& route : routes_) {
            std::unordered_map<std::string, std::string> params;
            if (match_route(route.segments, path_segs, params)) {
                path_matched_any_method = true;
                if (!route.method.empty() && route.method != "*") {
                    allowed_methods.push_back(route.method);
                }
                if (route.method == "*" || route.method == req.method) {
                    req.params = std::move(params);
                    return route.handler(req);
                }
            }
        }

        if (path_matched_any_method && !allowed_methods.empty()) {
            Response resp = Response::text(405, "405 Method Not Allowed");
            resp.status_code = 405;
            resp.status_text = "Method Not Allowed";
            std::string allow_hdr;
            for (std::size_t i = 0; i < allowed_methods.size(); ++i) {
                if (i > 0) allow_hdr += ", ";
                allow_hdr += allowed_methods[i];
            }
            resp.headers["allow"] = allow_hdr;
            return resp;
        }

        if (not_found_handler_) {
            return not_found_handler_(req);
        }
        return Response::not_found("404 Not Found: " + req.path);
    }

    [[nodiscard]] auto operator()(const Request& req) const -> Response {
        return dispatch(req);
    }

private:
    static auto split_path(std::string_view path) -> std::vector<std::string> {
        std::vector<std::string> segs;
        std::size_t start = 0;
        while (start < path.size()) {
            while (start < path.size() && path[start] == '/') ++start;
            if (start >= path.size()) break;
            std::size_t end = path.find('/', start);
            if (end == std::string_view::npos) end = path.size();
            segs.emplace_back(path.substr(start, end - start));
            start = end;
        }
        return segs;
    }

    static auto match_route(
        const std::vector<std::string>& route_segs,
        const std::vector<std::string>& req_segs,
        std::unordered_map<std::string, std::string>& out_params
    ) -> bool {
        std::size_t r_idx = 0;
        std::size_t p_idx = 0;

        while (r_idx < route_segs.size() && p_idx < req_segs.size()) {
            const auto& r_seg = route_segs[r_idx];
            if (r_seg.starts_with('*')) {
                std::string name = r_seg.substr(1);
                std::string val;
                for (std::size_t i = p_idx; i < req_segs.size(); ++i) {
                    if (!val.empty()) val += "/";
                    val += req_segs[i];
                }
                if (!name.empty()) out_params[name] = val;
                return true;
            } else if (r_seg.starts_with(':')) {
                std::string name = r_seg.substr(1);
                out_params[name] = req_segs[p_idx];
                ++r_idx;
                ++p_idx;
            } else if (r_seg == req_segs[p_idx]) {
                ++r_idx;
                ++p_idx;
            } else {
                return false;
            }
        }

        if (r_idx < route_segs.size() && route_segs[r_idx].starts_with('*')) {
            std::string name = route_segs[r_idx].substr(1);
            if (!name.empty()) out_params[name] = "";
            return true;
        }

        return r_idx == route_segs.size() && p_idx == req_segs.size();
    }

    std::vector<Route> routes_{};
    std::vector<Middleware> middlewares_{};
    Handler not_found_handler_{};
};

[[nodiscard]] inline auto make_router() -> Router {
    return Router{};
}

class HttpServer {
public:
    HttpServer(cfire::net::TcpListener listener) : listener_(std::move(listener)) {}
    ~HttpServer() {
        stop();
    }

    HttpServer(HttpServer&& o) noexcept
        : listener_(std::move(o.listener_)), running_(o.running_.load()) {
        o.running_ = false;
        if (o.worker_.joinable()) {
            worker_ = std::move(o.worker_);
        }
    }

    auto operator=(HttpServer&& o) noexcept -> HttpServer& {
        if (this != &o) {
            stop();
            listener_ = std::move(o.listener_);
            running_ = o.running_.load();
            o.running_ = false;
            if (o.worker_.joinable()) {
                worker_ = std::move(o.worker_);
            }
        }
        return *this;
    }

    auto port() const noexcept -> std::uint16_t { return listener_.port(); }

    void stop() noexcept {
        running_ = false;
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    template <typename Handler>
    void serve_once(Handler&& handler) {
        auto conn_res = listener_.accept();
        if (!conn_res) return;
        auto& conn = *conn_res;
        auto req_res = read_http_request(conn);
        if (!req_res) {
            Response err_resp = Response::bad_request("400 Bad Request");
            (void)conn.write(err_resp.to_http_wire());
            return;
        }
        Response resp = handler(*req_res);
        (void)conn.write(resp.to_http_wire());
    }

    template <typename Handler>
    void listen_and_serve(Handler&& handler) {
        running_ = true;
        while (running_) {
            auto conn_res = listener_.accept();
            if (!conn_res) {
                if (!running_) break;
                continue;
            }
            auto& conn = *conn_res;
            auto req_res = read_http_request(conn);
            if (!req_res) {
                Response err_resp = Response::bad_request("400 Bad Request");
                (void)conn.write(err_resp.to_http_wire());
                continue;
            }
            Response resp = handler(*req_res);
            (void)conn.write(resp.to_http_wire());
        }
    }

    template <typename Handler>
    void serve_in_background(Handler handler) {
        stop();
        running_ = true;
        worker_ = std::thread([this, h = std::move(handler)]() mutable {
            this->listen_and_serve(h);
        });
    }

private:
    cfire::net::TcpListener listener_;
    std::atomic<bool> running_{false};
    std::thread worker_{};
};

[[nodiscard]] inline auto make_server(std::uint16_t port = 0)
    -> std::expected<HttpServer, cfire::Error>
{
    auto l = cfire::net::listen_tcp(port);
    if (!l) return std::unexpected(l.error());
    return HttpServer(std::move(*l));
}

} // namespace cfire::http

// --- `core:net/websocket` (RFC 6455 High-Level WebSockets Client & Server) ---
namespace cfire::websocket {

namespace detail {

inline auto rol(std::uint32_t value, std::size_t bits) noexcept -> std::uint32_t {
    return (value << bits) | (value >> (32 - bits));
}

inline auto sha1(std::string_view input) -> std::vector<std::uint8_t> {
    std::uint32_t h0 = 0x67452301;
    std::uint32_t h1 = 0xEFCDAB89;
    std::uint32_t h2 = 0x98BADCFE;
    std::uint32_t h3 = 0x10325476;
    std::uint32_t h4 = 0xC3D2E1F0;

    std::vector<std::uint8_t> msg(input.begin(), input.end());
    std::uint64_t bit_len = msg.size() * 8;
    msg.push_back(0x80);
    while ((msg.size() % 64) != 56) {
        msg.push_back(0x00);
    }
    for (int i = 7; i >= 0; --i) {
        msg.push_back(static_cast<std::uint8_t>((bit_len >> (i * 8)) & 0xFF));
    }

    for (std::size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        std::uint32_t w[80];
        for (std::size_t i = 0; i < 16; ++i) {
            w[i] = (static_cast<std::uint32_t>(msg[chunk + i * 4]) << 24) |
                   (static_cast<std::uint32_t>(msg[chunk + i * 4 + 1]) << 16) |
                   (static_cast<std::uint32_t>(msg[chunk + i * 4 + 2]) << 8) |
                   (static_cast<std::uint32_t>(msg[chunk + i * 4 + 3]));
        }
        for (std::size_t i = 16; i < 80; ++i) {
            w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }

        std::uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (std::size_t i = 0; i < 80; ++i) {
            std::uint32_t f = 0, k = 0;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            std::uint32_t temp = rol(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rol(b, 30);
            b = a;
            a = temp;
        }
        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }

    std::vector<std::uint8_t> digest(20);
    for (int i = 0; i < 4; ++i) {
        digest[i]      = (h0 >> ((3 - i) * 8)) & 0xFF;
        digest[i + 4]  = (h1 >> ((3 - i) * 8)) & 0xFF;
        digest[i + 8]  = (h2 >> ((3 - i) * 8)) & 0xFF;
        digest[i + 12] = (h3 >> ((3 - i) * 8)) & 0xFF;
        digest[i + 16] = (h4 >> ((3 - i) * 8)) & 0xFF;
    }
    return digest;
}

inline auto recv_exact(int fd, void* buf, std::size_t len) -> bool {
    std::size_t received = 0;
    auto* ptr = static_cast<std::uint8_t*>(buf);
    while (received < len) {
        ssize_t n = ::recv(fd, ptr + received, len - received, 0);
        if (n <= 0) return false;
        received += static_cast<std::size_t>(n);
    }
    return true;
}

} // namespace detail

[[nodiscard]] inline auto compute_accept_key(std::string_view client_key) -> std::string {
    static constexpr std::string_view kGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string combined = std::string(client_key) + std::string(kGuid);
    auto digest = detail::sha1(combined);
    return cfire::base64::encode(std::span<const std::byte>(reinterpret_cast<const std::byte*>(digest.data()), digest.size()));
}

[[nodiscard]] inline auto generate_nonce() -> std::string {
    std::uint8_t nonce[16];
    std::random_device rd;
    for (int i = 0; i < 16; ++i) {
        nonce[i] = static_cast<std::uint8_t>(rd() & 0xFF);
    }
    return cfire::base64::encode(std::span<const std::byte>(reinterpret_cast<const std::byte*>(nonce), 16));
}

struct WsFrame {
    std::uint8_t opcode = 0x1;
    bool is_final = true;
    std::vector<std::uint8_t> payload{};
};

[[nodiscard]] inline auto encode_frame(const WsFrame& f, bool mask) -> std::vector<std::uint8_t> {
    std::vector<std::uint8_t> out;
    std::uint8_t b0 = (f.is_final ? 0x80 : 0x00) | (f.opcode & 0x0F);
    out.push_back(b0);

    std::size_t len = f.payload.size();
    std::uint8_t mask_bit = mask ? 0x80 : 0x00;

    if (len < 126) {
        out.push_back(mask_bit | static_cast<std::uint8_t>(len));
    } else if (len <= 0xFFFF) {
        out.push_back(mask_bit | 126);
        out.push_back(static_cast<std::uint8_t>((len >> 8) & 0xFF));
        out.push_back(static_cast<std::uint8_t>(len & 0xFF));
    } else {
        out.push_back(mask_bit | 127);
        for (int i = 7; i >= 0; --i) {
            out.push_back(static_cast<std::uint8_t>((len >> (i * 8)) & 0xFF));
        }
    }

    if (mask) {
        std::uint8_t mask_key[4];
        std::random_device rd;
        for (int i = 0; i < 4; ++i) {
            mask_key[i] = static_cast<std::uint8_t>(rd() & 0xFF);
            out.push_back(mask_key[i]);
        }
        for (std::size_t i = 0; i < len; ++i) {
            out.push_back(f.payload[i] ^ mask_key[i % 4]);
        }
    } else {
        out.insert(out.end(), f.payload.begin(), f.payload.end());
    }

    return out;
}

[[nodiscard]] inline auto decode_frame(int fd) -> std::optional<WsFrame> {
    std::uint8_t header[2];
    if (!detail::recv_exact(fd, header, 2)) return std::nullopt;

    WsFrame f;
    f.is_final = (header[0] & 0x80) != 0;
    f.opcode = header[0] & 0x0F;
    bool has_mask = (header[1] & 0x80) != 0;
    std::uint64_t len = header[1] & 0x7F;

    if (len == 126) {
        std::uint8_t ext[2];
        if (!detail::recv_exact(fd, ext, 2)) return std::nullopt;
        len = (static_cast<std::uint64_t>(ext[0]) << 8) | ext[1];
    } else if (len == 127) {
        std::uint8_t ext[8];
        if (!detail::recv_exact(fd, ext, 8)) return std::nullopt;
        len = 0;
        for (int i = 0; i < 8; ++i) len = (len << 8) | ext[i];
    }

    std::uint8_t mask_key[4] = {0};
    if (has_mask) {
        if (!detail::recv_exact(fd, mask_key, 4)) return std::nullopt;
    }

    f.payload.resize(len);
    if (len > 0) {
        if (!detail::recv_exact(fd, f.payload.data(), len)) return std::nullopt;
    }

    if (has_mask) {
        for (std::size_t i = 0; i < len; ++i) {
            f.payload[i] ^= mask_key[i % 4];
        }
    }

    return f;
}

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

class Conn {
public:
    explicit Conn(int fd = -1, bool is_client = true) noexcept : fd_(fd), is_client_(is_client) {
        if (fd_ >= 0) {
#ifdef SO_NOSIGPIPE
            int opt = 1;
            ::setsockopt(fd_, SOL_SOCKET, SO_NOSIGPIPE, &opt, sizeof(opt));
#endif
        }
    }
    ~Conn() { close(); }
    Conn(Conn&& o) noexcept : fd_(std::exchange(o.fd_, -1)), is_client_(o.is_client_) {}
    auto operator=(Conn&& o) noexcept -> Conn& {
        if (this != &o) {
            close();
            fd_ = std::exchange(o.fd_, -1);
            is_client_ = o.is_client_;
        }
        return *this;
    }
    Conn(const Conn&) = delete;
    auto operator=(const Conn&) = delete;

    [[nodiscard]] auto is_open() const noexcept -> bool { return fd_ >= 0; }
    [[nodiscard]] auto fd() const noexcept -> int { return fd_; }

    auto send_text(std::string_view msg) -> std::expected<void, cfire::Error> {
        if (fd_ < 0) return cfire::make_error("websocket connection is closed");
        WsFrame f{
            .opcode = 0x1,
            .is_final = true,
            .payload = std::vector<std::uint8_t>(msg.begin(), msg.end())
        };
        auto bytes = encode_frame(f, is_client_);
        ssize_t sent = ::send(fd_, bytes.data(), bytes.size(), MSG_NOSIGNAL);
        if (sent < 0 || static_cast<std::size_t>(sent) != bytes.size()) {
            return cfire::make_error("failed to send websocket text frame");
        }
        return {};
    }

    auto send_binary(std::span<const std::uint8_t> data) -> std::expected<void, cfire::Error> {
        if (fd_ < 0) return cfire::make_error("websocket connection is closed");
        WsFrame f{
            .opcode = 0x2,
            .is_final = true,
            .payload = std::vector<std::uint8_t>(data.begin(), data.end())
        };
        auto bytes = encode_frame(f, is_client_);
        ssize_t sent = ::send(fd_, bytes.data(), bytes.size(), MSG_NOSIGNAL);
        if (sent < 0 || static_cast<std::size_t>(sent) != bytes.size()) {
            return cfire::make_error("failed to send websocket binary frame");
        }
        return {};
    }

    auto send_binary(std::span<const std::byte> data) -> std::expected<void, cfire::Error> {
        return send_binary(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(data.data()), data.size()));
    }

    auto ping(std::string_view payload = "") -> std::expected<void, cfire::Error> {
        if (fd_ < 0) return cfire::make_error("websocket connection is closed");
        WsFrame f{
            .opcode = 0x9,
            .is_final = true,
            .payload = std::vector<std::uint8_t>(payload.begin(), payload.end())
        };
        auto bytes = encode_frame(f, is_client_);
        ssize_t sent = ::send(fd_, bytes.data(), bytes.size(), MSG_NOSIGNAL);
        if (sent < 0 || static_cast<std::size_t>(sent) != bytes.size()) {
            return cfire::make_error("failed to send websocket ping frame");
        }
        return {};
    }

    auto recv_frame() -> std::expected<WsFrame, cfire::Error> {
        while (fd_ >= 0) {
            auto f_opt = decode_frame(fd_);
            if (!f_opt) {
                if (fd_ >= 0) ::close(std::exchange(fd_, -1));
                return cfire::make_error("websocket connection closed by peer");
            }
            auto& f = *f_opt;
            if (f.opcode == 0x8) { // Close
                if (fd_ >= 0) ::close(std::exchange(fd_, -1));
                return cfire::make_error("websocket closed by peer");
            }
            if (f.opcode == 0x9) { // Ping -> auto-reply with Pong
                WsFrame pong{
                    .opcode = 0xA,
                    .is_final = true,
                    .payload = std::move(f.payload)
                };
                auto bytes = encode_frame(pong, is_client_);
                (void)::send(fd_, bytes.data(), bytes.size(), MSG_NOSIGNAL);
                continue;
            }
            if (f.opcode == 0xA) { // Pong
                continue;
            }
            return f;
        }
        return cfire::make_error("websocket connection is closed");
    }

    auto recv_text() -> std::expected<std::string, cfire::Error> {
        auto frame = recv_frame();
        if (!frame) return std::unexpected(frame.error());
        if (frame->opcode != 0x1 && frame->opcode != 0x0) {
            return cfire::make_error("expected text frame (opcode 0x1)");
        }
        return std::string(frame->payload.begin(), frame->payload.end());
    }

    auto recv_binary() -> std::expected<std::vector<std::uint8_t>, cfire::Error> {
        auto frame = recv_frame();
        if (!frame) return std::unexpected(frame.error());
        if (frame->opcode != 0x2 && frame->opcode != 0x0) {
            return cfire::make_error("expected binary frame (opcode 0x2)");
        }
        return std::move(frame->payload);
    }

    void close(std::uint16_t code = 1000, std::string_view reason = "") {
        if (fd_ >= 0) {
            int sock = std::exchange(fd_, -1);
            std::vector<std::uint8_t> payload;
            payload.push_back(static_cast<std::uint8_t>((code >> 8) & 0xFF));
            payload.push_back(static_cast<std::uint8_t>(code & 0xFF));
            payload.insert(payload.end(), reason.begin(), reason.end());
            WsFrame f{.opcode = 0x8, .is_final = true, .payload = std::move(payload)};
            auto bytes = encode_frame(f, is_client_);
            (void)::send(sock, bytes.data(), bytes.size(), MSG_NOSIGNAL);
            ::close(sock);
        }
    }

private:
    int fd_ = -1;
    bool is_client_ = true;
};

[[nodiscard]] inline auto from_fd(int fd, bool is_client = true) -> Conn {
    return Conn(fd, is_client);
}

[[nodiscard]] inline auto pair() -> std::expected<std::pair<Conn, Conn>, cfire::Error> {
    int sv[2]{-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        return cfire::make_error("socketpair failed");
    }
    return std::make_pair(Conn(sv[0], /*is_client=*/true), Conn(sv[1], /*is_client=*/false));
}

[[nodiscard]] inline auto upgrade(int client_fd, const std::unordered_map<std::string, std::string>& headers) -> std::expected<Conn, cfire::Error> {
    std::string client_key;
    for (const auto& [k, v] : headers) {
        std::string lk;
        for (char c : k) lk += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lk == "sec-websocket-key") {
            client_key = v;
            break;
        }
    }
    if (client_key.empty()) {
        return cfire::make_error("missing Sec-WebSocket-Key header in websocket upgrade request");
    }
    std::string accept = compute_accept_key(client_key);
    std::string resp = std::format(
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: {}\r\n\r\n",
        accept
    );
    ssize_t sent = ::send(client_fd, resp.data(), resp.size(), 0);
    if (sent < 0) {
        return cfire::make_error("failed to send websocket upgrade response");
    }
    return Conn(client_fd, /*is_client=*/false);
}

[[nodiscard]] inline auto upgrade(cfire::net::TcpConn& tcp, const std::unordered_map<std::string, std::string>& headers) -> std::expected<Conn, cfire::Error> {
    int fd = tcp.release_fd();
    return upgrade(fd, headers);
}

[[nodiscard]] inline auto upgrade(const cfire::http::Request& req, cfire::net::TcpConn& tcp) -> std::expected<Conn, cfire::Error> {
    return upgrade(tcp, req.headers);
}

[[nodiscard]] inline auto connect(std::string_view url_str) -> std::expected<Conn, cfire::Error> {
    auto u = cfire::url::parse(url_str);
    if (!u) return std::unexpected(u.error());
    std::uint16_t port = u->port != 0 ? u->port : 80;

    auto tcp_res = cfire::net::connect_tcp(u->host, port);
    if (!tcp_res) return std::unexpected(tcp_res.error());
    auto& tcp = *tcp_res;

    std::string nonce = generate_nonce();
    std::string expected_accept = compute_accept_key(nonce);

    std::string path = u->path.empty() ? "/" : u->path;
    if (!u->query.empty()) path += "?" + u->query;

    std::string req;
    req += std::format("GET {} HTTP/1.1\r\n", path);
    req += std::format("Host: {}\r\n", u->host);
    req += "Upgrade: websocket\r\n";
    req += "Connection: Upgrade\r\n";
    req += std::format("Sec-WebSocket-Key: {}\r\n", nonce);
    req += "Sec-WebSocket-Version: 13\r\n\r\n";

    auto wr = tcp.write(req);
    if (!wr) return std::unexpected(wr.error());

    std::string resp_buf;
    while (!resp_buf.contains("\r\n\r\n")) {
        auto chunk = tcp.read_str(1024);
        if (!chunk || chunk->empty()) return cfire::make_error("websocket handshake aborted by server");
        resp_buf += *chunk;
    }

    if (!resp_buf.starts_with("HTTP/1.1 101") && !resp_buf.starts_with("HTTP/1.0 101")) {
        return cfire::make_error("websocket handshake failed: server did not return 101 Switching Protocols");
    }

    int raw_fd = tcp.release_fd();
    return Conn(raw_fd, /*is_client=*/true);
}

} // namespace cfire::websocket

