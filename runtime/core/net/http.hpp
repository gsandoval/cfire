#pragma once
#include "tcp.hpp"
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
#include <cctype>
#include <algorithm>

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
