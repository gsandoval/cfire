#pragma once
// tests/unit/test_cf192.hpp — CF-192: HTTP keep-alive, pipelining, WebSocket sessions off the worker pool.

#include "../test_harness.hpp"
#include "core/net/http.hpp"

namespace cfire_test_cf192 {
// One HTTP response from `conn` (headers plus a Content-Length body); bytes after it stay in `buf`.
// "" when the connection closed or timed out first.
inline auto read_response(cfire::net::TcpConn& conn, std::string& buf) -> std::string {
    conn.set_timeout_ms(3000);
    for (;;) {
        const auto end = buf.find("\r\n\r\n");
        if (end != std::string::npos) {
            std::size_t len = 0;
            const auto cl = buf.find("Content-Length: ");
            if (cl != std::string::npos && cl < end) len = static_cast<std::size_t>(std::atoi(buf.c_str() + cl + 16));
            if (buf.size() >= end + 4 + len) {
                std::string out = buf.substr(0, end + 4 + len);
                buf.erase(0, end + 4 + len);
                return out;
            }
        }
        auto chunk = conn.read_text(4096);
        if (!chunk || chunk->empty()) return "";
        buf += *chunk;
    }
}

// True when the server closed `conn` (EOF) within `ms`.
inline auto closed_within(cfire::net::TcpConn& conn, int ms) -> bool {
    conn.set_timeout_ms(ms);
    auto chunk = conn.read_text(4096);
    return chunk.has_value() && chunk->empty();
}

inline auto ws_upgrade_request(std::string_view path) -> std::string {
    return std::format("GET {} HTTP/1.1\r\nHost: a\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                       "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n", path);
}
} // namespace cfire_test_cf192

void test_cf192_keepalive_and_websocket_sessions([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_cf192_keepalive_and_websocket_sessions (CF-192)");
    using cfire_test_cf192::closed_within;
    using cfire_test_cf192::read_response;
    using Status = cfire::http::detail::RequestAssembler::Status;

    // 1. The assembler on its own: byte-by-byte input, pipelined leftovers, keep-alive rules, error statuses.
    {
        cfire::http::detail::RequestAssembler as(64);
        const std::string two = "POST /a HTTP/1.1\r\nContent-Length: 3\r\n\r\nabcGET /b HTTP/1.0\r\n\r\n";
        Status st = Status::NeedMore;
        std::size_t fed = 0;
        while (st == Status::NeedMore && fed < two.size()) {
            as.feed(two.substr(fed++, 1));
            st = as.step();
        }
        require(st == Status::Ready && fed == two.find("GET"), std::format("CF-192: the first request is ready after its last byte (fed {})", fed));
        auto first = as.take();
        require(first.path == "/a" && first.body == "abc" && as.keep_alive(), "CF-192: HTTP/1.1 request is parsed and keeps the connection");
        as.feed(two.substr(fed));
        require(as.step() == Status::Ready, "CF-192: a pipelined request is ready from the leftover bytes");
        auto second = as.take();
        require(second.path == "/b" && !as.keep_alive(), "CF-192: HTTP/1.0 without keep-alive closes");
        require(!as.started(), "CF-192: nothing is left after both requests");

        auto status_of = [](std::string_view wire) {
            cfire::http::detail::RequestAssembler a(64);
            a.feed(wire);
            return a.step() == Status::Error ? a.error_status() : 0;
        };
        require(status_of("GET / HTTP/1.1\r\nConnection: close\r\n\r\n") == 0, "CF-192: a valid request is not an error");
        require(status_of("POST / HTTP/1.1\r\nContent-Length: 5\r\nTransfer-Encoding: chunked\r\n\r\n") == 400, "CF-192: CL + TE is 400");
        require(status_of("GET / HTTP/1.1\nHost: a\n\n\r\n\r\n") == 400, "CF-192: a bare LF is 400");
        require(status_of("POST / HTTP/1.1\r\nContent-Length: 999\r\n\r\n") == 413, "CF-192: a body over the cap is 413");
        require(status_of("GET / HTTP/1.1\r\nX: " + std::string(70000, 'a')) == 431, "CF-192: a header section over 64 KB is 431");
        cfire::http::detail::RequestAssembler chunked(64);
        chunked.feed("POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n0\r\n\r\nGET /next HTTP/1.1\r\n\r\n");
        require(chunked.step() == Status::Ready && chunked.take().body == "abc", "CF-192: a chunked body is decoded");
        require(chunked.step() == Status::Ready && chunked.take().path == "/next", "CF-192: the request after a chunked body is kept");
        cfire::http::detail::RequestAssembler ka10(64);
        ka10.feed("GET / HTTP/1.0\r\nConnection: Keep-Alive\r\n\r\n");
        require(ka10.step() == Status::Ready && ka10.keep_alive(), "CF-192: HTTP/1.0 with Connection: keep-alive keeps the connection");
    }

    // 2. Keep-alive on a live server.
    auto srv = cfire::http::make_server(0);
    require(srv.has_value(), "CF-192: server starts");
    if (!srv) return;
    cfire::http::ServerLimits lim;
    lim.workers = 2;
    lim.keepalive_timeout_ms = 400;
    lim.max_requests_per_conn = 3;
    lim.max_websockets = 3;
    srv->set_limits(lim);
    auto router = cfire::http::make_router();
    router.get("/x", [](const cfire::http::Request& req) { return cfire::http::Response::text(200, "x" + req.query("n")); });
    router.get("/bye", [](const cfire::http::Request&) {
        auto r = cfire::http::Response::text(200, "bye");
        r.set_header("Connection", "close");
        return r;
    });
    router.ws("/ws", [](const cfire::http::Request&, cfire::websocket::Conn& ws) {
        while (auto msg = ws.recv_text()) {
            if (!ws.send_text("echo:" + *msg)) break;
        }
    });
    srv->serve_in_background(router);
    const auto port = srv->port();
    auto dial = [port] { return cfire::net::connect_tcp("127.0.0.1", port); };
    {
        auto c = dial();
        require(c.has_value(), "CF-192: client connects");
        if (c) {
            std::string buf;
            (void)c->write_text("GET /x?n=1 HTTP/1.1\r\nHost: a\r\n\r\n");
            auto r1 = read_response(*c, buf);
            require(r1.contains("Connection: keep-alive") && r1.ends_with("x1"), "CF-192: the first response keeps the connection, got: " + r1);
            (void)c->write_text("GET /x?n=2 HTTP/1.1\r\nHost: a\r\n\r\n");
            auto r2 = read_response(*c, buf);
            require(r2.ends_with("x2"), "CF-192: a second request on the same connection is answered, got: " + r2);
            (void)c->write_text("GET /x?n=3 HTTP/1.1\r\nHost: a\r\n\r\n");
            auto r3 = read_response(*c, buf);
            require(r3.contains("Connection: close") && r3.ends_with("x3"), "CF-192: the max_requests_per_conn-th response says close, got: " + r3);
            require(closed_within(*c, 2000), "CF-192: the server closes after max_requests_per_conn");
        }
    }
    {
        auto c = dial();
        if (c) {
            std::string buf;
            (void)c->write_text("GET /x?n=a HTTP/1.1\r\nHost: a\r\n\r\nGET /x?n=b HTTP/1.1\r\nHost: a\r\n\r\n");
            auto ra = read_response(*c, buf);
            auto rb = read_response(*c, buf);
            require(ra.ends_with("xa") && rb.ends_with("xb"), std::format("CF-192: both pipelined requests are answered in order, got: {} | {}", ra, rb));
        }
    }
    {
        auto c = dial();
        if (c) {
            std::string buf;
            (void)c->write_text("GET /x HTTP/1.1\r\nHost: a\r\nConnection: close\r\n\r\n");
            auto r = read_response(*c, buf);
            require(r.contains("Connection: close") && closed_within(*c, 2000), "CF-192: Connection: close from the client closes, got: " + r);
        }
        auto c10 = dial();
        if (c10) {
            std::string buf;
            (void)c10->write_text("GET /x HTTP/1.0\r\n\r\n");
            auto r = read_response(*c10, buf);
            require(r.contains("Connection: close") && closed_within(*c10, 2000), "CF-192: HTTP/1.0 without keep-alive closes, got: " + r);
        }
        auto cbye = dial();
        if (cbye) {
            std::string buf;
            (void)cbye->write_text("GET /bye HTTP/1.1\r\nHost: a\r\n\r\n");
            auto r = read_response(*cbye, buf);
            require(r.contains("Connection: close") && !r.contains("connection: close") && closed_within(*cbye, 2000),
                    "CF-192: a handler's Connection: close header closes once, got: " + r);
        }
    }
    {
        auto c = dial();
        if (c) {
            std::string buf;
            (void)c->write_text("GET /x HTTP/1.1\r\nHost: a\r\n\r\n");
            (void)read_response(*c, buf);
            const auto t0 = std::chrono::steady_clock::now();
            const bool closed = closed_within(*c, 3000);
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
            require(closed && ms < 2500, std::format("CF-192: an idle keep-alive connection closes after keepalive_timeout_ms ({} ms)", ms));
        }
    }

    // 3. WebSocket sessions do not hold pool workers (2 workers, 3 sessions), and over the cap is 503.
    {
        std::vector<cfire::websocket::Conn> sockets;
        for (int i = 0; i < 3; ++i) {
            auto ws = cfire::websocket::connect(std::format("ws://127.0.0.1:{}/ws", port));
            require(ws.has_value(), std::format("CF-192: WebSocket {} connects", i));
            if (ws) sockets.push_back(std::move(*ws));
        }
        for (std::size_t i = 0; i < sockets.size(); ++i) {
            require(sockets[i].send_text(std::format("m{}", i)).has_value(), "CF-192: WebSocket send");
            auto echo = sockets[i].recv_text();
            require(echo.has_value() && *echo == std::format("echo:m{}", i), "CF-192: each WebSocket session is served");
        }
        auto t0 = std::chrono::steady_clock::now();
        auto plain = cfire::http::get(std::format("http://127.0.0.1:{}/x?n=p", port));
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        require(plain.has_value() && plain->body == "xp" && ms < 2000,
                std::format("CF-192: plain HTTP is served while more WebSockets than workers are open ({} ms)", ms));
        require(srv->websocket_sessions() == 3, std::format("CF-192: 3 live WebSocket sessions, got {}", srv->websocket_sessions()));
        auto extra = dial();
        if (extra) {
            std::string buf;
            (void)extra->write_text(cfire_test_cf192::ws_upgrade_request("/ws"));
            auto r = read_response(*extra, buf);
            require(r.starts_with("HTTP/1.1 503"), "CF-192: a WebSocket over max_websockets gets 503, got: " + r);
        }
        // 4. stop() returns while the sessions are still open (it shuts their sockets down).
        auto t1 = std::chrono::steady_clock::now();
        srv->stop();
        auto stop_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t1).count();
        require(stop_ms < 5000, std::format("CF-192: stop() returns with open WebSockets ({} ms)", stop_ms));
        require(!sockets.empty() && !sockets[0].recv_text().has_value(), "CF-192: an open WebSocket sees the server go away");
    }
}
