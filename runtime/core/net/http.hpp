#pragma once
#include "tcp.hpp"
#include "tls.hpp"
#include "url.hpp"

#include <functional>
#include <optional>
#include <vector>
#include <string>
#include <string_view>
#include <expected>
#include <unordered_map>
#include <atomic>
#include <thread>
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
                res.status_code = cfire::detail::parse_int<int>(code_sv).value_or(0);
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
        content_len = cfire::detail::parse_int<std::size_t>(cl_it->second).value_or(0);
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
    const std::unordered_map<std::string, std::string>& extra_headers = {},
    bool verify_peer = true
) -> std::expected<Response, cfire::Error> {
    auto u = cfire::url::parse(url_str);
    if (!u) return std::unexpected(u.error());

    auto conn_res = cfire::net::connect_tcp(u->host, u->port);
    if (!conn_res) return std::unexpected(conn_res.error());
    auto& conn = *conn_res;

    bool effective_verify = verify_peer;
    std::string ca_file;
    for (const auto& [k, v] : extra_headers) {
        std::string lk;
        for (char c : k) lk.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        if (lk == "x-cfire-tls-verify" && (v == "false" || v == "0")) {
            effective_verify = false;
        } else if (lk == "x-cfire-tls-ca") {
            ca_file = v;
        }
    }

    if (u->scheme == "https") {
        auto tls_res = cfire::tls::wrap_client(conn, u->host, effective_verify, ca_file);
        if (!tls_res) return std::unexpected(tls_res.error());
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
        if (lk == "x-cfire-tls-verify" || lk == "x-cfire-tls-ca") continue;
        req += std::format("{}: {}\r\n", detail::strip_crlf(k), detail::strip_crlf(v));
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
                        (void)conn.write(early->to_http_wire());
                        return true;
                    }
                }
                auto ws_res = cfire::websocket::upgrade(req, conn);
                if (!ws_res) {
                    Response err_resp = Response::bad_request(ws_res.error().message());
                    (void)conn.write(err_resp.to_http_wire());
                    return true;
                }
                ws_route.handler(req, *ws_res);
                return true;
            }
        }
        return false;
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

    template <typename Handler>
    void listen_and_serve(Handler&& handler) {
        running_ = true;
        while (running_) {
            auto conn_res = listener_.accept();
            if (!conn_res) {
                if (!running_) break;
                continue;
            }
            handle_conn(*conn_res, handler);
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
    template <typename Handler>
    void handle_conn(cfire::net::TcpConn& conn, Handler& handler) {
        if (tls_ctx_.has_value() && tls_ctx_->valid()) {
            auto wr = tls_ctx_->wrap_server(conn);
            if (!wr) return;
        }
        auto req_res = read_http_request(conn);
        if (!req_res) {
            Response err_resp = Response::bad_request("400 Bad Request");
            (void)conn.write(err_resp.to_http_wire());
            return;
        }
        if constexpr (std::is_same_v<std::decay_t<Handler>, Router>) {
            if (handler.try_dispatch_ws(*req_res, conn)) {
                return;
            }
        }
        Response resp = handler(*req_res);
        (void)conn.write(resp.to_http_wire());
    }

    cfire::net::TcpListener listener_;
    std::optional<cfire::tls::ServerContext> tls_ctx_{std::nullopt};
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

} // namespace cfire::http

#include "websocket.hpp"
