#pragma once
// src/build/package_loader.hpp — Multi-Package `.cf` Import Resolution & iOS Target Triples

#include "deps.hpp"
#include <deque>
#include <unordered_map>

#ifndef CFIRE_INFRA_INLINE
#ifdef CFIRE_SPLIT_TUS
#define CFIRE_INFRA_INLINE
#else
#define CFIRE_INFRA_INLINE inline
#endif
#endif

namespace cfire::build {

[[nodiscard]] CFIRE_INFRA_INLINE auto ast_has_top_level_main(const frontend::FileAST& ast) -> bool;

[[nodiscard]] CFIRE_INFRA_INLINE auto is_wasm_target(std::string_view target) noexcept -> bool;

[[nodiscard]] CFIRE_INFRA_INLINE auto is_windows_target(std::string_view target) noexcept -> bool;

[[nodiscard]] CFIRE_INFRA_INLINE auto is_platform_file_enabled(
    const std::filesystem::path& p,
    const BuildPlan& plan
) -> bool;

[[nodiscard]] CFIRE_INFRA_INLINE auto resolve_local_cf_imports(
    const std::filesystem::path& root_base_dir,
    frontend::FileAST& root_ast,
    BuildPlan& plan,
    const std::filesystem::path& runtime_dir = {},
    bool test_mode = false,
    const std::unordered_map<std::string, std::string>* source_overlays = nullptr
) -> std::expected<void, std::string>;

[[nodiscard]] CFIRE_INFRA_INLINE auto is_ios_target(std::string_view target) noexcept -> bool;

[[nodiscard]] CFIRE_INFRA_INLINE auto is_ios_simulator_target(std::string_view target) noexcept -> bool;

[[nodiscard]] CFIRE_INFRA_INLINE auto normalize_ios_triple(std::string_view target, std::string_view ios_min = "") -> std::string;

[[nodiscard]] CFIRE_INFRA_INLINE auto resolve_ios_sdk_path(bool simulator) -> std::expected<std::string, std::string>;

#if !defined(CFIRE_SPLIT_TUS) || defined(CFIRE_INFRA_IMPL)

[[nodiscard]] CFIRE_INFRA_INLINE auto ast_has_top_level_main(const frontend::FileAST& ast) -> bool {
    for (const auto& fn : ast.functions) {
        if (!fn.receiver && fn.name == "main") return true;
    }
    return false;
}

[[nodiscard]] CFIRE_INFRA_INLINE auto is_wasm_target(std::string_view target) noexcept -> bool {
    return target == "wasm" || target == "web" || target.find("wasm") != std::string_view::npos;
}

[[nodiscard]] CFIRE_INFRA_INLINE auto is_windows_target(std::string_view target) noexcept -> bool {
    if (target.find("windows") != std::string_view::npos ||
        target.find("win32") != std::string_view::npos ||
        target.find("mingw") != std::string_view::npos) {
        return true;
    }
#if defined(_WIN32)
    return target.empty();
#else
    return false;
#endif
}

[[nodiscard]] CFIRE_INFRA_INLINE auto is_platform_file_enabled(
    const std::filesystem::path& p,
    const BuildPlan& plan
) -> bool {
    std::string stem = p.stem().string();
    if (stem.ends_with("_test")) {
        stem.resize(stem.size() - 5);
    }
    bool is_apple = false;
    bool is_linux = false;
    bool is_windows = is_windows_target(plan.target_triple);
    bool is_wasm = is_wasm_target(plan.target_triple);
    if (!plan.target_triple.empty()) {
        is_apple = plan.target_triple.find("apple") != std::string::npos ||
                   plan.target_triple.find("darwin") != std::string::npos ||
                   plan.target_triple.find("macos") != std::string::npos;
        is_linux = plan.target_triple.find("linux") != std::string::npos;
    } else {
#if defined(__APPLE__)
        is_apple = true;
#elif defined(__linux__)
        is_linux = true;
#endif
    }
    if (stem.ends_with("_darwin") || stem.ends_with("_macos")) return is_apple && !plan.is_ios;
    if (stem.ends_with("_ios")) return plan.is_ios;
    if (stem.ends_with("_linux")) return is_linux;
    if (stem.ends_with("_windows")) return is_windows;
    if (stem.ends_with("_wasm")) return is_wasm;
    if (stem.ends_with("_unix")) return !is_windows && !is_wasm;
    return true;
}

[[nodiscard]] inline auto read_file_or_overlay(
    const std::filesystem::path& p,
    const std::unordered_map<std::string, std::string>* overlays
) -> std::expected<std::string, std::string> {
    if (overlays && !overlays->empty()) {
        if (auto it = overlays->find(p.string()); it != overlays->end()) {
            return it->second;
        }
        if (auto it = overlays->find(p.lexically_normal().string()); it != overlays->end()) {
            return it->second;
        }
        std::error_code ec;
        auto canon = std::filesystem::weakly_canonical(p, ec);
        if (!ec && !canon.empty()) {
            if (auto it = overlays->find(canon.string()); it != overlays->end()) {
                return it->second;
            }
        }
    }
    return read_file_text(p);
}

[[nodiscard]] CFIRE_INFRA_INLINE auto resolve_local_cf_imports(
    const std::filesystem::path& root_base_dir,
    frontend::FileAST& root_ast,
    BuildPlan& plan,
    const std::filesystem::path& /*runtime_dir*/,
    bool test_mode,
    const std::unordered_map<std::string, std::string>* source_overlays) -> std::expected<void, std::string> {
    std::vector<frontend::ImportDecl> kept_imports;
    std::unordered_set<std::string> visited_files;
    std::unordered_set<std::string> visited_build_cfs;
    std::unordered_set<std::string> seen_non_local_imports;

    bool is_test_entry = test_mode || (!root_ast.path.empty() && root_ast.path.ends_with("_test.cf"));
    bool root_has_main = ast_has_top_level_main(root_ast);

    if (!root_ast.path.empty()) {
        std::error_code ec;
        auto root_canon = std::filesystem::weakly_canonical(root_ast.path, ec);
        if (!ec && !root_canon.empty()) {
            visited_files.insert(root_canon.string());
        } else {
            visited_files.insert(root_ast.path);
        }
    }
    {
        std::error_code ec;
        auto root_bcf = root_base_dir / "build.cf";
        if (std::filesystem::exists(root_bcf, ec)) {
            visited_build_cfs.insert(std::filesystem::weakly_canonical(root_bcf, ec).string());
        }
    }

    struct WorkItem {
        std::filesystem::path base_dir;
        frontend::ImportDecl imp;
        bool is_auto_sibling = false;
    };
    std::deque<WorkItem> queue;

    // 0. Automatically discover same-directory sibling `.cf` files with matching `package_name` (ERG-02a / ERG-02b)
    const bool entry_file_only = explicit_entry_file_only() && !is_test_entry && root_ast.package_name == "main" && root_has_main;
    if (!root_ast.package_name.empty() && !entry_file_only) {
        std::error_code ec;
        std::vector<std::filesystem::path> sibling_candidates;
        for (const auto& entry : std::filesystem::directory_iterator(root_base_dir, ec)) {
            if (!entry.is_regular_file()) continue;
            auto fname = entry.path().filename().string();
            if (entry.path().extension() != ".cf" || fname == "build.cf") continue;
            if (!is_test_entry && fname.ends_with("_test.cf")) continue;
            if (!is_platform_file_enabled(entry.path(), plan)) continue;
            sibling_candidates.push_back(entry.path());
        }
        std::sort(sibling_candidates.begin(), sibling_candidates.end());
        for (const auto& sib_path : sibling_candidates) {
            std::string canon = std::filesystem::weakly_canonical(sib_path, ec).string();
            if (visited_files.contains(canon)) continue;
            frontend::ImportDecl sib_imp;
            sib_imp.is_cpp = false;
            sib_imp.path = "./" + sib_path.filename().string();
            queue.push_back({root_base_dir, std::move(sib_imp), true});
        }
    }

    for (const auto& imp : root_ast.imports) {
        queue.push_back({root_base_dir, imp, false});
    }

    while (!queue.empty()) {
        auto item = std::move(queue.front());
        queue.pop_front();

        const auto& imp = item.imp;
        const auto& base_dir = item.base_dir;

        if (!imp.is_cpp && (imp.path.starts_with("./") || imp.path.starts_with("../"))) {
            std::filesystem::path target = base_dir / imp.path;
            std::vector<std::filesystem::path> cf_files;
            std::filesystem::path pkg_dir;

            // Subpackage build.cf check (subpackage manifest checks)
            std::filesystem::path candidate_dir = std::filesystem::is_directory(target) ? target : target.parent_path();
            if (!candidate_dir.empty()) {
                auto sub_build_cf = candidate_dir / "build.cf";
                std::string sub_bcf_canon = std::filesystem::weakly_canonical(sub_build_cf).string();
                if (std::filesystem::exists(sub_build_cf) && !visited_build_cfs.contains(sub_bcf_canon)) {
                    visited_build_cfs.insert(sub_bcf_canon);
                    auto parsed = parse_build_cf(sub_build_cf, plan.target_triple);
                    if (!parsed) return std::unexpected(parsed.error());
                    auto add_sub_sources_dep = [&](const std::string& dep_name,
                                                   const std::vector<std::string>& srcs,
                                                   const std::vector<std::string>& incs,
                                                   const std::vector<std::string>& defs,
                                                   const std::vector<std::string>& cfs) {
                        if (srcs.empty()) return;
                        for (const auto& ed : plan.external_deps) {
                            if (ed.name == dep_name) return;
                        }
                        ExternalDepSpec dep;
                        dep.name = dep_name;
                        dep.driver = "CC";
                        dep.source = ".";
                        dep.base_dir = candidate_dir;
                        dep.sources = srcs;
                        dep.includes = incs;
                        for (const auto& d : parsed->defines) {
                            auto eq = d.find('=');
                            if (eq == std::string::npos) dep.defines[d] = "1";
                            else dep.defines[d.substr(0, eq)] = d.substr(eq + 1);
                        }
                        for (const auto& d : defs) {
                            auto eq = d.find('=');
                            if (eq == std::string::npos) dep.defines[d] = "1";
                            else dep.defines[d.substr(0, eq)] = d.substr(eq + 1);
                        }
                        for (const auto& c : parsed->cflags) {
                            for (auto& tok : split_ws(c)) dep.extra_cflags.push_back(std::move(tok));
                        }
                        for (const auto& c : cfs) {
                            for (auto& tok : split_ws(c)) dep.extra_cflags.push_back(std::move(tok));
                        }
                        plan.external_deps.push_back(std::move(dep));
                    };
                    apply_manifest_pkg_fields(
                        plan,
                        candidate_dir,
                        parsed->pkg_config,
                        parsed->libraries,
                        parsed->frameworks,
                        parsed->includes,
                        parsed->defines,
                        parsed->cflags,
                        parsed->ldflags
                    );
                    add_sub_sources_dep("__bcf_sub_pkg_sources_" + candidate_dir.filename().string(), parsed->sources, parsed->includes, {}, {});
                    for (const auto& ex : parsed->executables) {
                        apply_manifest_pkg_fields(
                            plan,
                            candidate_dir,
                            ex.pkg_config,
                            ex.libraries,
                            ex.frameworks,
                            ex.includes,
                            ex.defines,
                            ex.cflags,
                            ex.ldflags
                        );
                        add_sub_sources_dep("__bcf_sub_exe_" + ex.name + "_sources", ex.sources, ex.includes, ex.defines, ex.cflags);
                    }
                    for (auto& dep : parsed->external_deps) {
                        bool exists_already = false;
                        for (const auto& ed : plan.external_deps) {
                            if (ed.name == dep.name) { exists_already = true; break; }
                        }
                        if (!exists_already) {
                            plan.external_deps.push_back(std::move(dep));
                        }
                    }
                    for (auto& cmd : parsed->pre_build_commands) {
                        if (cmd.work_dir.empty()) cmd.work_dir = candidate_dir.string();
                        if (!source_overlays) {
                            if (auto r = execute_pre_build_command(cmd, candidate_dir); !r) {
                                return std::unexpected(r.error());
                            }
                        }
                        plan.pre_build_commands.push_back(std::move(cmd));
                    }
                }
            }

            if (std::filesystem::is_directory(target)) {
                pkg_dir = target;
                for (const auto& entry : std::filesystem::directory_iterator(target)) {
                    auto fname = entry.path().filename().string();
                    if (entry.is_regular_file() && entry.path().extension() == ".cf" &&
                        fname != "build.cf" && !fname.ends_with("_test.cf") &&
                        is_platform_file_enabled(entry.path(), plan)) {
                        cf_files.push_back(entry.path());
                    }
                }
            } else if (std::filesystem::exists(target.string() + ".cf")) {
                cf_files.push_back(target.string() + ".cf");
                pkg_dir = target.parent_path();
            } else if (std::filesystem::exists(target)) {
                cf_files.push_back(target);
                pkg_dir = target.parent_path();
            } else {
                return std::unexpected(std::format("local package import not found: {}", imp.path));
            }

            plan.local_cf_packages.push_back(imp.path);

            for (const auto& fpath : cf_files) {
                std::string canon = std::filesystem::weakly_canonical(fpath).string();
                if (visited_files.contains(canon)) continue;

                auto raw = read_file_or_overlay(fpath, source_overlays);
                if (!raw) return std::unexpected(raw.error());
                auto sub_ast = frontend::parse_source(fpath.string(), *raw);
                if (!sub_ast) {
                    if (item.is_auto_sibling) {
                        // Only skip if the sibling belongs to a different package; if it shares
                        // package_name or is a *_test.cf file, surface the syntax error!
                        std::string pkg_prefix = "package " + root_ast.package_name;
                        if (!fpath.string().ends_with("_test.cf") &&
                            raw->find(pkg_prefix) == std::string::npos) {
                            continue;
                        }
                    }
                    return std::unexpected(sub_ast.error());
                }
                if (is_test_entry) {
                    frontend::append_doctests(fpath.string(), *raw, *sub_ast);
                }

                if (item.is_auto_sibling) {
                    if (sub_ast->package_name != root_ast.package_name) {
                        continue;
                    }
                    // Exclude sibling standalone examples/executables that also define `fn main()`
                    if (ast_has_top_level_main(*sub_ast)) {
                        if (root_has_main) {
                            continue;
                        }
                        root_has_main = true;
                    }
                }
                visited_files.insert(canon);

                // Recursively enqueue subpackage imports (subpackage imports)
                for (const auto& simp : sub_ast->imports) {
                    queue.push_back({fpath.parent_path(), simp, false});
                }

                // Preserve subpackage AST arena lifetime in root_ast
                if (sub_ast->arena) root_ast.child_arenas.push_back(sub_ast->arena);
                root_ast.child_arenas.insert(root_ast.child_arenas.end(),
                    sub_ast->child_arenas.begin(), sub_ast->child_arenas.end());

                // Merge AST declarations
                std::string sub_pkg_tag;
                if (!sub_ast->package_name.empty() && sub_ast->package_name != root_ast.package_name) {
                    sub_pkg_tag = !imp.alias.empty() ? imp.alias : sub_ast->package_name;
                }
                for (auto& st : sub_ast->structs) {
                    if (st.package_name.empty() && !sub_pkg_tag.empty()) {
                        st.package_name = sub_pkg_tag;
                    }
                    root_ast.structs.push_back(std::move(st));
                }
                for (auto& en : sub_ast->enums) {
                    if (en.package_name.empty() && !sub_pkg_tag.empty()) {
                        en.package_name = sub_pkg_tag;
                    }
                    root_ast.enums.push_back(std::move(en));
                }
                for (auto& ifc : sub_ast->interfaces) {
                    if (ifc.package_name.empty() && !sub_pkg_tag.empty()) {
                        ifc.package_name = sub_pkg_tag;
                    }
                    root_ast.interfaces.push_back(std::move(ifc));
                }
                for (auto& gv : sub_ast->global_vars) {
                    if (gv.package_name.empty() && !sub_pkg_tag.empty()) {
                        gv.package_name = sub_pkg_tag;
                    }
                }
                root_ast.global_vars.insert(root_ast.global_vars.begin(),
                    std::make_move_iterator(sub_ast->global_vars.begin()),
                    std::make_move_iterator(sub_ast->global_vars.end()));
                for (auto& fn : sub_ast->functions) {
                    if (fn.package_name.empty() && !sub_pkg_tag.empty()) {
                        fn.package_name = sub_pkg_tag;
                    }
                }
                root_ast.functions.insert(root_ast.functions.begin(),
                    std::make_move_iterator(sub_ast->functions.begin()),
                    std::make_move_iterator(sub_ast->functions.end()));
                for (auto& td : sub_ast->tests) {
                    if (td.package_name.empty() && !sub_pkg_tag.empty()) {
                        td.package_name = sub_pkg_tag;
                    }
                    root_ast.tests.push_back(std::move(td));
                }
            }
        } else {
            // Deduplicate non-local imports (C++ or core imports), preserving & merging attributes
            std::string imp_key = (imp.is_cpp ? "cpp:" : "cf:") + imp.path + (imp.alias.empty() ? "" : ":" + imp.alias);
            if (!seen_non_local_imports.contains(imp_key)) {
                seen_non_local_imports.insert(imp_key);
                kept_imports.push_back(imp);
            } else {
                for (auto& existing : kept_imports) {
                    if (existing.is_cpp == imp.is_cpp && existing.path == imp.path && existing.alias == imp.alias) {
                        for (const auto& attr : imp.attrs) {
                            bool dup_attr = false;
                            for (const auto& ea : existing.attrs) {
                                if (ea.name == attr.name && ea.args == attr.args) {
                                    dup_attr = true;
                                    break;
                                }
                            }
                            if (!dup_attr) {
                                existing.attrs.push_back(attr);
                            }
                        }
                        break;
                    }
                }
            }
        }
    }

    // Disambiguate colliding struct/enum/interface names across packages
    std::unordered_map<std::string, std::size_t> type_counts;
    for (const auto& st : root_ast.structs) type_counts[st.name]++;
    for (const auto& en : root_ast.enums) type_counts[en.name]++;
    for (const auto& ifc : root_ast.interfaces) type_counts[ifc.name]++;

    std::unordered_map<std::string, std::string> pkg_type_renames;
    for (auto& st : root_ast.structs) {
        if (!st.package_name.empty() && type_counts[st.name] > 1) {
            std::string mangled = std::format("cfire_pkg_{}__{}", st.package_name, st.name);
            pkg_type_renames[st.package_name + "." + st.name] = mangled;
            pkg_type_renames[st.package_name + "::" + st.name] = mangled;
            st.name = mangled;
        }
    }
    for (auto& en : root_ast.enums) {
        if (!en.package_name.empty() && type_counts[en.name] > 1) {
            std::string mangled = std::format("cfire_pkg_{}__{}", en.package_name, en.name);
            pkg_type_renames[en.package_name + "." + en.name] = mangled;
            pkg_type_renames[en.package_name + "::" + en.name] = mangled;
            en.name = mangled;
        }
    }
    for (auto& ifc : root_ast.interfaces) {
        if (!ifc.package_name.empty() && type_counts[ifc.name] > 1) {
            std::string mangled = std::format("cfire_pkg_{}__{}", ifc.package_name, ifc.name);
            pkg_type_renames[ifc.package_name + "." + ifc.name] = mangled;
            pkg_type_renames[ifc.package_name + "::" + ifc.name] = mangled;
            ifc.name = mangled;
        }
    }

    if (!pkg_type_renames.empty()) {
        auto resolve_type_name = [&](const std::string& tname, const std::string& cur_pkg) -> std::string {
            if (auto it = pkg_type_renames.find(tname); it != pkg_type_renames.end()) {
                return it->second;
            }
            if (!cur_pkg.empty()) {
                if (auto it = pkg_type_renames.find(cur_pkg + "." + tname); it != pkg_type_renames.end()) {
                    return it->second;
                }
            }
            return tname;
        };

        auto rewrite_tref = [&](const auto& self, frontend::TypeRef& tr, const std::string& cur_pkg) -> void {
            tr.name = resolve_type_name(tr.name, cur_pkg);
            if (tr.elem_type) self(self, *tr.elem_type, cur_pkg);
            if (tr.key_type) self(self, *tr.key_type, cur_pkg);
            for (auto& ta : tr.type_args) self(self, ta, cur_pkg);
            for (auto& tt : tr.tuple_types) self(self, tt, cur_pkg);
        };

        auto rewrite_expr = [&](const auto& self, const auto& rewrite_stmts, frontend::ExprPtr& e, const std::string& cur_pkg) -> void {
            if (!e) return;
            std::visit([&](auto& ex) {
                using T = std::decay_t<decltype(ex)>;
                if constexpr (std::is_same_v<T, frontend::IdentExpr>) {
                    if (auto col = ex.name.find("::"); col != std::string::npos) {
                        std::string prefix = ex.name.substr(0, col);
                        std::string rest = ex.name.substr(col + 2);
                        std::string resolved = resolve_type_name(prefix, cur_pkg);
                        if (resolved != prefix) {
                            ex.name = resolved + "::" + rest;
                        }
                    } else {
                        ex.name = resolve_type_name(ex.name, cur_pkg);
                    }
                } else if constexpr (std::is_same_v<T, frontend::UnaryExpr>) {
                    self(self, rewrite_stmts, ex.operand, cur_pkg);
                } else if constexpr (std::is_same_v<T, frontend::BinaryExpr>) {
                    self(self, rewrite_stmts, ex.left, cur_pkg);
                    self(self, rewrite_stmts, ex.right, cur_pkg);
                } else if constexpr (std::is_same_v<T, frontend::MemberExpr>) {
                    if (const auto* id = ex.object ? std::get_if<frontend::IdentExpr>(&ex.object->node) : nullptr) {
                        if (auto it = pkg_type_renames.find(id->name + "." + ex.field); it != pkg_type_renames.end()) {
                            e->node = frontend::IdentExpr{it->second};
                            return;
                        }
                    }
                    self(self, rewrite_stmts, ex.object, cur_pkg);
                } else if constexpr (std::is_same_v<T, frontend::IndexExpr>) {
                    self(self, rewrite_stmts, ex.object, cur_pkg);
                    self(self, rewrite_stmts, ex.index, cur_pkg);
                } else if constexpr (std::is_same_v<T, frontend::SliceExpr>) {
                    self(self, rewrite_stmts, ex.object, cur_pkg);
                    self(self, rewrite_stmts, ex.start, cur_pkg);
                    self(self, rewrite_stmts, ex.end, cur_pkg);
                } else if constexpr (std::is_same_v<T, frontend::StructLitExpr>) {
                    ex.type_name = resolve_type_name(ex.type_name, cur_pkg);
                    for (auto& ta : ex.type_args) rewrite_tref(rewrite_tref, ta, cur_pkg);
                    for (auto& f : ex.fields) self(self, rewrite_stmts, f.value, cur_pkg);
                } else if constexpr (std::is_same_v<T, frontend::ArrayLitExpr> || std::is_same_v<T, frontend::TupleExpr>) {
                    for (auto& el : ex.elements) self(self, rewrite_stmts, el, cur_pkg);
                } else if constexpr (std::is_same_v<T, frontend::ClosureExpr>) {
                    for (auto& pt : ex.param_types) {
                        if (pt) rewrite_tref(rewrite_tref, *pt, cur_pkg);
                    }
                    if (ex.return_type) rewrite_tref(rewrite_tref, *ex.return_type, cur_pkg);
                    rewrite_stmts(rewrite_stmts, self, ex.body, cur_pkg);
                } else if constexpr (std::is_same_v<T, frontend::OrExpr>) {
                    self(self, rewrite_stmts, ex.inner, cur_pkg);
                    self(self, rewrite_stmts, ex.fallback_val, cur_pkg);
                    rewrite_stmts(rewrite_stmts, self, ex.handler_body, cur_pkg);
                } else if constexpr (std::is_same_v<T, frontend::TryExpr>) {
                    self(self, rewrite_stmts, ex.inner, cur_pkg);
                } else if constexpr (std::is_same_v<T, frontend::CallExpr>) {
                    self(self, rewrite_stmts, ex.callee, cur_pkg);
                    for (auto& ta : ex.type_args) rewrite_tref(rewrite_tref, ta, cur_pkg);
                    for (auto& a : ex.args) self(self, rewrite_stmts, a.value, cur_pkg);
                    if (ex.block_arg) {
                        for (auto& pt : ex.block_arg->param_types) {
                            if (pt) rewrite_tref(rewrite_tref, *pt, cur_pkg);
                        }
                        if (ex.block_arg->return_type) rewrite_tref(rewrite_tref, *ex.block_arg->return_type, cur_pkg);
                        rewrite_stmts(rewrite_stmts, self, ex.block_arg->body, cur_pkg);
                    }
                } else if constexpr (std::is_same_v<T, frontend::MatchExpr>) {
                    self(self, rewrite_stmts, ex.target, cur_pkg);
                    for (auto& arm : ex.arms) {
                        if (!arm.enum_prefix.empty()) arm.enum_prefix = resolve_type_name(arm.enum_prefix, cur_pkg);
                        for (auto& pat : arm.patterns) {
                            if (!pat.enum_prefix.empty()) pat.enum_prefix = resolve_type_name(pat.enum_prefix, cur_pkg);
                        }
                        if (arm.guard) self(self, rewrite_stmts, arm.guard, cur_pkg);
                        rewrite_stmts(rewrite_stmts, self, arm.body, cur_pkg);
                    }
                }
            }, e->node);
        };

        auto rewrite_stmts = [&](const auto& self, const auto& rexpr, std::vector<frontend::StmtPtr>& stmts, const std::string& cur_pkg) -> void {
            for (auto& s : stmts) {
                if (!s) continue;
                std::visit([&](auto& st) {
                    using T = std::decay_t<decltype(st)>;
                    if constexpr (std::is_same_v<T, frontend::VarDeclStmt>) {
                        if (st.type) rewrite_tref(rewrite_tref, *st.type, cur_pkg);
                        rexpr(rexpr, self, st.init, cur_pkg);
                    } else if constexpr (std::is_same_v<T, frontend::DestructureVarDeclStmt>) {
                        rexpr(rexpr, self, st.init, cur_pkg);
                    } else if constexpr (std::is_same_v<T, frontend::AssignStmt>) {
                        rexpr(rexpr, self, st.target, cur_pkg);
                        rexpr(rexpr, self, st.value, cur_pkg);
                    } else if constexpr (std::is_same_v<T, frontend::DestructureAssignStmt>) {
                        for (auto& t : st.targets) rexpr(rexpr, self, t, cur_pkg);
                        rexpr(rexpr, self, st.value, cur_pkg);
                    } else if constexpr (std::is_same_v<T, frontend::DeferStmt>) {
                        rexpr(rexpr, self, st.call, cur_pkg);
                        self(self, rexpr, st.body, cur_pkg);
                    } else if constexpr (std::is_same_v<T, frontend::ReturnStmt>) {
                        rexpr(rexpr, self, st.value, cur_pkg);
                    } else if constexpr (std::is_same_v<T, frontend::IfStmt>) {
                        rexpr(rexpr, self, st.cond, cur_pkg);
                        self(self, rexpr, st.then_body, cur_pkg);
                        self(self, rexpr, st.else_body, cur_pkg);
                    } else if constexpr (std::is_same_v<T, frontend::ForStmt>) {
                        rexpr(rexpr, self, st.iter_expr, cur_pkg);
                        rexpr(rexpr, self, st.range_end, cur_pkg);
                        self(self, rexpr, st.body, cur_pkg);
                    } else if constexpr (std::is_same_v<T, frontend::MatchStmt>) {
                        rexpr(rexpr, self, st.target, cur_pkg);
                        for (auto& arm : st.arms) {
                            if (!arm.enum_prefix.empty()) arm.enum_prefix = resolve_type_name(arm.enum_prefix, cur_pkg);
                            for (auto& pat : arm.patterns) {
                                if (!pat.enum_prefix.empty()) pat.enum_prefix = resolve_type_name(pat.enum_prefix, cur_pkg);
                            }
                            if (arm.guard) rexpr(rexpr, self, arm.guard, cur_pkg);
                            self(self, rexpr, arm.body, cur_pkg);
                        }
                    } else if constexpr (std::is_same_v<T, frontend::AsyncScopeStmt>) {
                        rexpr(rexpr, self, st.workers, cur_pkg);
                        self(self, rexpr, st.body, cur_pkg);
                    } else if constexpr (std::is_same_v<T, frontend::ExprStmt>) {
                        rexpr(rexpr, self, st.expr, cur_pkg);
                    }
                }, s->node);
            }
        };

        for (auto& st : root_ast.structs) {
            for (auto& tp : st.type_params) {
                for (auto& c : tp.constraints) c = resolve_type_name(c, st.package_name);
            }
            for (auto& fld : st.fields) {
                rewrite_tref(rewrite_tref, fld.type, st.package_name);
                rewrite_expr(rewrite_expr, rewrite_stmts, fld.default_val, st.package_name);
            }
        }
        for (auto& en : root_ast.enums) {
            for (auto& tp : en.type_params) {
                for (auto& c : tp.constraints) c = resolve_type_name(c, en.package_name);
            }
            for (auto& v : en.variants) {
                for (auto& pt : v.payload_types) rewrite_tref(rewrite_tref, pt, en.package_name);
            }
        }
        for (auto& ifc : root_ast.interfaces) {
            for (auto& m : ifc.methods) {
                for (auto& pt : m.param_types) rewrite_tref(rewrite_tref, pt, ifc.package_name);
                rewrite_tref(rewrite_tref, m.return_type, ifc.package_name);
            }
        }
        for (auto& gv : root_ast.global_vars) {
            if (gv.type) rewrite_tref(rewrite_tref, *gv.type, gv.package_name);
            rewrite_expr(rewrite_expr, rewrite_stmts, gv.value, gv.package_name);
        }
        for (auto& fn : root_ast.functions) {
            for (auto& tp : fn.type_params) {
                for (auto& c : tp.constraints) c = resolve_type_name(c, fn.package_name);
            }
            if (fn.receiver) rewrite_tref(rewrite_tref, fn.receiver->type, fn.package_name);
            for (auto& p : fn.params) rewrite_tref(rewrite_tref, p.type, fn.package_name);
            rewrite_tref(rewrite_tref, fn.return_type, fn.package_name);
            rewrite_stmts(rewrite_stmts, rewrite_expr, fn.body, fn.package_name);
        }
        for (auto& td : root_ast.tests) {
            for (auto& p : td.params) rewrite_tref(rewrite_tref, p.type, td.package_name);
            for (auto& bv : td.bench_variations) {
                rewrite_tref(rewrite_tref, bv.inferred_type, td.package_name);
                for (auto& vx : bv.values) rewrite_expr(rewrite_expr, rewrite_stmts, vx, td.package_name);
            }
            rewrite_stmts(rewrite_stmts, rewrite_expr, td.body, td.package_name);
        }
    }

    root_ast.imports = std::move(kept_imports);
    return {};
}

[[nodiscard]] CFIRE_INFRA_INLINE auto is_ios_target(std::string_view target) noexcept -> bool {
    return target == "ios" || target == "ios-sim" || target == "ios-simulator" ||
           target.find("-apple-ios") != std::string_view::npos;
}

[[nodiscard]] CFIRE_INFRA_INLINE auto is_ios_simulator_target(std::string_view target) noexcept -> bool {
    return target == "ios-sim" || target == "ios-simulator" ||
           (target.find("-apple-ios") != std::string_view::npos && target.find("simulator") != std::string_view::npos);
}

[[nodiscard]] CFIRE_INFRA_INLINE auto normalize_ios_triple(std::string_view target, std::string_view ios_min) -> std::string {
    std::string ver = ios_min.empty() ? std::string(kDefaultIosMinVersion) : std::string(ios_min);
    if (target == "ios-sim" || target == "ios-simulator") {
        return "arm64-apple-ios" + ver + "-simulator";
    }
    if (target == "ios") {
        return "arm64-apple-ios" + ver;
    }
    if (!ios_min.empty()) {
        if (auto pos = target.find("-apple-ios"); pos != std::string_view::npos) {
            std::string arch(target.substr(0, pos));
            bool sim = target.find("simulator") != std::string_view::npos;
            return arch + "-apple-ios" + ver + (sim ? "-simulator" : "");
        }
    }
    return std::string(target);
}

[[nodiscard]] CFIRE_INFRA_INLINE auto resolve_ios_sdk_path(bool simulator) -> std::expected<std::string, std::string> {
#if defined(__APPLE__)
    std::string sdk_name = simulator ? "iphonesimulator" : "iphoneos";
    std::string out = ProbeCache::instance().probe_ios_sdk_path(sdk_name);
    if (!out.empty() && std::filesystem::exists(out)) {
        return out;
    }
    return std::unexpected(std::format(
        "Xcode iOS SDK '{}' not found via xcrun.\n"
        "Ensure Xcode and the iOS platform SDK are installed (`xcode-select --install` or Xcode -> Settings -> Components).",
        sdk_name));
#else
    (void)simulator;
    return std::unexpected("iOS targets (--target ios-sim / ios) require macOS with Xcode installed.");
#endif
}

#endif // !defined(CFIRE_SPLIT_TUS) || defined(CFIRE_INFRA_IMPL)

} // namespace cfire::build

#undef CFIRE_INFRA_INLINE
