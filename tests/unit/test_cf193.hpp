#pragma once
// tests/unit/test_cf193.hpp — CF-193: the HTTP server's event loop (idle and slow clients hold no worker).

#include "../test_harness.hpp"
#include "core/net/http.hpp"
#include "core/net/poller.hpp"
#include "test_cf192.hpp"

namespace cfire_test_cf193 {
inline auto ms_since(std::chrono::steady_clock::time_point t0) -> long long {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
}

// The fastest of three plain GETs, so the timing checks are relative to this machine (CF-184).
inline auto baseline_ms(std::uint16_t port) -> long long {
    long long best = 1 << 30;
    for (int i = 0; i < 3; ++i) {
        auto t0 = std::chrono::steady_clock::now();
        (void)cfire::http::get(std::format("http://127.0.0.1:{}/x", port));
        best = std::min(best, ms_since(t0));
    }
    return best;
}

inline auto timed_get(std::uint16_t port) -> std::pair<bool, long long> {
    auto t0 = std::chrono::steady_clock::now();
    auto r = cfire::http::get(std::format("http://127.0.0.1:{}/x", port));
    return {r.has_value() && r->status_code == 200, ms_since(t0)};
}
} // namespace cfire_test_cf193

void test_cf193_http_event_loop([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_cf193_http_event_loop (CF-193)");
    using cfire_test_cf192::read_response;
    using cfire_test_cf193::ms_since;
    using cfire_test_cf193::timed_get;

    // 1. The poller: a readable event carries its token, wake() ends a wait, remove() stops events.
    {
        cfire::net::detail::Poller poller;
        require(poller.valid(), "CF-193: the poller starts");
        auto pair = cfire::net::socketpair();
        require(pair.has_value(), "CF-193: socketpair");
        if (poller.valid() && pair) {
            auto& [a, b] = *pair;
            poller.add(a.fd(), 7, false);
            require(poller.wait(0).empty(), "CF-193: nothing is ready before a write");
            (void)b.write_text("x");
            const auto& ev = poller.wait(1000);
            require(ev.size() == 1 && ev[0].token == 7 && ev[0].readable, "CF-193: a write makes the other end readable");
            (void)a.read_text(16);
            std::thread waker([&poller] {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                poller.wake();
            });
            auto t0 = std::chrono::steady_clock::now();
            const bool none = poller.wait(5000).empty();
            const auto waited = ms_since(t0);
            waker.join();
            require(none && waited < 3000, std::format("CF-193: wake() ends a wait ({} ms)", waited));
            poller.remove(a.fd());
            (void)b.write_text("y");
            require(poller.wait(100).empty(), "CF-193: a removed socket reports nothing");
        }
    }

    // 2. An IPv4 client reaches a real listener over TCP, not the in-process fallback queue. On Windows the
    //    dual-stack option was rejected (wrong size), so 127.0.0.1 was refused for 2 s and then quietly
    //    connected through that queue, which the event loop did not look at.
    {
        auto l = cfire::net::listen_tcp(0);
        if (l && l->fd() >= 0) {
            auto t0 = std::chrono::steady_clock::now();
            auto c = cfire::net::connect_tcp("127.0.0.1", l->port());
            const auto ms = ms_since(t0);
            require(c.has_value() && cfire::net::detail::pop_loopback_fd(l->port()) < 0,
                    std::format("CF-193: 127.0.0.1 reaches the dual-stack listener directly ({} ms)", ms));
        }
    }

    auto srv = cfire::http::make_server(0);
    require(srv.has_value(), "CF-193: server starts");
    if (!srv) return;
    cfire::http::ServerLimits lim;
    lim.workers = 2;
    lim.header_timeout_ms = 3000;
    lim.max_connections = 300;
    srv->set_limits(lim);
    auto router = cfire::http::make_router();
    router.get("/x", [](const cfire::http::Request&) { return cfire::http::Response::text(200, "x"); });
    router.get("/slow", [](const cfire::http::Request&) {
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        return cfire::http::Response::text(200, "slow");
    });
    router.ws("/ws", [](const cfire::http::Request&, cfire::websocket::Conn& ws) {
        while (ws.recv_text()) {}
    });
    srv->serve_in_background(router);
    const auto port = srv->port();
    const auto baseline = cfire_test_cf193::baseline_ms(port);
    auto dial = [port] { return cfire::net::connect_tcp("127.0.0.1", port); };

    // 3. 200 idle connections (100 times the workers) do not delay a GET.
    {
        std::vector<cfire::net::TcpConn> idle;
        for (int i = 0; i < 200; ++i) {
            if (auto c = dial()) idle.push_back(std::move(*c));
        }
        require(idle.size() == 200, std::format("CF-193: 200 idle clients connect, got {}", idle.size()));
        auto [ok, ms] = timed_get(port);
        require(ok && ms < baseline + 400, std::format("CF-193: a GET with 200 idle clients ({} ms, baseline {} ms)", ms, baseline));
    }

    // 4. Clients that send their headers one byte at a time (workers + 4 of them) do not delay a GET.
    {
        std::vector<cfire::net::TcpConn> slow;
        for (int i = 0; i < 6; ++i) {
            if (auto c = dial()) slow.push_back(std::move(*c));
        }
        const std::string head = "GET /x HTTP/1.1\r\nHost: a\r\n";
        std::atomic<bool> dripping{true};
        std::thread drip([&] {
            for (std::size_t k = 0; k < head.size() && dripping; ++k) {
                for (auto& c : slow) (void)c.write_text(head.substr(k, 1));
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        auto [ok, ms] = timed_get(port);
        dripping = false;
        drip.join();
        require(ok && ms < baseline + 400, std::format("CF-193: a GET while 6 clients trickle headers ({} ms, baseline {} ms)", ms, baseline));
    }

    // 5. Over max_pending (1 worker busy, 1 waiting) a request gets 503.
    {
        auto s2 = cfire::http::make_server(0);
        if (s2) {
            cfire::http::ServerLimits l2;
            l2.workers = 1;
            l2.max_pending = 1;
            s2->set_limits(l2);
            s2->serve_in_background(router);
            auto p2 = s2->port();
            auto send_slow = [p2] {
                auto c = cfire::net::connect_tcp("127.0.0.1", p2);
                if (c) (void)c->write_text("GET /slow HTTP/1.1\r\nHost: a\r\n\r\n");
                return c;
            };
            auto first = send_slow();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            auto second = send_slow();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            auto third = send_slow();
            if (first && second && third) {
                std::string b1, b2, b3;
                auto r3 = read_response(*third, b3);
                auto r1 = read_response(*first, b1);
                auto r2 = read_response(*second, b2);
                require(r1.ends_with("slow") && r2.ends_with("slow") && r3.starts_with("HTTP/1.1 503"),
                        std::format("CF-193: busy + waiting are served and the next is 503, got: {} | {} | {}",
                                    r1.substr(0, 12), r2.substr(0, 12), r3.substr(0, 12)));
            }
            s2->stop();
        }
    }

    // 6. Over max_connections a new client gets 503.
    {
        auto s3 = cfire::http::make_server(0);
        if (s3) {
            cfire::http::ServerLimits l3;
            l3.max_connections = 3;
            s3->set_limits(l3);
            s3->serve_in_background(router);
            std::vector<cfire::net::TcpConn> held;
            for (int i = 0; i < 3; ++i) {
                if (auto c = cfire::net::connect_tcp("127.0.0.1", s3->port())) held.push_back(std::move(*c));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            auto extra = cfire::net::connect_tcp("127.0.0.1", s3->port());
            if (extra) {
                std::string buf;
                auto r = read_response(*extra, buf);
                require(r.starts_with("HTTP/1.1 503"), "CF-193: a connection over max_connections gets 503, got: " + r);
            }
            s3->stop();
        }
    }

    // 7. A virtual in-process port (no socket to poll) is served through the wake hook.
    {
        std::uint16_t vport = 0;
        if (auto probe = cfire::net::listen_tcp(0)) vport = probe->port();  // free now, so a real connect is refused
        if (vport != 0) {
            cfire::http::HttpServer vsrv(cfire::net::TcpListener(-1, vport));
            vsrv.serve_in_background(router);
            auto r = cfire::http::get(std::format("http://127.0.0.1:{}/x", vport));
            require(r.has_value() && r->body == "x", "CF-193: a virtual loopback port is served by the event loop");
            vsrv.stop();
        }
    }

    // 8. HTTPS: idle TLS clients do not block a TLS GET, and one TLS connection serves two requests.
    if (cfire::tls::available()) {
        auto pair = cfire::tls::generate_self_signed_pem("localhost", 30);
        std::expected<cfire::http::HttpServer, cfire::Error> tsrv = cfire::make_error("no certificate");
        if (pair) tsrv = cfire::http::make_tls_server_pem(0, pair->cert_pem, pair->key_pem);
        require(tsrv.has_value(), "CF-193: TLS server starts");
        if (tsrv) {
            cfire::http::ServerLimits tl;
            tl.workers = 2;
            tsrv->set_limits(tl);
            tsrv->serve_in_background(router);
            const auto tport = tsrv->port();
            std::vector<cfire::net::TcpConn> idle;
            for (int i = 0; i < 10; ++i) {
                if (auto c = cfire::net::connect_tcp("127.0.0.1", tport)) idle.push_back(std::move(*c));  // never handshake
            }
            auto t0 = std::chrono::steady_clock::now();
            auto r = cfire::http::get(std::format("https://127.0.0.1:{}/x", tport), {}, false);
            auto ms = ms_since(t0);
            require(r.has_value() && r->body == "x" && ms < 3000, std::format("CF-193: a TLS GET with 10 idle TLS clients ({} ms)", ms));
            auto c = cfire::net::connect_tcp("127.0.0.1", tport);
            if (c && cfire::tls::wrap_client(*c, "localhost", false)) {
                std::string buf;
                (void)c->write_text("GET /x HTTP/1.1\r\nHost: a\r\n\r\n");
                auto r1 = read_response(*c, buf);
                (void)c->write_text("GET /x HTTP/1.1\r\nHost: a\r\nConnection: close\r\n\r\n");
                auto r2 = read_response(*c, buf);
                require(r1.contains("Connection: keep-alive") && r1.ends_with("x") && r2.ends_with("x"),
                        std::format("CF-193: TLS keep-alive serves two requests, got: {} | {}", r1, r2));
            } else {
                require(false, "CF-193: TLS client handshake");
            }
            tsrv->stop();
        }
    }

    // 9. stop() returns with idle, busy and WebSocket connections open.
    {
        std::vector<cfire::net::TcpConn> idle;
        for (int i = 0; i < 5; ++i) {
            if (auto c = dial()) idle.push_back(std::move(*c));
        }
        auto busy = dial();
        if (busy) (void)busy->write_text("GET /slow HTTP/1.1\r\nHost: a\r\n\r\n");
        auto ws = cfire::websocket::connect(std::format("ws://127.0.0.1:{}/ws", port));
        require(ws.has_value(), "CF-193: WebSocket connects");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        auto t0 = std::chrono::steady_clock::now();
        srv->stop();
        auto ms = ms_since(t0);
        require(ms < 3000, std::format("CF-193: stop() with idle, busy and WebSocket connections returns ({} ms)", ms));
        if (busy) {
            std::string buf;
            auto r = read_response(*busy, buf);
            require(r.ends_with("slow"), "CF-193: the in-flight request is still answered, got: " + r);
        }
    }
}
