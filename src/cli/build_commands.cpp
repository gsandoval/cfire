// src/cli/build_commands.cpp — Translation Unit for `clean`, `emit-cpp`, `build`, `run`, `test`, `bench`, `eject`

#include "../build/apple_bundle.hpp"
#include "../build/binary_cache.hpp"
#include "../build/clean.hpp"
#include "../build/package_loader.hpp"
#include "../build/probe_cache.hpp"
#include "../build/test_cache.hpp"
#include "../buildsys.hpp"
#include "common_args.hpp"
#include "dispatch.hpp"
#include "usage.hpp"
#include "../codegen.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#if __has_include(<print>)
#include <print>
#endif
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace cfire::cli {

auto run_build_commands(std::string_view sub, int argc, char** argv, const std::filesystem::path& runtime_dir) -> int {
    if (sub == "clean") {
        bool clean_cache = false;
        bool clean_all = false;
        bool dry_run = false;
        bool json_mode = false;
        std::filesystem::path target_dir;
        bool saw_dir = false;
        for (int i = 2; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--cache") {
                clean_cache = true;
            } else if (arg == "--all") {
                clean_all = true;
            } else if (arg == "--dry-run") {
                dry_run = true;
            } else if (arg == "--json") {
                json_mode = true;
            } else if (arg.starts_with("-")) {
                std::println(stderr, "error: unknown flag '{}' for 'cfire clean' (expected --cache, --all, --dry-run, --json)", arg);
                return 2;
            } else if (!saw_dir) {
                target_dir = arg;
                saw_dir = true;
            } else {
                std::println(stderr, "error: unexpected argument '{}' for 'cfire clean'", arg);
                return 2;
            }
        }
        cfire::build::CleanOptions opts;
        opts.clean_project = clean_all || !clean_cache || saw_dir;
        opts.clean_cache = clean_all || clean_cache;
        opts.dry_run = dry_run;
        opts.json = json_mode;
        opts.has_explicit_target = saw_dir;
        opts.target_path = saw_dir ? target_dir : std::filesystem::path{};
        opts.runtime_dir = runtime_dir;

        auto rep = cfire::build::run_clean(opts);
        if (json_mode) {
            std::println("{}", cfire::build::format_clean_report_json(rep));
        } else {
            const char* verb = dry_run ? "Would remove" : "Removed";
            for (const auto& item : rep.removed) {
                std::println("  {} {} ({} bytes)", verb, item.path, item.bytes);
            }
            for (const auto& sk : rep.skipped) {
                std::println(stderr, "error: skipped '{}': {}", sk.path, sk.reason);
            }
            if (rep.ok) {
                std::println("✓ Cleaned ({} {} entries, {} bytes{})",
                    rep.entries_removed,
                    dry_run ? "planned" : "removed",
                    rep.bytes_freed,
                    dry_run ? ", dry run" : "");
            } else {
                std::println(stderr, "✗ Clean aborted or partially skipped ({} entries processed)", rep.entries_removed);
            }
        }
        return rep.ok ? 0 : 1;
    }

    cfire::cli::CommonCliArgs args;
    if (auto err = cfire::cli::parse_common_cli_args(sub, argc, argv, args)) {
        return *err;
    }

    if (sub == "emit-cpp") {
        auto res = cfire::build::analyze_and_emit(
            args.target, runtime_dir, false, false, args.target_triple, args.sysroot,
            args.web_profile, args.subsystem, args.sanitize,
            args.extra_link_libs, args.extra_pkg_configs, args.extra_includes, args.extra_defines
        );
        if (!res) {
            std::println(stderr, "{}", res.error());
            return 1;
        }
        for (const auto& w : res->second.warnings) {
            std::println(stderr, "{}", w);
        }
        std::print("{}", res->first);
        return 0;
    }

    if (sub == "build") {
        if (args.target.filename() == "build.cf") {
            auto parsed = cfire::build::parse_build_cf(args.target, args.target_triple);
            if (!parsed) {
                std::println(stderr, "error parsing build.cf: {}", parsed.error());
                return 1;
            }
            if (parsed->executables.empty()) {
                std::println(stderr, "error: no executables declared in {}", args.target.string());
                return 1;
            }

            bool build_all = args.build_target_filter.empty() || args.build_target_filter == "all";
            bool matched_any = false;
            bool has_var_name_match = false;
            if (!build_all) {
                for (const auto& exe : parsed->executables) {
                    if (exe.var_name == args.build_target_filter) {
                        has_var_name_match = true;
                        break;
                    }
                }
            }

            for (const auto& exe : parsed->executables) {
                if (!build_all) {
                    bool matches = has_var_name_match
                        ? (exe.var_name == args.build_target_filter)
                        : (exe.name == args.build_target_filter || exe.app_name == args.build_target_filter);
                    if (!matches) continue;
                }
                matched_any = true;

                std::vector<std::string> active_targets = exe.targets;
                if (!args.target_triple.empty()) {
                    active_targets = { args.target_triple };
                }

                std::filesystem::path entry_file = exe.entry;
                if (entry_file.is_relative()) {
                    entry_file = args.target.parent_path() / entry_file;
                }

                for (const auto& tgt : active_targets) {
                    bool is_wasm_target = (tgt == "wasm" || tgt == "web" || tgt.starts_with("wasm32"));
                    std::string effective_triple = is_wasm_target ? "wasm32-unknown-emscripten" : (tgt == "native" ? "" : tgt);
                    std::string effective_profile = is_wasm_target ? (!args.web_profile.empty() && args.web_profile != "auto" ? args.web_profile : (exe.web_profile.empty() ? (exe.subsystem == "gui" ? "canvas" : "auto") : exe.web_profile)) : args.web_profile;
                    std::string effective_subsystem = !args.subsystem.empty() && args.subsystem != "auto" ? args.subsystem : (exe.subsystem.empty() ? "auto" : exe.subsystem);
                    std::string effective_app_name = !args.app_name_override.empty() ? args.app_name_override : exe.app_name;
                    std::string effective_bundle_id = !args.bundle_id_override.empty() ? args.bundle_id_override : exe.bundle_id;
                    std::string effective_icon = !args.icon_path_override.empty() ? args.icon_path_override : exe.icon;

                    std::filesystem::path out_bin_path;
                    if (!args.custom_output_path.empty()) {
                        out_bin_path = args.custom_output_path;
                    } else {
                        std::filesystem::path base_out_dir = exe.out_dir.empty() ? args.target.parent_path() : (args.target.parent_path() / exe.out_dir);
                        if (is_wasm_target) {
                            out_bin_path = base_out_dir / (exe.name + ".html");
                        } else {
                            std::error_code ec;
                            auto candidate = base_out_dir / exe.name;
                            if (std::filesystem::is_directory(candidate, ec)) {
                                out_bin_path = candidate / exe.name;
                            } else {
                                out_bin_path = candidate;
                            }
                            if (cfire::build::is_windows_target(effective_triple) && out_bin_path.extension() != ".exe") {
                                out_bin_path += ".exe";
                            }
                        }
                    }

                    std::error_code ec;
                    std::filesystem::create_directories(out_bin_path.parent_path(), ec);

                    auto plan = cfire::build::build_native(
                        entry_file,
                        runtime_dir,
                        out_bin_path,
                        false,
                        args.sanitize,
                        effective_triple,
                        args.sysroot,
                        args.extra_link_libs,
                        args.extra_includes,
                        effective_profile,
                        effective_subsystem,
                        effective_app_name,
                        effective_bundle_id,
                        effective_icon,
                        args.extra_pkg_configs,
                        args.extra_defines
                    );
                    if (!plan) {
                        std::println(stderr, "error building target '{}' for executable '{}':\n{}",
                            tgt, exe.name, plan.error());
                        return 1;
                    }

                    if (is_wasm_target) {
                        std::println("✓ Built WebAssembly: {} (profile={})", out_bin_path.string(), plan->web_profile);
                    } else if (plan->is_ios) {
                        std::println("✓ Built iOS App Bundle: {} (target={})", plan->ios_app_bundle, plan->target_triple);
                        if (!plan->ios_ipa_path.empty()) {
                            std::println("✓ Created iOS IPA: {}", plan->ios_ipa_path);
                        }
                    } else {
                        std::string bundle_info = plan->macos_app_bundle.empty() ? "" : std::format(" -> {}", plan->macos_app_bundle);
                        std::println("✓ Built Native {}: {}{}",
                            plan->subsystem == "gui" ? "App" : "Binary",
                            out_bin_path.string(),
                            bundle_info);
                    }
                    for (const auto& note : plan->portable_notes) {
                        std::println(stderr, "note: {}", note);
                    }
                }
            }

            if (!matched_any) {
                std::println(stderr, "error: executable '{}' not found in {}", args.build_target_filter, args.target.string());
                return 1;
            }
            return 0;
        }

        std::string out_bin;
        if (!args.custom_output_path.empty()) {
            out_bin = args.custom_output_path;
        } else {
            out_bin = args.target.parent_path().filename().string();
            if (out_bin.empty() || out_bin == ".") out_bin = "cfire_app";
            std::error_code ec;
            if (std::filesystem::is_directory(out_bin, ec)) {
                out_bin = (std::filesystem::path(out_bin) / args.target.stem().string()).string();
            }
        }
        if (cfire::build::is_windows_target(args.target_triple)) {
            if (!out_bin.ends_with(".exe")) out_bin += ".exe";
        }
        if (args.target_triple.starts_with("wasm")) {
            if (!out_bin.ends_with(".html") && !out_bin.ends_with(".js")) {
                out_bin += ".html";
            }
        }
        auto plan = cfire::build::build_native(
            args.target,
            runtime_dir,
            out_bin,
            false,
            args.sanitize,
            args.target_triple,
            args.sysroot,
            args.extra_link_libs,
            args.extra_includes,
            args.web_profile,
            args.subsystem,
            args.app_name_override,
            args.bundle_id_override,
            args.icon_path_override,
            args.extra_pkg_configs,
            args.extra_defines
        );
        if (!plan) {
            std::println(stderr, "{}", plan.error());
            return 1;
        }
        for (const auto& note : plan->portable_notes) {
            std::println(stderr, "note: {}", note);
        }
        std::string target_info = plan->target_triple.empty() ? "" : std::format(", target={}", plan->target_triple);
        std::println("✓ Built {} (generated {}, pch={}, sanitize={}{})",
            out_bin, plan->generated_cpp, plan->used_pch, plan->sanitized, target_info);
        if (!plan->macos_app_bundle.empty()) {
            std::println("✓ Created macOS App Bundle: {}", plan->macos_app_bundle);
        }
        if (!plan->ios_app_bundle.empty()) {
            std::println("✓ Created iOS App Bundle: {}", plan->ios_app_bundle);
        }
        if (!plan->ios_ipa_path.empty()) {
            std::println("✓ Created iOS IPA: {}", plan->ios_ipa_path);
        }
        return 0;
    }

    if (sub == "run" || sub == "test" || sub == "bench") {
        bool is_test = (sub == "test" || sub == "bench");
        const bool allow_test_cache = is_test &&
            !cfire::build::g_no_binary_cache &&
            !args.bench_mode &&
            !args.force_fuzz &&
            args.fuzz_runs_arg.empty() &&
            args.fuzz_seed_arg.empty() &&
            args.target_triple.empty();
        auto accumulate_json_suite = [](std::string_view out, int& total_passed, int& total_failed, std::string& combined_tests_json) {
            if (auto p_pos = out.find("\"passed\": "); p_pos != std::string_view::npos) {
                p_pos += 10;
                auto p_end = out.find_first_of(",}", p_pos);
                if (auto v = cfire::detail::parse_int<int>(out.substr(p_pos, p_end - p_pos))) {
                    total_passed += *v;
                }
            }
            if (auto f_pos = out.find("\"failed\": "); f_pos != std::string_view::npos) {
                f_pos += 10;
                auto f_end = out.find_first_of(",}", f_pos);
                if (auto v = cfire::detail::parse_int<int>(out.substr(f_pos, f_end - f_pos))) {
                    total_failed += *v;
                }
            }
            if (auto t_pos = out.find("\"tests\": ["); t_pos != std::string_view::npos) {
                t_pos += 10;
                auto t_end = out.rfind("]}");
                if (t_end != std::string_view::npos && t_end > t_pos) {
                    std::string inner(out.substr(t_pos, t_end - t_pos));
                    if (!inner.empty()) {
                        if (!combined_tests_json.empty()) combined_tests_json += ", ";
                        combined_tests_json += inner;
                    }
                }
            }
        };
        if (is_test) {
            if (args.json_mode) {
                args.run_args.emplace_back("--cfire-test-json");
            }
            if (args.bench_mode) {
                args.run_args.emplace_back("--cfire-bench");
            }
            if (!args.filter_pattern.empty()) {
                args.run_args.emplace_back(std::format("--cfire-test-filter={}", args.filter_pattern));
            }
            if (!args.fuzz_runs_arg.empty()) {
                args.run_args.emplace_back(std::format("--cfire-fuzz-runs={}", args.fuzz_runs_arg));
            }
            if (!args.fuzz_seed_arg.empty()) {
                args.run_args.emplace_back(std::format("--cfire-seed={}", args.fuzz_seed_arg));
            }
            if (!args.bench_time_arg.empty()) {
                args.run_args.emplace_back(std::format("--cfire-bench-time={}", args.bench_time_arg));
            }

            std::error_code test_ec;
            if (std::filesystem::is_directory(args.target, test_ec)) {
                // Discover all test suites in directory (ERG-01a), grouped by (directory, package_name)
                std::vector<std::filesystem::path> all_cf;
                for (const auto& entry : std::filesystem::recursive_directory_iterator(args.target, test_ec)) {
                    if (!entry.is_regular_file() || entry.path().extension() != ".cf") continue;
                    if (entry.path().filename() == "build.cf") continue;
                    if (cfire::cli::is_ignored_walk_dir(entry.path())) continue;
                    all_cf.push_back(entry.path());
                }
                std::sort(all_cf.begin(), all_cf.end());

                // Prefer `*_test.cf` files first within each directory so they act as the primary suite entry
                std::stable_sort(all_cf.begin(), all_cf.end(), [](const auto& a, const auto& b) {
                    bool a_test = a.filename().string().ends_with("_test.cf");
                    bool b_test = b.filename().string().ends_with("_test.cf");
                    if (a.parent_path() == b.parent_path() && a_test != b_test) {
                        return a_test > b_test;
                    }
                    return false;
                });

                std::unordered_set<std::string> seen_pkg_suites;
                std::vector<std::filesystem::path> suite_entries;
                for (const auto& fpath : all_cf) {
                    auto raw = cfire::build::read_file_text(fpath);
                    if (!raw) continue;
                    auto ast = cfire::frontend::parse_source(fpath.string(), *raw);
                    if (!ast) continue;
                    cfire::frontend::append_doctests(fpath.string(), *raw, *ast);
                    bool is_test_name = fpath.filename().string().ends_with("_test.cf");
                    if (!is_test_name && ast->tests.empty()) continue;

                    std::string pkg_key = std::filesystem::weakly_canonical(fpath.parent_path(), test_ec).string() + "::" + ast->package_name;
                    if (!ast->package_name.empty()) {
                        if (seen_pkg_suites.contains(pkg_key)) continue;
                        seen_pkg_suites.insert(pkg_key);
                    }
                    suite_entries.push_back(fpath);
                }

                if (suite_entries.empty()) {
                    if (args.json_mode) {
                        std::println("{{\"success\": true, \"passed\": 0, \"failed\": 0, \"tests\": []}}");
                    } else {
                        std::println("✓ No test suites found in {}", args.target.string());
                    }
                    return 0;
                }

                std::vector<std::expected<cfire::build::TestSuiteDeps, std::string>> suite_deps;
                suite_deps.reserve(suite_entries.size());
                for (const auto& sf : suite_entries) {
                    suite_deps.push_back(cfire::build::collect_test_suite_deps(
                        sf, runtime_dir, args.target_triple, args.sysroot,
                        args.extra_includes, args.extra_pkg_configs, args.extra_defines));
                }

                if (args.changed_only) {
                    std::vector<std::filesystem::path> changed_files;
                    bool used_git = false;
                    (void)cfire::build::detect_changed_workspace_files(args.target, changed_files, used_git);
                    std::unordered_set<std::string> changed_canon_set;
                    for (const auto& ch : changed_files) {
                        changed_canon_set.insert(cfire::build::canonical_or_abs_path(ch).generic_string());
                    }
                    std::vector<std::filesystem::path> filtered_entries;
                    std::vector<std::expected<cfire::build::TestSuiteDeps, std::string>> filtered_deps;
                    for (std::size_t i = 0; i < suite_entries.size(); ++i) {
                        if (!suite_deps[i].has_value() ||
                            cfire::build::suite_affected_by_changes(*suite_deps[i], changed_canon_set)) {
                            filtered_entries.push_back(std::move(suite_entries[i]));
                            filtered_deps.push_back(std::move(suite_deps[i]));
                        }
                    }
                    suite_entries = std::move(filtered_entries);
                    suite_deps = std::move(filtered_deps);
                    if (suite_entries.empty()) {
                        if (args.json_mode) {
                            std::println("{{\"success\": true, \"passed\": 0, \"failed\": 0, \"tests\": []}}");
                        } else {
                            std::println("✓ No test suites affected by current changes");
                        }
                        return 0;
                    }
                }

                int total_passed = 0;
                int total_failed = 0;
                int suite_failures = 0;
                std::string combined_tests_json;
                std::unordered_map<std::string, std::string> passed_file_hashes;

                for (std::size_t idx = 0; idx < suite_entries.size(); ++idx) {
                    const auto& suite_file = suite_entries[idx];
                    if (!args.json_mode && suite_entries.size() > 1) {
                        std::println("==> Suite {}/{}: {}", idx + 1, suite_entries.size(), suite_file.string());
                    }
                    std::string cache_key;
                    if (suite_deps[idx].has_value()) {
                        cache_key = cfire::build::compute_test_cache_key(
                            *suite_deps[idx], runtime_dir, args.sanitize, args.target_triple, args.sysroot,
                            args.extra_link_libs, args.extra_includes, args.extra_pkg_configs, args.extra_defines,
                            args.filter_pattern, args.json_mode);
                    }
                    if (allow_test_cache && !cache_key.empty()) {
                        std::string cached_out;
                        if (cfire::build::TestResultCache::try_load(cache_key, cached_out)) {
                            for (const auto& [fp, fh] : suite_deps[idx]->file_hashes) {
                                passed_file_hashes[fp] = fh;
                            }
                            if (args.json_mode) {
                                accumulate_json_suite(cached_out, total_passed, total_failed, combined_tests_json);
                            } else {
                                std::string formatted = cfire::build::TestResultCache::format_cached_output(cached_out, false);
                                std::fwrite(formatted.data(), 1, formatted.size(), stdout);
                                std::fflush(stdout);
                            }
                            continue;
                        }
                    }
                    const char* bin_ext = cfire::build::is_windows_target(args.target_triple) ? ".exe" : "";
                    // Build and run from a private 0700 dir so no other user can swap the binary.
                    cfire::detail::SecureTempDir suite_dir("cfire-test");
                    auto tmp_bin = suite_dir.file(std::format("suite-{}{}", idx, bin_ext));
                    auto plan = cfire::build::build_native(
                        suite_file, runtime_dir, tmp_bin, true, args.sanitize, args.target_triple,
                        args.sysroot, args.extra_link_libs, args.extra_includes, args.web_profile, args.subsystem,
                        args.app_name_override, args.bundle_id_override, args.icon_path_override, args.extra_pkg_configs, args.extra_defines
                    );
                    if (!plan) {
                        ++suite_failures;
                        if (!args.json_mode) {
                            std::println(stderr, "{}", plan.error());
                        }
                        continue;
                    }
                    std::string cmd = cfire::bridge::shell_quote(tmp_bin.string());
                    for (const auto& arg : args.run_args) {
                        cmd += " " + cfire::bridge::shell_quote(arg);
                    }
                    if (args.json_mode) {
                        int rc = 0;
                        std::string out = cfire::bridge::run_command_capture(cmd, rc);
                        std::filesystem::remove(tmp_bin, test_ec);
                        if (rc != 0) {
                            ++suite_failures;
                        } else {
                            if (allow_test_cache && !cache_key.empty()) {
                                cfire::build::TestResultCache::store_pass(cache_key, out);
                            }
                            if (suite_deps[idx].has_value()) {
                                for (const auto& [fp, fh] : suite_deps[idx]->file_hashes) {
                                    passed_file_hashes[fp] = fh;
                                }
                            }
                        }
                        accumulate_json_suite(out, total_passed, total_failed, combined_tests_json);
                    } else {
                        int rc = 0;
                        std::string out = cfire::bridge::run_command_capture(cmd, rc, false);
                        if (!out.empty()) {
                            std::fwrite(out.data(), 1, out.size(), stdout);
                            std::fflush(stdout);
                        }
                        std::filesystem::remove(tmp_bin, test_ec);
                        if (rc != 0) {
                            ++suite_failures;
                        } else {
                            if (allow_test_cache && !cache_key.empty()) {
                                cfire::build::TestResultCache::store_pass(cache_key, out);
                            }
                            if (suite_deps[idx].has_value()) {
                                for (const auto& [fp, fh] : suite_deps[idx]->file_hashes) {
                                    passed_file_hashes[fp] = fh;
                                }
                            }
                        }
                    }
                }

                if (!passed_file_hashes.empty()) {
                    cfire::build::record_workspace_file_hashes(args.target, passed_file_hashes);
                }

                if (args.json_mode) {
                    bool ok = (suite_failures == 0 && total_failed == 0);
                    std::println("{{\"success\": {}, \"passed\": {}, \"failed\": {}, \"tests\": [{}]}}",
                        ok ? "true" : "false", total_passed, total_failed, combined_tests_json);
                } else if (suite_entries.size() > 1) {
                    if (suite_failures == 0) {
                        std::println("✓ All {} test suite(s) passed", suite_entries.size());
                    } else {
                        std::println(stderr, "✗ {} of {} test suite(s) failed", suite_failures, suite_entries.size());
                    }
                }
                return suite_failures > 0 ? 1 : 0;
            }
        }

        if (!is_test && args.target.filename() == "build.cf") {
            bool is_wasm_run = args.target_triple.starts_with("wasm");
            bool is_ios_run = cfire::build::is_ios_target(args.target_triple);
            auto bcf_res = cfire::build::resolve_build_target(args.target, args.build_target_filter, is_wasm_run);
            if (!bcf_res) {
                std::println(stderr, "error: no matching executable found in {}", args.target.string());
                return 1;
            }

            if (is_ios_run) {
                if (args.ios_device) {
                    const std::string eff_sign = !args.sign_identity.empty() ? args.sign_identity : bcf_res->exe.sign_identity;
                    if (eff_sign.empty() || eff_sign == "-") {
                        std::println(stderr,
                            "error: running on a physical iOS device (`--ios-device`) requires a non-adhoc Apple signing identity "
                            "(`SignIdentity: \"Apple Development: ...\"` in build.cf or `--sign <identity>`)");
                        return 1;
                    }
                }
                const std::string run_triple = args.ios_device
                    ? "ios"
                    : ((args.target_triple == "ios" || args.target_triple == "ios-sim") ? "ios-sim" : args.target_triple);
                const auto ios_bin = bcf_res->base_out_dir / (args.ios_device ? "ios" : "ios-sim") / bcf_res->exe.name;
                std::error_code ec;
                std::filesystem::create_directories(ios_bin.parent_path(), ec);
                std::println("==> Building {} ({} target from build.cf)...",
                    bcf_res->exe.name, args.ios_device ? "iOS Device" : "iOS Simulator");
                auto plan = cfire::build::build_native(
                    bcf_res->entry_file,
                    runtime_dir,
                    ios_bin,
                    false,
                    false,
                    run_triple,
                    args.sysroot,
                    args.extra_link_libs,
                    args.extra_includes,
                    bcf_res->exe.web_profile,
                    bcf_res->exe.subsystem,
                    !args.app_name_override.empty() ? args.app_name_override : bcf_res->exe.app_name,
                    !args.bundle_id_override.empty() ? args.bundle_id_override : bcf_res->exe.bundle_id,
                    !args.icon_path_override.empty() ? args.icon_path_override : bcf_res->exe.icon,
                    args.extra_pkg_configs,
                    args.extra_defines
                );
                if (!plan) {
                    std::println(stderr, "error building iOS target: {}", plan.error());
                    return 1;
                }
                std::string bid = !args.bundle_id_override.empty() ? args.bundle_id_override
                    : (!plan->bundle_id.empty() ? plan->bundle_id
                    : (!bcf_res->exe.bundle_id.empty() ? bcf_res->exe.bundle_id : ("com.cfire." + bcf_res->exe.name)));
                auto launch_res = args.ios_device
                    ? cfire::build::launch_ios_device(plan->ios_app_bundle, bid, plan->sign_identity, args.ios_device_override, args.run_args)
                    : cfire::build::launch_ios_simulator(plan->ios_app_bundle, bid, args.ios_device_override, args.run_args);
                if (!launch_res) {
                    std::println(stderr, "{}", launch_res.error());
                    return 1;
                }
                return *launch_res;
            }

            if (is_wasm_run) {
                std::println("==> Building {} (WebAssembly target from build.cf)...", bcf_res->exe.name);
                std::error_code ec;
                std::filesystem::create_directories(bcf_res->wasm_html.parent_path(), ec);
                auto plan = cfire::build::build_native(
                    bcf_res->entry_file,
                    runtime_dir,
                    bcf_res->wasm_html,
                    false,
                    false,
                    "wasm32-unknown-emscripten",
                    "",
                    {},
                    {},
                    bcf_res->exe.web_profile.empty() ? (bcf_res->exe.subsystem == "gui" ? "canvas" : "auto") : bcf_res->exe.web_profile,
                    bcf_res->exe.subsystem.empty() ? "auto" : bcf_res->exe.subsystem,
                    bcf_res->exe.app_name,
                    bcf_res->exe.bundle_id,
                    bcf_res->exe.icon,
                    args.extra_pkg_configs,
                    args.extra_defines
                );
                if (!plan) {
                    std::println(stderr, "error auto-building wasm target: {}", plan.error());
                    return 1;
                }
                std::println("✓ Built WebAssembly: {} (profile={})", bcf_res->wasm_html.string(), plan->web_profile);
                return cfire::cli::run_dev_server_command(
                    bcf_res->wasm_html.parent_path(), args.web_port, args.open_browser,
                    bcf_res->wasm_html.filename().string()
                );
            }

            // Native run — always delegate to build_native() so BinaryCache hits cleanly and source/bundle/profile edits never run stale binaries
            [[maybe_unused]] bool is_gui = (bcf_res->exe.subsystem == "gui");
            {
                std::println("==> Building {} (Native target from build.cf)...", bcf_res->exe.name);
                std::error_code ec;
                std::filesystem::create_directories(bcf_res->native_bin.parent_path(), ec);
                auto plan = cfire::build::build_native(
                    bcf_res->entry_file,
                    runtime_dir,
                    bcf_res->native_bin,
                    false,
                    args.sanitize,
                    args.target_triple,
                    args.sysroot,
                    args.extra_link_libs,
                    args.extra_includes,
                    bcf_res->exe.web_profile,
                    bcf_res->exe.subsystem,
                    bcf_res->exe.app_name,
                    bcf_res->exe.bundle_id,
                    bcf_res->exe.icon,
                    args.extra_pkg_configs,
                    args.extra_defines
                );
                if (!plan) {
                    std::println(stderr, "error building native target: {}", plan.error());
                    return 1;
                }
                std::string bundle_info = plan->macos_app_bundle.empty() ? "" : std::format(" -> {}", plan->macos_app_bundle);
                std::println("✓ Built Native {}: {}{}",
                    plan->subsystem == "gui" ? "App" : "Binary",
                    bcf_res->native_bin.string(),
                    bundle_info);
            }

#if defined(__APPLE__)
            if (is_gui && !bcf_res->macos_app_bundle.empty() && std::filesystem::exists(bcf_res->macos_app_bundle)) {
                std::println("==> Launching macOS App: {}", bcf_res->macos_app_bundle.string());
                std::string open_cmd = "open " + cfire::bridge::shell_quote(bcf_res->macos_app_bundle.string());
                int rc = std::system(open_cmd.c_str());
                return rc == 0 ? 0 : 1;
            }
#endif
            std::string cmd = cfire::bridge::shell_quote(bcf_res->native_bin.string());
            for (const auto& arg : args.run_args) {
                cmd += " " + cfire::bridge::shell_quote(arg);
            }
            std::string sys_cmd = cfire::bridge::win32_quote_cmd(cmd);
            int rc = std::system(sys_cmd.c_str());
            return cfire::cli::unwrap_system_exit_code(rc);
        }

        if (args.target_triple.starts_with("wasm")) {
            cfire::detail::SecureTempDir wasm_dir("cfire-wasm");
            auto tmp_html = wasm_dir.file("index.html");
            auto js_file = tmp_html;
            js_file.replace_extension(".js");
            auto wasm_file = tmp_html;
            wasm_file.replace_extension(".wasm");
            auto cleanup_wasm = [&]() {
                std::error_code ec;
                std::filesystem::remove(tmp_html, ec);
                std::filesystem::remove(js_file, ec);
                std::filesystem::remove(wasm_file, ec);
            };
            auto plan = cfire::build::build_native(
                args.target, runtime_dir, tmp_html, is_test, args.sanitize, args.target_triple,
                args.sysroot, args.extra_link_libs, args.extra_includes, args.web_profile, args.subsystem,
                args.app_name_override, args.bundle_id_override, args.icon_path_override, args.extra_pkg_configs, args.extra_defines
            );
            if (!plan) {
                std::println(stderr, "{}", plan.error());
                return 1;
            }
            if (plan->web_profile == "cli") {
                int has_node = 0;
#if defined(_WIN32)
                (void)cfire::bridge::run_command_capture("where node 2>NUL", has_node);
#else
                (void)cfire::bridge::run_command_capture("which node 2>/dev/null", has_node);
#endif
                if (has_node == 0) {
                    std::string cmd = "node " + cfire::bridge::shell_quote(js_file.string());
                    for (const auto& arg : args.run_args) {
                        cmd += " " + cfire::bridge::shell_quote(arg);
                    }
                    std::string sys_cmd = cfire::bridge::win32_quote_cmd(cmd);
                    int rc = std::system(sys_cmd.c_str());
                    cleanup_wasm();
                    return cfire::cli::unwrap_system_exit_code(rc);
                }
            }
            int srv_rc = cfire::cli::run_dev_server_command(tmp_html, args.web_port, args.open_browser);
            cleanup_wasm();
            return srv_rc;
        }

        if (cfire::build::is_ios_target(args.target_triple)) {
            if (args.ios_device && (args.sign_identity.empty() || args.sign_identity == "-")) {
                std::error_code bec;
                auto parent_dir = args.target.parent_path().empty() ? std::filesystem::path(".") : args.target.parent_path();
                if (!std::filesystem::exists(parent_dir / "build.cf", bec)) {
                    std::println(stderr,
                        "error: running on a physical iOS device (`--ios-device`) requires a non-adhoc Apple signing identity "
                        "(`SignIdentity: \"Apple Development: ...\"` in build.cf or `--sign <identity>`)");
                    return 1;
                }
            }
            const std::string run_triple = args.ios_device
                ? "ios"
                : ((args.target_triple == "ios" || args.target_triple == "ios-sim") ? "ios-sim" : args.target_triple);
            std::string app_stem = args.target.parent_path().filename().string();
            if (app_stem.empty() || app_stem == ".") app_stem = args.target.stem().string();
            if (app_stem == "main") app_stem = "cfire_ios_app";
            // Private 0700 dir; kept after return because the simulator install reads from it.
            cfire::detail::SecureTempDir ios_dir("cfire-ios");
            auto tmp_ios_dir = ios_dir.release();
            std::error_code ec;
            auto tmp_ios_bin = tmp_ios_dir / app_stem;
            auto plan = cfire::build::build_native(
                args.target,
                runtime_dir,
                tmp_ios_bin,
                is_test,
                false,
                run_triple,
                args.sysroot,
                args.extra_link_libs,
                args.extra_includes,
                args.web_profile,
                args.subsystem,
                args.app_name_override,
                args.bundle_id_override,
                args.icon_path_override,
                args.extra_pkg_configs,
                args.extra_defines
            );
            if (!plan) {
                std::println(stderr, "{}", plan.error());
                std::filesystem::remove_all(tmp_ios_dir, ec);
                return 1;
            }
            std::string bid = !args.bundle_id_override.empty() ? args.bundle_id_override
                : (!plan->bundle_id.empty() ? plan->bundle_id : ("com.cfire." + app_stem));
            auto launch_res = args.ios_device
                ? cfire::build::launch_ios_device(plan->ios_app_bundle, bid, plan->sign_identity, args.ios_device_override, args.run_args)
                : cfire::build::launch_ios_simulator(plan->ios_app_bundle, bid, args.ios_device_override, args.run_args);
            std::filesystem::remove_all(tmp_ios_dir, ec);
            if (!launch_res) {
                std::println(stderr, "{}", launch_res.error());
                return 1;
            }
            return *launch_res;
        }

        std::optional<cfire::build::TestSuiteDeps> single_test_deps;
        std::string single_test_cache_key;
        auto single_ws_root = args.target.parent_path().empty() ? std::filesystem::path(".") : args.target.parent_path();
        if (is_test) {
            if (auto deps_res = cfire::build::collect_test_suite_deps(
                    args.target, runtime_dir, args.target_triple, args.sysroot,
                    args.extra_includes, args.extra_pkg_configs, args.extra_defines)) {
                single_test_deps = std::move(*deps_res);
                if (args.changed_only) {
                    std::vector<std::filesystem::path> changed_files;
                    bool used_git = false;
                    (void)cfire::build::detect_changed_workspace_files(single_ws_root, changed_files, used_git);
                    std::unordered_set<std::string> changed_canon_set;
                    for (const auto& ch : changed_files) {
                        changed_canon_set.insert(cfire::build::canonical_or_abs_path(ch).generic_string());
                    }
                    if (!cfire::build::suite_affected_by_changes(*single_test_deps, changed_canon_set)) {
                        if (args.json_mode) {
                            std::println("{{\"success\": true, \"passed\": 0, \"failed\": 0, \"tests\": []}}");
                        } else {
                            std::println("✓ {} is unaffected by current changes", args.target.string());
                        }
                        return 0;
                    }
                }
                single_test_cache_key = cfire::build::compute_test_cache_key(
                    *single_test_deps, runtime_dir, args.sanitize, args.target_triple, args.sysroot,
                    args.extra_link_libs, args.extra_includes, args.extra_pkg_configs, args.extra_defines,
                    args.filter_pattern, args.json_mode);
                if (allow_test_cache && !single_test_cache_key.empty()) {
                    std::string cached_out;
                    if (cfire::build::TestResultCache::try_load(single_test_cache_key, cached_out)) {
                        cfire::build::record_workspace_file_hashes(single_ws_root, single_test_deps->file_hashes);
                        std::string formatted = cfire::build::TestResultCache::format_cached_output(cached_out, args.json_mode);
                        std::fwrite(formatted.data(), 1, formatted.size(), stdout);
                        std::fflush(stdout);
                        return 0;
                    }
                }
            }
        }

        const char* bin_ext = cfire::build::is_windows_target(args.target_triple) ? ".exe" : "";
        // Build and run from a private 0700 dir so no other user can swap the binary.
        cfire::detail::SecureTempDir run_dir(std::format("cfire-{}", sub));
        auto tmp_bin = run_dir.file(std::format("{}{}", sub, bin_ext));
        auto plan = cfire::build::build_native(
            args.target, runtime_dir, tmp_bin, is_test, args.sanitize, args.target_triple,
            args.sysroot, args.extra_link_libs, args.extra_includes, args.web_profile, args.subsystem,
            args.app_name_override, args.bundle_id_override, args.icon_path_override, args.extra_pkg_configs, args.extra_defines
        );
        if (!plan) {
            std::println(stderr, "{}", plan.error());
            return 1;
        }
        std::string cmd = cfire::bridge::shell_quote(tmp_bin.string());
        for (const auto& arg : args.run_args) {
            cmd += " " + cfire::bridge::shell_quote(arg);
        }
        if (is_test) {
            int rc = 0;
            std::string out = cfire::bridge::run_command_capture(cmd, rc, false);
            if (!out.empty()) {
                std::fwrite(out.data(), 1, out.size(), stdout);
                std::fflush(stdout);
            }
            std::error_code rm_ec;
            std::filesystem::remove(tmp_bin, rm_ec);
            if (rc == 0) {
                if (allow_test_cache && !single_test_cache_key.empty()) {
                    cfire::build::TestResultCache::store_pass(single_test_cache_key, out);
                }
                if (single_test_deps.has_value()) {
                    cfire::build::record_workspace_file_hashes(single_ws_root, single_test_deps->file_hashes);
                }
            }
            return rc;
        }
        std::string sys_cmd = cfire::bridge::win32_quote_cmd(cmd);
        int rc = std::system(sys_cmd.c_str());
        // Windows can still hold the just-exited .exe open (loader, antivirus); a failed delete must not turn the
        // program's exit code into a crash. `run_dir` removes what is left.
        std::error_code rm_ec;
        std::filesystem::remove(tmp_bin, rm_ec);
        return cfire::cli::unwrap_system_exit_code(rc);
    }

    if (sub == "eject") {
        std::filesystem::path out_dir = !args.run_args.empty() ? std::filesystem::path(args.run_args[0]) : std::filesystem::path("ejected_cpp");
        auto res = cfire::build::eject_cmake(args.target, runtime_dir, out_dir, args.extra_pkg_configs, args.extra_defines);
        if (!res) {
            std::println(stderr, "{}", res.error());
            return 1;
        }
        std::println("✓ Ejected standalone C++23 + CMakeLists.txt to {}", out_dir.string());
        return 0;
    }

    cfire::cli::print_usage();
    return 1;
}

} // namespace cfire::cli
