#pragma once
// tests/unit/test_fs_http_time_ipc.hpp — Unit tests for filesystem primitives, HTTP host/timeout, Time formatting, and IPC streams.

#include "../test_harness.hpp"
#include "core/fs.hpp"
#include "core/net/http.hpp"
#include "core/net/ipc.hpp"
#include "core/net/tcp.hpp"
#include "core/net/websocket.hpp"
#include "core/time.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <sys/stat.h>
#include <unistd.h>
#endif

void test_fs_exclusive_create_stat_and_sync([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_fs_exclusive_create_stat_and_sync");
    ScopedTempDir tmp("cfire_fs_unit");
    auto target = tmp.subpath("sub") / "exclusive.dat";

    // create_new succeeds once with empty initial file, fails with ERR_EXISTS on retry.
    {
        auto f = cfire::fs::create_new(target);
        require(f.has_value(), std::format("create_new succeeds: {}", f ? "" : f.error().message()));
        if (f) {
            auto w = f->write_text("alpha-beta");
            require(w.has_value() && *w == 10, "write_text wrote 10 bytes");
            auto fl = f->flush();
            require(fl.has_value(), "File::flush succeeds");
            auto sy = f->sync();
            require(sy.has_value(), std::format("File::sync succeeds: {}", sy ? "" : sy.error().message()));
            f->close();
        }
    }

    auto dup = cfire::fs::create_new(target, 0600);
    require(!dup.has_value(), "second create_new on existing path fails");
    if (!dup) {
        require(dup.error().code() == cfire::fs::ERR_EXISTS,
            std::format("create_new collision returns ERR_EXISTS (got {})", dup.error().code()));
    }

    auto missing = cfire::fs::open(tmp.path / "does_not_exist.txt", cfire::fs::OpenMode::Read);
    require(!missing.has_value(), "open on missing file fails");
    if (!missing) {
        require(missing.error().code() == cfire::fs::ERR_NOT_FOUND,
            std::format("missing file returns ERR_NOT_FOUND (got {})", missing.error().code()));
    }

    auto st_file = cfire::fs::stat(target);
    require(st_file.has_value(), std::format("fs::stat file succeeds: {}", st_file ? "" : st_file.error().message()));
    if (st_file) {
        require(st_file->size == 10, "stat size is 10");
        require(!st_file->is_dir, "stat reports non-directory");
        require(!st_file->is_symlink, "stat reports non-symlink");
        require(st_file->mtime_ms > 0, "stat mtime_ms > 0");
    }

    auto st_dir = cfire::fs::stat(tmp.subpath("sub"));
    require(st_dir.has_value() && st_dir->is_dir, "fs::stat directory reports is_dir");

    auto rp = cfire::fs::realpath(target);
    require(rp.has_value(), std::format("fs::realpath succeeds: {}", rp ? "" : rp.error().message()));
    if (rp) {
        std::error_code ec;
        auto expected_canon = std::filesystem::weakly_canonical(target, ec);
        require(!ec && std::filesystem::path(*rp) == expected_canon,
            std::format("realpath matches canonical path ({} vs {})", *rp, expected_canon.string()));
    }

    auto missing_rp = cfire::fs::realpath(tmp.path / "missing_target");
    require(!missing_rp.has_value() && missing_rp.error().code() == cfire::fs::ERR_NOT_FOUND,
        "realpath on missing path returns ERR_NOT_FOUND");

    // Standard output non-owning handle flushes without failing on stdio.
    cfire::fs::File out_handle(1, false);
    require(out_handle.flush().has_value(), "File::flush on stdout non-owning fd succeeds");

    std::println("--- PASS: test_fs_exclusive_create_stat_and_sync");
}

void test_http_host_bind_and_client_timeout([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_http_host_bind_and_client_timeout");

    // 1. Loopback-only host bind ("127.0.0.1") + moving running HttpServer.
    {
        auto srv_res = cfire::http::serve_in_background(
            0,
            [](const cfire::http::Request& req) {
                return cfire::http::Response::text(200, "host-ok:" + req.path);
            },
            {},
            "127.0.0.1"
        );
        require(srv_res.has_value(), std::format("serve_in_background on 127.0.0.1 starts: {}",
            srv_res ? "" : srv_res.error().message()));
        if (srv_res) {
            // Move a running HttpServer while its background thread holds State*.
            cfire::http::HttpServer moved_srv = std::move(*srv_res);
            std::uint16_t port = moved_srv.port();
            require(port != 0, "ephemeral port assigned");

            auto ok = cfire::http::get(std::format("http://127.0.0.1:{}/check", port), {}, true, 5000);
            require(ok.has_value() && ok->status_code == 200 && ok->body == "host-ok:/check",
                "127.0.0.1 connection succeeds after moving running HttpServer");

            // Loopback-restricted IPv4 listener must NOT accept IPv6 ::1 loopback connections.
            auto v6_refused = cfire::net::connect_tcp_timeout("::1", port, 300);
            require(!v6_refused.has_value(), "127.0.0.1 listener does not accept ::1 connections");

            moved_srv.stop();
        }
    }

    // 2. WebSocket server loopback host bind + move while running.
    {
        auto ws_res = cfire::websocket::make_server(0, {}, "127.0.0.1");
        require(ws_res.has_value(), std::format("websocket::make_server on 127.0.0.1 starts: {}",
            ws_res ? "" : ws_res.error().message()));
        if (ws_res) {
            ws_res->serve_in_background([](cfire::websocket::Conn& conn) {
                auto msg = conn.recv_text();
                if (msg) (void)conn.send_text("ws:" + *msg);
                conn.close();
            });
            cfire::websocket::Server moved_ws = std::move(*ws_res);
            std::uint16_t port = moved_ws.port();
            require(port != 0, "websocket server assigned ephemeral port");

            auto cli = cfire::websocket::connect(std::format("ws://127.0.0.1:{}/", port));
            require(cli.has_value(), std::format("websocket loopback client connects: {}", cli ? "" : cli.error().message()));
            if (cli) {
                require(cli->send_text("ping").has_value(), "websocket send_text");
                auto rep = cli->recv_text();
                require(rep.has_value() && *rep == "ws:ping", "websocket echo received from moved server");
                cli->close();
            }
            moved_ws.stop();
        }
    }

    // 3. Client end-to-end deadline: stalled server fails fast across get/post/request/post_sse.
    {
        auto ln_res = cfire::net::listen(0, "127.0.0.1");
        require(ln_res.has_value(), "stalled test TcpListener binds");
        if (ln_res) {
            const std::uint16_t port = ln_res->port();
            std::thread server_thread([&]() {
                for (int i = 0; i < 4; ++i) {
                    auto conn = ln_res->accept();
                    if (!conn) break;
                    (void)conn->poll(50);
                    std::this_thread::sleep_for(std::chrono::milliseconds(550));
                    conn->close();
                }
            });

            auto url = std::format("http://127.0.0.1:{}/stall", port);

            auto t0 = std::chrono::steady_clock::now();
            auto r_get = cfire::http::get(url, {}, true, 200);
            auto dt_get = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
            require(!r_get.has_value() && dt_get < 2000,
                std::format("http::get fails within timeout (dt={}ms)", dt_get));

            auto t1 = std::chrono::steady_clock::now();
            auto r_post = cfire::http::post(url, "{\"a\":1}", "application/json", {}, true, 200);
            auto dt_post = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t1).count();
            require(!r_post.has_value() && dt_post < 2000,
                std::format("http::post fails within timeout (dt={}ms)", dt_post));

            auto t2 = std::chrono::steady_clock::now();
            auto r_req = cfire::http::request("PUT", url, "body", "text/plain", {}, true, "", 200);
            auto dt_req = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t2).count();
            require(!r_req.has_value() && dt_req < 2000,
                std::format("http::request fails within timeout (dt={}ms)", dt_req));

            auto t3 = std::chrono::steady_clock::now();
            auto r_sse = cfire::http::post_sse(url, "{}", [](const cfire::http::SseEvent&) { return true; }, true, "", 200);
            auto dt_sse = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t3).count();
            require(!r_sse.has_value() && dt_sse < 2000,
                std::format("http::post_sse fails within timeout (dt={}ms)", dt_sse));

            if (server_thread.joinable()) server_thread.join();
        }
    }

    // 4. Slow response succeeds when timeout_ms == 0 disables the deadline.
    {
        auto ln_res = cfire::net::listen(0, "127.0.0.1");
        require(ln_res.has_value(), "slow test TcpListener binds");
        if (ln_res) {
            const std::uint16_t port = ln_res->port();
            std::thread slow_thread([&]() {
                if (auto conn = ln_res->accept()) {
                    (void)conn->poll(50);
                    std::this_thread::sleep_for(std::chrono::milliseconds(220));
                    (void)conn->write_text("HTTP/1.1 200 OK\r\nContent-Length: 7\r\nConnection: close\r\n\r\nslow-ok");
                    conn->close();
                }
            });

            auto slow_ok = cfire::http::get(std::format("http://127.0.0.1:{}/slow", port), {}, true, 0);
            require(slow_ok.has_value() && slow_ok->status_code == 200 && slow_ok->body == "slow-ok",
                std::format("timeout_ms = 0 succeeds on slow response: {}", slow_ok ? slow_ok->body : slow_ok.error().message()));
            if (slow_thread.joinable()) slow_thread.join();
        }
    }

    std::println("--- PASS: test_http_host_bind_and_client_timeout");
}

void test_time_local_offset_and_format([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_time_local_offset_and_format");

    auto parsed = cfire::time::parse_rfc3339("2026-05-01T14:30:00-04:00");
    require(parsed.has_value(), std::format("parse_rfc3339 offset succeeds: {}", parsed ? "" : parsed.error().message()));
    if (parsed) {
        require(parsed->format_rfc3339() == "2026-05-01T14:30:00-04:00",
            std::format("format_rfc3339 preserves parsed offset (got {})", parsed->format_rfc3339()));

        cfire::time::Time as_utc = parsed->utc();
        cfire::time::Time as_local = parsed->local();
        require(as_utc == *parsed && as_local == *parsed, "offset views compare equal when representing the same instant");
        require((as_utc <=> *parsed) == std::strong_ordering::equal, "three-way comparison compares instants only");
        require(as_utc.unix_nanos() == parsed->unix_nanos() && as_local.unix_nanos() == parsed->unix_nanos(), "unix_nanos preserved");
        require(as_utc.format_rfc3339() == "2026-05-01T18:30:00Z",
            std::format("utc() formats as Z (got {})", as_utc.format_rfc3339()));

        std::string fmt = as_utc.format("%Y/%m/%d %H:%M:%S %z %Z %% %Q %");
        require(fmt == "2026/05/01 18:30:00 +0000 UTC % %Q %",
            std::format("Time::format formatted tokens safely without aborting (got '{}')", fmt));

        cfire::time::Time plus_offset = parsed->add(cfire::time::duration_millis(1000));
        require(*parsed < plus_offset, "earlier instant orders before later instant");
    }

    std::println("--- PASS: test_time_local_offset_and_format");
}

void test_ipc_stream_security_and_concurrency([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_ipc_stream_security_and_concurrency");
#if defined(_WIN32)
    const auto pid = static_cast<long>(::GetCurrentProcessId());
#else
    const auto pid = static_cast<long>(::getpid());
#endif

    // 1. Listener rejects overwriting a regular file (POSIX) / tests socket mode & stale cleanup.
    ScopedTempDir tmp("cfire_ipc_unit");
#if !defined(_WIN32)
    auto regular_path = (tmp.path / "existing.txt").string();
    {
        std::ofstream ofs(regular_path, std::ios::binary);
        ofs << "keep-intact";
    }
    auto collide_file = cfire::ipc::listen(regular_path);
    require(!collide_file.has_value(), "ipc::listen refuses to overwrite an existing non-socket file");
    {
        std::ifstream ifs(regular_path, std::ios::binary);
        std::string content;
        std::getline(ifs, content);
        require(content == "keep-intact", "existing regular file left completely intact after collision error");
    }

    // Use a short socket path within sun_path limit inside ScopedTempDir or private user cache.
    std::string sock_ep = (tmp.path / "s.sock").string();
    std::string extra_cleanup;
    if (sock_ep.size() >= 96) {
        auto udir = cfire::detail::user_cache_dir("ipc-test");
        if (!udir.empty()) {
            sock_ep = (udir / std::format("u_{}.sock", pid)).string();
            extra_cleanup = sock_ep;
        }
    }
    auto cleanup_sock = cfire::scope_exit([&]() {
        if (!extra_cleanup.empty()) ::unlink(extra_cleanup.c_str());
    });
    ::unlink(sock_ep.c_str());

    {
        auto stale_ln = cfire::ipc::listen(sock_ep);
        require(stale_ln.has_value(), std::format("initial listen creates socket: {}",
            stale_ln ? "" : stale_ln.error().message()));
        struct stat st{};
        require(::stat(sock_ep.c_str(), &st) == 0 && (st.st_mode & 0777) == 0600,
            std::format("IPC socket has 0600 permissions (got {:o})", static_cast<unsigned>(st.st_mode & 0777)));
        auto live_collide = cfire::ipc::listen(sock_ep);
        require(!live_collide.has_value(), "ipc::listen refuses when a live listener is already bound");
    }
    // Simulate a stale bound AF_UNIX socket left behind by an exited process.
    {
        int raw_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (raw_fd >= 0) {
            sockaddr_un addr{};
            addr.sun_family = AF_UNIX;
            std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", sock_ep.c_str());
            if (::bind(raw_fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == 0 && ::listen(raw_fd, 4) == 0) {
                ::close(raw_fd);
                auto recovered = cfire::ipc::listen(sock_ep);
                require(recovered.has_value(), std::format("ipc::listen recovers stale unlinked-less AF_UNIX socket: {}",
                    recovered ? "" : recovered.error().message()));
            } else {
                ::close(raw_fd);
            }
        }
    }
#endif

    // 2. Concurrent multi-client bidirectional streaming + peer_credentials + listener close isolation.
    const std::string ep = cfire::ipc::endpoint(std::format("cfire-unit-ipc-{}", pid));
    auto ln_res = cfire::ipc::listen(ep);
    require(ln_res.has_value(), std::format("ipc::listen({}) succeeds: {}", ep, ln_res ? "" : ln_res.error().message()));
    if (ln_res) {
        require(ln_res->is_open(), "ipc Listener reports is_open");
        require(ln_res->endpoint() == ep, "ipc Listener reports bound endpoint");

        constexpr int kClients = 4;
        std::vector<std::thread> clients;
        std::atomic<int> client_ok{0};
        for (int i = 0; i < kClients; ++i) {
            clients.emplace_back([&, i]() {
                auto cli = cfire::ipc::connect(ep, 3000);
                require(cli.has_value(), std::format("client {} connect succeeds: {}", i, cli ? "" : cli.error().message()));
                if (!cli) {
                    ln_res->close();
                    return;
                }
                std::string msg = std::format("msg-{:02d}", i);
                if (!cli->write_text(msg).has_value()) {
                    require(false, "client write_text");
                    ln_res->close();
                    return;
                }
                auto ack = cli->read_exact(msg.size());
                require(ack.has_value() && std::string(reinterpret_cast<const char*>(ack->data()), ack->size()) == msg,
                    std::format("client {} received echo", i));
                if (ack && std::string(reinterpret_cast<const char*>(ack->data()), ack->size()) == msg) {
                    client_ok.fetch_add(1, std::memory_order_relaxed);
                }
                cli->close();
            });
        }

        std::vector<cfire::ipc::Conn> accepted;
        accepted.reserve(kClients);
        for (int i = 0; i < kClients; ++i) {
            auto c = ln_res->accept();
            require(c.has_value(), std::format("server accept {} succeeds: {}", i, c ? "" : c.error().message()));
            if (c) accepted.push_back(std::move(*c));
        }

        // Closing listener must NOT break already-accepted connections.
        ln_res->close();
        require(!ln_res->is_open(), "listener reports closed after close()");

        for (auto& sc : accepted) {
            auto cred = sc.peer_credentials();
            require(cred.has_value(), std::format("peer_credentials succeeds: {}", cred ? "" : cred.error().message()));
            if (cred) {
                require(cred->same_user, "peer_credentials reports same_user");
                require(cred->pid == pid, std::format("peer_credentials pid matches current pid ({} vs {})", cred->pid, pid));
            }
            auto rx = sc.read_exact(6);
            require(rx.has_value(), "server read_exact succeeds even after listener closed");
            if (rx) {
                require(sc.write_bytes(*rx).has_value(), "server write_bytes echo succeeds");
            }
            sc.close();
        }

        for (auto& t : clients) {
            if (t.joinable()) t.join();
        }
        require(client_ok.load() == kClients, std::format("all {} concurrent IPC clients succeeded", kClients));
    }

    std::println("--- PASS: test_ipc_stream_security_and_concurrency");
}
