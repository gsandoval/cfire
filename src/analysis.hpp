#pragma once
// src/analysis.hpp — Unified Diagnostic Pipeline & Semantic Check for CLI, LSP, and MCP

#if defined(CFIRE_SPLIT_TUS) && !defined(CFIRE_ANALYSIS_IMPL)
#include "diagnostics.hpp"
#else
#include "buildsys.hpp"
#include "build/package_loader.hpp"
#include "build/syntax_check.hpp"
#include "diagnostics.hpp"
#include "codegen.hpp"
#include "parser.hpp"
#include "sema.hpp"
#endif

#ifdef CFIRE_SPLIT_TUS
#define CFIRE_ANALYSIS_INLINE
#else
#define CFIRE_ANALYSIS_INLINE inline
#endif
#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace cfire::frontend {
struct FileAST;
} // namespace cfire::frontend

namespace cfire::analysis {

using Diagnostic = cfire::diag::StructuredDiagnostic;
using FixIt = cfire::diag::FixItEdit;

struct CheckOptions {
    std::filesystem::path runtime_dir{};
    bool verify_cpp_backend = true;
    bool force_cpp_verify = false;
    bool prefer_dev_debug = true;
    bool resolve_sibling_packages = true;
    std::string target_triple{};
    std::string sysroot{};
    std::vector<std::string> extra_includes{};
    std::vector<std::string> extra_pkg_configs{};
    std::vector<std::string> extra_defines{};
    std::string web_profile{"auto"};
    std::string subsystem{"auto"};
    std::unordered_map<std::string, std::string> source_overlays{};
};

[[nodiscard]] inline auto is_ignored_walk_path(const std::filesystem::path& p) -> bool {
    std::string s = p.generic_string();
    return s.contains("/.cfire") || s.contains("/.tako") || s.contains("/.git") || s.contains("/dist") ||
           s.contains("/vendor") || s.contains("/node_modules") || s.contains("/ejected_cpp");
}

[[nodiscard]] CFIRE_ANALYSIS_INLINE auto needs_cpp_syntax_check(
    const cfire::frontend::FileAST& ast
) -> bool;

[[nodiscard]] CFIRE_ANALYSIS_INLINE auto check_source(
    std::string_view file_path,
    std::string_view content,
    const CheckOptions& opts = {}
) -> std::vector<Diagnostic>;

[[nodiscard]] CFIRE_ANALYSIS_INLINE auto check_path(
    const std::filesystem::path& target,
    const CheckOptions& opts = {}
) -> std::vector<Diagnostic>;

[[nodiscard]] CFIRE_ANALYSIS_INLINE auto check(
    std::string_view target_or_content,
    bool is_content = false,
    const CheckOptions& opts = {}
) -> std::vector<Diagnostic>;

#if !defined(CFIRE_SPLIT_TUS) || defined(CFIRE_ANALYSIS_IMPL)

namespace detail {

[[nodiscard]] inline auto stmts_contain_cpp_block(const std::vector<cfire::frontend::StmtPtr>& stmts) -> bool;

[[nodiscard]] inline auto expr_contains_cpp_block(const cfire::frontend::ExprPtr& e) -> bool {
    if (!e) return false;
    return std::visit([&](const auto& ex) -> bool {
        using T = std::decay_t<decltype(ex)>;
        if constexpr (std::is_same_v<T, cfire::frontend::LiteralExpr>) {
            for (const auto& snip : ex.interp_snippets) {
                if (expr_contains_cpp_block(snip.expr)) return true;
            }
            return false;
        } else if constexpr (std::is_same_v<T, cfire::frontend::UnaryExpr>) {
            return expr_contains_cpp_block(ex.operand);
        } else if constexpr (std::is_same_v<T, cfire::frontend::BinaryExpr>) {
            return expr_contains_cpp_block(ex.left) || expr_contains_cpp_block(ex.right);
        } else if constexpr (std::is_same_v<T, cfire::frontend::MemberExpr>) {
            return expr_contains_cpp_block(ex.object);
        } else if constexpr (std::is_same_v<T, cfire::frontend::IndexExpr>) {
            return expr_contains_cpp_block(ex.object) || expr_contains_cpp_block(ex.index);
        } else if constexpr (std::is_same_v<T, cfire::frontend::SliceExpr>) {
            return expr_contains_cpp_block(ex.object) || expr_contains_cpp_block(ex.start) || expr_contains_cpp_block(ex.end);
        } else if constexpr (std::is_same_v<T, cfire::frontend::StructLitExpr>) {
            for (const auto& f : ex.fields) {
                if (expr_contains_cpp_block(f.value)) return true;
            }
            return false;
        } else if constexpr (std::is_same_v<T, cfire::frontend::ArrayLitExpr> ||
                             std::is_same_v<T, cfire::frontend::TupleExpr>) {
            for (const auto& el : ex.elements) {
                if (expr_contains_cpp_block(el)) return true;
            }
            return false;
        } else if constexpr (std::is_same_v<T, cfire::frontend::ClosureExpr>) {
            for (const auto& sc : ex.slot_captures) {
                if (expr_contains_cpp_block(sc.second)) return true;
            }
            return stmts_contain_cpp_block(ex.body);
        } else if constexpr (std::is_same_v<T, cfire::frontend::CallExpr>) {
            if (expr_contains_cpp_block(ex.callee)) return true;
            for (const auto& a : ex.args) {
                if (expr_contains_cpp_block(a.value)) return true;
            }
            if (ex.block_arg) {
                for (const auto& sc : ex.block_arg->slot_captures) {
                    if (expr_contains_cpp_block(sc.second)) return true;
                }
                if (stmts_contain_cpp_block(ex.block_arg->body)) return true;
            }
            return false;
        } else if constexpr (std::is_same_v<T, cfire::frontend::TryExpr>) {
            return expr_contains_cpp_block(ex.inner);
        } else if constexpr (std::is_same_v<T, cfire::frontend::OrExpr>) {
            return expr_contains_cpp_block(ex.inner) ||
                   expr_contains_cpp_block(ex.fallback_val) ||
                   stmts_contain_cpp_block(ex.handler_body);
        } else if constexpr (std::is_same_v<T, cfire::frontend::MatchExpr>) {
            if (expr_contains_cpp_block(ex.target)) return true;
            for (const auto& arm : ex.arms) {
                if (expr_contains_cpp_block(arm.guard) || stmts_contain_cpp_block(arm.body)) return true;
            }
            return false;
        } else {
            return false;
        }
    }, e->node);
}

[[nodiscard]] inline auto stmts_contain_cpp_block(const std::vector<cfire::frontend::StmtPtr>& stmts) -> bool {
    for (const auto& s : stmts) {
        if (!s) continue;
        bool found = std::visit([&](const auto& st) -> bool {
            using T = std::decay_t<decltype(st)>;
            if constexpr (std::is_same_v<T, cfire::frontend::CppBlockStmt>) {
                return true;
            } else if constexpr (std::is_same_v<T, cfire::frontend::VarDeclStmt> ||
                                 std::is_same_v<T, cfire::frontend::DestructureVarDeclStmt>) {
                return expr_contains_cpp_block(st.init);
            } else if constexpr (std::is_same_v<T, cfire::frontend::AssignStmt>) {
                return expr_contains_cpp_block(st.target) || expr_contains_cpp_block(st.value);
            } else if constexpr (std::is_same_v<T, cfire::frontend::DestructureAssignStmt>) {
                for (const auto& t : st.targets) {
                    if (expr_contains_cpp_block(t)) return true;
                }
                return expr_contains_cpp_block(st.value);
            } else if constexpr (std::is_same_v<T, cfire::frontend::DeferStmt>) {
                return expr_contains_cpp_block(st.call) || stmts_contain_cpp_block(st.body);
            } else if constexpr (std::is_same_v<T, cfire::frontend::ReturnStmt>) {
                return expr_contains_cpp_block(st.value);
            } else if constexpr (std::is_same_v<T, cfire::frontend::IfStmt>) {
                return expr_contains_cpp_block(st.cond) ||
                       expr_contains_cpp_block(st.unwrap_init) ||
                       stmts_contain_cpp_block(st.then_body) ||
                       stmts_contain_cpp_block(st.else_body);
            } else if constexpr (std::is_same_v<T, cfire::frontend::ForStmt>) {
                return expr_contains_cpp_block(st.iter_expr) ||
                       expr_contains_cpp_block(st.range_end) ||
                       stmts_contain_cpp_block(st.body);
            } else if constexpr (std::is_same_v<T, cfire::frontend::MatchStmt>) {
                if (expr_contains_cpp_block(st.target)) return true;
                for (const auto& arm : st.arms) {
                    if (expr_contains_cpp_block(arm.guard) || stmts_contain_cpp_block(arm.body)) return true;
                }
                return false;
            } else if constexpr (std::is_same_v<T, cfire::frontend::AsyncScopeStmt>) {
                return expr_contains_cpp_block(st.workers) || stmts_contain_cpp_block(st.body);
            } else if constexpr (std::is_same_v<T, cfire::frontend::ExprStmt>) {
                return expr_contains_cpp_block(st.expr);
            } else {
                return false;
            }
        }, s->node);
        if (found) return true;
    }
    return false;
}

struct ScopedDevDebugGuard {
    bool active = false;

    explicit ScopedDevDebugGuard(bool enable) {
        if (!enable) return;
        std::lock_guard<std::mutex> lk(profile_mutex());
        if (depth() == 0) {
            saved_profile() = cfire::build::g_build_profile;
            saved_debug_mode() = cfire::build::g_debug_mode;
            cfire::build::set_build_profile(cfire::build::BuildProfile::DevDebug);
        }
        ++depth();
        active = true;
    }

    ~ScopedDevDebugGuard() {
        if (!active) return;
        std::lock_guard<std::mutex> lk(profile_mutex());
        if (--depth() == 0) {
            cfire::build::g_build_profile = saved_profile();
            cfire::build::g_debug_mode = saved_debug_mode();
        }
    }

    static auto profile_mutex() -> std::mutex& {
        static std::mutex m;
        return m;
    }
    static auto depth() -> int& {
        static int d = 0;
        return d;
    }
    static auto saved_profile() -> cfire::build::BuildProfile& {
        static cfire::build::BuildProfile p = cfire::build::BuildProfile::Release;
        return p;
    }
    static auto saved_debug_mode() -> bool& {
        static bool d = false;
        return d;
    }
};

} // namespace detail

[[nodiscard]] CFIRE_ANALYSIS_INLINE auto needs_cpp_syntax_check(
    const cfire::frontend::FileAST& ast
) -> bool {
    for (const auto& imp : ast.imports) {
        if (imp.is_cpp) return true;
    }
    for (const auto& st : ast.structs) {
        for (const auto& f : st.fields) {
            if (detail::expr_contains_cpp_block(f.default_val)) return true;
        }
    }
    for (const auto& gv : ast.global_vars) {
        if (detail::expr_contains_cpp_block(gv.value)) return true;
    }
    for (const auto& fn : ast.functions) {
        if (detail::stmts_contain_cpp_block(fn.body)) return true;
    }
    for (const auto& t : ast.tests) {
        for (const auto& bv : t.bench_variations) {
            for (const auto& v : bv.values) {
                if (detail::expr_contains_cpp_block(v)) return true;
            }
        }
        if (detail::stmts_contain_cpp_block(t.body)) return true;
    }
    return false;
}

[[nodiscard]] CFIRE_ANALYSIS_INLINE auto check_source(
    std::string_view file_path,
    std::string_view content,
    const CheckOptions& opts
) -> std::vector<Diagnostic> {
    detail::ScopedDevDebugGuard profile_guard(opts.prefer_dev_debug);
    std::vector<Diagnostic> diags;
    auto lines = cfire::diag::split_lines(content);

    // 1. Resilient in-memory parsing (never re-read `file_path` from disk)
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

    cfire::frontend::FileAST merged_ast = std::move(pres.ast);
    std::filesystem::path fpath(file_path);
    const bool is_test_f = fpath.filename().string().ends_with("_test.cf");
    auto rt_dir = opts.runtime_dir;
    if (rt_dir.empty()) {
        rt_dir = cfire::build::locate_toolchain_layout(nullptr).runtime_dir;
    }

    std::unordered_map<std::string, std::string> overlays = opts.source_overlays;
    if (!file_path.empty()) {
        std::string content_str(content);
        overlays[std::string(file_path)] = content_str;
        std::error_code cec;
        auto canon = std::filesystem::weakly_canonical(fpath, cec);
        if (!cec && !canon.empty()) {
            overlays[canon.string()] = content_str;
        }
    }

    cfire::build::BuildPlan local_plan;
    if (!rt_dir.empty()) {
        if (auto plan_res = cfire::build::init_build_plan_flags(
                fpath, rt_dir, is_test_f, opts.target_triple, opts.sysroot, opts.web_profile, opts.subsystem,
                false, opts.extra_pkg_configs, opts.extra_defines);
            plan_res) {
            local_plan = std::move(*plan_res);
        } else {
            local_plan.entry_file = fpath.string();
            local_plan.extra_pkg_configs = opts.extra_pkg_configs;
            local_plan.extra_defines = opts.extra_defines;
        }
    } else {
        local_plan.entry_file = fpath.string();
        local_plan.extra_pkg_configs = opts.extra_pkg_configs;
        local_plan.extra_defines = opts.extra_defines;
    }

    auto parent_dir = fpath.parent_path().empty() ? std::filesystem::path(".") : fpath.parent_path();
    std::error_code dir_ec;
    const bool has_real_dir = !file_path.empty() &&
        (!fpath.parent_path().empty() || std::filesystem::exists(fpath, dir_ec)) &&
        std::filesystem::is_directory(parent_dir, dir_ec);

    // 2. Local package / sibling & build.cf resolution (strictly using in-memory overlays)
    bool unresolved_local_package = false;
    if (opts.resolve_sibling_packages && has_real_dir) {
        if (auto bcr = cfire::build::discover_and_apply_build_cf(
                local_plan, fpath, parent_dir, is_test_f, /*run_pre_build=*/opts.source_overlays.empty());
            !bcr && bcr.error().contains("invalid C macro identifier")) {
            Diagnostic d;
            d.file = std::string(file_path);
            d.line = 1;
            d.col = 1;
            d.end_line = 1;
            d.end_col = 1;
            d.severity = "error";
            d.code = "cfire/INVALID-ATTRIBUTE";
            d.message = bcr.error();
            d.source = "cfire";
            diags.push_back(std::move(d));
        }
        auto resolved = cfire::build::resolve_local_cf_imports(
            parent_dir, merged_ast, local_plan, rt_dir, is_test_f, &overlays);
        // Surface missing local packages as a cfire diagnostic instead of letting clang report
        // "no member named '<pkg>' in namespace 'cfire'".
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

    // 3. Gate C++ interop & Clang verification cleanly so pure-.cf checks stay fast & subprocess-free
    const bool is_build_manifest = (fpath.filename() == "build.cf" || merged_ast.package_name == "build");
    const bool has_cpp_interop = !is_build_manifest && needs_cpp_syntax_check(merged_ast);
    const bool want_clang = !is_build_manifest && opts.verify_cpp_backend && (has_cpp_interop || opts.force_cpp_verify);

    std::unordered_map<std::string, std::vector<std::string>> pre_cpp_fn_params;
    std::unordered_map<std::string, std::string> pre_cpp_fn_returns;
    if (has_cpp_interop || want_clang) {
        if (has_real_dir) {
            (void)cfire::build::build_external_deps_for_plan(parent_dir, local_plan);
        }
        for (const auto& inc : opts.extra_includes) {
            if (inc.empty()) continue;
            if (std::find(local_plan.extra_includes.begin(), local_plan.extra_includes.end(), inc) == local_plan.extra_includes.end()) {
                local_plan.extra_includes.push_back(inc);
            }
            std::string iflag = "-I" + inc;
            if (std::find(local_plan.cflags.begin(), local_plan.cflags.end(), iflag) == local_plan.cflags.end()) {
                local_plan.cflags.push_back(std::move(iflag));
            }
        }
        (void)cfire::build::resolve_plan_pkg_configs(local_plan, rt_dir);
        if (auto dr = cfire::build::resolve_plan_defines(local_plan); !dr) {
            cfire::diag::Diagnostic d;
            d.file = std::string(file_path);
            d.line = 1;
            d.col = 1;
            d.end_line = 1;
            d.end_col = 1;
            d.severity = "error";
            d.code = "cfire/INVALID-ATTRIBUTE";
            d.message = dr.error();
            d.source = "cfire";
            diags.push_back(std::move(d));
        }

        if (has_cpp_interop) {
            std::vector<std::string> base_header_cflags = local_plan.cflags;
            if (has_real_dir) {
                std::string pflag = "-I" + parent_dir.string();
                if (std::find(base_header_cflags.begin(), base_header_cflags.end(), pflag) == base_header_cflags.end()) {
                    base_header_cflags.push_back(std::move(pflag));
                }
            }
            auto hdr_cache_dir = cfire::build::cache_subdir("headers");
            std::string cxx_bin = cfire::build::resolve_cxx_compiler(local_plan.target_triple);
            for (const auto& imp : merged_ast.imports) {
                if (!imp.is_cpp) continue;
                auto idx = bridge::inspect_header(imp.path, base_header_cflags, hdr_cache_dir, cxx_bin);
                if (idx) {
                    std::string qual = imp.alias.empty() ? "cpp" : imp.alias;
                    for (const auto& [sym_name, sym] : idx->symbols) {
                        if (sym.kind == "FunctionDecl") {
                            pre_cpp_fn_params[sym_name] = sym.param_types;
                            pre_cpp_fn_params[qual + "." + sym_name] = sym.param_types;
                            if (!sym.return_type.empty()) {
                                pre_cpp_fn_returns[sym_name] = sym.return_type;
                                pre_cpp_fn_returns[qual + "." + sym_name] = sym.return_type;
                            }
                        }
                    }
                    local_plan.cpp_headers[imp.path] = std::move(*idx);
                }
            }
        }
    }

    // 4. Single-pass resilient semantic analysis
    auto chk = cfire::frontend::analyze_resilient(std::move(merged_ast), std::move(pre_cpp_fn_params), std::move(pre_cpp_fn_returns));
    // Sema diagnostics are structured at detection; here they only get exact ranges and fix-its.
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

    // 5. Conditional single-pass C++23 backend syntax check over stdin (no redundant re-parse or disk writes)
    if (chk.errors.empty() && !unresolved_local_package && want_clang && !file_path.empty() && !rt_dir.empty()) {
        (void)cfire::build::populate_checked_build_plan(chk, local_plan, rt_dir, /*inspect_clang_ast=*/false);
        const std::string backend_code = local_plan.has_cpp_interop ? "cfire/TYPE-ERROR" : "cfire/INTERNAL-BACKEND-ERROR";
        std::string cpp_code = cfire::frontend::emit_cpp(chk, is_test_f);
        auto syntax_check = cfire::build::verify_cpp_syntax_only(fpath, cpp_code, local_plan, rt_dir, opts.extra_includes);
        if (!syntax_check) {
            auto clang_lines = cfire::diag::split_lines(syntax_check.error());
            bool found_diag = false;
            for (const auto& cl : clang_lines) {
                const bool is_err = cl.contains(": error:") || cl.contains(": fatal error:") || cl.starts_with("error: ");
                const bool is_warn = cl.contains(": warning:");
                if (is_err || is_warn) {
                    auto d = cfire::diag::parse_clang_diagnostic_line(
                        cl, file_path, is_err ? "error" : "warning", backend_code);
                    if (d.file == file_path && d.line == 1) {
                        for (const auto& imp : chk.ast.imports) {
                            if (imp.is_cpp && !imp.path.empty() && d.message.contains(imp.path)) {
                                d.line = imp.line;
                                d.end_line = imp.line;
                                break;
                            }
                        }
                        cfire::diag::resolve_diagnostic_range(d, lines);
                    }
                    diags.push_back(std::move(d));
                    found_diag = true;
                }
            }
            if (!found_diag && !syntax_check.error().empty()) {
                auto d = cfire::diag::parse_clang_diagnostic_line(
                    syntax_check.error(), file_path, "error", backend_code);
                diags.push_back(std::move(d));
            }
        }
    }

    return diags;
}

[[nodiscard]] CFIRE_ANALYSIS_INLINE auto check_path(
    const std::filesystem::path& target,
    const CheckOptions& opts
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
        if (is_ignored_walk_path(entry.path())) continue;
        if (std::find(check_files.begin(), check_files.end(), entry.path()) == check_files.end()) {
            check_files.push_back(entry.path());
        }
    }
    std::sort(check_files.begin(), check_files.end());

    std::unordered_set<std::string> seen_diags;
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
            std::string norm_file = fd.file.empty()
                ? std::string{}
                : std::filesystem::path(fd.file).lexically_normal().string();
            std::string dedup_key = std::format("{}:{}:{}:{}:{}",
                norm_file, fd.line, fd.col, fd.code, fd.message);
            if (!seen_diags.insert(std::move(dedup_key)).second) continue;
            if (!norm_file.empty()) fd.file = std::move(norm_file);
            diags.push_back(std::move(fd));
        }
    }

    return diags;
}

// Unified entry point: check file or directory, or inline content
[[nodiscard]] CFIRE_ANALYSIS_INLINE auto check(
    std::string_view target_or_content,
    bool is_content,
    const CheckOptions& opts
) -> std::vector<Diagnostic> {
    if (is_content) {
        return check_source("snippet.cf", target_or_content, opts);
    }
    return check_path(std::filesystem::path(target_or_content), opts);
}

#endif // !defined(CFIRE_SPLIT_TUS) || defined(CFIRE_ANALYSIS_IMPL)
#undef CFIRE_ANALYSIS_INLINE

} // namespace cfire::analysis
