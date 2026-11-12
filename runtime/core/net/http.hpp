#pragma once
#include "tcp.hpp"
#include "tls.hpp"
#include "url.hpp"

#if defined(__EMSCRIPTEN__) || defined(PLATFORM_WEB)
#include <emscripten.h>
#elif defined(__APPLE__)
#include <dlfcn.h>
#endif

#include <functional>
#include <optional>
#include <vector>
#include <string>
#include <string_view>
#include <expected>
#include <unordered_map>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <type_traits>
#include <cctype>
#include <algorithm>

namespace cfire::http {
struct Request;
}

namespace cfire::websocket {
class Conn;
[[nodiscard]] inline auto upgrade(const cfire::http::Request& req, cfire::net::TcpConn& tcp) -> std::expected<Conn, cfire::Error>;
}

// --- `core:net/http` (RFC 9112 HTTP/1.1 & HTTPS Client, Wire Protocol Engine, Server & Independent Router) ---
namespace cfire::http {

inline constexpr std::size_t kMaxHttpHeaders = 128;
inline constexpr std::size_t kMaxHttpQueryParams = 256;

namespace detail {
// CF-151: `x-cfire-*` header names are reserved and never sent. They used to be read as in-band TLS
// controls (`x-cfire-tls-verify: false` disabled certificate checks), so code that forwarded
// user-supplied headers could be downgraded. TLS behavior now comes only from typed parameters.
[[nodiscard]] inline auto is_reserved_cfire_header(std::string_view k) -> bool {
    if (k.size() < 8) return false;
    for (std::size_t i = 0; i < 8; ++i) {
        if (std::tolower(static_cast<unsigned char>(k[i])) != "x-cfire-"[i]) return false;
    }
    return true;
}

[[nodiscard]] inline auto strip_crlf(std::string_view sv) -> std::string {
    std::string out;
    out.reserve(sv.size());
    for (char c : sv) {
        if (c != '\r' && c != '\n' && c != '\0') {
            out.push_back(c);
        }
    }
    return out;
}
} // namespace detail

[[nodiscard]] inline auto status_text_for_code(int code) -> std::string {
    switch (code) {
        case 101: return "Switching Protocols";
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
        case 426: return "Upgrade Required";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        default: return (code >= 200 && code < 300) ? "OK" : "Error";
    }
}

struct Response {
    int status_code = 200;
    int status = 200;
    std::string status_text{"OK"};
    std::unordered_map<std::string, std::string> headers{};
    std::string body{};

    [[nodiscard]] auto is_ok() const noexcept -> bool { return status_code >= 200 && status_code < 300; }

    auto set_header(std::string_view key, std::string_view val) -> Response& {
        std::string lower_k;
        lower_k.reserve(key.size());
        for (char c : key) {
            if (c != '\r' && c != '\n' && c != '\0') {
                lower_k.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }
        }
        headers[lower_k] = detail::strip_crlf(val);
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
        std::string text = status_text.empty() ? status_text_for_code(status_code) : detail::strip_crlf(status_text);
        std::string out = std::format("HTTP/1.1 {} {}\r\nContent-Length: {}\r\nConnection: close\r\n",
            status_code, text, body.size());
        for (const auto& [k, v] : headers) {
            out += std::format("{}: {}\r\n", detail::strip_crlf(k), detail::strip_crlf(v));
        }
        out += "\r\n";
        out += body;
        return out;
    }

    static auto text(int code, std::string_view payload) -> Response {
        Response r;
        r.status_code = code;
        r.status = code;
        r.status_text = status_text_for_code(code);
        r.headers["content-type"] = "text/plain; charset=utf-8";
        r.body = std::string(payload);
        return r;
    }

    static auto json(int code, std::string_view json_str) -> Response {
        Response r;
        r.status_code = code;
        r.status = code;
        r.status_text = status_text_for_code(code);
        r.headers["content-type"] = "application/json";
        r.body = std::string(json_str);
        return r;
    }

    static auto html(int code, std::string_view html_content) -> Response {
        Response r;
        r.status_code = code;
        r.status = code;
        r.status_text = status_text_for_code(code);
        r.headers["content-type"] = "text/html; charset=utf-8";
        r.body = std::string(html_content);
        return r;
    }

    static auto redirect(std::string_view location, int code = 302) -> Response {
        Response r;
        r.status_code = code;
        r.status = code;
        r.status_text = status_text_for_code(code);
        r.headers["location"] = detail::strip_crlf(location);
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
    std::string remote_addr{};  // peer IP address of the client connection (CF-150)
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

struct SseEvent {
    std::string event{};
    std::string data{};
    std::string id{};
};

[[nodiscard]] inline auto decode_chunked_body(std::string_view raw) -> std::string {
    std::string out;
    std::size_t pos = 0;
    while (pos < raw.size()) {
        std::size_t crlf = raw.find("\r\n", pos);
        std::size_t delim_len = 2;
        if (crlf == std::string_view::npos) {
            crlf = raw.find('\n', pos);
            delim_len = 1;
        }
        if (crlf == std::string_view::npos) break;
        std::string_view hex_sv = raw.substr(pos, crlf - pos);
        if (auto semi = hex_sv.find(';'); semi != std::string_view::npos) {
            hex_sv = hex_sv.substr(0, semi);
        }
        while (!hex_sv.empty() && (hex_sv.front() == ' ' || hex_sv.front() == '\t')) hex_sv.remove_prefix(1);
        while (!hex_sv.empty() && (hex_sv.back() == ' ' || hex_sv.back() == '\t')) hex_sv.remove_suffix(1);
        if (hex_sv.empty()) {
            pos = crlf + delim_len;
            continue;
        }
        std::size_t chunk_len = 0;
        for (char c : hex_sv) {
            chunk_len <<= 4;
            if (c >= '0' && c <= '9') chunk_len |= static_cast<std::size_t>(c - '0');
            else if (c >= 'a' && c <= 'f') chunk_len |= static_cast<std::size_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') chunk_len |= static_cast<std::size_t>(c - 'A' + 10);
            else return std::string(raw);
        }
        pos = crlf + delim_len;
        if (chunk_len == 0) break;
        std::size_t avail = std::min(chunk_len, raw.size() - pos);
        out.append(raw.substr(pos, avail));
        pos += avail;
        if (pos + 2 <= raw.size() && raw[pos] == '\r' && raw[pos + 1] == '\n') pos += 2;
        else if (pos < raw.size() && raw[pos] == '\n') pos += 1;
    }
    return out;
}

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
                res.status_code = cfire::detail::parse_int<int>(code_sv).value_or(0);
                res.status = res.status_code;
                if (sp2 != std::string_view::npos) {
                    res.status_text = std::string(line.substr(sp2 + 1));
                }
            }
        } else {
            std::size_t colon = line.find(':');
            if (colon != std::string_view::npos) {
                if (res.headers.size() >= kMaxHttpHeaders) {
                    return cfire::make_error("HTTP response header count exceeds maximum limit (128)");
                }
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
    if (auto it = res.headers.find("transfer-encoding"); it != res.headers.end() && it->second.contains("chunked")) {
        res.body = decode_chunked_body(res.body);
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
                        if (req.query_params.size() >= kMaxHttpQueryParams) {
                            return cfire::make_error("HTTP query parameter count exceeds maximum limit (256)");
                        }
                        req.query_params[cfire::url::decode(pair.substr(0, eq))] = cfire::url::decode(pair.substr(eq + 1));
                    } else if (!pair.empty()) {
                        if (req.query_params.size() >= kMaxHttpQueryParams) {
                            return cfire::make_error("HTTP query parameter count exceeds maximum limit (256)");
                        }
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
                if (req.headers.size() >= kMaxHttpHeaders) {
                    return cfire::make_error("HTTP request header count exceeds maximum limit (128)");
                }
                std::string_view k = line.substr(0, colon);
                std::string_view v = line.substr(colon + 1);
                while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.remove_prefix(1);
                std::string lower_k;
                lower_k.reserve(k.size());
                for (char c : k) lower_k.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                if ((lower_k == "content-length" || lower_k == "transfer-encoding") && req.headers.contains(lower_k)) {
                    // Q22 (CF-150): duplicate framing headers are a request-smuggling vector.
                    return cfire::make_error(std::format("duplicate {} header", lower_k), 400);
                }
                req.headers[lower_k] = std::string(v);
            }
        }
        line_start = line_end + 1;
    }

    return req;
}

// Q22 (CF-150): per-connection limits for the built-in server. Errors carry the HTTP status to send
// (Error::code): 400 malformed / ambiguous framing, 408 deadline exceeded, 413 body too large.
// Q31: the server certificate for `make(http.Server, port, tls: http.TlsConfig{...})`: files or in-memory PEM.
struct TlsConfig {
    std::string cert_file{};
    std::string key_file{};
    std::string cert_pem{};
    std::string key_pem{};
};

struct ServerLimits {
    int header_timeout_ms = 10000;
    int body_timeout_ms = 30000;
    std::size_t max_body_bytes = 10 * 1024 * 1024;
    std::size_t workers = std::max<std::size_t>(4, std::thread::hardware_concurrency());
    std::size_t max_pending = 128;
};

namespace detail {
enum class ChunkStatus : std::uint8_t { NeedMore, Done, Malformed, TooLarge };

// Strict incremental chunked-body decoder: `raw` is everything received after the header section.
// Only CRLF line endings, hex sizes (with optional `;ext`), CRLF after each chunk, trailers ignored.
[[nodiscard]] inline auto decode_chunked_strict(std::string_view raw, std::size_t max_bytes, std::string& out) -> ChunkStatus {
    out.clear();
    std::size_t pos = 0;
    for (;;) {
        std::size_t crlf = raw.find("\r\n", pos);
        if (crlf == std::string_view::npos) return raw.size() - pos > 64 ? ChunkStatus::Malformed : ChunkStatus::NeedMore;
        std::string_view size_sv = raw.substr(pos, crlf - pos);
        if (auto semi = size_sv.find(';'); semi != std::string_view::npos) size_sv = size_sv.substr(0, semi);
        if (size_sv.empty() || size_sv.size() > 15) return ChunkStatus::Malformed;
        std::size_t len = 0;
        for (char c : size_sv) {
            int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
            if (d < 0) return ChunkStatus::Malformed;
            len = (len << 4) | static_cast<std::size_t>(d);
        }
        pos = crlf + 2;
        if (len == 0) {
            // Trailer section: lines until an empty line.
            for (;;) {
                std::size_t t = raw.find("\r\n", pos);
                if (t == std::string_view::npos) return ChunkStatus::NeedMore;
                if (t == pos) return ChunkStatus::Done;
                pos = t + 2;
            }
        }
        if (out.size() + len > max_bytes) return ChunkStatus::TooLarge;
        if (raw.size() < pos + len + 2) return ChunkStatus::NeedMore;
        if (raw[pos + len] != '\r' || raw[pos + len + 1] != '\n') return ChunkStatus::Malformed;
        out.append(raw.substr(pos, len));
        pos += len + 2;
    }
}

enum class ReadMore : std::uint8_t { Data, Eof, Deadline };

// Reads more bytes into `buf` before an absolute deadline. A per-read socket timeout (recv error after
// waiting) keeps waiting until the deadline; an immediate error (reset) or a zero-length read is EOF.
[[nodiscard]] inline auto read_more_until(cfire::net::TcpConn& conn, std::string& buf,
                                          std::chrono::steady_clock::time_point deadline) -> ReadMore {
    using clock = std::chrono::steady_clock;
    for (;;) {
        auto now = clock::now();
        if (now >= deadline) return ReadMore::Deadline;
        auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        conn.set_timeout_ms(static_cast<int>(std::clamp<long long>(left, 1, 2000)));
        auto started = clock::now();
        auto chunk = conn.read_text(8192);
        if (chunk) {
            if (chunk->empty()) return ReadMore::Eof;
            buf.append(*chunk);
            return ReadMore::Data;
        }
        if (clock::now() - started < std::chrono::milliseconds(1)) return ReadMore::Eof;
    }
}
} // namespace detail

inline auto read_http_request(cfire::net::TcpConn& conn, const ServerLimits& lim)
    -> std::expected<Request, cfire::Error>
{
    using clock = std::chrono::steady_clock;
    std::string raw;
    std::size_t header_end = std::string::npos;
    const auto header_deadline = clock::now() + std::chrono::milliseconds(lim.header_timeout_ms);
    while (header_end == std::string::npos) {
        auto st = detail::read_more_until(conn, raw, header_deadline);
        if (st == detail::ReadMore::Deadline) return cfire::make_error("request header deadline exceeded", 408);
        if (st == detail::ReadMore::Eof) {
            if (raw.empty()) return cfire::make_error("connection closed before request");
            return cfire::make_error("malformed HTTP request: missing header delimiter", 400);
        }
        header_end = raw.find("\r\n\r\n");
        if (header_end == std::string::npos && raw.size() > 65536) {
            return cfire::make_error("HTTP header section exceeds maximum size (64KB)", 431);
        }
    }
    std::string_view head = std::string_view(raw).substr(0, header_end);
    for (std::size_t i = 0; i < head.size(); ++i) {
        if (head[i] == '\n' && (i == 0 || head[i - 1] != '\r')) {
            return cfire::make_error("malformed HTTP request: bare LF line ending", 400);
        }
    }
    auto req_res = parse_request(std::string_view(raw).substr(0, header_end + 4));
    if (!req_res) {
        int code = req_res.error().code() != 0 ? req_res.error().code() : 400;
        return cfire::make_error(std::string(req_res.error().message()), code);
    }
    auto& req = *req_res;
    std::string rest = raw.substr(header_end + 4);

    auto cl_it = req.headers.find("content-length");
    auto te_it = req.headers.find("transfer-encoding");
    if (cl_it != req.headers.end() && te_it != req.headers.end()) {
        return cfire::make_error("request has both Content-Length and Transfer-Encoding", 400);
    }
    const auto body_deadline = clock::now() + std::chrono::milliseconds(lim.body_timeout_ms);
    if (te_it != req.headers.end()) {
        std::string te;
        for (char c : te_it->second) if (c != ' ' && c != '\t') te.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        if (te != "chunked") return cfire::make_error(std::format("unsupported Transfer-Encoding '{}'", te_it->second), 400);
        std::string body;
        for (;;) {
            auto st = detail::decode_chunked_strict(rest, lim.max_body_bytes, body);
            if (st == detail::ChunkStatus::Done) break;
            if (st == detail::ChunkStatus::Malformed) return cfire::make_error("malformed chunked request body", 400);
            if (st == detail::ChunkStatus::TooLarge) return cfire::make_error("payload too large", 413);
            auto rs = detail::read_more_until(conn, rest, body_deadline);
            if (rs == detail::ReadMore::Deadline) return cfire::make_error("request body deadline exceeded", 408);
            if (rs == detail::ReadMore::Eof) return cfire::make_error("truncated chunked request body", 400);
        }
        req.body = std::move(body);
        req.headers.erase("transfer-encoding");
        return req;
    }

    std::size_t content_len = 0;
    if (cl_it != req.headers.end()) {
        std::string_view v = cl_it->second;
        while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) v.remove_suffix(1);
        if (v.empty() || v.size() > 19 || !std::all_of(v.begin(), v.end(), [](char c) { return c >= '0' && c <= '9'; })) {
            return cfire::make_error(std::format("invalid Content-Length '{}'", cl_it->second), 400);
        }
        content_len = cfire::detail::parse_int<std::size_t>(v).value_or(0);
    }
    if (content_len > lim.max_body_bytes) {
        return cfire::make_error(std::format("payload too large: Content-Length {} exceeds limit {}", content_len, lim.max_body_bytes), 413);
    }
    while (rest.size() < content_len) {
        auto rs = detail::read_more_until(conn, rest, body_deadline);
        if (rs == detail::ReadMore::Deadline) return cfire::make_error("request body deadline exceeded", 408);
        if (rs == detail::ReadMore::Eof) {
            return cfire::make_error(std::format("truncated request body: got {} of {} bytes", rest.size(), content_len), 400);
        }
    }
    rest.resize(content_len);
    req.body = std::move(rest);
    return req;
}

inline auto read_http_request(cfire::net::TcpConn& conn, std::size_t max_body_size = 10 * 1024 * 1024)
    -> std::expected<Request, cfire::Error>
{
    ServerLimits lim;
    lim.max_body_bytes = max_body_size;
    return read_http_request(conn, lim);
}

#if defined(__APPLE__)
namespace detail {
[[nodiscard]] inline auto request_apple_foundation(
    std::string_view method,
    std::string_view url_str,
    std::string_view body,
    std::string_view content_type,
    const std::unordered_map<std::string, std::string>& extra_headers
) -> std::expected<Response, cfire::Error> {
    void* objc_lib = ::dlopen("/usr/lib/libobjc.A.dylib", RTLD_LAZY | RTLD_GLOBAL);
    (void)::dlopen("/System/Library/Frameworks/Foundation.framework/Foundation", RTLD_LAZY | RTLD_GLOBAL);
    (void)::dlopen("/System/Library/Frameworks/CFNetwork.framework/CFNetwork", RTLD_LAZY | RTLD_GLOBAL);
    void* handle = objc_lib ? objc_lib : RTLD_DEFAULT;

    using ObjcGetClassFn = void*(*)(const char*);
    using SelRegisterNameFn = void*(*)(const char*);
    using ObjcMsgSendFn = void(*)();

    auto get_class = reinterpret_cast<ObjcGetClassFn>(::dlsym(handle, "objc_getClass"));
    auto sel_reg = reinterpret_cast<SelRegisterNameFn>(::dlsym(handle, "sel_registerName"));
    auto msg_send = reinterpret_cast<ObjcMsgSendFn>(::dlsym(handle, "objc_msgSend"));
    if (!get_class || !sel_reg || !msg_send) {
        return cfire::make_error("Objective-C runtime unavailable for HTTPS request");
    }

    void* nsStrCls = get_class("NSString");
    void* nsUrlCls = get_class("NSURL");
    void* nsReqCls = get_class("NSMutableURLRequest");
    void* nsDataCls = get_class("NSData");
    void* nsConnCls = get_class("NSURLConnection");
    if (!nsStrCls || !nsUrlCls || !nsReqCls || !nsDataCls || !nsConnCls) {
        return cfire::make_error("Foundation networking classes unavailable");
    }

    using FnStrUTF8 = void*(*)(void*, void*, const char*);
    using FnObj1 = void*(*)(void*, void*, void*);
    using FnVoid1 = void(*)(void*, void*, void*);
    using FnVoid2 = void(*)(void*, void*, void*, void*);
    using FnDataBytes = void*(*)(void*, void*, const void*, unsigned long);
    using FnSyncReq = void*(*)(void*, void*, void*, void**, void**);
    using FnLong = long(*)(void*, void*);
    using FnBytes = const void*(*)(void*, void*);
    using FnULong = unsigned long(*)(void*, void*);

    auto fnStr = reinterpret_cast<FnStrUTF8>(msg_send);
    void* selUTF8 = sel_reg("stringWithUTF8String:");

    std::string u_s(url_str);
    void* urlNsStr = fnStr(nsStrCls, selUTF8, u_s.c_str());
    void* nsUrl = urlNsStr ? reinterpret_cast<FnObj1>(msg_send)(nsUrlCls, sel_reg("URLWithString:"), urlNsStr) : nullptr;
    if (!nsUrl) return cfire::make_error("invalid URL for Foundation HTTPS request");

    void* req = reinterpret_cast<FnObj1>(msg_send)(nsReqCls, sel_reg("requestWithURL:"), nsUrl);
    if (!req) return cfire::make_error("failed to allocate NSMutableURLRequest");

    std::string m_s(method);
    void* mNsStr = fnStr(nsStrCls, selUTF8, m_s.c_str());
    reinterpret_cast<FnVoid1>(msg_send)(req, sel_reg("setHTTPMethod:"), mNsStr);

    void* selSetHdr = sel_reg("setValue:forHTTPHeaderField:");
    if (!content_type.empty()) {
        std::string ct_s(content_type);
        void* kStr = fnStr(nsStrCls, selUTF8, "Content-Type");
        void* vStr = fnStr(nsStrCls, selUTF8, ct_s.c_str());
        if (kStr && vStr) reinterpret_cast<FnVoid2>(msg_send)(req, selSetHdr, vStr, kStr);
    }
    for (const auto& [k, v] : extra_headers) {
        if (detail::is_reserved_cfire_header(k)) continue;
        void* kStr = fnStr(nsStrCls, selUTF8, k.c_str());
        void* vStr = fnStr(nsStrCls, selUTF8, v.c_str());
        if (kStr && vStr) reinterpret_cast<FnVoid2>(msg_send)(req, selSetHdr, vStr, kStr);
    }
    if (!body.empty()) {
        void* bodyData = reinterpret_cast<FnDataBytes>(msg_send)(
            nsDataCls, sel_reg("dataWithBytes:length:"), body.data(), static_cast<unsigned long>(body.size()));
        if (bodyData) {
            reinterpret_cast<FnVoid1>(msg_send)(req, sel_reg("setHTTPBody:"), bodyData);
        }
    }

    void* urlResp = nullptr;
    void* urlErr = nullptr;
    void* respData = reinterpret_cast<FnSyncReq>(msg_send)(
        nsConnCls, sel_reg("sendSynchronousRequest:returningResponse:error:"), req, &urlResp, &urlErr);
    if (!respData && urlErr) {
        return cfire::make_error("Foundation HTTPS request failed");
    }

    Response out;
    if (urlResp) {
        long code = reinterpret_cast<FnLong>(msg_send)(urlResp, sel_reg("statusCode"));
        if (code > 0) {
            out.status_code = static_cast<int>(code);
            out.status = out.status_code;
        }
        out.status_text = status_text_for_code(out.status_code);
    }
    if (respData) {
        const void* ptr = reinterpret_cast<FnBytes>(msg_send)(respData, sel_reg("bytes"));
        unsigned long len = reinterpret_cast<FnULong>(msg_send)(respData, sel_reg("length"));
        if (ptr && len > 0) {
            out.body.assign(reinterpret_cast<const char*>(ptr), len);
        }
    }
    return out;
}
} // namespace detail
#endif

[[nodiscard]] inline auto request(
    std::string_view method,
    std::string_view url_str,
    std::string_view body = "",
    std::string_view content_type = "",
    const std::unordered_map<std::string, std::string>& extra_headers = {},
    bool verify_peer = true,
    std::string_view ca_file = ""
) -> std::expected<Response, cfire::Error> {
#if defined(__EMSCRIPTEN__) || defined(PLATFORM_WEB)
    (void)verify_peer;
    (void)ca_file;
    std::string m_str(method);
    std::string u_str(url_str);
    std::string b_str(body);
    std::string ct_str(content_type);
    std::string hdrs_packed;
    for (const auto& [k, v] : extra_headers) {
        if (detail::is_reserved_cfire_header(k)) continue;
        hdrs_packed += detail::strip_crlf(k) + ": " + detail::strip_crlf(v) + "\n";
    }
    char* raw_ptr = reinterpret_cast<char*>(EM_ASM_PTR({
        try {
            var method = UTF8ToString($0);
            var url = UTF8ToString($1);
            var body = UTF8ToString($2);
            var contentType = UTF8ToString($3);
            var extraHeaders = UTF8ToString($4);
            var xhr = new XMLHttpRequest();
            xhr.open(method, url, false);
            if (contentType && contentType.length > 0) {
                xhr.setRequestHeader("Content-Type", contentType);
            }
            if (extraHeaders && extraHeaders.length > 0) {
                var lines = extraHeaders.split("\n");
                for (var i = 0; i < lines.length; i++) {
                    var idx = lines[i].indexOf(":");
                    if (idx > 0) {
                        var hk = lines[i].substring(0, idx).trim();
                        var hv = lines[i].substring(idx + 1).trim();
                        if (hk.length > 0) xhr.setRequestHeader(hk, hv);
                    }
                }
            }
            if (body && body.length > 0) {
                xhr.send(body);
            } else {
                xhr.send(null);
            }
            if (xhr.status === 0) return 0;
            var combined = String(xhr.status) + "\n" + (xhr.responseText || "");
            var len = lengthBytesUTF8(combined) + 1;
            var buf = _malloc(len);
            stringToUTF8(combined, buf, len);
            return buf;
        } catch (e) {
            return 0;
        }
    }, m_str.c_str(), u_str.c_str(), b_str.c_str(), ct_str.c_str(), hdrs_packed.c_str()));

    if (!raw_ptr) {
        return cfire::make_error("WASM XMLHttpRequest failed or server unreachable");
    }
    std::string combined(raw_ptr);
    std::free(raw_ptr);
    auto nl = combined.find('\n');
    int status_code = (nl != std::string::npos)
        ? cfire::detail::parse_int<int>(combined.substr(0, nl)).value_or(200)
        : 200;
    Response res;
    res.status_code = status_code;
    res.status = status_code;
    res.status_text = status_text_for_code(status_code);
    res.body = (nl != std::string::npos) ? combined.substr(nl + 1) : std::move(combined);
    return res;
#else
    auto u = cfire::url::parse(url_str);
    if (!u) return std::unexpected(u.error());

#if defined(PLATFORM_IOS) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
    if (u->scheme == "https") {
        return detail::request_apple_foundation(method, url_str, body, content_type, extra_headers);
    }
#endif

    auto conn_res = cfire::net::connect_tcp(u->host, u->port);
    if (!conn_res) return std::unexpected(conn_res.error());
    auto& conn = *conn_res;

    if (u->scheme == "https") {
        auto tls_res = cfire::tls::wrap_client(conn, u->host, verify_peer, std::string(ca_file));
        if (!tls_res) {
#if defined(__APPLE__)
            return detail::request_apple_foundation(method, url_str, body, content_type, extra_headers);
#else
            return std::unexpected(tls_res.error());
#endif
        }
    }

    std::string req;
    std::string full_path = u->path;
    if (!u->query.empty()) full_path += "?" + u->query;

    req += std::format("{} {} HTTP/1.1\r\n", detail::strip_crlf(method), detail::strip_crlf(full_path));
    req += std::format("Host: {}\r\n", detail::strip_crlf(u->host));
    req += "Connection: close\r\n";
    req += "User-Agent: cfire-http/1.0\r\n";
    if (!content_type.empty()) {
        req += std::format("Content-Type: {}\r\n", detail::strip_crlf(content_type));
    }
    if (!body.empty() || method == "POST" || method == "PUT" || method == "PATCH") {
        req += std::format("Content-Length: {}\r\n", body.size());
    }
    for (const auto& [k, v] : extra_headers) {
        std::string lk;
        for (char c : k) lk.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        if (detail::is_reserved_cfire_header(lk)) continue;
        req += std::format("{}: {}\r\n", detail::strip_crlf(k), detail::strip_crlf(v));
    }
    req += "\r\n";
    if (!body.empty()) {
        req += body;
    }

    auto wr = conn.write_text(req);
    if (!wr) return std::unexpected(wr.error());

    std::string full_response;
    for (;;) {
        auto chunk = conn.read_text(4096);
        if (!chunk || chunk->empty()) break;
        full_response.append(*chunk);
    }

    return parse_response(full_response);
#endif
}

[[nodiscard]] inline auto get(
    std::string_view url_str,
    const std::unordered_map<std::string, std::string>& headers = {},
    bool verify_peer = true
) -> std::expected<Response, cfire::Error> {
    return request("GET", url_str, "", "", headers, verify_peer);
}

[[nodiscard]] inline auto get_insecure(
    std::string_view url_str,
    const std::unordered_map<std::string, std::string>& headers = {}
) -> std::expected<Response, cfire::Error> {
    return request("GET", url_str, "", "", headers, false);
}

[[nodiscard]] inline auto post(
    std::string_view url_str,
    std::string_view body,
    std::string_view content_type = "application/json",
    const std::unordered_map<std::string, std::string>& headers = {},
    bool verify_peer = true
) -> std::expected<Response, cfire::Error> {
    return request("POST", url_str, body, content_type, headers, verify_peer);
}

[[nodiscard]] inline auto post_insecure(
    std::string_view url_str,
    std::string_view body,
    std::string_view content_type = "application/json",
    const std::unordered_map<std::string, std::string>& headers = {}
) -> std::expected<Response, cfire::Error> {
    return request("POST", url_str, body, content_type, headers, false);
}

template <typename Callback>
[[nodiscard]] inline auto post_sse(
    std::string_view url_str,
    std::string_view body,
    const std::unordered_map<std::string, std::string>& extra_headers,
    Callback&& on_event,
    bool verify_peer = true,
    std::string_view ca_file = ""
) -> std::expected<Response, cfire::Error> {
    auto dispatch_sse_text = [&](std::string_view text, SseEvent& cur_ev, Response& res_acc, bool& stop_flag) {
        std::size_t pos = 0;
        while (pos < text.size() && !stop_flag) {
            std::size_t nl = text.find('\n', pos);
            if (nl == std::string_view::npos) break;
            std::string_view line = text.substr(pos, nl - pos);
            if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
            pos = nl + 1;

            if (line.empty()) {
                if (!cur_ev.data.empty() || !cur_ev.event.empty()) {
                    res_acc.body += cur_ev.data + "\n";
                    if constexpr (std::is_same_v<std::invoke_result_t<Callback&, const SseEvent&>, bool>) {
                        if (!on_event(cur_ev)) stop_flag = true;
                    } else {
                        on_event(cur_ev);
                    }
                    cur_ev = SseEvent{};
                }
                continue;
            }
            if (line.starts_with(':')) continue;
            if (line.starts_with("event:")) {
                std::string_view v = line.substr(6);
                while (!v.empty() && v.front() == ' ') v.remove_prefix(1);
                cur_ev.event = std::string(v);
            } else if (line.starts_with("id:")) {
                std::string_view v = line.substr(3);
                while (!v.empty() && v.front() == ' ') v.remove_prefix(1);
                cur_ev.id = std::string(v);
            } else if (line.starts_with("data:")) {
                std::string_view v = line.substr(5);
                if (!v.empty() && v.front() == ' ') v.remove_prefix(1);
                if (!cur_ev.data.empty()) cur_ev.data += "\n";
                cur_ev.data.append(v);
            }
        }
        return pos;
    };

#if defined(__EMSCRIPTEN__) || defined(PLATFORM_WEB)
    auto hdrs = extra_headers;
    hdrs["Accept"] = "text/event-stream";
    auto res = request("POST", url_str, body, "application/json", hdrs, verify_peer);
    if (!res) return res;
    std::string raw_sse = std::move(res->body);
    if (!raw_sse.ends_with("\n\n")) raw_sse += "\n\n";
    res->body.clear();
    SseEvent cur_ev;
    bool stop_flag = false;
    (void)dispatch_sse_text(raw_sse, cur_ev, *res, stop_flag);
    return res;
#else
    auto u = cfire::url::parse(url_str);
    if (!u) return std::unexpected(u.error());

#if defined(PLATFORM_IOS) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
    if (u->scheme == "https") {
        auto hdrs = extra_headers;
        hdrs["Accept"] = "text/event-stream";
        auto res = detail::request_apple_foundation("POST", url_str, body, "application/json", hdrs);
        if (!res) return res;
        std::string raw_sse = std::move(res->body);
        if (!raw_sse.ends_with("\n\n")) raw_sse += "\n\n";
        res->body.clear();
        SseEvent cur_ev;
        bool stop_flag = false;
        (void)dispatch_sse_text(raw_sse, cur_ev, *res, stop_flag);
        return res;
    }
#endif

    auto conn_res = cfire::net::connect_tcp(u->host, u->port);
    if (!conn_res) return std::unexpected(conn_res.error());
    auto& conn = *conn_res;

    if (u->scheme == "https") {
        auto tls_res = cfire::tls::wrap_client(conn, u->host, verify_peer, std::string(ca_file));
        if (!tls_res) {
#if defined(__APPLE__)
            auto hdrs = extra_headers;
            hdrs["Accept"] = "text/event-stream";
            auto res = detail::request_apple_foundation("POST", url_str, body, "application/json", hdrs);
            if (!res) return res;
            std::string raw_sse = std::move(res->body);
            if (!raw_sse.ends_with("\n\n")) raw_sse += "\n\n";
            res->body.clear();
            SseEvent cur_ev;
            bool stop_flag = false;
            (void)dispatch_sse_text(raw_sse, cur_ev, *res, stop_flag);
            return res;
#else
            return std::unexpected(tls_res.error());
#endif
        }
    }

    std::string full_path = u->path;
    if (!u->query.empty()) full_path += "?" + u->query;

    std::string req;
    req += std::format("POST {} HTTP/1.1\r\n", detail::strip_crlf(full_path));
    req += std::format("Host: {}\r\n", detail::strip_crlf(u->host));
    req += "Connection: close\r\n";
    req += "Accept: text/event-stream\r\n";
    req += "Content-Type: application/json\r\n";
    req += std::format("Content-Length: {}\r\n", body.size());
    for (const auto& [k, v] : extra_headers) {
        std::string lk;
        for (char c : k) lk.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        if (detail::is_reserved_cfire_header(lk) || lk == "accept" || lk == "content-type") continue;
        req += std::format("{}: {}\r\n", detail::strip_crlf(k), detail::strip_crlf(v));
    }
    req += "\r\n";
    req += body;

    if (auto wr = conn.write_text(req); !wr) return std::unexpected(wr.error());

    std::string raw_buf;
    std::size_t hdr_end = std::string::npos;
    std::size_t body_offset = 0;
    for (;;) {
        auto chunk = conn.read_text(4096);
        if (!chunk || chunk->empty()) break;
        raw_buf.append(*chunk);
        hdr_end = raw_buf.find("\r\n\r\n");
        if (hdr_end != std::string::npos) {
            body_offset = hdr_end + 4;
            break;
        }
        hdr_end = raw_buf.find("\n\n");
        if (hdr_end != std::string::npos) {
            body_offset = hdr_end + 2;
            break;
        }
    }
    if (hdr_end == std::string::npos) {
        return cfire::make_error("SSE response missing HTTP header delimiter");
    }

    auto parsed_hdr = parse_response(raw_buf.substr(0, body_offset));
    if (!parsed_hdr) return parsed_hdr;
    Response res = std::move(*parsed_hdr);
    std::string stream_buf = raw_buf.substr(body_offset);

    if (res.status_code >= 400) {
        for (;;) {
            auto chunk = conn.read_text(4096);
            if (!chunk || chunk->empty()) break;
            stream_buf.append(*chunk);
        }
        if (auto it = res.headers.find("transfer-encoding"); it != res.headers.end() && it->second.contains("chunked")) {
            res.body = decode_chunked_body(stream_buf);
        } else {
            res.body = std::move(stream_buf);
        }
        return res;
    }

    bool is_chunked = false;
    if (auto it = res.headers.find("transfer-encoding"); it != res.headers.end() && it->second.contains("chunked")) {
        is_chunked = true;
    }

    SseEvent cur_ev;
    bool stop_flag = false;
    std::string sse_pending;

    auto process_raw = [&]() {
        if (is_chunked) {
            // Decode complete HTTP chunks from stream_buf into sse_pending
            std::size_t pos = 0;
            while (pos < stream_buf.size()) {
                std::size_t crlf = stream_buf.find("\r\n", pos);
                std::size_t dlen = 2;
                if (crlf == std::string::npos) {
                    crlf = stream_buf.find('\n', pos);
                    dlen = 1;
                }
                if (crlf == std::string::npos) break;
                std::string_view hex_sv(stream_buf.data() + pos, crlf - pos);
                if (auto semi = hex_sv.find(';'); semi != std::string_view::npos) hex_sv = hex_sv.substr(0, semi);
                while (!hex_sv.empty() && (hex_sv.front() == ' ' || hex_sv.front() == '\t')) hex_sv.remove_prefix(1);
                while (!hex_sv.empty() && (hex_sv.back() == ' ' || hex_sv.back() == '\t')) hex_sv.remove_suffix(1);
                if (hex_sv.empty()) { pos = crlf + dlen; continue; }
                std::size_t clen = 0;
                bool ok_hex = true;
                for (char c : hex_sv) {
                    clen <<= 4;
                    if (c >= '0' && c <= '9') clen |= static_cast<std::size_t>(c - '0');
                    else if (c >= 'a' && c <= 'f') clen |= static_cast<std::size_t>(c - 'a' + 10);
                    else if (c >= 'A' && c <= 'F') clen |= static_cast<std::size_t>(c - 'A' + 10);
                    else { ok_hex = false; break; }
                }
                if (!ok_hex) {
                    sse_pending.append(stream_buf.substr(pos));
                    pos = stream_buf.size();
                    break;
                }
                if (clen == 0) {
                    pos = stream_buf.size();
                    break;
                }
                std::size_t data_start = crlf + dlen;
                if (data_start + clen + dlen > stream_buf.size()) break; // Wait for more bytes
                sse_pending.append(stream_buf.data() + data_start, clen);
                pos = data_start + clen;
                if (pos + 2 <= stream_buf.size() && stream_buf[pos] == '\r' && stream_buf[pos + 1] == '\n') pos += 2;
                else if (pos < stream_buf.size() && stream_buf[pos] == '\n') pos += 1;
            }
            if (pos > 0) stream_buf.erase(0, pos);
        } else {
            sse_pending.append(stream_buf);
            stream_buf.clear();
        }
        std::size_t consumed = dispatch_sse_text(sse_pending, cur_ev, res, stop_flag);
        if (consumed > 0) sse_pending.erase(0, consumed);
    };

    process_raw();
    while (!stop_flag) {
        auto chunk = conn.read_text(4096);
        if (!chunk || chunk->empty()) break;
        stream_buf.append(*chunk);
        process_raw();
    }
    if (!stop_flag && (!sse_pending.empty() || !cur_ev.data.empty())) {
        sse_pending += "\n\n";
        (void)dispatch_sse_text(sse_pending, cur_ev, res, stop_flag);
    }
    return res;
#endif
}

template <typename Callback>
[[nodiscard]] inline auto post_sse(
    std::string_view url_str,
    std::string_view body,
    Callback&& on_event,
    bool verify_peer = true
) -> std::expected<Response, cfire::Error> {
    return post_sse(url_str, body, std::unordered_map<std::string, std::string>{}, std::forward<Callback>(on_event), verify_peer);
}

class Router {
public:
    using Handler = std::function<Response(const Request&)>;
    using Middleware = std::function<std::optional<Response>(Request&)>;
    using WsHandler = std::function<void(const Request&, cfire::websocket::Conn&)>;

    struct Route {
        std::string method{};
        std::string pattern{};
        std::vector<std::string> segments{};
        Handler handler{};
    };

    struct WsRoute {
        std::string pattern{};
        std::vector<std::string> segments{};
        WsHandler handler{};
    };

    Router() = default;

    auto use(Middleware mw) -> Router& {
        middlewares_.push_back(std::move(mw));
        return *this;
    }

    template <typename F>
        requires std::is_invocable_r_v<int, F, const Request&> || std::is_invocable_r_v<int, F, Request&>
    auto use(F&& fn) -> Router& {
        middlewares_.push_back([f = std::forward<F>(fn)](Request& req) -> std::optional<Response> {
            int code = f(req);
            if (code != 0) {
                return Response::text(code, status_text_for_code(code));
            }
            return std::nullopt;
        });
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

    auto ws(std::string_view pattern, WsHandler handler) -> Router& {
        WsRoute r;
        r.pattern = std::string(pattern);
        r.segments = split_path(pattern);
        r.handler = std::move(handler);
        ws_routes_.push_back(std::move(r));
        return *this;
    }

    [[nodiscard]] auto has_ws_routes() const noexcept -> bool {
        return !ws_routes_.empty();
    }

    void set_not_found_handler(Handler handler) {
        not_found_handler_ = std::move(handler);
    }

    template <typename TcpConnT = cfire::net::TcpConn>
    [[nodiscard]] auto try_dispatch_ws(Request& req, TcpConnT& conn) const -> bool {
        if (ws_routes_.empty()) return false;
        std::string upg = req.header("upgrade");
        for (char& c : upg) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (upg != "websocket") return false;

        std::vector<std::string> path_segs = split_path(req.path);
        for (const auto& ws_route : ws_routes_) {
            std::unordered_map<std::string, std::string> params;
            if (match_route(ws_route.segments, path_segs, params)) {
                req.params = std::move(params);
                for (const auto& mw : middlewares_) {
                    auto early = mw(req);
                    if (early.has_value()) {
                        (void)conn.write_text(early->to_http_wire());
                        return true;
                    }
                }
                auto ws_res = cfire::websocket::upgrade(req, conn);
                if (!ws_res) {
                    Response err_resp = Response::bad_request(ws_res.error().message());
                    (void)conn.write_text(err_resp.to_http_wire());
                    return true;
                }
                ws_route.handler(req, *ws_res);
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] auto dispatch(Request req) const -> Response {
        std::size_t qm = req.path.find('?');
        if (qm != std::string::npos) {
            std::string qstr = req.path.substr(qm + 1);
            req.path = req.path.substr(0, qm);
            if (req.raw_query.empty()) {
                req.raw_query = qstr;
            }
            std::string_view qsv = qstr;
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
        }

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

        if (!ws_routes_.empty()) {
            for (const auto& ws_route : ws_routes_) {
                std::unordered_map<std::string, std::string> params;
                if (match_route(ws_route.segments, path_segs, params)) {
                    return Response::text(426, "426 Upgrade Required: WebSocket connection expected");
                }
            }
        }

        if (path_matched_any_method && !allowed_methods.empty()) {
            Response resp = Response::text(405, "405 Method Not Allowed");
            resp.status_code = 405;
            resp.status = 405;
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
    std::vector<WsRoute> ws_routes_{};
    std::vector<Middleware> middlewares_{};
    Handler not_found_handler_{};
};

[[nodiscard]] inline auto make_router() -> Router {
    return Router{};
}

class HttpServer {
public:
    HttpServer(cfire::net::TcpListener listener, std::optional<cfire::tls::ServerContext> tls_ctx = std::nullopt)
        : listener_(std::move(listener)), tls_ctx_(std::move(tls_ctx)) {}
    ~HttpServer() {
        stop();
    }

    HttpServer(HttpServer&& o) noexcept
        : listener_(std::move(o.listener_)),
          tls_ctx_(std::move(o.tls_ctx_)),
          running_(o.running_.load()) {
        o.running_ = false;
        if (o.worker_.joinable()) {
            worker_ = std::move(o.worker_);
        }
    }

    auto operator=(HttpServer&& o) noexcept -> HttpServer& {
        if (this != &o) {
            stop();
            listener_ = std::move(o.listener_);
            tls_ctx_ = std::move(o.tls_ctx_);
            running_ = o.running_.load();
            o.running_ = false;
            if (o.worker_.joinable()) {
                worker_ = std::move(o.worker_);
            }
        }
        return *this;
    }

    auto port() const noexcept -> std::uint16_t { return listener_.port(); }
    [[nodiscard]] auto is_tls() const noexcept -> bool { return tls_ctx_.has_value() && tls_ctx_->valid(); }

    auto enable_tls(cfire::tls::ServerContext ctx) -> HttpServer& {
        tls_ctx_ = std::move(ctx);
        return *this;
    }

    auto enable_tls_files(std::string_view cert_file, std::string_view key_file)
        -> std::expected<void, cfire::Error>
    {
        auto ctx_res = cfire::tls::ServerContext::from_files(cert_file, key_file);
        if (!ctx_res) return std::unexpected(ctx_res.error());
        tls_ctx_ = std::move(*ctx_res);
        return {};
    }

    auto enable_tls_pem(std::string_view cert_pem, std::string_view key_pem)
        -> std::expected<void, cfire::Error>
    {
        auto ctx_res = cfire::tls::ServerContext::from_pem(cert_pem, key_pem);
        if (!ctx_res) return std::unexpected(ctx_res.error());
        tls_ctx_ = std::move(*ctx_res);
        return {};
    }

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
        handle_conn(*conn_res, handler);
    }

    // Q22 (CF-150): connections are served by a bounded worker pool, so one slow or idle client cannot
    // stall the server; when every worker is busy and the queue is full, new clients get 503.
    template <typename Handler>
    void listen_and_serve(Handler&& handler) {
        running_ = true;
        std::mutex mu;
        std::condition_variable cv;
        std::deque<cfire::net::TcpConn> queue;
        bool closing = false;
        std::vector<std::thread> pool;
        const std::size_t n_workers = std::max<std::size_t>(1, limits_.workers);
        pool.reserve(n_workers);
        for (std::size_t i = 0; i < n_workers; ++i) {
            pool.emplace_back([&] {
                for (;;) {
                    std::optional<cfire::net::TcpConn> conn;
                    {
                        std::unique_lock<std::mutex> lk(mu);
                        cv.wait(lk, [&] { return closing || !queue.empty(); });
                        if (queue.empty()) return;
                        conn.emplace(std::move(queue.front()));
                        queue.pop_front();
                    }
                    handle_conn(*conn, handler);
                }
            });
        }
        while (running_) {
            auto conn_res = listener_.accept();
            if (!conn_res) {
                if (!running_) break;
                continue;
            }
            std::unique_lock<std::mutex> lk(mu);
            if (queue.size() >= limits_.max_pending) {
                lk.unlock();
                Response busy = Response::text(503, "503 Service Unavailable");
                (void)conn_res->write_text(busy.to_http_wire());
                continue;
            }
            queue.push_back(std::move(*conn_res));
            lk.unlock();
            cv.notify_one();
        }
        {
            std::lock_guard<std::mutex> lk(mu);
            closing = true;
        }
        cv.notify_all();
        for (auto& t : pool) t.join();
    }

    void set_limits(const ServerLimits& lim) noexcept {
        limits_ = lim;
        // From `.cf` code `workers = 0` means "keep the default pool size".
        if (limits_.workers == 0) limits_.workers = ServerLimits{}.workers;
    }
    [[nodiscard]] auto limits() const noexcept -> const ServerLimits& { return limits_; }

    template <typename Handler>
    void serve_in_background(Handler handler) {
        stop();
        running_ = true;
        worker_ = std::thread([this, h = std::move(handler)]() mutable {
            this->listen_and_serve(h);
        });
    }

private:
    template <typename Handler>
    void handle_conn(cfire::net::TcpConn& conn, Handler& handler) {
        if (tls_ctx_.has_value() && tls_ctx_->valid()) {
            auto wr = tls_ctx_->wrap_server(conn);
            if (!wr) return;
        }
        auto req_res = read_http_request(conn, limits_);
        if (!req_res) {
            int status = req_res.error().code();
            if (status != 400 && status != 408 && status != 413 && status != 431) status = 400;
            std::string_view reason = status == 408 ? "Request Timeout" : status == 413 ? "Payload Too Large"
                                    : status == 431 ? "Request Header Fields Too Large" : "Bad Request";
            Response err_resp = Response::text(status, std::format("{} {}", status, reason));
            (void)conn.write_text(err_resp.to_http_wire());
            return;
        }
        req_res->remote_addr = conn.endpoint();
        if constexpr (std::is_same_v<std::decay_t<Handler>, Router>) {
            if (handler.try_dispatch_ws(*req_res, conn)) {
                return;
            }
        }
        Response resp = handler(*req_res);
        (void)conn.write_text(resp.to_http_wire());
    }

    cfire::net::TcpListener listener_;
    std::optional<cfire::tls::ServerContext> tls_ctx_{std::nullopt};
    std::atomic<bool> running_{false};
    std::thread worker_{};
    ServerLimits limits_{};
};

[[nodiscard]] inline auto make_server(std::uint16_t port = 0)
    -> std::expected<HttpServer, cfire::Error>
{
    auto l = cfire::net::listen_tcp(port);
    if (!l) return std::unexpected(l.error());
    return HttpServer(std::move(*l));
}

[[nodiscard]] inline auto make_tls_server(
    std::uint16_t port,
    std::string_view cert_file,
    std::string_view key_file
) -> std::expected<HttpServer, cfire::Error> {
    auto ctx_res = cfire::tls::ServerContext::from_files(cert_file, key_file);
    if (!ctx_res) return std::unexpected(ctx_res.error());
    auto l = cfire::net::listen_tcp(port);
    if (!l) return std::unexpected(l.error());
    return HttpServer(std::move(*l), std::move(*ctx_res));
}

[[nodiscard]] inline auto make_tls_server_pem(
    std::uint16_t port,
    std::string_view cert_pem,
    std::string_view key_pem
) -> std::expected<HttpServer, cfire::Error> {
    auto ctx_res = cfire::tls::ServerContext::from_pem(cert_pem, key_pem);
    if (!ctx_res) return std::unexpected(ctx_res.error());
    auto l = cfire::net::listen_tcp(port);
    if (!l) return std::unexpected(l.error());
    return HttpServer(std::move(*l), std::move(*ctx_res));
}

// Q31: `make(http.Server, port, tls: cfg)`. An empty config builds a plain HTTP server.
[[nodiscard]] inline auto make_server(std::uint16_t port, const TlsConfig& tls) -> std::expected<HttpServer, cfire::Error> {
    if (!tls.cert_file.empty() || !tls.key_file.empty()) return make_tls_server(port, tls.cert_file, tls.key_file);
    if (!tls.cert_pem.empty() || !tls.key_pem.empty()) return make_tls_server_pem(port, tls.cert_pem, tls.key_pem);
    return make_server(port);
}

} // namespace cfire::http

#include "websocket.hpp"
