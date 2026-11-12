#pragma once
#include "../refactor.hpp"
// src/cli/cmd_check_fix.hpp — `cfire fix` & `cfire check` Subcommands

#include "../analysis.hpp"
#include "scaffold.hpp"

namespace cfire::cli {

[[nodiscard]] inline auto run_fix_subcommand(
    std::string_view sub,
    const std::filesystem::path& target,
    bool json_mode,
    bool dry_run,
    std::string_view only_codes = {}
) -> int {
        // `--only a,b`: restrict fixes to these diagnostic codes (`cfire/RETIRED-API` runs only the 0.16 migration).
        std::vector<std::string> only;
        for (std::string_view rest = only_codes; !rest.empty();) {
            auto comma = rest.find(',');
            std::string code(rest.substr(0, comma));
            if (!code.empty() && !code.starts_with("cfire/")) code = "cfire/" + code;
            if (!code.empty()) only.push_back(std::move(code));
            if (comma == std::string_view::npos) break;
            rest.remove_prefix(comma + 1);
        }
        auto wanted = [&](std::string_view code) {
            return only.empty() || std::find(only.begin(), only.end(), code) != only.end();
        };
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

            for (int pass = 0; pass < 8; ++pass) {
                auto pres = cfire::frontend::parse_source_resilient(fpath.string(), current_text);
                auto lines = cfire::diag::split_lines(current_text);
                std::vector<cfire::diag::FixItEdit> pass_edits;

                // 1. Missing stdlib imports
                auto import_fixes = wanted("cfire/MISSING-IMPORT") ? cfire::diag::detect_missing_stdlib_imports(pres.ast, lines)
                                                                     : decltype(cfire::diag::detect_missing_stdlib_imports(pres.ast, lines)){};
                bool has_batch_import_fix = !import_fixes.empty();
                for (auto& f : import_fixes) {
                    pass_edits.push_back(std::move(f));
                }

                // 2. Parser diagnostics & quick-fixes (e.g. deprecations)
                for (const auto& err : pres.source_errors) {
                    auto d = cfire::diag::from_source_error(err);
                    if (!wanted(d.code)) continue;
                    cfire::diag::resolve_diagnostic_range(d, lines);
                    cfire::diag::synthesize_fixits_for_diagnostic(d, nullptr, lines);
                    for (auto& fix : d.fixits) {
                        if (fix.is_preferred) pass_edits.push_back(std::move(fix));
                    }
                }

                // 3. Sema diagnostics & quick-fixes
                auto chk = cfire::frontend::analyze_resilient(std::move(pres.ast));
                for (auto d : chk.diagnostics) {
                    if (d.severity == "error" && has_batch_import_fix && d.code == "cfire/MISSING-IMPORT") continue;
                    if (!wanted(d.code)) continue;
                    if (d.file.empty()) d.file = fpath.string();
                    cfire::diag::resolve_diagnostic_range(d, lines);
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

            // Q28: FIELD-NAME-CASE renames the field and every use across the workspace (the rename engine), so
            // it runs after this file's in-place edits are on disk. Each rename is planned against fresh text.
            if (!dry_run && wanted("cfire/FIELD-NAME-CASE")) {
                for (int guard = 0; guard < 64; ++guard) {
                    auto txt = cfire::build::read_file_text(fpath);
                    if (!txt) break;
                    auto chk = cfire::frontend::analyze_resilient(cfire::frontend::parse_source_resilient(fpath.string(), *txt).ast);
                    auto lines = cfire::diag::split_lines(*txt);
                    bool renamed = false;
                    for (const auto& d : chk.diagnostics) {
                        if (d.code != "cfire/FIELD-NAME-CASE") continue;
                        const std::string field = cfire::diag::fact_of(d, "field");
                        const std::string to = cfire::diag::fact_of(d, "to");
                        if (to.empty() || d.line < 1 || static_cast<std::size_t>(d.line) > lines.size()) continue;
                        const std::string& ln = lines[static_cast<std::size_t>(d.line - 1)];
                        auto pos = ln.find(field);
                        if (pos == std::string::npos) continue;
                        auto plan = cfire::refactor::plan_rename(fpath, d.line, static_cast<int>(pos) + 1, to);
                        if (!plan.error.empty() || !cfire::refactor::apply_rename(plan)) continue;
                        cfire::diag::FixItEdit note;
                        note.title = std::format("Rename field '{}' to '{}' across the workspace (cfire/FIELD-NAME-CASE)", field, to);
                        total_applied.push_back(std::move(note));
                        renamed = true;
                        break;
                    }
                    if (!renamed) break;
                }
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
