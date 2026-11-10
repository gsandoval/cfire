#pragma once
// src/cli/cmd_check_fix.hpp — `cfire fix` & `cfire check` Subcommands

#include "scaffold.hpp"

namespace cfire::cli {

[[nodiscard]] inline auto run_fix_subcommand(
    std::string_view sub,
    const std::filesystem::path& target,
    bool json_mode,
    bool dry_run
) -> int {
        std::vector<std::filesystem::path> fix_files;
        std::error_code fix_ec;
        if (std::filesystem::is_directory(target, fix_ec)) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(target, fix_ec)) {
                if (!entry.is_regular_file() || entry.path().extension() != ".cf") continue;
                if (entry.path().filename() == "build.cf") continue;
                if (is_ignored_walk_dir(entry.path())) continue;
                fix_files.push_back(entry.path());
            }
            std::sort(fix_files.begin(), fix_files.end());
        } else {
            fix_files.push_back(target);
        }

        std::size_t grand_total_fixes = 0;
        std::vector<std::string> file_jsons;
        bool any_read_error = false;

        for (const auto& fpath : fix_files) {
            auto raw_txt = cfire::build::read_file_text(fpath);
            if (!raw_txt) {
                any_read_error = true;
                if (json_mode && fix_files.size() == 1) {
                    std::println("{{\"success\": false, \"file\": \"{}\", \"applied_fixes\": 0, \"edits\": []}}",
                        cfire::diag::json_escape(fpath.string()));
                    return 1;
                } else if (!json_mode) {
                    std::println(stderr, "error reading '{}': {}", fpath.string(), raw_txt.error());
                }
                continue;
            }

            std::string current_text = *raw_txt;
            std::vector<cfire::diag::FixItEdit> total_applied;

            for (int pass = 0; pass < 3; ++pass) {
                auto pres = cfire::frontend::parse_source_resilient(fpath.string(), current_text);
                auto lines = cfire::diag::split_lines(current_text);
                std::vector<cfire::diag::FixItEdit> pass_edits;

                // 1. Missing stdlib imports
                auto import_fixes = cfire::diag::detect_missing_stdlib_imports(pres.ast, lines);
                bool has_batch_import_fix = !import_fixes.empty();
                for (auto& f : import_fixes) {
                    pass_edits.push_back(std::move(f));
                }

                // 2. Parser diagnostics & quick-fixes (e.g. deprecations)
                for (const auto& err : pres.errors) {
                    auto d = cfire::diag::parse_diagnostic_string(err, fpath.string(), "error", "cfire/SYNTAX");
                    cfire::diag::synthesize_fixits_for_diagnostic(d, nullptr, lines);
                    for (auto& fix : d.fixits) {
                        if (fix.is_preferred) pass_edits.push_back(std::move(fix));
                    }
                }

                // 3. Sema diagnostics & quick-fixes
                auto chk = cfire::frontend::analyze_resilient(std::move(pres.ast));
                for (const auto& w : chk.warnings) {
                    auto d = cfire::diag::parse_diagnostic_string(w, fpath.string(), "warning", "cfire/WARNING");
                    cfire::diag::synthesize_fixits_for_diagnostic(d, &chk.ast, lines);
                    for (auto& fix : d.fixits) {
                        if (fix.is_preferred) pass_edits.push_back(std::move(fix));
                    }
                }
                for (const auto& err : chk.errors) {
                    auto d = cfire::diag::parse_diagnostic_string(err, fpath.string(), "error", "cfire/SEMA-ERROR");
                    if (has_batch_import_fix && d.code == "cfire/MISSING-IMPORT") continue;
                    cfire::diag::synthesize_fixits_for_diagnostic(d, &chk.ast, lines);
                    for (auto& fix : d.fixits) {
                        if (fix.is_preferred) pass_edits.push_back(std::move(fix));
                    }
                }

                if (pass_edits.empty()) {
                    break;
                }

                auto [new_text, applied] = cfire::diag::apply_fixits(current_text, pass_edits);
                if (applied.empty() || new_text == current_text) {
                    break;
                }

                for (auto& a : applied) {
                    total_applied.push_back(std::move(a));
                }
                current_text = std::move(new_text);
            }

            if (!dry_run && !total_applied.empty()) {
                std::ofstream out(fpath, std::ios::binary | std::ios::trunc);
                out << current_text;
            }

            grand_total_fixes += total_applied.size();
            if (sub == "fix") {
                if (json_mode) {
                    file_jsons.push_back(cfire::diag::serialize_fix_result_json(fpath.string(), true, total_applied.size(), total_applied));
                } else if (fix_files.size() == 1 || !total_applied.empty()) {
                    if (total_applied.empty()) {
                        std::println("✓ No fixes needed for {}", fpath.string());
                    } else {
                        std::println("✓ Applied {} fix(es) to {}{}", total_applied.size(), fpath.string(), dry_run ? " (dry run)" : "");
                        for (const auto& edit : total_applied) {
                            std::println("  - {}", edit.title);
                        }
                    }
                }
            }
        }

        if (sub == "fix") {
            if (json_mode) {
                if (fix_files.size() == 1 && !file_jsons.empty()) {
                    std::println("{}", file_jsons[0]);
                } else {
                    std::string joined;
                    for (std::size_t i = 0; i < file_jsons.size(); ++i) {
                        if (i > 0) joined += ", ";
                        joined += file_jsons[i];
                    }
                    std::println("{{\"success\": {}, \"applied_fixes\": {}, \"files\": [{}]}}",
                        !any_read_error ? "true" : "false", grand_total_fixes, joined);
                }
            } else if (fix_files.size() > 1 && grand_total_fixes == 0) {
                std::println("✓ No fixes needed across {} file(s) in {}", fix_files.size(), target.string());
            }
            return any_read_error ? 1 : 0;
        }
    return 0;
}

[[nodiscard]] inline auto run_check_subcommand(
    const std::filesystem::path& target,
    const std::filesystem::path& runtime_dir,
    bool json_mode,
    const std::string& target_triple,
    const std::string& sysroot,
    const std::vector<std::string>& extra_includes,
    const std::string& web_profile,
    const std::string& subsystem
) -> int {
        std::vector<std::filesystem::path> check_targets;
        std::error_code chk_ec;
        if (std::filesystem::is_directory(target, chk_ec)) {
            auto bcf = target / "build.cf";
            if (std::filesystem::exists(bcf, chk_ec)) {
                if (auto parsed = cfire::build::parse_build_cf(bcf)) {
                    for (const auto& exe : parsed->executables) {
                        std::filesystem::path ep = exe.entry;
                        if (ep.is_relative()) ep = target / ep;
                        if (std::filesystem::exists(ep, chk_ec)) {
                            check_targets.push_back(ep);
                        }
                    }
                }
                if (!check_targets.empty()) {
                    for (const auto& entry : std::filesystem::recursive_directory_iterator(target, chk_ec)) {
                        if (!entry.is_regular_file() || entry.path().extension() != ".cf") continue;
                        if (is_ignored_walk_dir(entry.path())) continue;
                        if (entry.path().filename().string().ends_with("_test.cf")) {
                            check_targets.push_back(entry.path());
                        }
                    }
                }
            }
            if (check_targets.empty()) {
                // Group directory `.cf` files by parent directory + package_name so multi-file packages are checked once via their primary file
                std::unordered_set<std::string> seen_pkg_dirs;
                std::vector<std::filesystem::path> all_cf;
                for (const auto& entry : std::filesystem::recursive_directory_iterator(target, chk_ec)) {
                    if (!entry.is_regular_file() || entry.path().extension() != ".cf") continue;
                    if (entry.path().filename() == "build.cf") continue;
                    if (is_ignored_walk_dir(entry.path())) continue;
                    all_cf.push_back(entry.path());
                }
                std::sort(all_cf.begin(), all_cf.end());
                for (const auto& fpath : all_cf) {
                    auto raw = cfire::build::read_file_text(fpath);
                    if (!raw) {
                        check_targets.push_back(fpath);
                        continue;
                    }
                    auto ast = cfire::frontend::parse_source(fpath.string(), *raw);
                    if (!ast) {
                        check_targets.push_back(fpath);
                        continue;
                    }
                    bool has_main = cfire::build::ast_has_top_level_main(*ast);
                    bool is_test_f = fpath.filename().string().ends_with("_test.cf");
                    std::string key = std::filesystem::weakly_canonical(fpath.parent_path(), chk_ec).string() + "::" + ast->package_name;
                    if (has_main || is_test_f || !seen_pkg_dirs.contains(key)) {
                        seen_pkg_dirs.insert(key);
                        check_targets.push_back(fpath);
                    }
                }
            }
        } else {
            check_targets.push_back(target);
        }

        if (check_targets.empty()) {
            if (json_mode) {
                std::println("{{\"success\": true, \"file\": \"{}\", \"diagnostics\": []}}",
                    cfire::diag::json_escape(target.string()));
            } else {
                std::println("✓ No .cf targets found in {}", target.string());
            }
            return 0;
        }

        if (json_mode) {
            std::vector<cfire::diag::StructuredDiagnostic> all_diags;
            for (const auto& ctarget : check_targets) {
                auto raw_txt = cfire::build::read_file_text(ctarget);
                if (!raw_txt) {
                    cfire::diag::StructuredDiagnostic d;
                    d.file = ctarget.string();
                    d.line = 1;
                    d.col = 1;
                    d.severity = "error";
                    d.code = "cfire/FILE-NOT-FOUND";
                    d.message = raw_txt.error();
                    all_diags.push_back(std::move(d));
                    continue;
                }

                auto pres = cfire::frontend::parse_source_resilient(ctarget.string(), *raw_txt);
                if (!pres.ok()) {
                    for (const auto& err : pres.errors) {
                        all_diags.push_back(cfire::diag::parse_diagnostic_string(err, ctarget.string(), "error", "cfire/SYNTAX-ERROR"));
                    }
                } else {
                    auto lines = cfire::diag::split_lines(*raw_txt);
                    cfire::build::BuildPlan local_plan;
                    local_plan.entry_file = ctarget.string();
                    auto dir = ctarget.parent_path();
                    if (dir.empty()) dir = ".";
                    bool is_test_f = ctarget.filename().string().ends_with("_test.cf");
                    (void)cfire::build::resolve_local_cf_imports(dir, pres.ast, local_plan, runtime_dir, is_test_f);

                    auto chk = cfire::frontend::analyze_resilient(std::move(pres.ast));
                    for (const auto& w : chk.warnings) {
                        auto d = cfire::diag::parse_diagnostic_string(w, ctarget.string(), "warning", "cfire/WARNING");
                        cfire::diag::synthesize_fixits_for_diagnostic(d, &chk.ast, lines);
                        all_diags.push_back(std::move(d));
                    }
                    for (const auto& err : chk.errors) {
                        auto d = cfire::diag::parse_diagnostic_string(err, ctarget.string(), "error", "cfire/SEMA-ERROR");
                        cfire::diag::synthesize_fixits_for_diagnostic(d, &chk.ast, lines);
                        all_diags.push_back(std::move(d));
                    }

                    if (chk.errors.empty()) {
                        auto emit_res = cfire::build::analyze_and_emit(ctarget, runtime_dir, true, is_test_f, target_triple, sysroot);
                        if (!emit_res) {
                            auto d = cfire::diag::parse_diagnostic_string(emit_res.error(), ctarget.string(), "error", "cfire/SEMA-ERROR");
                            all_diags.push_back(std::move(d));
                        } else {
                            auto syntax_check = cfire::build::verify_cpp_syntax_only(ctarget, emit_res->first, emit_res->second, runtime_dir, extra_includes);
                            if (!syntax_check) {
                                auto clang_lines = cfire::diag::split_lines(syntax_check.error());
                                bool found_diag = false;
                                for (const auto& cl : clang_lines) {
                                    if (cl.contains(": error:") || cl.contains(": warning:")) {
                                        auto d = cfire::diag::parse_diagnostic_string(cl, ctarget.string(), cl.contains(": error:") ? "error" : "warning", "cfire/TYPE-ERROR");
                                        all_diags.push_back(std::move(d));
                                        found_diag = true;
                                    }
                                }
                                if (!found_diag && !syntax_check.error().empty()) {
                                    auto d = cfire::diag::parse_diagnostic_string(syntax_check.error(), ctarget.string(), "error", "cfire/TYPE-ERROR");
                                    all_diags.push_back(std::move(d));
                                }
                            }
                        }
                    }
                }
            }

            bool has_errors = false;
            for (const auto& d : all_diags) {
                if (d.severity == "error") {
                    has_errors = true;
                    break;
                }
            }
            std::println("{}", cfire::diag::serialize_diagnostics_json(target.string(), !has_errors, all_diags));
            return has_errors ? 1 : 0;
        }

        int failed_checks = 0;
        for (const auto& ctarget : check_targets) {
            bool is_test_f = ctarget.filename().string().ends_with("_test.cf");
            auto res = cfire::build::analyze_and_emit(ctarget, runtime_dir, true, is_test_f, target_triple, sysroot, web_profile, subsystem);
            if (!res) {
                std::println(stderr, "{}", res.error());
                ++failed_checks;
                continue;
            }
            for (const auto& w : res->second.warnings) {
                std::println(stderr, "{}", w);
            }
            auto syntax_check = cfire::build::verify_cpp_syntax_only(ctarget, res->first, res->second, runtime_dir, extra_includes);
            if (!syntax_check) {
                std::println(stderr, "{}", syntax_check.error());
                ++failed_checks;
                continue;
            }
            if (res->second.warnings.empty()) {
                std::println("✓ {} checked cleanly (0 errors, 0 warnings) ({} auto-linked packages, {} C++ headers ingested)",
                    ctarget.string(), res->second.auto_linked.size(), res->second.cpp_headers.size());
            } else {
                std::println("✓ {} checked (0 errors, {} warning{}) ({} auto-linked packages, {} C++ headers ingested)",
                    ctarget.string(), res->second.warnings.size(), res->second.warnings.size() == 1 ? "" : "s",
                    res->second.auto_linked.size(), res->second.cpp_headers.size());
            }
        }
        return failed_checks > 0 ? 1 : 0;
}

} // namespace cfire::cli
