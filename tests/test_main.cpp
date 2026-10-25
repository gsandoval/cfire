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
        test_google_cpp_and_language_design(rt_dir);
        test_section2_language_ergonomics(rt_dir);
        test_section3_agent_tooling(rt_dir);
    }
    if (should_run("e2e")) {
        test_end_to_end_canonical_examples(rt_dir);
        test_raylib_vendor_codegen_and_eject(rt_dir);
        test_cfire_cross_compile(rt_dir);
        test_wasm_and_gui_subsystem(rt_dir);
    }
    if (should_run("stdlib")) {
        test_crypto_sha256_and_csprng(rt_dir);
        test_composable_expression_try(rt_dir);
        test_buildsys_incremental_caching(rt_dir);
        test_error_handling_divergence(rt_dir);
        test_subprocess_spawning_and_capture(rt_dir);
        test_cpp_ast_ingestion_and_overloads(rt_dir);
        test_parameter_intent_validation(rt_dir);
        test_cpp_escape_hatch_execution(rt_dir);
        test_http_client_server_and_urls(rt_dir);
        test_monadic_type_orthogonality(rt_dir);
        test_structured_concurrency_lifecycle(rt_dir);
        test_explicit_borrow_references(rt_dir);
        test_cfg_move_analysis(rt_dir);
        test_data_encodings_base64_hex_csv(rt_dir);
        test_relocatable_cmake_ejection(rt_dir);
        test_variant_pattern_matching(rt_dir);
        test_mutation_exclusivity_path_alias(rt_dir);
        test_preprocessor_macro_injection(rt_dir);
        test_regex_matching_and_captures(rt_dir);
        test_collections_and_in_place_sort(rt_dir);
        test_generic_call_disambiguation(rt_dir);
        test_time_durations_and_calendar(rt_dir);
        test_structured_logging_json_and_text(rt_dir);
        test_shared_library_dynamic_linking(rt_dir);
        test_multi_return_tuples_and_destructuring(rt_dir);
        test_filesystem_streaming_and_paths(rt_dir);
        test_json_dynamic_dom_and_serializer(rt_dir);
        test_variable_declarations_unification(rt_dir);
        test_recursive_package_resolution(rt_dir);
        test_pre_build_codegen_hooks(rt_dir);
        test_unicode_utf8_runes_and_case(rt_dir);
        test_struct_tags_and_metadata(rt_dir);
        test_agent_tooling_outline_and_json(rt_dir);
        test_ide_configuration_setup(rt_dir);
        test_cfire_eval(rt_dir);
        test_cfire_remote_vendor(rt_dir);
        test_in_process_sha256_file_hashing();
        test_crypto_hmac_and_aes(rt_dir);
        test_sync_context(rt_dir);
        test_net_websocket(rt_dir);
        test_net_http_and_router(rt_dir);
        test_security_and_fuzzing(rt_dir);
        test_performance_benchmarks(rt_dir);
    }
    if (suite == "fuzz") {
        test_security_and_fuzzing(rt_dir);
    }
    if (suite == "bench") {
        test_performance_benchmarks(rt_dir);
    }

    if (g_failures.load() > 0) {
        std::println(stderr, "FAIL ({} failures)", g_failures.load());
        return 1;
    }
    std::println("PASS (all C++23 cfire conformance tests succeeded)");
    return 0;
}


