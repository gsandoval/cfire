#include "test_harness.hpp"
#include "runner_self_tests.hpp"

auto main(int argc, char** argv) -> int {
    test_ignore_sigpipe();
    auto rt_dir = std::filesystem::current_path() / "runtime";
    std::string suite = (argc > 1) ? argv[1] : "all";

    auto should_run = [&](std::string_view category) {
        return suite == "all" || suite == category;
    };

    // the conformance corpus and the GUIDE doc-tests (formerly scripts/run_conformance.py and run_guide_tests.py).
    // `all` does not include them: `make check` runs each once, and the sanitizer targets rerun conformance with
    // their own --run-flags.
    if (suite == "conformance" || suite == "guide") {
        conformance::Options opt;
        opt.cfire = std::filesystem::absolute(cfire_exe()).string();
        opt.runtime_dir = rt_dir;
        std::string filter = "*";
        std::size_t jobs = default_jobs();
        bool verbose = false;
        for (int i = 2; i < argc; ++i) {
            std::string_view a = argv[i];
            auto value = [&](std::string_view flag) -> std::optional<std::string> {
                if (a.starts_with(flag) && a.size() > flag.size() && a[flag.size()] == '=') return std::string(a.substr(flag.size() + 1));
                if (a == flag && i + 1 < argc) return std::string(argv[++i]);
                return std::nullopt;
            };
            if (auto v = value("--filter")) filter = *v;
            else if (auto v = value("--jobs")) jobs = static_cast<std::size_t>(std::max(1, std::atoi(v->c_str())));
            else if (auto v = value("--timeout")) opt.timeout = std::max(1, std::atoi(v->c_str()));
            else if (auto v = value("--cfire")) opt.cfire = std::filesystem::absolute(*v).string();
            else if (auto v = value("--run-flags")) {
                for (auto& f : conformance::split_ws(*v)) opt.run_flags.push_back(f);
            } else if (a == "-v" || a == "--verbose") verbose = true;
            else {
                std::println(stderr, "unknown {} option: {}", suite, a);
                return 2;
            }
        }
        if (suite == "guide") return guide::run_guide(opt.cfire, std::filesystem::current_path(), jobs);
        return conformance::run_corpus(opt, std::filesystem::current_path() / "tests" / "conformance", filter, jobs, verbose);
    }

    if (should_run("unit")) {
        test_sema_intent_and_move_diagnostics();
        test_diagnostic_golden_ranges();
        test_golden_codegen_snapshots(std::filesystem::current_path());
        test_formatter_lsp_and_docgen(rt_dir);
        test_cfire_refactor_rename(rt_dir);
        test_slice_pattern_matching(rt_dir);
        test_hard_deprecation_enforcement(rt_dir);
        test_duplicate_declaration_pass(rt_dir);
        test_catalog_gated_import_validation(rt_dir);
        test_plain_enum_usability_cluster(rt_dir);
        test_uniform_generic_type_application(rt_dir);
        test_cpp_backend_error_leak_prevention(rt_dir);
        test_scoped_name_resolution_cpp_ffi(rt_dir);
        test_match_exhaustiveness_and_reachability(rt_dir);
        test_or_chaining_and_block_disambiguation(rt_dir);
        test_lexer_exponents_keywords_and_interpolation(rt_dir);
        test_content_hashed_pch_and_cli_flag_validation(rt_dir);
        test_multiline_lambda_source_mapping(rt_dir);
        test_interned_typeid_lattice_and_diagnostics(rt_dir);
        test_unified_stdlib_metadata_table(rt_dir);
        test_unified_diagnostics_pipeline(rt_dir);
        test_concurrency_match_and_flow_control(rt_dir);
        test_advanced_language_and_safety(rt_dir);
        test_lsp_refactor_and_docgen_depth(rt_dir);
        test_negative_parser_corpus_and_fuzz_runtime(rt_dir);
        test_crash_proof_frontend_and_tools(rt_dir);
        test_sync_context_parent_lifetime(rt_dir);
        test_secure_temp_storage(rt_dir);
        test_no_inband_tls_controls(rt_dir);
        test_http_server_hardening(rt_dir);
        test_verified_dependency_cache(rt_dir);
        test_mcp_workspace_confinement(rt_dir);
        test_secure_runtime_defaults(rt_dir);
        test_retired_api_migration_rewrites(rt_dir);
        test_builtin_runtime_semantics(rt_dir);
        test_dual_stack_tcp_listener(rt_dir);
        test_formatter_roundtrip_corpus(rt_dir);
        test_rename_scopes_and_diff(rt_dir);
        test_generated_doc_integrity(rt_dir);
        test_standalone_scripts_and_fmt_flags(rt_dir);
        test_stdlib_type_docs_and_hover(rt_dir);
        test_http_keepalive_and_websocket_sessions(rt_dir);
        test_http_server_event_loop(rt_dir);
        test_websocket_server_sessions(rt_dir);
        test_websocket_handshake_read_ahead(rt_dir);
        test_deflate_stops_at_end_of_input(rt_dir);
        test_google_cpp_and_language_design(rt_dir);
        test_language_ergonomics(rt_dir);
        test_cli_agent_and_workspace_tooling(rt_dir);
        test_conformance_and_guide_runner_self_tests(rt_dir);
        test_toolchain_pch_and_binary_caching(rt_dir);
        test_single_pass_in_memory_check(rt_dir);
        test_sharded_cpp_codegen(rt_dir);
        test_vendored_archives_and_portable_releases(rt_dir);
        test_language_consistency_and_docs(rt_dir);
        test_ext_soundness_cache_and_clean(rt_dir);
        test_package_fixes_and_generics(rt_dir);
        test_error_binding_and_named_arg_validation();
        test_webui_security_and_desktop(rt_dir);
    }
    if (should_run("e2e")) {
        test_end_to_end_canonical_examples(rt_dir);
        test_cinterop_codegen_and_eject(rt_dir);
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
        test_webui_rpc_events_and_linux_backend(rt_dir);
        test_fs_exclusive_create_stat_and_sync(rt_dir);
        test_http_host_bind_and_client_timeout(rt_dir);
        test_time_local_offset_and_format(rt_dir);
        test_ipc_stream_security_and_concurrency(rt_dir);
        test_security_and_fuzzing(rt_dir);
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


