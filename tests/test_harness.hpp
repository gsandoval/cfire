#pragma once
// tests/test_harness.hpp — Shared Conformance Test Harness & Declarations
#ifndef CFIRE_TEST_MODE
#define CFIRE_TEST_MODE 1
#endif

#include "../src/analysis.hpp"
#include "../src/buildsys.hpp"
#include "../src/docgen.hpp"
#include "../src/formatter.hpp"
#include "../src/lsp.hpp"
#include "../src/refactor.hpp"
#include "../runtime/cfire_core_stdlib.hpp"
#include <atomic>
#include <cstdlib>
#include <filesystem>
#if __has_include(<print>)
#include <print>
#endif
#include <sstream>
#include <string>
#include <vector>
#include <csignal>

inline std::atomic<int> g_failures{0};

// CF-184: the few POSIX calls the suites make, with their Windows equivalents, so the runner builds everywhere.
inline void test_set_env(const char* name, const char* value) {
#if defined(_WIN32)
    (void)::_putenv_s(name, value);
#else
    (void)::setenv(name, value, 1);
#endif
}
// The cfire binary built in the repo root, as a command the platform shell runs (cmd.exe has no `./`).
[[nodiscard]] inline auto cfire_exe() -> std::string {
#if defined(_WIN32)
    return ".\\cfire.exe";
#else
    return "./cfire";
#endif
}
inline void test_ignore_sigpipe() {
#if !defined(_WIN32)
    ::signal(SIGPIPE, SIG_IGN);
#endif
}
namespace test_sock {
// A connected stream-socket pair (AF_UNIX on POSIX, loopback TCP on Windows), as plain descriptors.
[[nodiscard]] inline auto pair(int sv[2]) -> bool { return cfire::net::detail::create_loopback_pair(sv); }
inline void send_bytes(int fd, const void* data, std::size_t len) {
#if defined(_WIN32)
    (void)::send(static_cast<SOCKET>(fd), static_cast<const char*>(data), static_cast<int>(len), 0);
#else
    (void)::send(fd, data, len, MSG_NOSIGNAL);
#endif
}
// Reads and discards whatever is already buffered, without blocking.
inline void drain(int fd) {
    char buf[64];
#if defined(_WIN32)
    u_long nonblocking = 1;
    ::ioctlsocket(static_cast<SOCKET>(fd), FIONBIO, &nonblocking);
    while (::recv(static_cast<SOCKET>(fd), buf, sizeof(buf), 0) > 0) {}
    nonblocking = 0;
    ::ioctlsocket(static_cast<SOCKET>(fd), FIONBIO, &nonblocking);
#else
    while (::recv(fd, buf, sizeof(buf), MSG_DONTWAIT) > 0) {}
#endif
}
inline void close(int fd) { cfire::net::detail::close_sock(fd); }
} // namespace test_sock

inline void require(bool cond, std::string_view msg) {
    if (!cond) {
        std::println(stderr, "  [FAIL] {}", msg);
        ++g_failures;
    }
}

// Structured parse errors (Q24) used as a failure message.
template <typename E>
    requires std::is_same_v<std::remove_cvref_t<E>, cfire::frontend::SourceError>
inline void require(bool cond, const E& msg) {
    require(cond, std::string_view(msg.text()));
}

// RAII Hermetic Test Directory (thread-safe, mkdtemp-backed, and PID-isolated)
struct ScopedTempDir {
    std::filesystem::path path;

    explicit ScopedTempDir(std::string_view prefix = "cfire_test") {
        static std::atomic<std::uint64_t> s_id{0};
        auto base = std::filesystem::temp_directory_path() / std::format("{}_{}_{}_XXXXXX", prefix, ::getpid(), s_id++);
        std::string tmpl = base.string();
#if !defined(_WIN32)
        char* res = ::mkdtemp(tmpl.data());
        if (res != nullptr) {
            path = std::filesystem::path(res);
            return;
        }
#endif
        path = tmpl;
        std::error_code ec;
        std::filesystem::create_directories(path, ec);
    }

    ~ScopedTempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }

    ScopedTempDir(const ScopedTempDir&) = delete;
    ScopedTempDir& operator=(const ScopedTempDir&) = delete;
    ScopedTempDir(ScopedTempDir&&) = default;
    ScopedTempDir& operator=(ScopedTempDir&&) = default;

    auto file(std::string_view filename, std::string_view content) const -> std::filesystem::path {
        auto p = path / filename;
        std::error_code ec;
        if (p.has_parent_path()) {
            std::filesystem::create_directories(p.parent_path(), ec);
        }
        (void)cfire::fs::write_text(p, content);
        return p;
    }

    auto subpath(std::string_view sub) const -> std::filesystem::path {
        auto p = path / sub;
        std::error_code ec;
        std::filesystem::create_directories(p, ec);
        return p;
    }
};

// Fluent Subprocess Result Assertion Wrapper
struct RunResult {
    int exit_code = 0;
    std::string output{};

    [[nodiscard]] auto ok() const noexcept -> bool { return exit_code == 0; }

    auto assert_ok(std::string_view label = "") const -> const RunResult& {
        require(exit_code == 0, label.empty() ? std::format("command exit 0, got {}: {}", exit_code, output)
                                              : std::format("{}: exit 0, got {}: {}", label, exit_code, output));
        return *this;
    }

    auto assert_fail(std::string_view label = "") const -> const RunResult& {
        require(exit_code != 0, label.empty() ? "expected command failure (non-zero exit code)"
                                              : std::format("{}: expected failure (non-zero exit code)", label));
        return *this;
    }

    auto assert_contains(std::string_view needle, std::string_view label = "") const -> const RunResult& {
        require(output.contains(needle),
            label.empty() ? std::format("output contains '{}', got: {}", needle, output)
                          : std::format("{}: output contains '{}', got: {}", label, needle, output));
        return *this;
    }
};

struct ExampleCase {
    std::string name;
    std::string cf_path;
    std::string args;
    std::vector<std::string> expected_stdout;
    std::vector<std::string> expected_cpp;
};

inline auto run_cfire(std::string_view args) -> RunResult {
    RunResult r;
    r.output = cfire::bridge::run_command_capture(std::format("{} {}", cfire_exe(), args), r.exit_code);
    return r;
}

inline auto run_cfire_source(std::string_view source, std::string_view prefix = "cfire_run") -> RunResult {
    ScopedTempDir dir(prefix);
    auto file = dir.file("main.cf", source);
    return run_cfire(std::format("run \"{}\"", file.string()));
}

inline auto compile_and_run_cpp(std::string_view cpp_source, const std::filesystem::path& rt_dir, std::string_view test_name = "test") -> RunResult {
    ScopedTempDir dir(test_name);
    auto cpp_file = dir.file("test.cpp", cpp_source);
    auto bin_file = dir.path / "test_bin";
    std::string linux_flags;
#if defined(__linux__)
    linux_flags = " -Wno-builtin-macro-redefined -D__cpp_concepts=202002L";
#endif
    std::string cmd = std::format("clang++ -std=c++2b{} -O2 -I\"{}\" \"{}\" -o \"{}\"",
        linux_flags, rt_dir.string(), cpp_file.string(), bin_file.string());
    int rc = 0;
    std::string comp_out = cfire::bridge::run_command_capture(cmd, rc);
    if (rc != 0) {
        return RunResult{.exit_code = rc, .output = std::format("compile error: {}", comp_out)};
    }
    int run_rc = 0;
    std::string run_out = cfire::bridge::run_command_capture(bin_file.string(), run_rc);
    return RunResult{.exit_code = run_rc, .output = std::move(run_out)};
}

inline void require_sema_error(
    std::string_view src,
    std::string_view expected_diag,
    std::string_view filename = "test.cf",
    int expected_line = -1,
    int expected_col = -1,
    int expected_diag_count = -1
) {
    auto ast = cfire::frontend::parse_source(filename, src);
    require(ast.has_value(), std::format("parse {}", filename));
    if (!ast) return;
    std::unordered_map<std::string, std::vector<std::string>> pre_cpp_fn_params;
    for (const auto& imp : ast->imports) {
        if (imp.is_cpp) {
            auto cache_dir = cfire::detail::user_cache_dir("test-header-cache");  // not /tmp/.cfire (CF-153, CF-178)
            std::vector<std::string> cflags;
            auto idx = cfire::bridge::inspect_header(imp.path, cflags, cache_dir, "clang++");
            if (idx) {
                for (const auto& [sym_name, sym] : idx->symbols) {
                    if (sym.kind == "FunctionDecl") {
                        pre_cpp_fn_params[sym_name] = sym.param_types;
                    }
                }
            }
        }
    }
    auto checked = cfire::frontend::analyze(std::move(*ast), std::move(pre_cpp_fn_params));
    require(!checked.has_value(), std::format("expected diagnostic {}", expected_diag));
    if (!checked) {
        require(checked.error().contains(expected_diag),
            std::format("expected diagnostic {}, got: {}", expected_diag, checked.error()));
        if (expected_line > 0) {
            std::string line_tag = std::format(":{}:", expected_line);
            require(checked.error().contains(line_tag),
                std::format("expected line {} in diagnostic, got: {}", expected_line, checked.error()));
        }
        if (expected_col > 0) {
            std::string col_tag = std::format(":{}:", expected_col);
            require(checked.error().contains(col_tag),
                std::format("expected col {} in diagnostic, got: {}", expected_col, checked.error()));
        }
        if (expected_diag_count > 0) {
            std::size_t count = 0;
            std::size_t pos = 0;
            while ((pos = checked.error().find(filename, pos)) != std::string::npos) {
                ++count;
                pos += filename.size();
            }
            if (count > 0) {
                require(static_cast<int>(count) >= expected_diag_count,
                    std::format("expected at least {} diagnostics, got {}", expected_diag_count, count));
            }
        }
    }
}

inline void require_sema_ok(std::string_view src, std::string_view filename = "test.cf") {
    auto ast = cfire::frontend::parse_source(filename, src);
    require(ast.has_value(), std::format("parse {}", filename));
    if (!ast) return;
    std::unordered_map<std::string, std::vector<std::string>> pre_cpp_fn_params;
    for (const auto& imp : ast->imports) {
        if (imp.is_cpp) {
            auto cache_dir = cfire::detail::user_cache_dir("test-header-cache");  // not /tmp/.cfire (CF-153, CF-178)
            std::vector<std::string> cflags;
            auto idx = cfire::bridge::inspect_header(imp.path, cflags, cache_dir, "clang++");
            if (idx) {
                for (const auto& [sym_name, sym] : idx->symbols) {
                    if (sym.kind == "FunctionDecl") {
                        pre_cpp_fn_params[sym_name] = sym.param_types;
                    }
                }
            }
        }
    }
    auto checked = cfire::frontend::analyze(std::move(*ast), std::move(pre_cpp_fn_params));
    require(checked.has_value(), std::format("sema ok for {}{}", filename, checked.has_value() ? "" : ": " + checked.error()));
}

// Frontend Suites
void test_formatter_lsp_and_docgen(const std::filesystem::path& rt_dir);
void test_golden_codegen_snapshots(const std::filesystem::path& repo_root);
void test_cfire_refactor_rename(const std::filesystem::path& rt_dir);
void test_phase4_tooling_depth(const std::filesystem::path& rt_dir);
void test_google_cpp_and_language_design(const std::filesystem::path& rt_dir);
void test_section2_language_ergonomics(const std::filesystem::path& rt_dir);

// Sema Suites
void test_sema_intent_and_move_diagnostics();
void test_diagnostic_golden_ranges();
void test_slice_pattern_matching(const std::filesystem::path& rt_dir);
void test_hard_deprecation_enforcement(const std::filesystem::path& rt_dir);
void test_duplicate_declaration_pass(const std::filesystem::path& rt_dir);
void test_catalog_gated_import_validation(const std::filesystem::path& rt_dir);
void test_plain_enum_usability_cluster(const std::filesystem::path& rt_dir);
void test_uniform_generic_type_application(const std::filesystem::path& rt_dir);
void test_cpp_backend_error_leak_prevention(const std::filesystem::path& rt_dir);
void test_scoped_name_resolution_cpp_ffi(const std::filesystem::path& rt_dir);
void test_match_exhaustiveness_and_reachability(const std::filesystem::path& rt_dir);
void test_or_chaining_and_block_disambiguation(const std::filesystem::path& rt_dir);
void test_lexer_exponents_keywords_and_interpolation(const std::filesystem::path& rt_dir);
void test_content_hashed_pch_and_cli_flag_validation(const std::filesystem::path& rt_dir);
void test_multiline_lambda_source_mapping_cf114(const std::filesystem::path& rt_dir);
void test_interned_typeid_lattice_and_diagnostics_cf116(const std::filesystem::path& rt_dir);
void test_unified_stdlib_metadata_table_cf117(const std::filesystem::path& rt_dir);
void test_unified_diagnostics_pipeline_cf118(const std::filesystem::path& rt_dir);
void test_phase2_concurrency_match_and_flow_control(const std::filesystem::path& rt_dir);
void test_phase3_advanced_language_and_safety(const std::filesystem::path& rt_dir);
void test_negative_parser_corpus_and_fuzz_runtime(const std::filesystem::path& rt_dir);
void test_cf140_crash_proof_tools(const std::filesystem::path& rt_dir);
void test_cf177_context_lifetime(const std::filesystem::path& rt_dir);
void test_cf153_secure_temp_storage(const std::filesystem::path& rt_dir);
void test_cf151_no_inband_tls_controls(const std::filesystem::path& rt_dir);
void test_w4_migration_rewrites(const std::filesystem::path& rt_dir);
void test_w4_builtin_runtime_semantics(const std::filesystem::path& rt_dir);
void test_cf180_dual_stack_listener(const std::filesystem::path& rt_dir);
void test_cf156_formatter_corpus(const std::filesystem::path& rt_dir);
void test_cf155_rename_scopes_and_diff(const std::filesystem::path& rt_dir);
void test_cf158_doc_integrity(const std::filesystem::path& rt_dir);
void test_cf183_cli_and_tooling(const std::filesystem::path& rt_dir);
void test_cf188_stdlib_type_surface(const std::filesystem::path& rt_dir);
void test_cf192_keepalive_and_websocket_sessions(const std::filesystem::path& rt_dir);
void test_cf150_http_server_hardening(const std::filesystem::path& rt_dir);
void test_cf152_verified_dependency_cache(const std::filesystem::path& rt_dir);
void test_cf160_mcp_workspace_confinement(const std::filesystem::path& rt_dir);
void test_cf168_secure_runtime_defaults(const std::filesystem::path& rt_dir);

// Granular Stdlib Intent Suites
void test_crypto_sha256_and_csprng(const std::filesystem::path& rt_dir);
void test_composable_expression_try(const std::filesystem::path& rt_dir);
void test_buildsys_incremental_caching(const std::filesystem::path& rt_dir);
void test_error_handling_divergence(const std::filesystem::path& rt_dir);
void test_subprocess_spawning_and_capture(const std::filesystem::path& rt_dir);
void test_cpp_ast_ingestion_and_overloads(const std::filesystem::path& rt_dir);
void test_parameter_intent_validation(const std::filesystem::path& rt_dir);
void test_cpp_escape_hatch_execution(const std::filesystem::path& rt_dir);
void test_http_client_server_and_urls(const std::filesystem::path& rt_dir);
void test_monadic_type_orthogonality(const std::filesystem::path& rt_dir);
void test_structured_concurrency_lifecycle(const std::filesystem::path& rt_dir);
void test_explicit_borrow_references(const std::filesystem::path& rt_dir);
void test_cfg_move_analysis(const std::filesystem::path& rt_dir);
void test_data_encodings_base64_hex_csv(const std::filesystem::path& rt_dir);
void test_relocatable_cmake_ejection(const std::filesystem::path& rt_dir);
void test_variant_pattern_matching(const std::filesystem::path& rt_dir);
void test_mutation_exclusivity_path_alias(const std::filesystem::path& rt_dir);
void test_preprocessor_macro_injection(const std::filesystem::path& rt_dir);
void test_regex_matching_and_captures(const std::filesystem::path& rt_dir);
void test_collections_and_in_place_sort(const std::filesystem::path& rt_dir);
void test_generic_call_disambiguation(const std::filesystem::path& rt_dir);
void test_time_durations_and_calendar(const std::filesystem::path& rt_dir);
void test_structured_logging_json_and_text(const std::filesystem::path& rt_dir);
void test_shared_library_dynamic_linking(const std::filesystem::path& rt_dir);
void test_multi_return_tuples_and_destructuring(const std::filesystem::path& rt_dir);
void test_filesystem_streaming_and_paths(const std::filesystem::path& rt_dir);
void test_json_dynamic_dom_and_serializer(const std::filesystem::path& rt_dir);
void test_variable_declarations_unification(const std::filesystem::path& rt_dir);
void test_recursive_package_resolution(const std::filesystem::path& rt_dir);
void test_pre_build_codegen_hooks(const std::filesystem::path& rt_dir);
void test_unicode_utf8_runes_and_case(const std::filesystem::path& rt_dir);
void test_struct_tags_and_metadata(const std::filesystem::path& rt_dir);
void test_agent_tooling_outline_and_json(const std::filesystem::path& rt_dir);
void test_ide_configuration_setup(const std::filesystem::path& rt_dir);
void test_cfire_eval(const std::filesystem::path& rt_dir);
void test_cfire_remote_vendor(const std::filesystem::path& rt_dir);
void test_in_process_sha256_file_hashing();
void test_crypto_hmac_and_aes(const std::filesystem::path& rt_dir);
void test_sync_context(const std::filesystem::path& rt_dir);
void test_net_websocket(const std::filesystem::path& rt_dir);
void test_net_http_and_router(const std::filesystem::path& rt_dir);
void test_section3_agent_tooling(const std::filesystem::path& rt_dir);
void test_webui_rpc_events_and_linux_backend(const std::filesystem::path& rt_dir);
void test_security_and_fuzzing(const std::filesystem::path& rt_dir);
void test_performance_benchmarks(const std::filesystem::path& rt_dir);

// E2E Suites
void test_end_to_end_canonical_examples(const std::filesystem::path& rt_dir);
void test_cinterop_codegen_and_eject(const std::filesystem::path& rt_dir);
void test_cfire_cross_compile(const std::filesystem::path& rt_dir);
void test_wasm_and_gui_subsystem(const std::filesystem::path& rt_dir);
