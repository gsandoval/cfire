#pragma once
// src/build/package_loader.hpp — Multi-Package `.cf` Import Resolution & iOS Target Triples

#include "deps.hpp"
#include <deque>

namespace cfire::build {

[[nodiscard]] inline auto ast_has_top_level_main(const frontend::FileAST& ast) -> bool {
    for (const auto& fn : ast.functions) {
        if (!fn.receiver && fn.name == "main") return true;
    }
    return false;
}

[[nodiscard]] inline auto is_platform_file_enabled(
    const std::filesystem::path& p,
    const BuildPlan& plan
) -> bool {
    std::string stem = p.stem().string();
    if (stem.ends_with("_test")) {
        stem.resize(stem.size() - 5);
    }
    bool is_apple = false;
    bool is_linux = false;
    bool is_windows = false;
    if (!plan.target_triple.empty()) {
        is_apple = plan.target_triple.find("apple") != std::string::npos ||
                   plan.target_triple.find("darwin") != std::string::npos ||
                   plan.target_triple.find("macos") != std::string::npos;
        is_linux = plan.target_triple.find("linux") != std::string::npos;
        is_windows = plan.target_triple.find("windows") != std::string::npos ||
                     plan.target_triple.find("mingw") != std::string::npos ||
                     plan.target_triple.find("win32") != std::string::npos;
    } else {
#if defined(__APPLE__)
        is_apple = true;
#elif defined(__linux__)
        is_linux = true;
#elif defined(_WIN32)
        is_windows = true;
#endif
    }
    if (stem.ends_with("_darwin") || stem.ends_with("_macos")) return is_apple && !plan.is_ios;
    if (stem.ends_with("_ios")) return plan.is_ios;
    if (stem.ends_with("_linux")) return is_linux;
    if (stem.ends_with("_windows")) return is_windows;
    return true;
}

// Recursively resolves local `.cf` imports (e.g. `import "./pkg/checksum"` or `import "pkg:raygui_ext"`)
// and automatically discovers same-directory sibling `.cf` files sharing `root_ast.package_name`.
[[nodiscard]] inline auto resolve_local_cf_imports(
    const std::filesystem::path& root_base_dir,
    frontend::FileAST& root_ast,
    BuildPlan& plan,
    const std::filesystem::path& runtime_dir = {},
    bool test_mode = false
) -> std::expected<void, std::string> {
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

    std::filesystem::path toolchain_pkg_dir = runtime_dir.empty()
        ? locate_toolchain_layout().pkg_dir
        : derive_pkg_dir(runtime_dir);

    struct WorkItem {
        std::filesystem::path base_dir;
        frontend::ImportDecl imp;
        bool is_auto_sibling = false;
    };
    std::deque<WorkItem> queue;

    // 0. Automatically discover same-directory sibling `.cf` files with matching `package_name` (ERG-02a / ERG-02b)
    if (!root_ast.package_name.empty()) {
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

        if (!imp.is_cpp && (imp.path.starts_with("./") || imp.path.starts_with("../") || imp.path.starts_with("pkg:"))) {
            std::filesystem::path target;
            if (imp.path.starts_with("pkg:")) {
                std::string pkg_rel = imp.path.substr(4);
                target = toolchain_pkg_dir / pkg_rel;
                if (!std::filesystem::exists(target) && std::filesystem::exists(root_base_dir / "pkg" / pkg_rel)) {
                    target = root_base_dir / "pkg" / pkg_rel;
                }
            } else {
                target = base_dir / imp.path;
            }
            std::vector<std::filesystem::path> cf_files;
            std::filesystem::path pkg_dir;

            // Subpackage build.cf check (BS-07 & BS-08)
            std::filesystem::path candidate_dir = std::filesystem::is_directory(target) ? target : target.parent_path();
            if (!candidate_dir.empty()) {
                auto sub_build_cf = candidate_dir / "build.cf";
                std::string sub_bcf_canon = std::filesystem::weakly_canonical(sub_build_cf).string();
                if (std::filesystem::exists(sub_build_cf) && !visited_build_cfs.contains(sub_bcf_canon)) {
                    visited_build_cfs.insert(sub_bcf_canon);
                    auto parsed = parse_build_cf(sub_build_cf);
                    if (!parsed) return std::unexpected(parsed.error());
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
                        if (auto r = execute_pre_build_command(cmd, candidate_dir); !r) {
                            return std::unexpected(r.error());
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

                auto raw = read_file_text(fpath);
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

                // Recursively enqueue subpackage imports (BS-07)
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
                for (auto& st : sub_ast->structs) root_ast.structs.push_back(std::move(st));
                for (auto& en : sub_ast->enums) root_ast.enums.push_back(std::move(en));
                for (auto& ifc : sub_ast->interfaces) root_ast.interfaces.push_back(std::move(ifc));
                for (auto& gv : sub_ast->global_vars) {
                    if (gv.package_name.empty() && !sub_pkg_tag.empty()) {
                        gv.package_name = sub_pkg_tag;
                    }
                    root_ast.global_vars.push_back(std::move(gv));
                }
                for (auto& fn : sub_ast->functions) {
                    if (fn.package_name.empty() && !sub_pkg_tag.empty()) {
                        fn.package_name = sub_pkg_tag;
                    }
                }
                root_ast.functions.insert(root_ast.functions.begin(),
                    std::make_move_iterator(sub_ast->functions.begin()),
                    std::make_move_iterator(sub_ast->functions.end()));
                for (auto& td : sub_ast->tests) root_ast.tests.push_back(std::move(td));
            }
        } else {
            // Deduplicate non-local imports (C++ or core imports)
            std::string imp_key = (imp.is_cpp ? "cpp:" : "cf:") + imp.path + (imp.alias.empty() ? "" : ":" + imp.alias);
            if (!seen_non_local_imports.contains(imp_key)) {
                seen_non_local_imports.insert(imp_key);
                kept_imports.push_back(imp);
            }
        }
    }

    root_ast.imports = std::move(kept_imports);
    return {};
}

[[nodiscard]] inline auto is_ios_target(std::string_view target) noexcept -> bool {
    return target == "ios" || target == "ios-sim" || target == "ios-simulator" ||
           target.find("-apple-ios") != std::string_view::npos;
}

[[nodiscard]] inline auto is_ios_simulator_target(std::string_view target) noexcept -> bool {
    return target == "ios-sim" || target == "ios-simulator" ||
           (target.find("-apple-ios") != std::string_view::npos && target.find("simulator") != std::string_view::npos);
}

[[nodiscard]] inline auto normalize_ios_triple(std::string_view target) -> std::string {
    if (target == "ios-sim" || target == "ios-simulator") {
        return "arm64-apple-ios17.0-simulator";
    }
    if (target == "ios") {
        return "arm64-apple-ios17.0";
    }
    return std::string(target);
}

[[nodiscard]] inline auto resolve_ios_sdk_path(bool simulator) -> std::expected<std::string, std::string> {
#if defined(__APPLE__)
    std::string sdk_name = simulator ? "iphonesimulator" : "iphoneos";
    int rc = 0;
    std::string out = bridge::run_command_capture(std::format("xcrun --sdk {} --show-sdk-path 2>/dev/null", sdk_name), rc);
    out = trim_whitespace(out);
    if (rc == 0 && !out.empty() && std::filesystem::exists(out)) {
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


} // namespace cfire::build
