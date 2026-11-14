#pragma once
// tests/unit/test_cf195.hpp — CF-195: bytes that arrive with the WebSocket handshake are not dropped.

#include "../test_harness.hpp"
#include "core/net/websocket.hpp"

namespace cfire_test_cf195 {
// A server frame (unmasked text) for `msg`.
inline auto text_frame(std::string_view msg) -> std::string {
    cfire::websocket::WsFrame f{.opcode = 0x1, .is_final = true, .payload = std::vector<std::uint8_t>(msg.begin(), msg.end())};
    auto bytes = cfire::websocket::encode_frame(f, /*mask=*/false);
    return std::string(bytes.begin(), bytes.end());
}

// Reads one HTTP request head from `c` and answers it with a 101 and `frames` in a single write.
inline void answer_with_frames(cfire::net::TcpConn& c, std::string_view frames) {
    std::string head;
    c.set_timeout_ms(3000);
    while (!head.contains("\r\n\r\n")) {
        auto chunk = c.read_text(4096);
        if (!chunk || chunk->empty()) return;
        head += *chunk;
    }
    std::string key;
    if (auto k = head.find("Sec-WebSocket-Key: "); k != std::string::npos) key = head.substr(k + 19, head.find("\r\n", k) - k - 19);
    std::string resp = std::format("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: {}\r\n\r\n",
                                   cfire::websocket::compute_accept_key(key));
    resp += frames;
    (void)c.write_text(resp);
}
} // namespace cfire_test_cf195

void test_cf195_websocket_handshake_read_ahead([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_cf195_websocket_handshake_read_ahead (CF-195)");
    using cfire_test_cf195::answer_with_frames;
    using cfire_test_cf195::text_frame;

    // 1. A server that writes the 101 and two frames in one send: the client reads both, every time.
    {
        auto l = cfire::net::listen_tcp(0);
        require(l.has_value(), "CF-195: listener starts");
        if (!l) return;
        constexpr int kRounds = 100;
        std::thread srv([&l] {
            for (int i = 0; i < kRounds; ++i) {
                auto c = l->accept();
                if (!c) return;
                answer_with_frames(*c, text_frame("first") + text_frame("second"));
                c->set_timeout_ms(2000);
                (void)c->read_text(4096);  // the client's close frame, or EOF
            }
        });
        int ok = 0;
        for (int i = 0; i < kRounds; ++i) {
            auto ws = cfire::websocket::connect(std::format("ws://127.0.0.1:{}/", l->port()));
            if (!ws) continue;
            ws->set_timeout_ms(2000);
            auto a = ws->recv_text();
            auto b = ws->recv_text();
            if (a && *a == "first" && b && *b == "second") ++ok;
            ws->close();
        }
        srv.join();
        require(ok == kRounds, std::format("CF-195: frames sent with the 101 are read ({}/{})", ok, kRounds));
    }

    // 2. A client that sends its request and first frame in one write: `websocket.Server` keeps the frame.
    {
        auto srv = cfire::websocket::make_server(0);
        require(srv.has_value(), "CF-195: websocket server starts");
        if (srv) {
            srv->serve_in_background([](cfire::websocket::Conn& ws) {
                if (auto m = ws.recv_text()) (void)ws.send_text("got:" + *m);
            });
            auto c = cfire::net::connect_tcp("127.0.0.1", srv->port());
            require(c.has_value(), "CF-195: raw client connects");
            if (c) {
                cfire::websocket::WsFrame f{.opcode = 0x1, .is_final = true, .payload = {'h', 'i'}};
                auto frame = cfire::websocket::encode_frame(f, /*mask=*/true);
                std::string wire = "GET / HTTP/1.1\r\nHost: a\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                                   "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n";
                wire.append(frame.begin(), frame.end());
                (void)c->write_text(wire);
                c->set_timeout_ms(3000);
                std::string got;
                while (!got.contains("got:hi")) {
                    auto chunk = c->read_text(4096);
                    if (!chunk || chunk->empty()) break;
                    got += *chunk;
                }
                require(got.starts_with("HTTP/1.1 101") && got.contains("got:hi"), "CF-195: the server reads a frame sent with the request");
            }
            srv->stop();
        }
    }

    // 3. The same over TLS: the 101 and a frame in one TLS write.
    if (cfire::tls::available()) {
        auto pair = cfire::tls::generate_self_signed_pem("localhost", 30);
        std::optional<cfire::tls::ServerContext> ctx;
        if (pair) {
            if (auto r = cfire::tls::ServerContext::from_pem(pair->cert_pem, pair->key_pem)) ctx = std::move(*r);
        }
        auto l = cfire::net::listen_tcp(0);
        require(ctx.has_value() && l.has_value(), "CF-195: TLS listener starts");
        if (ctx && l) {
            std::thread srv([&] {
                auto c = l->accept();
                if (!c || !ctx->wrap_server(*c)) return;
                answer_with_frames(*c, text_frame("secure"));
                c->set_timeout_ms(2000);
                (void)c->read_text(4096);
            });
            auto ws = cfire::websocket::connect(std::format("wss://localhost:{}/", l->port()), /*verify_peer=*/false);
            require(ws.has_value(), "CF-195: wss client connects");
            if (ws) {
                ws->set_timeout_ms(3000);
                auto m = ws->recv_text();
                require(m && *m == "secure", "CF-195: a frame sent with the 101 over TLS is read");
                ws->close();
            }
            srv.join();
        }
    }
}
