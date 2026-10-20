#include "test_harness.hpp"

auto main(int argc, char** argv) -> int {
    ::signal(SIGPIPE, SIG_IGN);
    auto rt_dir = std::filesystem::current_path() / "runtime";
    std::string suite = (argc > 1) ? argv[1] : "all";

    auto should_run = [&](std::string_view category) {
        return suite == "all" || suite == category;
    };

    if (should_run("unit")) {
        test_sema_intent_and_move_diagnostics();
        test_formatter_lsp_and_docgen(rt_dir);
        test_cfire_refactor_rename(rt_dir);
        test_slice_pattern_matching(rt_dir);
    }
    if (should_run("e2e")) {
        test_end_to_end_canonical_examples(rt_dir);
        test_raylib_vendor_codegen_and_eject(rt_dir);
        test_cfire_cross_compile(rt_dir);
    }
    if (should_run("stdlib")) {
        test_p1_features(rt_dir);
        test_p2_features(rt_dir);
        test_p2_batch2_features(rt_dir);
        test_p2_batch3_features(rt_dir);
        test_p2_batch4_features(rt_dir);
        test_p2_batch5_features(rt_dir);
        test_cfire_eval(rt_dir);
        test_cfire_remote_vendor(rt_dir);
        test_in_process_sha256_file_hashing();
        test_crypto_hmac_and_aes(rt_dir);
        test_sync_context(rt_dir);
        test_net_websocket(rt_dir);
        test_net_http_and_router(rt_dir);
    }
    if (should_run("selfhost") || should_run("benchmarks")) {
        test_selfhost_lexer_parity_and_benchmark(rt_dir);
        test_selfhost_formatter_parity_and_benchmark(rt_dir);
        test_selfhost_ast_parity_and_benchmark(rt_dir);
        test_selfhost_refactor_parity_and_benchmark(rt_dir);
        test_selfhost_docgen_parity_and_benchmark(rt_dir);
        test_selfhost_parser_parity_and_benchmark(rt_dir);
        test_selfhost_sema_parity_and_benchmark(rt_dir);
        test_selfhost_codegen_parity_and_benchmark(rt_dir);
        test_selfhost_buildsys_parity_and_benchmark(rt_dir);
        test_selfhost_lsp_parity_and_benchmark(rt_dir);
        test_selfhost_main_parity_and_benchmark(rt_dir);
        test_selfhost_clang_bridge_parity_and_benchmark(rt_dir);
    }

    if (g_failures.load() > 0) {
        std::println(stderr, "FAIL ({} failures)", g_failures.load());
        return 1;
    }
    std::println("PASS (all C++23 cfire conformance tests succeeded)");
    return 0;
}


