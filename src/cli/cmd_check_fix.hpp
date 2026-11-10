#pragma once
// src/cli/cmd_check_fix.hpp — `cfire fix` & `cfire check` Subcommands

#include "../analysis.hpp"
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
    cfire::analysis::CheckOptions opts;
    opts.runtime_dir = runtime_dir;
    opts.target_triple = target_triple;
    opts.sysroot = sysroot;
    opts.extra_includes = extra_includes;
    opts.web_profile = web_profile;
    opts.subsystem = subsystem;

    auto all_diags = cfire::analysis::check_path(target, opts);

    bool has_errors = false;
    for (const auto& d : all_diags) {
        if (d.severity == "error") {
            has_errors = true;
            break;
        }
    }

    if (json_mode) {
        std::println("{}", cfire::diag::serialize_diagnostics_json(target.string(), !has_errors, all_diags));
        return has_errors ? 1 : 0;
    }

    if (all_diags.empty()) {
        std::println("✓ {} checked cleanly (0 errors, 0 warnings)", target.string());
        return 0;
    }

    for (const auto& d : all_diags) {
        std::println(stderr, "{}:{}:{}: {}: {} ({})", d.file, d.line, d.col, d.severity, d.message, d.code);
        for (const auto& fix : d.fixits) {
            std::println(stderr, "  fix: {}", fix.title);
        }
    }

    if (!has_errors) {
        std::println("✓ {} checked (0 errors, {} warning{})", target.string(), all_diags.size(), all_diags.size() == 1 ? "" : "s");
    }

    return has_errors ? 1 : 0;
}

} // namespace cfire::cli
