#pragma once
// tests/unit/test_cf198_199_200.hpp — Tests for CF-198 (Parallel TUs), CF-199 (Config/ProbeCache), CF-200 (Profiles/PCH/BinCache)

#include "../test_harness.hpp"
#include "../../src/build/binary_cache.hpp"
#include "../../src/build/cache_paths.hpp"
#include "../../src/build/pch.hpp"
#include "../../src/build/probe_cache.hpp"
#include "../../src/build/toolchain_config.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#if __has_include(<print>)
#include <print>
#endif
#include <string>
#include <thread>
#include <vector>

namespace test_cf198_detail {

struct ScopedEnvVar {
    std::string key;
    bool had_old = false;
    std::string old_val;

    ScopedEnvVar(std::string k, const std::string& val) : key(std::move(k)) {
        if (const char* cur = std::getenv(key.c_str())) {
            had_old = true;
            old_val = cur;
        }
        cfire::build::set_process_env(key.c_str(), val);
    }

    ~ScopedEnvVar() {
        if (had_old) {
            cfire::build::set_process_env(key.c_str(), old_val);
        } else {
            cfire::build::unset_process_env(key.c_str());
        }
    }
};

struct ScopedTempRoot {
    std::filesystem::path dir;
    explicit ScopedTempRoot(const std::string& prefix) {
        std::error_code ec;
        auto base = std::filesystem::temp_directory_path(ec);
        auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        dir = base / std::format("{}_{}_{}", prefix, cfire::build::current_pid(), now);
        std::filesystem::create_directories(dir, ec);
    }
    ~ScopedTempRoot() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
};

inline void write_file_str(const std::filesystem::path& p, const std::string& s) {
    std::error_code ec;
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << s;
}

} // namespace test_cf198_detail

void test_cf198_199_200_build_perf_and_caching([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_cf198_199_200_build_perf_and_caching (CF-198/199/200)");

    test_cf198_detail::ScopedTempRoot root("cfire_cf199_200");
    auto cfg_path = root.dir / "config.json";
    auto cache_dir = root.dir / "cache";

    test_cf198_detail::ScopedEnvVar env_cfg("CFIRE_CONFIG_PATH", cfg_path.string());
    test_cf198_detail::ScopedEnvVar env_cache("CFIRE_CACHE_DIR", cache_dir.string());

    // 1. CF-199: `cfire config` CRUD + precedence (CLI > Env > config.json > Auto) + `doctor [source=...]`
    {
        cfire::build::cli_toolchain_overrides() = {};
        cfire::build::ProbeCache::instance().clear_memory_and_disk();

        int rc = 0;
        auto out_set = cfire::bridge::run_command_capture(
            cfire_exe() + " config set cxx /nonexistent/cfg_clang++ 2>&1", rc);
        require(rc == 0 && out_set.contains("set cxx="), "config set cxx exits 0");

        auto out_get = cfire::bridge::run_command_capture(cfire_exe() + " config get cxx 2>&1", rc);
        require(rc == 0 && out_get.contains("/nonexistent/cfg_clang++"), "config get cxx reads written value");

        auto out_list = cfire::bridge::run_command_capture(cfire_exe() + " config list --json 2>&1", rc);
        require(rc == 0 && out_list.contains("cfg_clang++"), "config list --json prints configured entry");

        // In-process precedence verification
        auto res_cfg = cfire::build::resolve_cxx_compiler_detailed();
        require(res_cfg.source == cfire::build::ToolSource::Config &&
                res_cfg.command == "/nonexistent/cfg_clang++",
                "config.json resolves as ToolSource::Config");

        {
            test_cf198_detail::ScopedEnvVar env_cxx("CFIRE_CXX", "/nonexistent/env_clang++");
            auto res_env = cfire::build::resolve_cxx_compiler_detailed();
            require(res_env.source == cfire::build::ToolSource::Env &&
                    res_env.command == "/nonexistent/env_clang++",
                    "CFIRE_CXX overrides config.json as ToolSource::Env");

            cfire::build::cli_toolchain_overrides().cxx = "/nonexistent/cli_clang++";
            auto res_cli = cfire::build::resolve_cxx_compiler_detailed();
            require(res_cli.source == cfire::build::ToolSource::Cli &&
                    res_cli.command == "/nonexistent/cli_clang++",
                    "--cxx CLI override beats CFIRE_CXX and config.json");
            cfire::build::cli_toolchain_overrides() = {};
        }

        auto out_bad_key = cfire::bridge::run_command_capture(
            cfire_exe() + " config set invalid_key foo 2>&1", rc);
        require(rc != 0 && out_bad_key.contains("unknown config key"), "config set rejects unknown key");

        auto out_unset = cfire::bridge::run_command_capture(cfire_exe() + " config unset cxx 2>&1", rc);
        require(rc == 0 && out_unset.contains("unset cxx"), "config unset cxx exits 0");

        auto out_get_missing = cfire::bridge::run_command_capture(cfire_exe() + " config get cxx 2>&1", rc);
        require(rc != 0 && out_get_missing.contains("is not set"), "config get after unset exits non-zero");

        auto out_doc = cfire::bridge::run_command_capture(cfire_exe() + " doctor --cxx clang++ 2>&1", rc);
        require(rc == 0 && out_doc.contains("[source=cli]"), "doctor reports [source=cli] for --cxx override");
    }

    // 2. CF-199: ProbeCache persistence + `.pc` stamp invalidation
    {
        cfire::build::ProbeCache::instance().clear_memory_and_disk();
        auto cp = cfire::build::ProbeCache::instance().probe_compiler();
        require(cp.ok && cp.major_version >= 14, "ProbeCache probes host C++ compiler");
        require(std::filesystem::exists(cache_dir / "probes.json"), "ProbeCache persists probes.json");

        if (!cfire::build::find_executable_in_path("pkg-config").empty()) {
            auto pc_dir = root.dir / "pc_test";
            auto pc_file = pc_dir / "cf_probe_dummy.pc";
            test_cf198_detail::write_file_str(
                pc_file,
                "Name: cf_probe_dummy\nDescription: Dummy test pkg\nVersion: 1.0.0\nCflags: -DCF_DUMMY_V1\nLibs:\n"
            );
            auto p1 = cfire::build::ProbeCache::instance().probe_pkg_config(
                "cf_probe_dummy", false, {}, {pc_dir.string()});
            require(p1.found && p1.modversion == "1.0.0" && p1.cflags.contains("-DCF_DUMMY_V1"),
                    "ProbeCache reads initial .pc metadata");

            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            test_cf198_detail::write_file_str(
                pc_file,
                "Name: cf_probe_dummy\nDescription: Dummy test pkg updated\nVersion: 1.0.1\nCflags: -DCF_DUMMY_V22\nLibs:\n"
            );
            std::error_code tec;
            std::filesystem::last_write_time(
                pc_file, std::filesystem::file_time_type::clock::now() + std::chrono::seconds(2), tec);
            auto p2 = cfire::build::ProbeCache::instance().probe_pkg_config(
                "cf_probe_dummy", false, {}, {pc_dir.string()});
            require(p2.found && p2.modversion == "1.0.1" && p2.cflags.contains("-DCF_DUMMY_V22"),
                    "ProbeCache invalidates stale entry when .pc fingerprint changes");
        }
    }

    // 3. CF-200: BuildProfile defaults (`run` traps overflow in DevDebug, `--no-trap`/`--release` wrap, conflict error)
    {
        auto proj = root.dir / "proj_prof";
        auto ov_cf = proj / "main.cf";
        test_cf198_detail::write_file_str(ov_cf,
            "package main\n"
            "fn add_one(x: i32) -> i32 {\n"
            "    return x + 1\n"
            "}\n"
            "fn main() {\n"
            "    v := add_one(2147483647)\n"
            "    println(\"${v}\")\n"
            "}\n"
        );

        int rc = 0;
        (void)cfire::bridge::run_command_capture(
            cfire_exe() + " run " + cfire::bridge::shell_quote(ov_cf.string()) + " 2>&1", rc);
        require(rc != 0, "`cfire run` defaults to DevDebug and traps signed overflow");

        auto out_notrap = cfire::bridge::run_command_capture(
            cfire_exe() + " run --no-trap " + cfire::bridge::shell_quote(ov_cf.string()) + " 2>&1", rc);
        require(rc == 0 && out_notrap.contains("-2147483648"),
                "`cfire run --no-trap` disables overflow traps and wraps cleanly");

        auto out_rel = cfire::bridge::run_command_capture(
            cfire_exe() + " run --release " + cfire::bridge::shell_quote(ov_cf.string()) + " 2>&1", rc);
        require(rc == 0 && out_rel.contains("-2147483648"),
                "`cfire run --release` builds Release and wraps overflow");

        auto out_conflict = cfire::bridge::run_command_capture(
            cfire_exe() + " build --debug --release " + cfire::bridge::shell_quote(ov_cf.string()) + " 2>&1", rc);
        require(rc != 0 && out_conflict.contains("conflicting profile flags"),
                "`--debug --release` together fails with explicit conflict diagnostic");
    }

    // 4. CF-200: Composite PCH (Debug + Release), persistent .cfire/obj/*.o, BinaryCache restore & LRU eviction, clean
    {
        auto proj = root.dir / "proj_cache";
        auto hello_cf = proj / "main.cf";
        test_cf198_detail::write_file_str(hello_cf,
            "package main\n"
            "fn main() {\n"
            "    println(\"cache_ok\")\n"
            "}\n"
        );

#if defined(_WIN32)
        auto out_dbg = proj / "app_dbg.exe";
        auto out_rel = proj / "app_rel.exe";
#else
        auto out_dbg = proj / "app_dbg";
        auto out_rel = proj / "app_rel";
#endif
        int rc = 0;
        auto b_dbg = cfire::bridge::run_command_capture(
            cfire_exe() + " build --debug -o " + cfire::bridge::shell_quote(out_dbg.string()) + " " +
            cfire::bridge::shell_quote(hello_cf.string()) + " 2>&1", rc);
        require(rc == 0 && b_dbg.contains("pch=true"), "DevDebug build uses Composite PCH");
        require(std::filesystem::exists(out_dbg), "DevDebug binary created");

        bool has_obj = false;
        std::error_code ec;
        if (std::filesystem::is_directory(proj / ".cfire" / "obj", ec)) {
            for (const auto& e : std::filesystem::directory_iterator(proj / ".cfire" / "obj", ec)) {
                if (e.is_regular_file(ec) && e.path().extension() == ".o") {
                    has_obj = true;
                    break;
                }
            }
        }
        require(has_obj, "DevDebug keeps persistent .o in .cfire/obj/");

        bool has_dbg_pch = false;
        bool has_bin_cache = false;
        for (const auto& e : std::filesystem::directory_iterator(cache_dir / "pch", ec)) {
            if (e.is_regular_file(ec) && e.path().extension() == ".pch" &&
                e.path().filename().string().contains("_debug_")) {
                has_dbg_pch = true;
            }
        }
        for (const auto& e : std::filesystem::directory_iterator(cache_dir / "bin", ec)) {
            if (e.is_regular_file(ec) && e.path().extension() == ".bin") {
                has_bin_cache = true;
            }
        }
        require(has_dbg_pch, "Composite Debug PCH exists in CFIRE_CACHE_DIR/pch");
        require(has_bin_cache, "BinaryCache populated in CFIRE_CACHE_DIR/bin");

        // Restore debug binary from BinaryCache (same project .o untouched)
        std::filesystem::remove(out_dbg, ec);
        auto b_hit = cfire::bridge::run_command_capture(
            cfire_exe() + " build --debug -o " + cfire::bridge::shell_quote(out_dbg.string()) + " " +
            cfire::bridge::shell_quote(hello_cf.string()) + " 2>&1", rc);
        require(rc == 0 && b_hit.contains("pch=true") && std::filesystem::exists(out_dbg),
                "BinaryCache restores deleted output binary");
        auto run_hit = cfire::bridge::run_command_capture(cfire::bridge::shell_quote(out_dbg.string()) + " 2>&1", rc);
        require(rc == 0 && run_hit.contains("cache_ok"), "BinaryCache restored binary executes cleanly");

        auto b_nocache = cfire::bridge::run_command_capture(
            cfire_exe() + " build --debug --no-cache -o " + cfire::bridge::shell_quote(out_dbg.string()) + " " +
            cfire::bridge::shell_quote(hello_cf.string()) + " 2>&1", rc);
        require(rc == 0 && b_nocache.contains("Built"), "`--no-cache` build succeeds");

        auto b_rel = cfire::bridge::run_command_capture(
            cfire_exe() + " build --release -o " + cfire::bridge::shell_quote(out_rel.string()) + " " +
            cfire::bridge::shell_quote(hello_cf.string()) + " 2>&1", rc);
        require(rc == 0 && b_rel.contains("pch=true"), "Release build uses Composite PCH");

        bool has_rel_pch = false;
        for (const auto& e : std::filesystem::directory_iterator(cache_dir / "pch", ec)) {
            if (e.is_regular_file(ec) && e.path().extension() == ".pch" &&
                e.path().filename().string().contains("_rel_")) {
                has_rel_pch = true;
            }
        }
        require(has_rel_pch, "Composite Release PCH exists in CFIRE_CACHE_DIR/pch");

        // BinaryCache LRU eviction unit check
        {
            test_cf198_detail::ScopedTempRoot lru_cache("cfire_lru_bin");
            test_cf198_detail::ScopedEnvVar env_lru("CFIRE_CACHE_DIR", lru_cache.dir.string());
            auto f1 = lru_cache.dir / "src1.bin";
            auto f2 = lru_cache.dir / "src2.bin";
            test_cf198_detail::write_file_str(f1, std::string(1024, 'A'));
            test_cf198_detail::write_file_str(f2, std::string(1024, 'B'));
            cfire::build::BinaryCache::store("old_key", f1, {true, ""}, 4096);
            std::filesystem::last_write_time(
                cfire::build::BinaryCache::dir() / "old_key.bin",
                std::filesystem::file_time_type::clock::now() - std::chrono::seconds(60), ec);
            cfire::build::BinaryCache::store("new_key", f2, {true, ""}, 1500);
            require(!std::filesystem::exists(cfire::build::BinaryCache::dir() / "old_key.bin", ec),
                    "BinaryCache LRU evicts oldest entry exceeding byte budget");
            require(std::filesystem::exists(cfire::build::BinaryCache::dir() / "new_key.bin", ec),
                    "BinaryCache LRU preserves newest entry within budget");
        }

        // Clean project vs clean --cache
        auto clean_proj = cfire::bridge::run_command_capture(
            cfire_exe() + " clean " + cfire::bridge::shell_quote(proj.string()) + " 2>&1", rc);
        require(rc == 0 && clean_proj.contains("Cleaned") && !std::filesystem::exists(proj / ".cfire", ec),
                "`cfire clean` removes project .cfire directory");
        require(std::filesystem::exists(cache_dir / "pch", ec),
                "`cfire clean` without --cache preserves global PCH/binary cache");

        auto clean_all = cfire::bridge::run_command_capture(
            cfire_exe() + " clean --cache " + cfire::bridge::shell_quote(proj.string()) + " 2>&1", rc);
        require(rc == 0 && clean_all.contains("Cleaned") && !std::filesystem::exists(cache_dir, ec),
                "`cfire clean --cache` purges global CFIRE_CACHE_DIR");

        // CF-198: `--verbose` flag accepted by `cfire build`
        auto verbose_bin = proj / "verbose_app";
        auto out_verbose = cfire::bridge::run_command_capture(
            cfire_exe() + " build --verbose -o " + cfire::bridge::shell_quote(verbose_bin.string()) + " " +
            cfire::bridge::shell_quote(hello_cf.string()) + " 2>&1", rc);
        require(rc == 0 && std::filesystem::exists(verbose_bin, ec),
                "`cfire build --verbose` succeeds cleanly: " + out_verbose);
    }

    // CF-198: host compiler sources under src/ must never #include cfire_core_stdlib.hpp or runtime/core/*
    {
        std::filesystem::path src_dir;
        for (auto cur = std::filesystem::current_path(); !cur.empty(); cur = cur.parent_path()) {
            if (std::filesystem::exists(cur / "src/main.cpp")) {
                src_dir = cur / "src";
                break;
            }
            if (cur == cur.parent_path()) break;
        }
        require(!src_dir.empty(), "CF-198 hygiene check located src/ directory");
        std::vector<std::string> bad_includes;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(src_dir)) {
            if (!entry.is_regular_file()) continue;
            auto ext = entry.path().extension();
            if (ext != ".hpp" && ext != ".cpp" && ext != ".inc") continue;
            auto content = cfire::build::read_file_text(entry.path());
            if (!content) continue;
            std::istringstream iss(*content);
            std::string line;
            int ln = 0;
            while (std::getline(iss, line)) {
                ++ln;
                auto first = line.find_first_not_of(" \t");
                if (first == std::string::npos || line[first] != '#') continue;
                auto after_hash = line.find_first_not_of(" \t", first + 1);
                if (after_hash == std::string::npos || !std::string_view(line).substr(after_hash).starts_with("include")) continue;
                if (line.contains("cfire_core_stdlib") || line.contains("runtime/core/") || line.contains("\"core/")) {
                    bad_includes.push_back(std::format("{}:{}: {}", entry.path().filename().string(), ln, line));
                }
            }
        }
        require(bad_includes.empty(),
                "CF-198: src/ must not #include runtime/core/* or cfire_core_stdlib.hpp (found: " +
                (bad_includes.empty() ? std::string("") : bad_includes.front()) + ")");
#if !defined(__APPLE__) || !(defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        require(CFIRE_TARGET_IOS == 0, "CF-198: CFIRE_TARGET_IOS stays 0 on desktop host even when toolchain headers precede runtime");
#endif
        {
            cfire::testing_detail::FuzzRng rng(42);
            auto b0 = cfire::testing_detail::Arbitrary<std::uint8_t>::generate(rng, 0);
            auto b8 = cfire::testing_detail::Arbitrary<std::uint8_t>::generate(rng, 8);
            require(b0 == 0, "CF-200: Arbitrary<uint8_t> iter 0 edge value without recursion");
            (void)b8;
        }
    }

    cfire::build::cli_toolchain_overrides() = {};
    cfire::build::ProbeCache::instance().clear_memory_and_disk();
    cfire::build::set_build_profile(cfire::build::BuildProfile::Release);
    cfire::build::g_no_trap = false;
    cfire::build::g_emit_dsym = false;
    cfire::build::g_no_binary_cache = false;
    cfire::build::g_verbose_mode = false;

    std::println("--- PASS: test_cf198_199_200_build_perf_and_caching");
}
