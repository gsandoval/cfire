#pragma once
// tests/test_harness.hpp — Shared Conformance Test Harness & Declarations

#include "../src/buildsys.hpp"
#include "../src/docgen.hpp"
#include "../src/formatter.hpp"
#include "../src/lsp.hpp"
#include "../src/refactor.hpp"
#include "../runtime/cfire_core_stdlib.hpp"
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <print>
#include <sstream>
#include <string>
#include <vector>
#include <csignal>

inline std::atomic<int> g_failures{0};

inline void require(bool cond, std::string_view msg) {
    if (!cond) {
        std::println(stderr, "  [FAIL] {}", msg);
        ++g_failures;
    }
}

// RAII Hermetic Test Directory (thread-safe and PID-isolated)
struct ScopedTempDir {
    std::filesystem::path path;

    explicit ScopedTempDir(std::string_view prefix = "cfire_test") {
        static std::atomic<std::uint64_t> s_id{0};
        path = std::filesystem::temp_directory_path() / std::format("{}_{}_{}", prefix, ::getpid(), s_id++);
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
    r.output = cfire::bridge::run_command_capture(std::format("./cfire {}", args), r.exit_code);
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
    std::string cmd = std::format("clang++ -std=c++2b -O2 -I\"{}\" \"{}\" -o \"{}\"",
        rt_dir.string(), cpp_file.string(), bin_file.string());
    int rc = 0;
    std::string comp_out = cfire::bridge::run_command_capture(cmd, rc);
    if (rc != 0) {
        return RunResult{.exit_code = rc, .output = std::format("compile error: {}", comp_out)};
    }
    int run_rc = 0;
    std::string run_out = cfire::bridge::run_command_capture(bin_file.string(), run_rc);
    return RunResult{.exit_code = run_rc, .output = std::move(run_out)};
}

inline void require_sema_error(std::string_view src, std::string_view expected_diag, std::string_view filename = "test.cf") {
    auto ast = cfire::frontend::parse_source(filename, src);
    require(ast.has_value(), std::format("parse {}", filename));
    if (!ast) return;
    auto checked = cfire::frontend::analyze(std::move(*ast));
    require(!checked.has_value(), std::format("expected diagnostic {}", expected_diag));
    if (!checked) {
        require(checked.error().contains(expected_diag),
            std::format("expected diagnostic {}, got: {}", expected_diag, checked.error()));
    }
}

inline void require_sema_ok(std::string_view src, std::string_view filename = "test.cf") {
    auto ast = cfire::frontend::parse_source(filename, src);
    require(ast.has_value(), std::format("parse {}", filename));
    if (!ast) return;
    auto checked = cfire::frontend::analyze(std::move(*ast));
    require(checked.has_value(), std::format("sema ok for {}", filename));
}

// Frontend Suites
void test_formatter_lsp_and_docgen(const std::filesystem::path& rt_dir);
void test_cfire_refactor_rename(const std::filesystem::path& rt_dir);
void test_google_cpp_and_language_design(const std::filesystem::path& rt_dir);

// Sema Suites
void test_sema_intent_and_move_diagnostics();
void test_slice_pattern_matching(const std::filesystem::path& rt_dir);

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

// E2E Suites
void test_end_to_end_canonical_examples(const std::filesystem::path& rt_dir);
void test_raylib_vendor_codegen_and_eject(const std::filesystem::path& rt_dir);
void test_cfire_cross_compile(const std::filesystem::path& rt_dir);
