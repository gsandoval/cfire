#pragma once
// tests/unit/test_cf194.hpp — CF-194: `websocket.Server` serves each connection on its own session thread.

#include "../test_harness.hpp"
#include "core/net/websocket.hpp"

#include <atomic>
#include <chrono>

void test_cf194_websocket_server_sessions([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_cf194_websocket_server_sessions (CF-194)");
    using clock = std::chrono::steady_clock;

    auto srv = cfire::websocket::make_server(0);
    require(srv.has_value(), "CF-194: server starts");
    if (!srv) return;
    cfire::websocket::ServerLimits lim;
    lim.max_websockets = 2;
    srv->set_limits(lim);
    std::atomic<int> served{0};
    srv->serve_in_background([&served](cfire::websocket::Conn& ws) {
        served.fetch_add(1);
        while (auto msg = ws.recv_text()) {
            if (!ws.send_text("echo:" + *msg)) break;
        }
    });
    const std::string url = std::format("ws://127.0.0.1:{}/", srv->port());

    // 1. Two clients at once: the second is answered while the first stays open.
    auto a = cfire::websocket::connect(url);
    auto b = cfire::websocket::connect(url);
    require(a.has_value() && b.has_value(), "CF-194: two clients connect");
    if (a && b) {
        a->set_timeout_ms(3000);
        b->set_timeout_ms(3000);
        require(b->send_text("b1").has_value(), "CF-194: the second client sends");
        auto rb = b->recv_text();
        require(rb && *rb == "echo:b1", "CF-194: the second client is answered while the first is open");
        require(a->send_text("a1").has_value(), "CF-194: the first client sends");
        auto ra = a->recv_text();
        require(ra && *ra == "echo:a1", "CF-194: the first client is still served");
        require(srv->sessions() == 2, std::format("CF-194: two sessions are open, got {}", srv->sessions()));
    }

    // 2. Over `max_websockets` a client gets 503 and the open sessions keep working.
    {
        auto c = cfire::net::connect_tcp("127.0.0.1", srv->port());
        require(c.has_value(), "CF-194: a third client connects over TCP");
        if (c) {
            c->set_timeout_ms(3000);
            std::string got;
            while (!got.contains("\r\n\r\n")) {
                auto chunk = c->read_text(4096);
                if (!chunk || chunk->empty()) break;
                got += *chunk;
            }
            require(got.starts_with("HTTP/1.1 503"), "CF-194: over the cap is 503, got: " + got);
        }
        if (a) {
            require(a->send_text("a2").has_value(), "CF-194: the first client sends after the 503");
            auto ra = a->recv_text();
            require(ra && *ra == "echo:a2", "CF-194: an open session is not affected by the 503");
        }
    }

    // 3. A closed session frees its slot.
    if (b) {
        b->close();
        bool reconnected = false;
        for (int i = 0; i < 50 && !reconnected; ++i) {
            if (srv->sessions() < 2) {
                auto d = cfire::websocket::connect(url);
                if (d && d->send_text("d1")) {
                    d->set_timeout_ms(3000);
                    auto rd = d->recv_text();
                    reconnected = rd && *rd == "echo:d1";
                }
            }
            if (!reconnected) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        require(reconnected, "CF-194: a new client is served once a session has closed");
    }

    // 4. `stop()` returns with sessions open, and the clients see the connection close.
    {
        for (int i = 0; i < 50 && srv->sessions() >= 2; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        auto e = cfire::websocket::connect(url);
        require(e.has_value(), "CF-194: one more client connects for stop()");
        const auto t0 = clock::now();
        srv->stop();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - t0).count();
        require(ms < 5000, std::format("CF-194: stop() returns with sessions open (took {} ms)", ms));
        require(srv->sessions() == 0, "CF-194: stop() joined every session");
        if (a) {
            a->set_timeout_ms(2000);
            require(!a->recv_text().has_value(), "CF-194: an open client sees the server close its session");
        }
    }
    require(served.load() >= 3, std::format("CF-194: every upgraded client ran the handler, got {}", served.load()));

    // 5. A `(Request, Conn)` handler sees the upgrade request; `listen_and_serve` on the caller's thread joins
    //    its sessions before it returns.
    {
        auto s2 = cfire::websocket::make_server(0);
        require(s2.has_value(), "CF-194: second server starts");
        if (s2) {
            std::thread loop([&s2] {
                s2->listen_and_serve([](const cfire::http::Request& req, cfire::websocket::Conn& ws) {
                    (void)ws.send_text(req.path);  // sent right behind the 101 (CF-195)
                    (void)ws.recv_text();
                });
            });
            auto f = cfire::websocket::connect(std::format("ws://127.0.0.1:{}/room/7", s2->port()));
            require(f.has_value(), "CF-194: client reaches the foreground server");
            if (f) {
                f->set_timeout_ms(3000);
                auto path = f->recv_text();
                require(path && *path == "/room/7", "CF-194: the handler gets the upgrade request");
            }
            s2->stop();
            loop.join();
            require(s2->sessions() == 0, "CF-194: listen_and_serve joined its sessions");
        }
    }
}
