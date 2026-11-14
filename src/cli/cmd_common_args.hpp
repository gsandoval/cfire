#pragma once
// src/cli/cmd_common_args.hpp — Shared Flag Parsing, BuildProfile Selection & Target Discovery

#include "../build/pch.hpp"
#include "../build/toolchain_layout.hpp"
#include "usage.hpp"

#include <algorithm>
#include <filesystem>
#if __has_include(<print>)
#include <print>
#endif
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cfire::cli {

struct CommonCliArgs {
    bool sanitize = false;
    bool json_mode = false;
    bool fix_mode = false;
    bool dry_run = false;
    bool verify_cpp = false;
    bool explicit_release = false;
    std::string fix_only_codes;
    bool bench_mode = false;
    std::string filter_pattern;
    std::string fuzz_runs_arg;
    std::string fuzz_seed_arg;
    std::string bench_time_arg;
    std::string custom_output_path;
    std::string target_triple;
    std::string sysroot;
    std::string web_profile = "auto";
    std::string subsystem = "auto";
    std::string app_name_override;
    std::string bundle_id_override;
    std::string icon_path_override;
    std::string ios_device_override;
    std::string build_target_filter;
    int web_port = 8080;
    bool open_browser = true;
    std::vector<std::string> extra_link_libs;
    std::vector<std::string> extra_includes;
    std::filesystem::path target;
    std::vector<std::string> run_args;
};

[[nodiscard]] inline auto levenshtein_dist(std::string_view s1, std::string_view s2) -> std::size_t {
    std::vector<std::size_t> v0(s2.size() + 1);
    std::vector<std::size_t> v1(s2.size() + 1);
    for (std::size_t i = 0; i <= s2.size(); ++i) v0[i] = i;
    for (std::size_t i = 0; i < s1.size(); ++i) {
        v1[0] = i + 1;
        for (std::size_t j = 0; j < s2.size(); ++j) {
            std::size_t cost = (s1[i] == s2[j]) ? 0 : 1;
            v1[j + 1] = std::min({v1[j] + 1, v0[j + 1] + 1, v0[j] + cost});
        }
        v0 = v1;
    }
    return v0[s2.size()];
}

[[nodiscard]] inline auto parse_common_cli_args(
    std::string_view sub,
    int argc,
    char** argv,
    CommonCliArgs& out
) -> std::optional<int> {
    if (argc < 3) {
        if (sub != "build" && sub != "run" && sub != "test" && sub != "bench" && sub != "check" && sub != "fix") {
            print_usage();
            return 1;
        }
    }

    cfire::build::set_build_profile(cfire::build::BuildProfile::Release);
    cfire::build::g_profile_mode = false;
    cfire::build::g_no_trap = false;
    cfire::build::g_emit_dsym = false;
    cfire::build::g_no_binary_cache = false;
    cfire::build::g_no_shard = false;
    cfire::build::g_verbose_mode = false;
    cfire::build::g_sanitizer_set = "address,undefined";
    cfire::build::explicit_entry_file_only() = false;

    out = CommonCliArgs{};
    out.bench_mode = (sub == "bench");

    bool saw_debug = false;
    bool saw_release = false;
    bool after_dashdash = false;

    for (int i = 2; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (after_dashdash) {
            out.run_args.emplace_back(arg);
            continue;
        }
        if (arg == "--") {
            after_dashdash = true;
            continue;
        }
        if (arg == "--debug" || arg == "-g") {
            saw_debug = true;
            continue;
        }
        if (arg == "--release" || arg == "-r") {
            saw_release = true;
            continue;
        }
        if (arg == "--no-trap") {
            cfire::build::g_no_trap = true;
            continue;
        }
        if (arg == "--dsym") {
            cfire::build::g_emit_dsym = true;
            continue;
        }
        if (arg == "--no-cache") {
            cfire::build::g_no_binary_cache = true;
            continue;
        }
        if (arg == "--no-shard") {
            cfire::build::g_no_shard = true;
            continue;
        }
        if (arg == "--verbose" || arg == "-v") {
            cfire::build::g_verbose_mode = true;
            continue;
        }
        if (arg == "--profile") {
            cfire::build::g_profile_mode = true;
            continue;
        }
        if (arg == "--sanitize" || arg == "--sanitize=address") {
            out.sanitize = true;
            cfire::build::g_sanitizer_set = "address,undefined";
            continue;
        }
        if (arg == "--sanitize=thread") {
            out.sanitize = true;
            cfire::build::g_sanitizer_set = "thread";
            continue;
        }
        if (arg == "--json") {
            out.json_mode = true;
            continue;
        }
        if (arg == "--fix") {
            out.fix_mode = true;
            continue;
        }
        if (arg == "--dry-run") {
            out.dry_run = true;
            continue;
        }
        if (arg == "--verify-cpp" || arg == "--full") {
            out.verify_cpp = true;
            continue;
        }
        if (arg == "--only" && i + 1 < argc) {
            // `cfire fix --only cfire/RETIRED-API[,code...]`: apply only fixes for these diagnostic codes.
            out.fix_only_codes = argv[++i];
            continue;
        }
        if (arg == "--bench" || arg == "-bench") {
            out.bench_mode = true;
            continue;
        }
        if ((arg == "--fuzz-runs" || arg == "-fuzz-runs") && i + 1 < argc) {
            out.fuzz_runs_arg = argv[++i];
            continue;
        }
        if (arg.starts_with("--fuzz-runs=")) {
            out.fuzz_runs_arg = std::string(arg.substr(12));
            continue;
        }
        if ((arg == "--seed" || arg == "-seed") && i + 1 < argc) {
            out.fuzz_seed_arg = argv[++i];
            continue;
        }
        if (arg.starts_with("--seed=")) {
            out.fuzz_seed_arg = std::string(arg.substr(7));
            continue;
        }
        if ((arg == "--bench-time" || arg == "-bench-time") && i + 1 < argc) {
            out.bench_time_arg = argv[++i];
            continue;
        }
        if (arg.starts_with("--bench-time=")) {
            out.bench_time_arg = std::string(arg.substr(13));
            continue;
        }
        if ((arg == "--filter" || arg == "-filter") && i + 1 < argc) {
            out.filter_pattern = argv[++i];
            continue;
        }
        if (arg.starts_with("--filter=")) {
            out.filter_pattern = arg.substr(9);
            continue;
        }
        if (arg.starts_with("-filter=")) {
            out.filter_pattern = arg.substr(8);
            continue;
        }
        if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            out.custom_output_path = argv[++i];
            continue;
        }
        if (arg.starts_with("--output=")) {
            out.custom_output_path = arg.substr(9);
            continue;
        }
        if (arg.starts_with("-o=")) {
            out.custom_output_path = arg.substr(3);
            continue;
        }
        if ((arg == "--icon" || arg == "-icon") && i + 1 < argc) {
            out.icon_path_override = argv[++i];
            continue;
        }
        if (arg.starts_with("--icon=")) {
            out.icon_path_override = arg.substr(7);
            continue;
        }
        if ((arg == "--name" || arg == "--app-name") && i + 1 < argc) {
            out.app_name_override = argv[++i];
            continue;
        }
        if (arg.starts_with("--name=")) {
            out.app_name_override = arg.substr(7);
            continue;
        }
        if (arg.starts_with("--app-name=")) {
            out.app_name_override = arg.substr(11);
            continue;
        }
        if ((arg == "--bundle-id" || arg == "-bundle-id") && i + 1 < argc) {
            out.bundle_id_override = argv[++i];
            continue;
        }
        if (arg.starts_with("--bundle-id=")) {
            out.bundle_id_override = arg.substr(12);
            continue;
        }
        if (arg == "--target=wasm" || arg == "-target=wasm" || arg == "--target=web" || arg == "-target=web" || arg == "--web" || arg == "-web") {
            out.target_triple = "wasm32-unknown-emscripten";
            continue;
        }
        if (arg == "--ios" || arg == "-ios" || arg == "--ios-sim" || arg == "-ios-sim" ||
            arg == "--target=ios-sim" || arg == "-target=ios-sim") {
            out.target_triple = "ios-sim";
            continue;
        }
        if (arg == "--target=ios" || arg == "-target=ios") {
            out.target_triple = "ios";
            continue;
        }
        if ((arg == "--device" || arg == "-device") && i + 1 < argc) {
            out.ios_device_override = argv[++i];
            continue;
        }
        if (arg.starts_with("--device=")) {
            out.ios_device_override = arg.substr(9);
            continue;
        }
        if (arg.starts_with("--web-profile=")) {
            out.web_profile = arg.substr(14);
            continue;
        }
        if (arg.starts_with("-web-profile=")) {
            out.web_profile = arg.substr(13);
            continue;
        }
        if (arg.starts_with("--subsystem=")) {
            out.subsystem = arg.substr(12);
            continue;
        }
        if (arg.starts_with("-subsystem=")) {
            out.subsystem = arg.substr(11);
            continue;
        }
        if (arg == "--no-open") {
            out.open_browser = false;
            continue;
        }
        if (arg == "--open") {
            out.open_browser = true;
            continue;
        }
        if ((arg == "-p" || arg == "--port") && i + 1 < argc) {
            try { out.web_port = std::stoi(argv[++i]); } catch (...) {}
            continue;
        }
        if (arg.starts_with("--port=")) {
            try { out.web_port = std::stoi(std::string(arg.substr(7))); } catch (...) {}
            continue;
        }
        if (arg.starts_with("-p=")) {
            try { out.web_port = std::stoi(std::string(arg.substr(3))); } catch (...) {}
            continue;
        }
        if ((arg == "--target" || arg == "-target") && i + 1 < argc) {
            out.target_triple = argv[++i];
            if (out.target_triple == "wasm" || out.target_triple == "web") {
                out.target_triple = "wasm32-unknown-emscripten";
            }
            continue;
        }
        if (arg.starts_with("--target=")) {
            out.target_triple = arg.substr(9);
            if (out.target_triple == "wasm" || out.target_triple == "web") {
                out.target_triple = "wasm32-unknown-emscripten";
            }
            continue;
        }
        if (arg.starts_with("-target=")) {
            out.target_triple = arg.substr(8);
            if (out.target_triple == "wasm" || out.target_triple == "web") {
                out.target_triple = "wasm32-unknown-emscripten";
            }
            continue;
        }
        if (arg == "--sysroot" && i + 1 < argc) {
            out.sysroot = argv[++i];
            continue;
        }
        if (arg.starts_with("--sysroot=")) {
            out.sysroot = arg.substr(10);
            continue;
        }
        if (arg == "--link-lib" && i + 1 < argc) {
            out.extra_link_libs.emplace_back(argv[++i]);
            continue;
        }
        if (arg.starts_with("--link-lib=")) {
            out.extra_link_libs.emplace_back(arg.substr(11));
            continue;
        }
        if (arg == "--include" && i + 1 < argc) {
            out.extra_includes.emplace_back(argv[++i]);
            continue;
        }
        if (arg.starts_with("--include=")) {
            out.extra_includes.emplace_back(arg.substr(10));
            continue;
        }
        if (arg.starts_with("-") && arg != "-" && arg != "--") {
            static const std::vector<std::string_view> kKnownFlags = {
                "--release", "-r", "--debug", "-g", "--no-trap", "--dsym", "--no-cache",
                "--profile", "--sanitize", "--sanitize=address", "--sanitize=thread",
                "--bench", "--fuzz-runs", "--seed", "--bench-time", "--filter",
                "--output", "-o", "--icon", "--name", "--app-name", "--bundle-id", "--device",
                "--ios", "--ios-sim", "--web", "--web-profile", "--subsystem",
                "--no-open", "--open", "--port", "-p", "--target", "--sysroot",
                "--link-lib", "--include", "--json", "--fix", "--dry-run", "--only",
                "--verify-cpp", "--full", "--no-shard",
                "--cxx", "--cc", "--emxx", "--ar", "--ld",
                "--verbose", "-v", "--help", "-h"
            };
            std::string_view flag_name = arg;
            if (auto eq = flag_name.find('='); eq != std::string_view::npos) {
                flag_name = flag_name.substr(0, eq);
            }
            std::string_view best_match;
            std::size_t best_dist = 4;
            for (auto kf : kKnownFlags) {
                auto d = levenshtein_dist(flag_name, kf);
                if (d < best_dist) {
                    best_dist = d;
                    best_match = kf;
                }
            }
            if (!best_match.empty()) {
                std::println(stderr, "error: unrecognized flag '{}'; did you mean '{}'?", arg, best_match);
            } else {
                std::println(stderr, "error: unrecognized flag '{}'", arg);
            }
            return 1;
        }
        if (out.target.empty()) {
            out.target = arg;
        } else {
            out.run_args.emplace_back(arg);
        }
    }

    if (saw_debug && saw_release) {
        std::println(stderr, "error: conflicting profile flags '--debug' and '--release'");
        return 1;
    }
    out.explicit_release = saw_release;
    if (saw_debug) {
        cfire::build::set_build_profile(cfire::build::BuildProfile::DevDebug);
    } else if (saw_release) {
        cfire::build::set_build_profile(cfire::build::BuildProfile::Release);
    } else if (sub == "run" || sub == "check" || (sub == "test" && !out.bench_mode)) {
        cfire::build::set_build_profile(cfire::build::BuildProfile::DevDebug);
    } else {
        cfire::build::set_build_profile(cfire::build::BuildProfile::Release);
    }

    if (sub == "build" || sub == "run") {
        auto build_cf_cur = std::filesystem::current_path() / "build.cf";
        if (std::error_code dec; !out.target.empty() && out.target != "." && std::filesystem::is_directory(out.target, dec) &&
                                 std::filesystem::exists(out.target / "build.cf", dec)) {
            out.target = out.target / "build.cf";  // CF-185: `cfire run path/to/project` uses that project's build.cf
        } else if (out.target.empty() || out.target == "." || out.target == "all") {
            if (std::filesystem::exists(build_cf_cur)) {
                if (out.target == "all") out.build_target_filter = "all";
                out.target = build_cf_cur;
            }
        } else if (out.target != "build.cf" && out.target.extension() != ".cf") {
            if (std::filesystem::exists(build_cf_cur)) {
                out.build_target_filter = out.target.string();
                out.target = build_cf_cur;
            }
        }
        if (sub == "build" && out.target.filename() == "build.cf" && out.build_target_filter.empty() && !out.run_args.empty()) {
            out.build_target_filter = out.run_args[0];
        }
    } else if (sub == "test" || sub == "bench" || sub == "check" || sub == "fix") {
        if (out.target.empty()) {
            out.target = ".";
        }
    }

    if (out.target.empty()) {
        print_usage();
        return 1;
    }

    // Q35 (CF-173): `cfire run a.cf` builds a.cf and its imports, like `go run a.go`; a directory builds the package.
    if (sub == "run" || sub == "build" || sub == "check" || sub == "emit-cpp" || sub == "eject") {
        std::error_code tec;
        auto declares_main_package = [](const std::filesystem::path& p) {
            auto txt = cfire::build::read_file_text(p);
            if (!txt) return std::pair{false, false};
            const bool is_main_pkg = txt->starts_with("package main") || txt->contains("\npackage main");
            return std::pair{is_main_pkg, txt->contains("fn main(")};
        };
        auto package_files = [](const std::filesystem::path& dir) {
            std::vector<std::filesystem::path> files;
            std::error_code dec;
            for (const auto& e : std::filesystem::directory_iterator(dir, dec)) {
                const auto name = e.path().filename().string();
                if (e.is_regular_file() && e.path().extension() == ".cf" && name != "build.cf" && !name.ends_with("_test.cf")) files.push_back(e.path());
            }
            std::sort(files.begin(), files.end());
            return files;
        };
        if (std::filesystem::is_regular_file(out.target, tec) && out.target.extension() == ".cf" && out.target.filename() != "build.cf" &&
            !out.target.filename().string().ends_with("_test.cf")) {
            auto [is_main_pkg, has_main] = declares_main_package(out.target);
            if (is_main_pkg && has_main) {
                cfire::build::explicit_entry_file_only() = true;
                auto dir = out.target.parent_path().empty() ? std::filesystem::path(".") : out.target.parent_path();
                std::size_t others = 0;
                bool all_standalone = true;  // Q45 (CF-183): every sibling has its own `fn main` (a folder of scripts)
                for (const auto& f : package_files(dir)) {
                    if (std::filesystem::equivalent(f, out.target, tec)) continue;
                    auto [sib_main_pkg, sib_has_main] = declares_main_package(f);
                    if (!sib_main_pkg) continue;
                    ++others;
                    if (!sib_has_main) all_standalone = false;
                }
                if (others > 0 && !all_standalone && !out.json_mode) {
                    std::println(stderr, "note: compiling only {} and its imports; {} other 'package main' file{} in {} {} not included (use 'cfire {} {}' to build the whole package)",
                        out.target.filename().string(), others, others == 1 ? "" : "s", dir.string(), others == 1 ? "is" : "are", sub, dir.string());
                }
            }
        } else if ((sub == "run" || sub == "build" || sub == "emit-cpp" || sub == "eject") && std::filesystem::is_directory(out.target, tec) &&
                   !std::filesystem::exists(out.target / "build.cf", tec)) {
            // A package directory: its entry is `main.cf`, or the one file that declares `fn main`.
            std::filesystem::path entry;
            if (std::filesystem::exists(out.target / "main.cf", tec)) {
                entry = out.target / "main.cf";
            } else {
                for (const auto& f : package_files(out.target)) {
                    if (declares_main_package(f).second) {
                        if (!entry.empty()) {
                            std::println(stderr, "error: {} has more than one file declaring 'fn main' ({} and {}); name the entry file", out.target.string(), entry.filename().string(), f.filename().string());
                            return 1;
                        }
                        entry = f;
                    }
                }
            }
            if (entry.empty()) {
                std::println(stderr, "error: {} has no main.cf and no file declaring 'fn main'", out.target.string());
                return 1;
            }
            out.target = entry;
        }
    }

    return std::nullopt;
}

} // namespace cfire::cli
