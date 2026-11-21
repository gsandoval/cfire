#include "test_harness.hpp"
#include "runner_self_tests.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <mutex>
#include <ranges>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

struct SuiteEntry {
    std::string_view category;
    std::string_view name;
    void (*run)(const std::filesystem::path& rt_dir);
    bool requires_loopback = false;  // needs unsandboxed loopback TCP, AF_UNIX sockets, or macOS Cocoa GUI
    bool touches_shared_examples = false;  // reads or mutates shared repo trees (examples/, docs/, IDE configs)
    int weight = 1;
};

void run_test_sema_intent_and_move_diagnostics(const std::filesystem::path&) { test_sema_intent_and_move_diagnostics(); }
void run_test_diagnostic_golden_ranges(const std::filesystem::path&) { test_diagnostic_golden_ranges(); }
void run_test_golden_codegen_snapshots(const std::filesystem::path& rt_dir) {
    auto repo = rt_dir.parent_path();
    test_golden_codegen_snapshots(repo.empty() ? std::filesystem::current_path() : repo);
}
void run_test_error_binding_and_named_arg_validation(const std::filesystem::path&) { test_error_binding_and_named_arg_validation(); }
void run_test_in_process_sha256_file_hashing(const std::filesystem::path&) { test_in_process_sha256_file_hashing(); }
void run_test_closed_type_checker(const std::filesystem::path&) { test_closed_type_checker(); }

inline constexpr SuiteEntry kSuites[] = {
    // Unit suites
    {"unit", "test_sema_intent_and_move_diagnostics", run_test_sema_intent_and_move_diagnostics, false, false, 1},
    {"unit", "test_diagnostic_golden_ranges", run_test_diagnostic_golden_ranges, false, false, 1},
    {"unit", "test_golden_codegen_snapshots", run_test_golden_codegen_snapshots, false, false, 1},
    {"unit", "test_formatter_lsp_and_docgen", test_formatter_lsp_and_docgen, false, true, 30},
    {"unit", "test_lsp_completion_and_sync", test_lsp_completion_and_sync, false, false, 15},
    {"unit", "test_cfire_refactor_rename", test_cfire_refactor_rename, false, false, 5},
    {"unit", "test_slice_pattern_matching", test_slice_pattern_matching, false, false, 2},
    {"unit", "test_hard_deprecation_enforcement", test_hard_deprecation_enforcement, false, false, 2},
    {"unit", "test_duplicate_declaration_pass", test_duplicate_declaration_pass, false, false, 2},
    {"unit", "test_catalog_gated_import_validation", test_catalog_gated_import_validation, false, false, 2},
    {"unit", "test_plain_enum_usability_cluster", test_plain_enum_usability_cluster, false, false, 2},
    {"unit", "test_uniform_generic_type_application", test_uniform_generic_type_application, false, false, 2},
    {"unit", "test_cpp_backend_error_leak_prevention", test_cpp_backend_error_leak_prevention, false, false, 5},
    {"unit", "test_scoped_name_resolution_cpp_ffi", test_scoped_name_resolution_cpp_ffi, false, false, 10},
    {"unit", "test_match_exhaustiveness_and_reachability", test_match_exhaustiveness_and_reachability, false, false, 2},
    {"unit", "test_or_chaining_and_block_disambiguation", test_or_chaining_and_block_disambiguation, false, false, 2},
    {"unit", "test_lexer_exponents_keywords_and_interpolation", test_lexer_exponents_keywords_and_interpolation, false, false, 5},
    {"unit", "test_content_hashed_pch_and_cli_flag_validation", test_content_hashed_pch_and_cli_flag_validation, false, false, 5},
    {"unit", "test_multiline_lambda_source_mapping", test_multiline_lambda_source_mapping, false, false, 5},
    {"unit", "test_interned_typeid_lattice_and_diagnostics", test_interned_typeid_lattice_and_diagnostics, false, false, 5},
    {"unit", "test_unified_stdlib_metadata_table", test_unified_stdlib_metadata_table, false, false, 5},
    {"unit", "test_unified_diagnostics_pipeline", test_unified_diagnostics_pipeline, false, false, 10},
    {"unit", "test_concurrency_match_and_flow_control", test_concurrency_match_and_flow_control, false, false, 10},
    {"unit", "test_advanced_language_and_safety", test_advanced_language_and_safety, false, false, 10},
    {"unit", "test_lsp_refactor_and_docgen_depth", test_lsp_refactor_and_docgen_depth, false, false, 10},
    {"unit", "test_negative_parser_corpus_and_fuzz_runtime", test_negative_parser_corpus_and_fuzz_runtime, false, false, 15},
    {"unit", "test_crash_proof_frontend_and_tools", test_crash_proof_frontend_and_tools, false, false, 10},
    {"unit", "test_sync_context_parent_lifetime", test_sync_context_parent_lifetime, false, false, 5},
    {"unit", "test_secure_temp_storage", test_secure_temp_storage, false, false, 10},
    {"unit", "test_no_inband_tls_controls", test_no_inband_tls_controls, true, false, 10},
    {"unit", "test_http_server_hardening", test_http_server_hardening, true, false, 15},
    {"unit", "test_verified_dependency_cache", test_verified_dependency_cache, false, false, 10},
    {"unit", "test_mcp_workspace_confinement", test_mcp_workspace_confinement, false, false, 5},
    {"unit", "test_secure_runtime_defaults", test_secure_runtime_defaults, true, false, 5},
    {"unit", "test_retired_api_migration_rewrites", test_retired_api_migration_rewrites, true, false, 10},
    {"unit", "test_builtin_runtime_semantics", test_builtin_runtime_semantics, false, false, 10},
    {"unit", "test_dual_stack_tcp_listener", test_dual_stack_tcp_listener, true, false, 10},
    {"unit", "test_formatter_roundtrip_corpus", test_formatter_roundtrip_corpus, false, false, 10},
    {"unit", "test_rename_scopes_and_diff", test_rename_scopes_and_diff, false, false, 10},
    {"unit", "test_generated_doc_integrity", test_generated_doc_integrity, false, true, 15},
    {"unit", "test_standalone_scripts_and_fmt_flags", test_standalone_scripts_and_fmt_flags, false, false, 10},
    {"unit", "test_stdlib_type_docs_and_hover", test_stdlib_type_docs_and_hover, false, false, 10},
    {"unit", "test_http_keepalive_and_websocket_sessions", test_http_keepalive_and_websocket_sessions, true, false, 15},
    {"unit", "test_http_server_event_loop", test_http_server_event_loop, true, false, 15},
    {"unit", "test_websocket_server_sessions", test_websocket_server_sessions, true, false, 15},
    {"unit", "test_websocket_handshake_read_ahead", test_websocket_handshake_read_ahead, true, false, 15},
    {"unit", "test_deflate_stops_at_end_of_input", test_deflate_stops_at_end_of_input, false, false, 5},
    {"unit", "test_google_cpp_and_language_design", test_google_cpp_and_language_design, false, false, 10},
    {"unit", "test_language_ergonomics", test_language_ergonomics, true, false, 45},
    {"unit", "test_cli_agent_and_workspace_tooling", test_cli_agent_and_workspace_tooling, false, true, 20},
    {"unit", "test_conformance_and_guide_runner_self_tests", test_conformance_and_guide_runner_self_tests, false, false, 25},
    {"unit", "test_toolchain_pch_and_binary_caching", test_toolchain_pch_and_binary_caching, false, false, 80},
    {"unit", "test_single_pass_in_memory_check", test_single_pass_in_memory_check, false, false, 30},
    {"unit", "test_sharded_cpp_codegen", test_sharded_cpp_codegen, false, false, 85},
    {"unit", "test_vendored_archives_and_portable_releases", test_vendored_archives_and_portable_releases, false, false, 95},
    {"unit", "test_toolchain_upgrade", test_toolchain_upgrade, false, false, 60},
    {"unit", "test_language_consistency_and_docs", test_language_consistency_and_docs, false, true, 65},
    {"unit", "test_ext_soundness_cache_and_clean", test_ext_soundness_cache_and_clean, true, false, 40},
    {"unit", "test_package_fixes_and_generics", test_package_fixes_and_generics, false, false, 30},
    {"unit", "test_error_binding_and_named_arg_validation", run_test_error_binding_and_named_arg_validation, false, false, 1},
    {"unit", "test_closed_type_checker", run_test_closed_type_checker, false, false, 2},
    {"unit", "test_webui_security_and_desktop", test_webui_security_and_desktop, true, false, 75},
    {"unit", "test_build_manifest_ios", test_build_manifest_ios, false, false, 20},

    // E2E suites
    {"e2e", "test_end_to_end_canonical_examples", test_end_to_end_canonical_examples, true, true, 100},
    {"e2e", "test_cinterop_codegen_and_eject", test_cinterop_codegen_and_eject, false, true, 40},
    {"e2e", "test_cfire_cross_compile", test_cfire_cross_compile, false, false, 70},
    {"e2e", "test_wasm_and_gui_subsystem", test_wasm_and_gui_subsystem, false, false, 70},

    // Stdlib suites
    {"stdlib", "test_crypto_sha256_and_csprng", test_crypto_sha256_and_csprng, false, false, 1},
    {"stdlib", "test_composable_expression_try", test_composable_expression_try, false, false, 5},
    {"stdlib", "test_buildsys_incremental_caching", test_buildsys_incremental_caching, false, false, 10},
    {"stdlib", "test_error_handling_divergence", test_error_handling_divergence, false, false, 1},
    {"stdlib", "test_subprocess_spawning_and_capture", test_subprocess_spawning_and_capture, false, false, 2},
    {"stdlib", "test_cpp_ast_ingestion_and_overloads", test_cpp_ast_ingestion_and_overloads, false, false, 1},
    {"stdlib", "test_parameter_intent_validation", test_parameter_intent_validation, false, false, 1},
    {"stdlib", "test_cpp_escape_hatch_execution", test_cpp_escape_hatch_execution, false, false, 5},
    {"stdlib", "test_http_client_server_and_urls", test_http_client_server_and_urls, true, false, 10},
    {"stdlib", "test_monadic_type_orthogonality", test_monadic_type_orthogonality, false, false, 5},
    {"stdlib", "test_structured_concurrency_lifecycle", test_structured_concurrency_lifecycle, false, false, 5},
    {"stdlib", "test_explicit_borrow_references", test_explicit_borrow_references, false, false, 5},
    {"stdlib", "test_cfg_move_analysis", test_cfg_move_analysis, false, false, 2},
    {"stdlib", "test_data_encodings_base64_hex_csv", test_data_encodings_base64_hex_csv, false, false, 5},
    {"stdlib", "test_relocatable_cmake_ejection", test_relocatable_cmake_ejection, false, true, 30},
    {"stdlib", "test_variant_pattern_matching", test_variant_pattern_matching, false, false, 5},
    {"stdlib", "test_mutation_exclusivity_path_alias", test_mutation_exclusivity_path_alias, false, false, 2},
    {"stdlib", "test_preprocessor_macro_injection", test_preprocessor_macro_injection, false, false, 5},
    {"stdlib", "test_regex_matching_and_captures", test_regex_matching_and_captures, false, false, 5},
    {"stdlib", "test_collections_and_in_place_sort", test_collections_and_in_place_sort, false, false, 5},
    {"stdlib", "test_generic_call_disambiguation", test_generic_call_disambiguation, false, false, 5},
    {"stdlib", "test_time_durations_and_calendar", test_time_durations_and_calendar, false, false, 5},
    {"stdlib", "test_structured_logging_json_and_text", test_structured_logging_json_and_text, false, false, 5},
    {"stdlib", "test_shared_library_dynamic_linking", test_shared_library_dynamic_linking, false, false, 10},
    {"stdlib", "test_multi_return_tuples_and_destructuring", test_multi_return_tuples_and_destructuring, false, false, 5},
    {"stdlib", "test_filesystem_streaming_and_paths", test_filesystem_streaming_and_paths, false, false, 5},
    {"stdlib", "test_json_dynamic_dom_and_serializer", test_json_dynamic_dom_and_serializer, false, false, 5},
    {"stdlib", "test_variable_declarations_unification", test_variable_declarations_unification, false, false, 5},
    {"stdlib", "test_recursive_package_resolution", test_recursive_package_resolution, false, false, 10},
    {"stdlib", "test_pre_build_codegen_hooks", test_pre_build_codegen_hooks, false, false, 10},
    {"stdlib", "test_unicode_utf8_runes_and_case", test_unicode_utf8_runes_and_case, false, false, 5},
    {"stdlib", "test_struct_tags_and_metadata", test_struct_tags_and_metadata, false, false, 5},
    {"stdlib", "test_agent_tooling_outline_and_json", test_agent_tooling_outline_and_json, false, true, 15},
    {"stdlib", "test_ide_configuration_setup", test_ide_configuration_setup, false, false, 10},
    {"stdlib", "test_cfire_eval", test_cfire_eval, false, false, 25},
    {"stdlib", "test_cfire_remote_vendor", test_cfire_remote_vendor, false, false, 20},
    {"stdlib", "test_in_process_sha256_file_hashing", run_test_in_process_sha256_file_hashing, false, false, 1},
    {"stdlib", "test_crypto_hmac_and_aes", test_crypto_hmac_and_aes, false, false, 5},
    {"stdlib", "test_sync_context", test_sync_context, false, false, 5},
    {"stdlib", "test_net_websocket", test_net_websocket, true, false, 10},
    {"stdlib", "test_net_http_and_router", test_net_http_and_router, true, false, 10},
    {"stdlib", "test_webui_rpc_events_and_linux_backend", test_webui_rpc_events_and_linux_backend, true, false, 15},
    {"stdlib", "test_fs_exclusive_create_stat_and_sync", test_fs_exclusive_create_stat_and_sync, false, false, 5},
    {"stdlib", "test_http_host_bind_and_client_timeout", test_http_host_bind_and_client_timeout, true, false, 15},
    {"stdlib", "test_time_local_offset_and_format", test_time_local_offset_and_format, false, false, 5},
    {"stdlib", "test_ipc_stream_security_and_concurrency", test_ipc_stream_security_and_concurrency, true, false, 10},
    {"stdlib", "test_security_and_fuzzing", test_security_and_fuzzing, false, false, 15},
};

[[nodiscard]] auto check_loopback_connect_allowed() -> bool {
#if defined(_WIN32)
    return true;
#else
    cfire::net::detail::ensure_winsock();
    int lfd = cfire::net::detail::open_tcp_socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) return errno != EPERM && errno != EACCES;
    cfire::net::detail::set_reuseaddr(lfd);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(0);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(lfd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || ::listen(lfd, 1) != 0) {
        int err = errno;
        cfire::net::detail::close_sock(lfd);
        return err != EPERM && err != EACCES;
    }
    socklen_t alen = sizeof(addr);
    if (::getsockname(lfd, reinterpret_cast<sockaddr*>(&addr), &alen) != 0) {
        cfire::net::detail::close_sock(lfd);
        return true;
    }
    int cfd = cfire::net::detail::open_tcp_socket(AF_INET, SOCK_STREAM, 0);
    if (cfd < 0) {
        int err = errno;
        cfire::net::detail::close_sock(lfd);
        return err != EPERM && err != EACCES;
    }
    cfire::net::detail::set_nonblocking(cfd, true);
    int rc = ::connect(cfd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    bool blocked = false;
    if (rc != 0) {
        if (errno == EPERM || errno == EACCES) {
            blocked = true;
        } else if (errno == EINPROGRESS) {
            pollfd pfd{.fd = cfd, .events = POLLOUT, .revents = 0};
            if (::poll(&pfd, 1, 200) > 0) {
                int so_err = 0;
                socklen_t slen = sizeof(so_err);
                if (::getsockopt(cfd, SOL_SOCKET, SO_ERROR, &so_err, &slen) == 0 &&
                    (so_err == EPERM || so_err == EACCES)) {
                    blocked = true;
                }
            }
        }
    }
    cfire::net::detail::close_sock(cfd);
    cfire::net::detail::close_sock(lfd);
    return !blocked;
#endif
}

[[nodiscard]] auto find_suite(std::string_view name) -> const SuiteEntry* {
    for (const auto& s : kSuites) {
        if (s.name == name) return &s;
    }
    return nullptr;
}

[[nodiscard]] auto matches_category(const SuiteEntry& s, std::string_view suite) -> bool {
    if (suite == "all") return true;
    if (suite == "fast") return s.category != "e2e" && !s.requires_loopback && s.weight < 80;
    return s.category == suite;
}

void prewarm_composite_pchs(const std::filesystem::path& rt_dir) {
    struct ProfileGuard {
        cfire::build::BuildProfile prev = cfire::build::current_build_profile();
        ~ProfileGuard() { cfire::build::set_build_profile(prev); }
    } guard;

    auto warm_set = [&](bool dev_debug, bool test_mode) {
        cfire::build::set_build_profile(dev_debug ? cfire::build::BuildProfile::DevDebug : cfire::build::BuildProfile::Release);
        auto plan = cfire::build::init_build_plan_flags("prewarm.cf", rt_dir, test_mode, "", "", "", "", false);
        if (!plan) return;
        const std::string opt_flags = dev_debug ? "-O0 -g" : "-O2";
        (void)cfire::build::ensure_composite_pch(rt_dir, "#pragma once\n#include \"cfire_prelude.hpp\"\n", opt_flags, plan->cflags);
        (void)cfire::build::ensure_composite_pch(
            rt_dir,
            "#pragma once\n#include \"cfire_prelude.hpp\"\n#include \"core/str.hpp\"\n#include \"core/builtin_methods.hpp\"\n",
            opt_flags,
            plan->cflags
        );
    };

    warm_set(false, false);
    warm_set(true, false);
    warm_set(true, true);
}

auto run_selected_parallel(
    const std::string& self_exe,
    const std::filesystem::path& rt_dir,
    const std::vector<const SuiteEntry*>& selected,
    std::size_t jobs,
    bool fail_fast,
    bool verbose
) -> int {
    prewarm_composite_pchs(rt_dir);

    std::vector<const SuiteEntry*> ordered = selected;
    std::ranges::stable_sort(ordered, [](const SuiteEntry* a, const SuiteEntry* b) {
        return a->weight > b->weight;
    });

    const std::size_t n = ordered.size();
    std::vector<std::uint8_t> claimed(n, 0);
    std::vector<std::string> outputs(n);
    std::vector<int> exit_codes(n, 0);
    std::vector<double> durations(n, 0.0);
    std::atomic<std::size_t> completed{0};
    std::atomic<bool> stop_early{false};
    bool shared_examples_active = false;
    std::mutex sched_mu;
    std::mutex print_mu;

    const std::filesystem::path cwd = std::filesystem::current_path();
    const std::size_t workers = std::max<std::size_t>(1, std::min(jobs, n));
    {
        std::vector<std::jthread> pool;
        pool.reserve(workers);
        for (std::size_t w = 0; w < workers; ++w) {
            pool.emplace_back([&] {
                while (!stop_early.load(std::memory_order_acquire)) {
                    std::size_t idx = n;
                    bool took_shared = false;
                    {
                        std::lock_guard<std::mutex> lk(sched_mu);
                        for (std::size_t i = 0; i < n; ++i) {
                            if (claimed[i]) continue;
                            if (ordered[i]->touches_shared_examples && shared_examples_active) continue;
                            claimed[i] = 1;
                            idx = i;
                            if (ordered[i]->touches_shared_examples) {
                                shared_examples_active = true;
                                took_shared = true;
                            }
                            break;
                        }
                        if (idx == n) {
                            bool any_left = false;
                            for (std::size_t i = 0; i < n; ++i) {
                                if (!claimed[i]) { any_left = true; break; }
                            }
                            if (!any_left) break;
                        }
                    }
                    if (idx == n) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(15));
                        continue;
                    }

                    const auto t0 = std::chrono::steady_clock::now();
                    auto r = run_process({self_exe, "--run-single", std::string(ordered[idx]->name)}, cwd, 600);
                    const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

                    if (took_shared) {
                        std::lock_guard<std::mutex> lk(sched_mu);
                        shared_examples_active = false;
                    }

                    durations[idx] = sec;
                    const bool ok = !r.spawn_failed && !r.timed_out && r.exit_code == 0;
                    exit_codes[idx] = ok ? 0 : (r.exit_code != 0 ? r.exit_code : 1);
                    outputs[idx] = r.out + r.err;

                    if (!ok && fail_fast) {
                        stop_early.store(true, std::memory_order_release);
                    }

                    const std::size_t done = ++completed;
                    {
                        std::lock_guard<std::mutex> plk(print_mu);
                        if (!ok) {
                            std::println(stderr, "[FAIL] ({}/{}) {} ({:.2f}s)", done, n, ordered[idx]->name, sec);
                            if (!outputs[idx].empty()) {
                                std::println(stderr, "{}", outputs[idx]);
                            }
                        } else if (verbose || sec >= 2.0) {
                            std::println("[PASS] ({}/{}) {} ({:.2f}s)", done, n, ordered[idx]->name, sec);
                        }
                    }
                }
            });
        }
    }

    int failed = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (!claimed[i]) continue;
        if (exit_codes[i] != 0) ++failed;
    }
    if (failed > 0) {
        std::println(stderr, "FAIL ({} suite(s) failed)", failed);
        return 1;
    }
    std::println("PASS (all C++23 cfire conformance tests succeeded)");
    return 0;
}

} // namespace

auto main(int argc, char** argv) -> int {
    test_ignore_sigpipe();
    auto rt_dir = std::filesystem::current_path() / "runtime";

    std::string suite = "all";
    bool suite_set = false;
    std::string filter = "*";
    std::string run_single;
    bool list_only = false;
    bool fail_fast = false;
    bool verbose = false;
    std::size_t jobs = default_jobs();

    conformance::Options conf_opt;
    conf_opt.cfire = std::filesystem::absolute(cfire_exe()).string();
    conf_opt.runtime_dir = rt_dir;

    for (int i = 1; i < argc; ++i) {
        std::string_view a = argv[i];
        auto value = [&](std::string_view flag) -> std::optional<std::string> {
            if (a.starts_with(flag) && a.size() > flag.size() && a[flag.size()] == '=') return std::string(a.substr(flag.size() + 1));
            if (a == flag && i + 1 < argc) return std::string(argv[++i]);
            return std::nullopt;
        };
        if (a == "--list") {
            list_only = true;
        } else if (a == "--fail-fast") {
            fail_fast = true;
        } else if (a == "-v" || a == "--verbose") {
            verbose = true;
        } else if (auto v = value("--run-single")) {
            run_single = *v;
        } else if (auto v = value("--filter")) {
            filter = *v;
        } else if (auto v = value("--jobs")) {
            jobs = static_cast<std::size_t>(std::max(1, std::atoi(v->c_str())));
        } else if (auto v = value("--timeout")) {
            conf_opt.timeout = std::max(1, std::atoi(v->c_str()));
        } else if (auto v = value("--cfire")) {
            conf_opt.cfire = std::filesystem::absolute(*v).string();
        } else if (auto v = value("--run-flags")) {
            for (auto& f : conformance::split_ws(*v)) conf_opt.run_flags.push_back(f);
        } else if (!a.starts_with("-") && !suite_set) {
            suite = std::string(a);
            suite_set = true;
        } else {
            std::println(stderr, "unknown option: {}", a);
            return 2;
        }
    }

    if (list_only) {
        for (const auto& s : kSuites) {
            if ((suite == "all" || matches_category(s, suite)) && conformance::glob_match(filter, s.name)) {
                std::println("{}\t{}", s.category, s.name);
            }
        }
        return 0;
    }

    if (!run_single.empty()) {
        const SuiteEntry* entry = find_suite(run_single);
        if (!entry) {
            std::println(stderr, "unknown test suite: {}", run_single);
            return 2;
        }
        if (entry->requires_loopback && !check_loopback_connect_allowed()) {
            std::println(stderr,
                "error: OS sandbox blocked loopback TCP connect(127.0.0.1) (EPERM). "
                "Run full gates outside the sandbox, or run 'make check-fast' / './.cfire_test_runner fast' for sandbox-safe compiler checks.");
            return 2;
        }
        entry->run(rt_dir);
        if (g_failures.load() > 0) {
            std::println(stderr, "FAIL ({} failures)", g_failures.load());
            return 1;
        }
        return 0;
    }

    if (suite == "conformance") {
        if (!check_loopback_connect_allowed()) {
            std::println(stderr,
                "error: OS sandbox blocked loopback TCP connect(127.0.0.1) (EPERM). "
                "Run full gates outside the sandbox, or run 'make check-fast' / './.cfire_test_runner fast' for sandbox-safe compiler checks.");
            return 2;
        }
        return conformance::run_corpus(conf_opt, std::filesystem::current_path() / "tests" / "conformance", filter, jobs, verbose);
    }
    if (suite == "guide") {
        return guide::run_guide(conf_opt.cfire, std::filesystem::current_path(), jobs);
    }
    if (suite == "fuzz") {
        test_security_and_fuzzing(rt_dir);
        if (g_failures.load() > 0) {
            std::println(stderr, "FAIL ({} failures)", g_failures.load());
            return 1;
        }
        std::println("PASS (all C++23 cfire conformance tests succeeded)");
        return 0;
    }
    if (suite == "bench") {
        test_performance_benchmarks(rt_dir);
        if (g_failures.load() > 0) {
            std::println(stderr, "FAIL ({} failures)", g_failures.load());
            return 1;
        }
        std::println("PASS (all C++23 cfire conformance tests succeeded)");
        return 0;
    }

    if (suite != "all" && suite != "unit" && suite != "e2e" && suite != "stdlib" && suite != "fast") {
        std::println(stderr, "unknown suite: {}", suite);
        return 2;
    }

    std::vector<const SuiteEntry*> selected;
    bool needs_loopback = false;
    for (const auto& s : kSuites) {
        if (matches_category(s, suite) && conformance::glob_match(filter, s.name)) {
            selected.push_back(&s);
            needs_loopback = needs_loopback || s.requires_loopback;
        }
    }
    if (selected.empty()) {
        std::println(stderr, "no suites match category '{}' and filter '{}'", suite, filter);
        return 2;
    }

    if (needs_loopback && !check_loopback_connect_allowed()) {
        std::println(stderr,
            "error: OS sandbox blocked loopback TCP connect(127.0.0.1) (EPERM). "
            "Run full gates outside the sandbox, or run 'make check-fast' / './.cfire_test_runner fast' for sandbox-safe compiler checks.");
        return 2;
    }

    if (jobs > 1 && selected.size() > 1) {
        std::string self_exe = (argv && argc > 0 && argv[0]) ? std::string(argv[0]) : "./.cfire_test_runner";
        if (self_exe.find('/') != std::string::npos || self_exe.find('\\') != std::string::npos) {
            self_exe = std::filesystem::absolute(self_exe).string();
        }
        return run_selected_parallel(self_exe, rt_dir, selected, jobs, fail_fast, verbose);
    }

    for (const SuiteEntry* s : selected) {
        s->run(rt_dir);
        if (fail_fast && g_failures.load() > 0) break;
    }

    if (g_failures.load() > 0) {
        std::println(stderr, "FAIL ({} failures)", g_failures.load());
        return 1;
    }
    std::println("PASS (all C++23 cfire conformance tests succeeded)");
    return 0;
}
