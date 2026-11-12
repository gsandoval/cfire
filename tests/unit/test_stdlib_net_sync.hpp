#pragma once

void test_in_process_sha256_file_hashing() {
    std::println("=== RUN   test_in_process_sha256_file_hashing");
    ScopedTempDir tmp("cfire_sha_test");
    auto empty_file = tmp.path / "empty.txt";
    (void)cfire::fs::write_text(empty_file, "");
    auto h_empty = cfire::sha256::hash_file_hex(empty_file);
    require(h_empty.has_value(), "hash_file_hex on empty file succeeds");
    require(*h_empty == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        std::format("empty file SHA-256 mismatch: {}", *h_empty));

    auto hello_file = tmp.path / "hello.txt";
    (void)cfire::fs::write_text(hello_file, "hello world");
    auto h_hello = cfire::sha256::hash_file_hex(hello_file);
    require(h_hello.has_value(), "hash_file_hex on hello world succeeds");
    require(*h_hello == "b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9",
        std::format("hello world SHA-256 mismatch: {}", *h_hello));

    auto nonexistent = tmp.path / "does_not_exist.txt";
    auto h_missing = cfire::sha256::hash_file_hex(nonexistent);
    require(!h_missing.has_value(), "hash_file_hex on missing file returns error");

    std::println("--- PASS: test_in_process_sha256_file_hashing");
}

void test_crypto_hmac_and_aes(const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_crypto_hmac_and_aes");
    (void)rt_dir;
    // 1. In-process C++ unit tests
    // hmac::equal
    require(cfire::hmac::equal("abcdef123456", "abcdef123456"), "hmac::equal identical strings");
    require(!cfire::hmac::equal("abcdef123456", "abcdef123457"), "hmac::equal different strings");
    require(!cfire::hmac::equal("abcdef123456", "abcdef"), "hmac::equal different lengths");

    std::string key_str = "01234567890123456789012345678901"; // 32 bytes
    std::string iv_str  = "1234567890123456";                 // 16 bytes
    std::string pt_str  = "Hello Systems Programming in Cfire!";

    // AES-256 CTR
    auto ct_res = cfire::aes::encrypt_ctr(key_str, iv_str, pt_str);
    require(ct_res.has_value(), "aes::encrypt_ctr success");
    auto dec_ctr = cfire::aes::decrypt_ctr(key_str, iv_str, *ct_res);
    require(dec_ctr.has_value(), "aes::decrypt_ctr success");
    std::string dec_ctr_str(reinterpret_cast<const char*>(dec_ctr->data()), dec_ctr->size());
    require(dec_ctr_str == pt_str, "aes::decrypt_ctr roundtrip match");

    // AES-256 GCM
    std::string nonce_str = "123456789012"; // 12 bytes
    std::string aad_str = "auth_data_v1";
    auto gcm_res = cfire::aes::encrypt_gcm(key_str, nonce_str, pt_str, aad_str);
    require(gcm_res.has_value(), "aes::encrypt_gcm success");
    require(gcm_res->tag.size() == 16, "aes::encrypt_gcm tag size 16");

    auto dec_gcm_str = cfire::aes::decrypt_gcm_str(key_str, nonce_str, gcm_res->ciphertext, gcm_res->tag, aad_str);
    require(dec_gcm_str.has_value(), "aes::decrypt_gcm_str success");
    require(*dec_gcm_str == pt_str, "aes::decrypt_gcm_str roundtrip match");

    // GCM Tampering detection
    auto tampered_tag = gcm_res->tag;
    tampered_tag[0] = static_cast<std::byte>(static_cast<unsigned char>(tampered_tag[0]) ^ 0x01);
    auto tamper_res = cfire::aes::decrypt_gcm_str(key_str, nonce_str, gcm_res->ciphertext, tampered_tag, aad_str);
    require(!tamper_res.has_value(), "aes::decrypt_gcm rejects corrupted tag");

    auto tampered_ct = gcm_res->ciphertext;
    tampered_ct[0] = static_cast<std::byte>(static_cast<unsigned char>(tampered_ct[0]) ^ 0x01);
    auto tamper_ct_res = cfire::aes::decrypt_gcm_str(key_str, nonce_str, tampered_ct, gcm_res->tag, aad_str);
    require(!tamper_ct_res.has_value(), "aes::decrypt_gcm rejects corrupted ciphertext");

    auto tamper_aad_res = cfire::aes::decrypt_gcm_str(key_str, nonce_str, gcm_res->ciphertext, gcm_res->tag, "wrong_aad");
    require(!tamper_aad_res.has_value(), "aes::decrypt_gcm rejects mismatched AAD");

    // 2. End-to-end cfire run invocation
    run_cfire_source(R"(package main

import "core:crypto/aes"
import "core:crypto/hmac"

fn main() {
    key := "01234567890123456789012345678901"
    nonce := "012345678901"
    msg := "cfire aes-gcm authenticated encryption secret"
    aad := "meta-header"

    res := try aes.encrypt_gcm(key, nonce, msg, aad)
    dec := try aes.decrypt_gcm_str(key, nonce, res.ciphertext, res.tag, aad)
    println("decrypted={}", dec)

    eq1 := hmac.equal("hash123456", "hash123456")
    eq2 := hmac.equal("hash123456", "hash999999")
    println("eq1={} eq2={}", eq1, eq2)
}
)", "cfire_crypto")
        .assert_ok("cfire run crypto main.cf exit 0")
        .assert_contains("decrypted=cfire aes-gcm authenticated encryption secret", "output contains decrypted secret")
        .assert_contains("eq1=true eq2=false", "output contains eq1=true eq2=false");

    std::println("--- PASS: test_crypto_hmac_and_aes");
}

void test_sync_context([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_sync_context");
    // 1. C++ runtime assertions
    auto bg = cfire::sync::context::background();
    require(!bg.is_cancelled(), "background context is not cancelled");
    require(!bg.done(), "background context done() is false");

    // with_cancel manual cancellation
    auto [child1, cancel1] = cfire::sync::context::with_cancel(bg);
    require(!child1.is_cancelled(), "child context not cancelled initially");
    cancel1("request aborted");
    require(child1.is_cancelled(), "child context cancelled after cancel()");
    require(child1.reason() == "request aborted", "child context records cancellation reason");
    require(!bg.is_cancelled(), "parent background context remains uncancelled");

    // Parent cancellation cascading to multiple children
    auto [parent_ctx, parent_cancel] = cfire::sync::context::with_cancel(bg);
    auto [sub_child1, _c1] = cfire::sync::context::with_cancel(parent_ctx);
    auto [sub_child2, _c2] = cfire::sync::context::with_cancel(parent_ctx);
    require(!sub_child1.is_cancelled() && !sub_child2.is_cancelled(), "sub-children active");
    parent_cancel("parent terminated");
    require(sub_child1.is_cancelled(), "sub_child1 cancelled via cascade");
    require(sub_child2.is_cancelled(), "sub_child2 cancelled via cascade");
    require(sub_child1.reason() == "parent terminated", "sub_child1 inherited parent reason");

    // with_timeout automatic expiration
    auto [timeout_ctx, timeout_cancel] = cfire::sync::context::with_timeout(bg, 50);
    require(!timeout_ctx.is_cancelled(), "timeout context active initially");
    bool timeout_w = timeout_ctx.wait_for(150);
    require(timeout_w, "timeout_ctx wait_for unblocks on deadline expiration");
    require(timeout_ctx.is_cancelled(), "timeout_ctx is cancelled after expiration");
    require(timeout_ctx.reason() == "deadline exceeded", "timeout_ctx reason is deadline exceeded");

    // sleep_ctx interrupted by cancellation
    auto [sleep_ctx_handle, sleep_cancel] = cfire::sync::context::with_cancel(bg);
    std::thread cancel_thread([sleep_cancel]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        sleep_cancel("interrupted");
    });
    auto t0 = std::chrono::steady_clock::now();
    bool sleep_res = cfire::time::sleep_ctx(sleep_ctx_handle, 1000);
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    cancel_thread.join();
    require(!sleep_res, "sleep_ctx returns false when cancelled early");
    require(elapsed_ms < 500, "sleep_ctx unblocks promptly upon cancellation");

    // Channel.recv_ctx unblocks immediately on cancellation
    cfire::sync::Channel<int> ch;
    auto [ch_ctx, ch_cancel] = cfire::sync::context::with_cancel(bg);
    std::atomic<bool> unblocked = false;
    std::string err_msg;
    std::thread recv_thread([&ch, &ch_ctx, &unblocked, &err_msg]() {
        auto res = ch.recv_ctx(ch_ctx);
        unblocked = true;
        if (!res) err_msg = std::string(res.error().message());
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    require(!unblocked, "receiver blocks on empty channel before cancellation");
    ch_cancel("client disconnected");
    recv_thread.join();
    require(unblocked, "receiver unblocks promptly when context cancelled");
    require(err_msg.contains("context cancelled: client disconnected"), "recv_ctx returns error with cancellation reason");

    // 2. End-to-end .cf script execution
    run_cfire_source(R"(package main

import "core:sync/context"
import "core:sync"
import "core:time"

fn main() {
    bg := context.background()
    (ctx1, cancel1) := context.with_cancel(bg)
    println("ctx1.done={}", ctx1.done())
    cancel1("stop")
    println("ctx1.cancelled={}", ctx1.is_cancelled())
    println("ctx1.reason={}", ctx1.reason())

    (tctx, tcancel) := context.with_timeout(bg, 30)
    // Block until the deadline fires (bounded) instead of racing a fixed sleep,
    // which is flaky on loaded CI runners.
    _ = tctx.wait_for(5000)
    println("tctx.done={}", tctx.done())
    println("tctx.reason={}", tctx.reason())
}
)", "cfire_ctx")
        .assert_ok("cfire run context exit 0")
        .assert_contains("ctx1.done=false", "ctx1 initial state")
        .assert_contains("ctx1.cancelled=true", "ctx1 cancelled state")
        .assert_contains("ctx1.reason=stop", "ctx1 reason output")
        .assert_contains("tctx.done=true", "tctx expired state")
        .assert_contains("tctx.reason=deadline exceeded", "tctx reason output");

    std::println("--- PASS: test_sync_context");
}

void test_net_websocket([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_net_websocket");
    // 1. C++ runtime assertions
    // RFC 6455 Handshake vector test:
    std::string rfc_key = "dGhlIHNhbXBsZSBub25jZQ==";
    std::string accept = cfire::websocket::compute_accept_key(rfc_key);
    require(accept == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", "RFC 6455 Sec-WebSocket-Accept test vector match");

    // Loopback pair test
    auto pair_res = cfire::websocket::pair();
    require(pair_res.has_value(), "cfire::websocket::pair() succeeds");
    auto [client, server] = std::move(*pair_res);
    require(client.is_open() && server.is_open(), "both sockets open");

    // Text frame client -> server
    auto send_txt = client.send_text("Hello RFC 6455 WebSockets!");
    require(send_txt.has_value(), "client.send_text succeeds");
    auto recv_txt = server.recv_text();
    require(recv_txt.has_value(), "server.recv_text succeeds");
    require(*recv_txt == "Hello RFC 6455 WebSockets!", "text frame content matches exactly");

    // Text frame server -> client
    auto reply_txt = server.send_text("Server Ack");
    require(reply_txt.has_value(), "server.send_text succeeds");
    auto got_reply = client.recv_text();
    require(got_reply.has_value() && *got_reply == "Server Ack", "client receives unmasked server text frame");

    // Binary frame client -> server
    std::vector<std::uint8_t> bin_data = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04};
    auto send_bin = client.send_binary(bin_data);
    require(send_bin.has_value(), "client.send_binary succeeds");
    auto recv_bin = server.recv_binary();
    require(recv_bin.has_value() && *recv_bin == bin_data, "server receives binary frame matching payload");

    // Ping / Pong handling
    auto ping_res = client.ping("ping-heartbeat");
    require(ping_res.has_value(), "client.ping succeeds");
    auto srv_post_ping = server.send_text("alive");
    require(srv_post_ping.has_value(), "server sends alive");
    auto cl_got_alive = client.recv_text();
    require(cl_got_alive.has_value() && *cl_got_alive == "alive", "client receives text after ping");

    // Clean close
    client.close(1000, "done");
    require(!client.is_open(), "client closed");
    auto srv_recv_closed = server.recv_text();
    require(!srv_recv_closed.has_value(), "server recognizes closed peer");

    // NET-P0-01: URL default port for ws:// (80) and wss:// (443)
    auto u_ws = cfire::url::parse("ws://stream.example.com/feed");
    require(u_ws.has_value() && u_ws->scheme == "ws" && u_ws->port == 80, "ws:// defaults to port 80");
    auto u_wss = cfire::url::parse("wss://stream.example.com/feed");
    require(u_wss.has_value() && u_wss->scheme == "wss" && u_wss->port == 443, "wss:// defaults to port 443");
    auto u_wss_custom = cfire::url::parse("wss://stream.example.com:9443/feed");
    require(u_wss_custom.has_value() && u_wss_custom->port == 9443, "wss:// respects explicit port");

    // NET-P0-02: Standalone websocket::Server (ws://)
    auto ws_srv_res = cfire::websocket::make_server(0);
    require(ws_srv_res.has_value(), "websocket::make_server(0) succeeds");
    if (ws_srv_res.has_value()) {
        auto ws_port = ws_srv_res->port();
        ws_srv_res->serve_in_background([](const cfire::http::Request& req, cfire::websocket::Conn& ws) {
            auto in_msg = ws.recv_text();
            if (in_msg) {
                (void)ws.send_text("srv_echo:" + req.path + ":" + *in_msg);
            }
        });

        auto ws_cli = cfire::websocket::connect(std::format("ws://127.0.0.1:{}/realtime", ws_port));
        require(ws_cli.has_value(), "websocket::connect to standalone websocket::Server succeeds");
        if (ws_cli.has_value()) {
            require(ws_cli->send_text("ping123").has_value(), "ws client send_text to standalone server");
            auto echo = ws_cli->recv_text();
            require(echo.has_value() && *echo == "srv_echo:/realtime:ping123", "ws client receives echoed message from standalone server");
            ws_cli->close();
        }
        ws_srv_res->stop();
    }

    // NET-P0-03 & NET-P0-04: TLS Self-Signed Cert Generation & WSS Server/Client
    require(cfire::tls::available(), "OpenSSL dynamic TLS bridge is available");
    auto cert_pair = cfire::tls::generate_self_signed_pem("localhost", 365);
    require(cert_pair.has_value(), "tls::generate_self_signed_pem succeeds");
    require(cert_pair->cert_pem.contains("BEGIN CERTIFICATE"), "generated cert_pem has PEM header");
    require(cert_pair->key_pem.contains("PRIVATE KEY"), "generated key_pem has PEM header");

    auto wss_srv_res = cfire::websocket::make_tls_server_pem(0, cert_pair->cert_pem, cert_pair->key_pem);
    require(wss_srv_res.has_value(), "websocket::make_tls_server_pem succeeds");
    if (wss_srv_res.has_value()) {
        auto wss_port = wss_srv_res->port();
        wss_srv_res->serve_in_background([](cfire::websocket::Conn& ws) {
            auto msg = ws.recv_text();
            if (msg) {
                (void)ws.send_text("wss_secure:" + *msg);
            }
        });

        // Connecting with verify_peer=true against a self-signed cert should fail verification
        auto strict_cli = cfire::websocket::connect(std::format("wss://127.0.0.1:{}/ws", wss_port), /*verify_peer=*/true);
        require(!strict_cli.has_value(), "wss:// with verify_peer=true rejects untrusted self-signed cert");

        // Connecting with connect_insecure (verify_peer=false) succeeds over TLS
        auto wss_cli = cfire::websocket::connect_insecure(std::format("wss://127.0.0.1:{}/ws", wss_port));
        require(wss_cli.has_value(), "websocket::connect_insecure over wss:// succeeds");
        if (wss_cli.has_value()) {
            require(wss_cli->is_tls(), "wss client connection is TLS-backed");
            require(wss_cli->send_text("encrypted-payload").has_value(), "wss send_text succeeds");
            auto reply = wss_cli->recv_text();
            require(reply.has_value() && *reply == "wss_secure:encrypted-payload", "wss recv_text matches encrypted echo");
            wss_cli->close();
        }
        wss_srv_res->stop();
    }

    // 2. End-to-end .cf script execution (pair + standalone server + TLS/WSS)
    run_cfire_source(R"(package main

import "core:net/websocket"
import "core:net/tls"

fn main() {
    (mut client, mut server) := try websocket.pair()
    try client.send_text("cf-websocket-hello")
    msg := try server.recv_text()
    println("ws.server.msg={}", msg)

    try server.send_text("cf-websocket-ack")
    ack := try client.recv_text()
    println("ws.client.ack={}", ack)
    client.close()

    pair := try tls.generate_self_signed_pem("localhost", 30)
    mut wss_srv := try make(websocket.Server, 0, tls: websocket.TlsConfig{cert_pem: pair.cert_pem, key_pem: pair.key_pem})
    wss_srv.serve_in_background(|mut ws: websocket.Conn| {
        m := ws.recv_text() or ""
        _ = ws.send_text("wss-echo:" + m)
    })
    defer wss_srv.stop()

    mut wss_cli := try websocket.connect("wss://127.0.0.1:{wss_srv.port()}/stream", verify_peer: false)
    defer wss_cli.close()
    try wss_cli.send_text("tls13")
    wss_reply := try wss_cli.recv_text()
    println("wss.reply={}", wss_reply)
}
)", "cfire_ws")
        .assert_ok("cfire run websocket exit 0")
        .assert_contains("ws.server.msg=cf-websocket-hello", "websocket server receive output")
        .assert_contains("ws.client.ack=cf-websocket-ack", "websocket client ack output")
        .assert_contains("wss.reply=wss-echo:tls13", "wss server/client TLS roundtrip output");

    std::println("--- PASS: test_net_websocket");
}

void test_net_http_and_router(const std::filesystem::path& /*rt_dir*/) {
    std::println("=== RUN   test_net_http_and_router");

    // 1. Request Parser Testing
    std::string raw_get = "GET /api/users/42?sort=desc&limit=10 HTTP/1.1\r\n"
                          "Host: 127.0.0.1:8080\r\n"
                          "User-Agent: test-client/1.0\r\n"
                          "Accept: application/json\r\n\r\n";
    auto req_res = cfire::http::parse_request(raw_get);
    require(req_res.has_value(), "parse_request succeeds for valid GET");
    if (req_res.has_value()) {
        const auto& req = *req_res;
        require(req.method == "GET", "parsed method is GET");
        require(req.path == "/api/users/42", "parsed path is /api/users/42");
        require(req.raw_query == "sort=desc&limit=10", "parsed raw query");
        require(req.query("sort") == "desc", "parsed query param sort");
        require(req.query("limit") == "10", "parsed query param limit");
        require(req.query("missing", "default") == "default", "query fallback value");
        require(req.header("host") == "127.0.0.1:8080", "header lookup lower-case");
        require(req.header("Host") == "127.0.0.1:8080", "header lookup case-insensitive");
        require(req.header("Accept") == "application/json", "header lookup Accept");
    }

    std::string raw_post = "POST /submit HTTP/1.1\r\n"
                           "Content-Type: application/json\r\n"
                           "Content-Length: 15\r\n\r\n"
                           "{\"status\":\"ok\"}";
    auto post_res = cfire::http::parse_request(raw_post);
    require(post_res.has_value(), "parse_request succeeds for valid POST with body");
    if (post_res.has_value()) {
        require(post_res->method == "POST", "parsed method is POST");
        require(post_res->path == "/submit", "parsed path is /submit");
        require(post_res->body == "{\"status\":\"ok\"}", "parsed body matches Content-Length");
    }

    auto bad_res = cfire::http::parse_request("invalid without crlfcrlf");
    require(!bad_res.has_value(), "parse_request returns error for malformed request");

    // 2. Response & Wire Formatting Testing
    auto resp_txt = cfire::http::Response::text(200, "hello world");
    require(resp_txt.status_code == 200, "Response::text status code 200");
    require(resp_txt.header("content-type").contains("text/plain"), "Response::text content-type");
    require(resp_txt.body == "hello world", "Response::text body");

    auto resp_json = cfire::http::Response::json(201, "{\"id\":99}");
    require(resp_json.status_code == 201, "Response::json status code 201");
    require(resp_json.header("content-type") == "application/json", "Response::json content-type");

    auto resp_wire = resp_json.to_http_wire();
    require(resp_wire.starts_with("HTTP/1.1 201 Created\r\n"), "to_http_wire status line");
    require(resp_wire.contains("Content-Length: 9\r\n"), "to_http_wire Content-Length");
    require(resp_wire.ends_with("{\"id\":99}"), "to_http_wire payload suffix");

    // 3. Independent Router Testing (pure in-memory dispatch, zero sockets)
    auto router = cfire::http::make_router();

    router.get("/health", [](const cfire::http::Request&) {
        return cfire::http::Response::json(200, "{\"healthy\":true}");
    });

    router.get("/users/:id", [](const cfire::http::Request& req) {
        return cfire::http::Response::text(200, "user_id=" + req.param("id"));
    });

    router.get("/orgs/:org/repos/:repo", [](const cfire::http::Request& req) {
        return cfire::http::Response::text(200, req.param("org") + "/" + req.param("repo"));
    });

    router.get("/static/*filepath", [](const cfire::http::Request& req) {
        return cfire::http::Response::text(200, "file=" + req.param("filepath"));
    });

    router.post("/items", [](const cfire::http::Request& req) {
        return cfire::http::Response::json(201, "created:" + req.body);
    });

    // Exact match
    cfire::http::Request r1{.method = "GET", .path = "/health"};
    auto out1 = router.dispatch(r1);
    require(out1.status_code == 200 && out1.body == "{\"healthy\":true}", "router exact match /health");

    // Parameter match :id
    cfire::http::Request r2{.method = "GET", .path = "/users/456"};
    auto out2 = router.dispatch(r2);
    require(out2.status_code == 200 && out2.body == "user_id=456", "router parameter match :id");

    // Multi-parameter match :org and :repo
    cfire::http::Request r3{.method = "GET", .path = "/orgs/google/repos/cfire"};
    auto out3 = router.dispatch(r3);
    require(out3.status_code == 200 && out3.body == "google/cfire", "router multi-param match :org/:repo");

    // Wildcard match *filepath
    cfire::http::Request r4{.method = "GET", .path = "/static/assets/css/main.css"};
    auto out4 = router.dispatch(r4);
    require(out4.status_code == 200 && out4.body == "file=assets/css/main.css", "router wildcard match *filepath");

    // POST dispatch
    cfire::http::Request r5{.method = "POST", .path = "/items", .body = "gadget"};
    auto out5 = router.dispatch(r5);
    require(out5.status_code == 201 && out5.body == "created:gadget", "router POST dispatch with body");

    // 405 Method Not Allowed (path matches POST /items but method is GET)
    cfire::http::Request r6{.method = "GET", .path = "/items"};
    auto out6 = router.dispatch(r6);
    require(out6.status_code == 405, "router returns 405 Method Not Allowed for unmatched verb");
    require(out6.header("allow") == "POST", "router sets Allow header for 405 response");

    // 404 Not Found
    cfire::http::Request r7{.method = "GET", .path = "/nonexistent"};
    auto out7 = router.dispatch(r7);
    require(out7.status_code == 404, "router returns 404 for unmatched path");

    // Middleware test
    auto mw_router = cfire::http::make_router();
    mw_router.use([](cfire::http::Request& req) -> std::optional<cfire::http::Response> {
        if (req.header("authorization") != "Bearer secret") {
            return cfire::http::Response::text(401, "Unauthorized");
        }
        return std::nullopt;
    });
    mw_router.get("/protected", [](const cfire::http::Request&) {
        return cfire::http::Response::text(200, "SecretData");
    });

    cfire::http::Request unauth_req{.method = "GET", .path = "/protected"};
    require(mw_router.dispatch(unauth_req).status_code == 401, "middleware rejects missing auth");

    cfire::http::Request auth_req{.method = "GET", .path = "/protected", .headers = {{"authorization", "Bearer secret"}}};
    require(mw_router.dispatch(auth_req).status_code == 200, "middleware allows authenticated request");

    // 4. Server + Router Loopback End-to-End
    auto srv_res = cfire::http::make_server(0);
    require(srv_res.has_value(), "make_server creates listener on ephemeral port");
    if (srv_res.has_value()) {
        auto port = srv_res->port();
        srv_res->serve_in_background(router);

        std::string target_get = std::format("http://127.0.0.1:{}/users/888", port);
        auto client_get = cfire::http::get(target_get);
        require(client_get.has_value(), "client GET against live server with router succeeds");
        if (client_get.has_value()) {
            require(client_get->status_code == 200, "live GET status code 200");
            require(client_get->body == "user_id=888", "live GET received router output");
        }

        std::string target_post = std::format("http://127.0.0.1:{}/items", port);
        auto client_post = cfire::http::post(target_post, "gizmo");
        require(client_post.has_value(), "client POST against live server succeeds");
        if (client_post.has_value()) {
            require(client_post->status_code == 201, "live POST status code 201");
            require(client_post->body == "created:gizmo", "live POST received router output");
        }

        srv_res->stop();
    }

    // 4b. Router::ws WebSocket Route Upgrade + HTTPS Server/Client End-to-End
    router.ws("/chat/:room", [](const cfire::http::Request& req, cfire::websocket::Conn& ws) {
        std::string room = req.param("room");
        auto msg = ws.recv_text();
        if (msg) {
            (void)ws.send_text("room=" + room + ";msg=" + *msg);
        }
    });

    auto cert_pair = cfire::tls::generate_self_signed_pem("localhost", 365);
    require(cert_pair.has_value(), "generate_self_signed_pem for HTTPS server succeeds");
    auto https_srv = cfire::http::make_tls_server_pem(0, cert_pair->cert_pem, cert_pair->key_pem);
    require(https_srv.has_value(), "http::make_tls_server_pem succeeds");
    if (https_srv.has_value()) {
        auto tls_port = https_srv->port();
        https_srv->serve_in_background(router);

        // HTTPS GET
        auto https_get = cfire::http::get_insecure(std::format("https://127.0.0.1:{}/users/777", tls_port));
        require(https_get.has_value(), "https::get_insecure against live TLS server succeeds");
        if (https_get.has_value()) {
            require(https_get->status_code == 200 && https_get->body == "user_id=777", "HTTPS GET body matches");
        }

        // WSS upgrade on the same HTTPS server via router.ws("/chat/:room", ...)
        auto wss_chat = cfire::websocket::connect_insecure(std::format("wss://127.0.0.1:{}/chat/general", tls_port));
        require(wss_chat.has_value(), "WSS upgrade on Router::ws route succeeds");
        if (wss_chat.has_value()) {
            require(wss_chat->send_text("hello-room").has_value(), "WSS chat send_text succeeds");
            auto chat_reply = wss_chat->recv_text();
            require(chat_reply.has_value() && *chat_reply == "room=general;msg=" "hello-room", "Router::ws extracted :room param and echoed over WSS");
            wss_chat->close();
        }

        https_srv->stop();
    }

    // 5. End-to-end .cf script execution
    run_cfire_source(R"(package main

import "core:net/http"

fn main() {
    mut router := make(http.Router)
    router.get("/ping", |req| {
        return http.Response.text(200, "pong")
    })
    router.get("/users/:id", |req| {
        return http.Response.text(200, "uid:" + req.param("id"))
    })

    // 1. In-memory standalone dispatch test
    req1 := http.Request{ method: "GET", path: "/ping" }
    res1 := router.dispatch(req1)
    println("cf.router.ping={}", res1.body)

    req2 := http.Request{ method: "GET", path: "/users/99" }
    res2 := router.dispatch(req2)
    println("cf.router.user={}", res2.body)
}
)", "cfire_http_router")
        .assert_ok("cfire run http router exit 0")
        .assert_contains("cf.router.ping=pong", "http router ping dispatch output")
        .assert_contains("cf.router.user=uid:99", "http router user parameter dispatch output");

    std::println("--- PASS: test_net_http_and_router");
}
