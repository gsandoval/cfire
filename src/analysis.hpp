#pragma once
// src/analysis.hpp — Unified Diagnostic Pipeline & Semantic Check for CLI, LSP, and MCP (CF-118)

#include "buildsys.hpp"
#include "diagnostics.hpp"
#include "codegen.hpp"
#include "parser.hpp"
#include "sema.hpp"
#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cfire::analysis {

using Diagnostic = cfire::diag::StructuredDiagnostic;
using FixIt = cfire::diag::FixItEdit;

struct CheckOptions {
    std::filesystem::path runtime_dir{};
    bool verify_cpp_backend = true;
    bool resolve_sibling_packages = true;
    std::string target_triple{};
    std::string sysroot{};
    std::vector<std::string> extra_includes{};
    std::string web_profile{"auto"};
    std::string subsystem{"auto"};
};

[[nodiscard]] inline auto is_ignored_walk_path(const std::filesystem::path& p) -> bool {
    std::string s = p.generic_string();
    return s.contains("/.cfire") || s.contains("/.tako") || s.contains("/.git") || s.contains("/dist") ||
           s.contains("/vendor") || s.contains("/node_modules") || s.contains("/ejected_cpp");
}

[[nodiscard]] inline auto check_source(
    std::string_view file_path,
    std::string_view content,
    const CheckOptions& opts = {}
) -> std::vector<Diagnostic> {
    std::vector<Diagnostic> diags;
    auto lines = cfire::diag::split_lines(content);

    // 1. Resilient parsing
    auto pres = cfire::frontend::parse_source_resilient(std::string(file_path), content);
    for (const auto& err : pres.source_errors) {
        auto d = cfire::diag::from_source_error(err);
        if (d.file.empty()) d.file = std::string(file_path);
        cfire::diag::resolve_diagnostic_range(d, lines);
        cfire::diag::synthesize_fixits_for_diagnostic(d, &pres.ast, lines);
        diags.push_back(std::move(d));
    }

    if (!pres.ok() && pres.ast.functions.empty() && pres.ast.structs.empty() && pres.ast.enums.empty()) {
        return diags;
    }

    // 2. Local package / sibling resolution if requested
    cfire::frontend::FileAST merged_ast = std::move(pres.ast);
    std::filesystem::path fpath(file_path);
    bool unresolved_local_package = false;
    if (opts.resolve_sibling_packages && !file_path.empty()) {
        std::error_code ec;
        auto parent_dir = fpath.parent_path();
        if (parent_dir.empty()) parent_dir = ".";
        if (std::filesystem::is_directory(parent_dir, ec)) {
            cfire::build::BuildPlan local_plan;
            local_plan.entry_file = fpath.string();
            bool is_test_f = fpath.filename().string().ends_with("_test.cf");
            auto resolved = cfire::build::resolve_local_cf_imports(parent_dir, merged_ast, local_plan, opts.runtime_dir, is_test_f);
            // Surface missing local packages as a cfire diagnostic instead of letting clang report
            // "no member named '<pkg>' in namespace 'cfire'" (CF-140).
            constexpr std::string_view kNotFound = "local package import not found: ";
            if (!resolved && resolved.error().starts_with(kNotFound)) {
                std::string missing = resolved.error().substr(kNotFound.size());
                int imp_line = 1;
                for (const auto& imp : merged_ast.imports) {
                    if (imp.path == missing) { imp_line = imp.line; break; }
                }
                Diagnostic d;
                d.file = file_path.empty() ? std::string("<input>") : std::string(file_path);
                d.line = imp_line;
                d.col = 1;
                d.end_line = imp_line;
                d.end_col = 1;
                d.severity = "error";
                d.code = "cfire/UNRESOLVED-PACKAGE";
                d.message = std::format("local package '{}' not found relative to '{}'",
                    missing, parent_dir.string());
                d.source = "cfire";
                diags.push_back(std::move(d));
                unresolved_local_package = true;
            }
        }
    }

    // Inspect C++ headers before Sema so Sema knows declared C/C++ symbols
    std::unordered_map<std::string, std::vector<std::string>> pre_cpp_fn_params;
    for (const auto& imp : merged_ast.imports) {
        if (imp.is_cpp) {
            std::filesystem::path cache_dir = opts.runtime_dir.empty() ? ".cfire_cache" : (opts.runtime_dir / ".cfire_cache");
            std::vector<std::string> cflags;
            auto idx = bridge::inspect_header(imp.path, cflags, cache_dir, "clang++");
            if (idx) {
                for (const auto& [sym_name, sym] : idx->symbols) {
                    if (sym.kind == "FunctionDecl") {
                        pre_cpp_fn_params[sym_name] = sym.param_types;
                    }
                }
            }
        }
    }

    // 3. Resilient semantic analysis
    auto chk = cfire::frontend::analyze_resilient(std::move(merged_ast), std::move(pre_cpp_fn_params));
    // Q24 (CF-149): Sema diagnostics are structured at detection; here they only get exact ranges and fix-its.
    // Diagnostics from sibling package files keep their own file; ranges are resolved against this file only.
    for (auto d : chk.diagnostics) {
        if (d.file.empty()) d.file = std::string(file_path);
        if (d.file == file_path) {
            cfire::diag::resolve_diagnostic_range(d, lines);
        } else if (d.col < 1) {
            d.col = 1;
            d.end_col = 1;
        }
        cfire::diag::synthesize_fixits_for_diagnostic(d, &chk.ast, lines);
        diags.push_back(std::move(d));
    }

    // 4. C++23 backend syntax check (if sema succeeded and C++ backend verification is enabled)
    if (chk.errors.empty() && !unresolved_local_package && opts.verify_cpp_backend && !file_path.empty()) {
        auto rt_dir = opts.runtime_dir;
        if (rt_dir.empty()) {
            rt_dir = cfire::build::locate_toolchain_layout(nullptr).runtime_dir;
        }
        if (!rt_dir.empty()) {
            bool is_test_f = fpath.filename().string().ends_with("_test.cf");
            std::string cpp_code;
            cfire::build::BuildPlan plan;
            // CF-176: use the same plan as `cfire build` (build.cf deps, native library and pkg-config include
            // paths) so headers that `run` finds are found by `check` too. Dependency builds are cached.
            bool have_full_plan = false;
            if (!file_path.empty() && opts.target_triple.empty()) {
                if (auto full = cfire::build::analyze_and_emit(fpath, rt_dir, false, is_test_f); full) {
                    cpp_code = std::move(full->first);
                    plan = std::move(full->second);
                    have_full_plan = true;
                }
            }
            if (!have_full_plan) {
            cpp_code = cfire::frontend::emit_cpp(chk, is_test_f);
            plan.entry_file = fpath.string();
            plan.cflags = {"-std=c++2b", "-Wno-c++11-narrowing", "-I" + rt_dir.string()};
#if defined(__linux__)
            plan.cflags.push_back("-Wno-builtin-macro-redefined");
            plan.cflags.push_back("-D__cpp_concepts=202002L");
            plan.cflags.push_back("-pthread");
#endif
            if (is_test_f) {
                plan.cflags.push_back("-DCFIRE_TEST_MODE=1");
                plan.cflags.push_back("-fsanitize=signed-integer-overflow,shift,integer-divide-by-zero");
                plan.cflags.push_back("-fsanitize-trap=all");
            } else {
                plan.cflags.push_back("-fwrapv");
            }
            std::error_code ec;
            if (std::filesystem::is_directory(rt_dir / "vendor", ec)) {
                plan.cflags.push_back("-I" + (rt_dir / "vendor").string());
            }
            }
            auto syntax_check = cfire::build::verify_cpp_syntax_only(fpath, cpp_code, plan, rt_dir, opts.extra_includes);
            if (!syntax_check) {
                auto clang_lines = cfire::diag::split_lines(syntax_check.error());
                bool found_diag = false;
                for (const auto& cl : clang_lines) {
                    if (cl.contains(": error:") || cl.contains(": warning:")) {
                        auto d = cfire::diag::parse_clang_diagnostic_line(cl, file_path, cl.contains(": error:") ? "error" : "warning", "cfire/TYPE-ERROR");
                        diags.push_back(std::move(d));
                        found_diag = true;
                    }
                }
                if (!found_diag && !syntax_check.error().empty()) {
                    auto d = cfire::diag::parse_clang_diagnostic_line(syntax_check.error(), file_path, "error", "cfire/TYPE-ERROR");
                    diags.push_back(std::move(d));
                }
            }
        }
    }

    return diags;
}

[[nodiscard]] inline auto check_path(
    const std::filesystem::path& target,
    const CheckOptions& opts = {}
) -> std::vector<Diagnostic> {
    std::vector<Diagnostic> diags;
    std::error_code ec;

    if (!std::filesystem::exists(target, ec)) {
        Diagnostic d;
        d.file = target.string();
        d.line = 1;
        d.col = 1;
        d.end_line = 1;
        d.end_col = 1;
        d.severity = "error";
        d.code = "cfire/FILE-NOT-FOUND";
        d.message = std::format("path '{}' not found", target.string());
        d.source = "cfire";
        diags.push_back(std::move(d));
        return diags;
    }

    if (!std::filesystem::is_directory(target, ec)) {
        auto content = cfire::build::read_file_text(target);
        if (!content) {
            Diagnostic d;
            d.file = target.string();
            d.line = 1;
            d.col = 1;
            d.end_line = 1;
            d.end_col = 1;
            d.severity = "error";
            d.code = "cfire/FILE-NOT-FOUND";
            d.message = content.error();
            d.source = "cfire";
            diags.push_back(std::move(d));
            return diags;
        }
        return check_source(target.string(), *content, opts);
    }

    // Directory mode: find all .cf targets in directory
    std::vector<std::filesystem::path> check_files;
    auto bcf = target / "build.cf";
    if (std::filesystem::exists(bcf, ec)) {
        if (auto parsed = cfire::build::parse_build_cf(bcf)) {
            for (const auto& exe : parsed->executables) {
                std::filesystem::path ep = exe.entry;
                if (ep.is_relative()) ep = target / ep;
                if (std::filesystem::exists(ep, ec)) {
                    check_files.push_back(ep);
                }
            }
        }
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(target, ec)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".cf") continue;
        if (entry.path().filename() == "build.cf") continue;
        if (is_ignored_walk_path(entry.path())) continue;
        if (std::find(check_files.begin(), check_files.end(), entry.path()) == check_files.end()) {
            check_files.push_back(entry.path());
        }
    }
    std::sort(check_files.begin(), check_files.end());

    for (const auto& f : check_files) {
        auto content = cfire::build::read_file_text(f);
        if (!content) {
            Diagnostic d;
            d.file = f.string();
            d.line = 1;
            d.col = 1;
            d.end_line = 1;
            d.end_col = 1;
            d.severity = "error";
            d.code = "cfire/FILE-NOT-FOUND";
            d.message = content.error();
            d.source = "cfire";
            diags.push_back(std::move(d));
            continue;
        }
        auto file_diags = check_source(f.string(), *content, opts);
        for (auto& fd : file_diags) {
            diags.push_back(std::move(fd));
        }
    }

    return diags;
}

// Unified entry point: check file or directory, or inline content
[[nodiscard]] inline auto check(
    std::string_view target_or_content,
    bool is_content = false,
    const CheckOptions& opts = {}
) -> std::vector<Diagnostic> {
    if (is_content) {
        return check_source("snippet.cf", target_or_content, opts);
    }
    return check_path(std::filesystem::path(target_or_content), opts);
}

} // namespace cfire::analysis
