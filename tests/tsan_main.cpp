// ThreadSanitizer driver (CF-159): runs the runtime-level concurrency tests (sync contexts,
// cancellation cascades, deadlines) under -fsanitize=thread. Generated-program concurrency is
// covered by `tests/conformance/concurrency/*` via `make test-tsan`.
#include "test_harness.hpp"
#include "unit/test_stdlib_net_sync.hpp"
#include "unit/test_audit_w0.hpp"
#include "unit/test_audit_w2.hpp"
#include "unit/test_cf192.hpp"
#include "unit/test_cf193.hpp"

auto main() -> int {
    ::signal(SIGPIPE, SIG_IGN);
    auto rt_dir = std::filesystem::current_path() / "runtime";
    test_sync_context(rt_dir);
    test_cf177_context_lifetime(rt_dir);
    test_cf150_http_server_hardening(rt_dir);  // worker pool under TSan
    test_cf192_keepalive_and_websocket_sessions(rt_dir);  // keep-alive and WebSocket session threads
    test_cf193_http_event_loop(rt_dir);  // the event loop and its workers
    if (g_failures.load() > 0) {
        std::println("FAIL ({} tsan test assertion(s) failed)", g_failures.load());
        return 1;
    }
    std::println("PASS (tsan concurrency tests succeeded)");
    return 0;
}
