#pragma once
// tests/unit/test_audit_w2.hpp — Wave W2 (network & supply-chain security) regression tests.

#include "../test_harness.hpp"
#include "core/net/http.hpp"
#include "core/net/tls.hpp"
#include "../../src/buildsys.hpp"
#include "../../src/mcp.hpp"
#include "core/ssh.hpp"

// CF-151: TLS behavior comes only from typed parameters; `x-cfire-*` headers are never sent or read.
void test_cf151_no_inband_tls_controls([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_cf151_no_inband_tls_controls (CF-151)");

    // 1. Reserved headers are stripped from the wire; ordinary headers pass through.
    {
        auto srv = cfire::http::make_server(0);
        require(srv.has_value(), "CF-151: plain HTTP server starts");
        if (srv) {
            srv->serve_in_background([](const cfire::http::Request& req) {
                std::string seen;
                for (const auto& [k, v] : req.headers) seen += k + "=" + v + ";";
                return cfire::http::Response::text(200, seen);
            });
            auto url = std::format("http://127.0.0.1:{}/echo", srv->port());
            auto resp = cfire::http::get(url, {{"X-Cfire-Tls-Verify", "false"}, {"x-cfire-tls-ca", "/tmp/evil.pem"}, {"X-Trace", "abc"}});
            require(resp.has_value(), "CF-151: echo request succeeds");
            if (resp) {
                std::string lower;
                for (char c : resp->body) lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                require(lower.find("x-cfire") == std::string::npos,
                    std::format("CF-151: x-cfire-* headers must not reach the wire, server saw: {}", resp->body));
                require(lower.find("x-trace=abc") != std::string::npos, "CF-151: ordinary headers are still sent");
            }
            srv->stop();
        }
    }

    // 2. A spoofed control header cannot disable certificate verification against a self-signed server.
    if (cfire::tls::available()) {
        auto pair = cfire::tls::generate_self_signed_pem("localhost", 30);
        require(pair.has_value(), "CF-151: self-signed certificate generated");
        if (pair) {
            auto tls_srv = cfire::http::make_tls_server_pem(0, pair->cert_pem, pair->key_pem);
            require(tls_srv.has_value(), "CF-151: TLS server starts");
            if (tls_srv) {
                tls_srv->serve_in_background([](const cfire::http::Request&) {
                    return cfire::http::Response::text(200, "secret");
                });
                auto url = std::format("https://127.0.0.1:{}/", tls_srv->port());
                auto spoofed = cfire::http::get(url, {{"x-cfire-tls-verify", "false"}});
                require(!spoofed.has_value() || spoofed->body != "secret",
                    "CF-151: 'x-cfire-tls-verify: false' must not bypass certificate verification");
                auto explicit_insecure = cfire::http::get(url, {}, false);
                require(explicit_insecure.has_value() && explicit_insecure->body == "secret",
                    "CF-151: the typed verify_peer=false opt-out still works");
                tls_srv->stop();
            }
        }
    }
}

// CF-150 / Q22: HTTP server deadlines, worker pool, strict framing, chunked decode, peer address.
void test_cf150_http_server_hardening([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_cf150_http_server_hardening (CF-150)");
    auto srv = cfire::http::make_server(0);
    require(srv.has_value(), "CF-150: server starts");
    if (!srv) return;
    cfire::http::ServerLimits lim;
    lim.header_timeout_ms = 600;
    lim.body_timeout_ms = 600;
    lim.max_body_bytes = 64;
    lim.workers = 4;
    srv->set_limits(lim);
    srv->serve_in_background([](const cfire::http::Request& req) {
        return cfire::http::Response::text(200, std::format("body={};peer={}", req.body, req.remote_addr));
    });
    const auto port = srv->port();
    auto raw_exchange = [port](std::string_view wire) -> std::string {
        auto c = cfire::net::connect_tcp("127.0.0.1", port);
        if (!c) return "<connect failed>";
        (void)c->write_text(wire);
        c->set_timeout_ms(3000);
        std::string out;
        for (int i = 0; i < 50; ++i) {
            auto chunk = c->read_text(4096);
            if (!chunk || chunk->empty()) break;
            out += *chunk;
        }
        return out;
    };

    // 1. Slowloris: an idle client must not block other requests, and is dropped with 408.
    {
        auto timed_get = [port]() {
            auto t0 = std::chrono::steady_clock::now();
            auto r = cfire::http::get(std::format("http://127.0.0.1:{}/x", port));
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
            return std::pair{std::move(r), ms};
        };
        // CF-184: compare against this machine's own request time (an emulated VM is many times slower). A request
        // stuck behind the idle client would wait out the 600 ms header deadline; allow less than that on top.
        auto baseline = std::min(timed_get().second, timed_get().second);
        auto idle = cfire::net::connect_tcp("127.0.0.1", port);
        require(idle.has_value(), "CF-150: idle client connects");
        auto [ok, dt] = timed_get();
        require(ok.has_value() && ok->body.starts_with("body=;peer="), "CF-150: normal request succeeds while a client idles");
        require(dt < baseline + 400, std::format("CF-150: normal request is not blocked by the idle client ({} ms, baseline {} ms)", dt, baseline));
        if (idle) {
            idle->set_timeout_ms(3000);
            auto r = idle->read_text(4096);
            require(r.has_value() && r->find("408") != std::string::npos, "CF-150: idle client receives 408 after the header deadline");
        }
        if (ok) require(ok->body.ends_with("peer=127.0.0.1"), std::format("CF-150: Request.remote_addr is set (got '{}')", ok->body));
    }
    // 2. Framing / smuggling vectors.
    require(raw_exchange("POST / HTTP/1.1\r\nHost: a\r\nContent-Length: 5\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n").starts_with("HTTP/1.1 400"),
        "CF-150: Content-Length + Transfer-Encoding is rejected with 400");
    require(raw_exchange("POST / HTTP/1.1\r\nHost: a\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\nab").starts_with("HTTP/1.1 400"),
        "CF-150: duplicate Content-Length is rejected with 400");
    require(raw_exchange("POST / HTTP/1.1\r\nHost: a\r\nContent-Length: abc\r\n\r\n").starts_with("HTTP/1.1 400"),
        "CF-150: invalid Content-Length is rejected with 400");
    require(raw_exchange("GET / HTTP/1.1\nHost: a\n\n\r\n\r\n").starts_with("HTTP/1.1 400"),
        "CF-150: bare-LF header lines are rejected with 400");
    // 3. Chunked bodies: decoded, capped, strictly framed.
    auto chunked = raw_exchange("POST / HTTP/1.1\r\nHost: a\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n6;ext=1\r\n world\r\n0\r\n\r\n");
    require(chunked.find("body=hello world;") != std::string::npos, std::format("CF-150: chunked body is decoded, got: {}", chunked));
    require(raw_exchange("POST / HTTP/1.1\r\nHost: a\r\nTransfer-Encoding: chunked\r\n\r\n41\r\n" + std::string(65, 'x') + "\r\n0\r\n\r\n").starts_with("HTTP/1.1 413"),
        "CF-150: chunked body over max_body_bytes is rejected with 413");
    require(raw_exchange("POST / HTTP/1.1\r\nHost: a\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\n").starts_with("HTTP/1.1 400"),
        "CF-150: malformed chunk size is rejected with 400");
    require(raw_exchange("POST / HTTP/1.1\r\nHost: a\r\nContent-Length: 999\r\n\r\n").starts_with("HTTP/1.1 413"),
        "CF-150: Content-Length over max_body_bytes is rejected with 413");
    require(raw_exchange("POST / HTTP/1.1\r\nHost: a\r\nContent-Length: 10\r\n\r\nabc").starts_with("HTTP/1.1 408"),
        "CF-150: a body that never completes hits the body deadline (408)");
    srv->stop();
}

// CF-152: dependency cache hits are verified against build.lock; fetch arguments cannot inject options.
void test_cf152_verified_dependency_cache([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_cf152_verified_dependency_cache (CF-152)");
    namespace b = cfire::build;
    require(!b::validate_fetch_arg("git url", "--upload-pack=touch /tmp/pwned").has_value(), "CF-152: option-like url rejected");
    require(!b::validate_fetch_arg("git ref", "-oProxyCommand=x").has_value(), "CF-152: option-like ref rejected");
    require(!b::validate_fetch_arg("git url", "ext::sh -c touch% /tmp/pwned").has_value(), "CF-152: ext:: transport rejected");
    require(b::validate_fetch_arg("git url", "https://github.com/madler/zlib.git").has_value(), "CF-152: normal url accepted");

    ScopedTempDir work("cfire_cf152");
    auto upstream = work.path / "upstream";
    std::filesystem::create_directories(upstream);
    auto sh = [](const std::string& cmd) {
        int rc = 0;
        (void)cfire::bridge::run_command_capture(cmd, rc);
        return rc;
    };
    auto q = [](const std::filesystem::path& p) { return cfire::bridge::shell_quote(p.string()); };
    { std::ofstream(upstream / "lib.c") << "int v(void) { return 1; }\n"; }
    bool have_git = sh(std::format("git -C {} init -q && git -C {} -c user.email=t@t -c user.name=t add lib.c && "
                                   "git -C {} -c user.email=t@t -c user.name=t commit -qm init", q(upstream), q(upstream), q(upstream))) == 0;
    if (have_git) {
        int rc = 0;
        std::string head = b::trim_whitespace(cfire::bridge::run_command_capture(std::format("git -C {} rev-parse HEAD", q(upstream)), rc));
        auto cache = work.path / "cache" / "dep";
        std::string resolved;
        auto r1 = b::fetch_git_repository(upstream.string(), "", cache, resolved, head);
        require(r1.has_value() && resolved == head, "CF-152: clean fetch resolves the locked commit");

        // Tampered tracked file in the cache: discarded and refetched.
        { std::ofstream(cache / "lib.c") << "int v(void) { return 666; }\n"; }
        auto r2 = b::fetch_git_repository(upstream.string(), "", cache, resolved, head);
        auto content = cfire::fs::read_text(cache / "lib.c");
        require(r2.has_value() && content && content->find("666") == std::string::npos,
            "CF-152: a cache with modified tracked files is refetched");

        // Cache HEAD moved away from the lock: refetched to the pinned commit.
        (void)sh(std::format("git -C {} -c user.email=t@t -c user.name=t commit -q --allow-empty -m drift", q(cache)));
        auto r3 = b::fetch_git_repository(upstream.string(), "", cache, resolved, head);
        require(r3.has_value() && resolved == head, "CF-152: a cache whose HEAD differs from build.lock is refetched");

        // Upstream no longer matches the pin: hard error, not silent drift.
        auto r4 = b::fetch_git_repository(upstream.string(), "", work.path / "cache" / "dep2", resolved, std::string(40, 'a'));
        require(!r4.has_value() && r4.error().find("cfire/INTEGRITY-MISMATCH") != std::string::npos,
            "CF-152: a fetched commit that differs from build.lock is an INTEGRITY-MISMATCH");
    }

    // Archive cache: the recorded hash must match the pin, otherwise the cache is discarded.
    auto arch = work.path / "archive_cache";
    std::filesystem::create_directories(arch);
    { std::ofstream(arch / "file.txt") << "x"; }
    { std::ofstream(arch / ".cfire_sha256") << "abc123\n"; }
    std::string actual;
    auto hit = b::fetch_archive("https://invalid.invalid/dep.tar.gz", "ABC123", arch, actual);
    require(hit.has_value() && actual == "abc123", "CF-152: archive cache hit with matching recorded hash is reused");
    auto miss = b::fetch_archive("https://invalid.invalid/dep.tar.gz", "def456", arch, actual);
    require(!miss.has_value() && !std::filesystem::exists(arch / "file.txt"),
        "CF-152: archive cache with a different recorded hash is discarded and refetched");
}

// CF-160 / Q15: MCP tools are confined to the workspace root.
void test_cf160_mcp_workspace_confinement(const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_cf160_mcp_workspace_confinement (CF-160)");
    ScopedTempDir ws("cfire_cf160_ws");
    ScopedTempDir outside("cfire_cf160_out");
    { std::ofstream(ws.path / "main.cf") << "package main\nfn main() {\n    println(\"ok\")\n}\n"; }
    { std::ofstream(outside.path / "secret.cf") << "package main\nfn main() {\n    println(\"secret\")\n}\n"; }
    std::error_code ec;
    std::filesystem::create_directory_symlink(outside.path, ws.path / "link", ec);
    auto srv = cfire::mcp::build_compiler_mcp_server(rt_dir, cfire::mcp::McpOptions{ws.path, false});
    auto call = [&](std::string_view tool, std::string_view args) {
        return srv.handle_jsonrpc(std::format(R"({{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{{"name":"{}","arguments":{}}}}})", tool, args));
    };
    auto esc = [](const std::filesystem::path& p) { return cfire::lsp::detail::json_escape(p.string()); };
    require(call("cfire_check", std::format(R"({{"path":"{}"}})", esc(ws.path / "main.cf"))).contains(R"("isError":false)"),
        "CF-160: a path inside the root is allowed");
    for (const auto& bad : {outside.path / "secret.cf", ws.path / ".." / outside.path.filename() / "secret.cf", ws.path / "link" / "secret.cf"}) {
        auto resp = call("cfire_check", std::format(R"({{"path":"{}"}})", esc(bad)));
        require(resp.contains(R"("isError":true)") && resp.contains("outside the MCP workspace root"),
            std::format("CF-160: escape via '{}' is rejected", bad.string()));
    }
    auto ren = call("cfire_rename", std::format(R"({{"target":"{}:2:4","new_name":"x","dry_run":true}})", esc(outside.path / "secret.cf")));
    require(ren.contains("outside the MCP workspace root"), "CF-160: cfire_rename outside the root is rejected");
    auto opt_out = cfire::mcp::build_compiler_mcp_server(rt_dir, cfire::mcp::McpOptions{ws.path, true});
    auto resp = opt_out.handle_jsonrpc(std::format(R"({{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{{"name":"cfire_check","arguments":{{"path":"{}"}}}}}})", esc(outside.path / "secret.cf")));
    require(!resp.contains("outside the MCP workspace root"), "CF-160: --allow-outside-root opts out");
}

// CF-168: no privileged SSH default user; an empty user means the local login name (like ssh(1)).
void test_cf168_secure_runtime_defaults([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_cf168_secure_runtime_defaults (CF-168)");
#if !defined(_WIN32)
    cfire::ssh::detail::mock_enabled_flag().store(true);
    std::string saved_user = std::getenv("USER") ? std::getenv("USER") : "";
    ::setenv("USER", "cf168-alice", 1);
    auto s1 = cfire::ssh::dial("127.0.0.1", 2222);
    require(s1.has_value(), "CF-168: ssh.dial without a user connects (mock)");
    ::unsetenv("USER");
    std::string saved_logname = std::getenv("LOGNAME") ? std::getenv("LOGNAME") : "";
    ::unsetenv("LOGNAME");
    auto s2 = cfire::ssh::dial("127.0.0.1", 2222);
    require(!s2.has_value() && std::string(s2.error().message()).contains("no user given"),
        "CF-168: ssh.dial with no user and no login name fails instead of defaulting to root");
    if (!saved_user.empty()) ::setenv("USER", saved_user.c_str(), 1);
    if (!saved_logname.empty()) ::setenv("LOGNAME", saved_logname.c_str(), 1);
    cfire::ssh::detail::mock_enabled_flag().store(false);
#endif
}
