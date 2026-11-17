// ThreadSanitizer driver: runs the runtime-level concurrency tests (sync contexts,
// cancellation cascades, deadlines) under -fsanitize=thread. Generated-program concurrency is
// covered by `tests/conformance/concurrency/*` via `make test-tsan`.
#include "test_harness.hpp"
#include "unit/test_stdlib_net_sync.hpp"
#include "unit/test_diagnostics_and_build_audit.hpp"
#include "unit/test_net_security.hpp"
#include "unit/test_http_keepalive_and_ws.hpp"
#include "unit/test_http_event_loop.hpp"
#include "unit/test_websocket_sessions.hpp"
#include "unit/test_websocket_handshake_tail.hpp"
#include "unit/test_fs_http_time_ipc.hpp"

auto main() -> int {
    ::signal(SIGPIPE, SIG_IGN);
    auto rt_dir = std::filesystem::current_path() / "runtime";
    test_sync_context(rt_dir);
    test_sync_context_parent_lifetime(rt_dir);
    test_http_server_hardening(rt_dir);  // worker pool under TSan
    test_http_keepalive_and_websocket_sessions(rt_dir);  // keep-alive and WebSocket session threads
    test_http_server_event_loop(rt_dir);  // the event loop and its workers
    test_websocket_server_sessions(rt_dir);  // websocket.Server session threads
    test_websocket_handshake_read_ahead(rt_dir);  // frames that arrive with the handshake
    test_http_host_bind_and_client_timeout(rt_dir);
    test_ipc_stream_security_and_concurrency(rt_dir);
    if (g_failures.load() > 0) {
        std::println("FAIL ({} tsan test assertion(s) failed)", g_failures.load());
        return 1;
    }
    std::println("PASS (tsan concurrency tests succeeded)");
    return 0;
}
