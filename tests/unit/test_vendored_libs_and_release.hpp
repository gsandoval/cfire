#pragma once
// tests/unit/test_vendored_libs_and_release.hpp — Tests for vendored sqlite3/simdjson archive cache, portable builds, release matrix, and installers

#include "../test_harness.hpp"
#include "../../src/build/cache_paths.hpp"
#include "../../src/build/deps.hpp"
#include "../../src/build/lib_cache.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#if __has_include(<print>)
#include <print>
#endif
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifndef PROCESSOR_ARCHITECTURE_ARM64
#define PROCESSOR_ARCHITECTURE_ARM64 12
#endif
#endif

namespace test_portable_release_detail {

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

    explicit ScopedEnvVar(std::string k, std::nullptr_t) : key(std::move(k)) {
        if (const char* cur = std::getenv(key.c_str())) {
            had_old = true;
            old_val = cur;
        }
        cfire::build::unset_process_env(key.c_str());
    }

    ~ScopedEnvVar() {
        if (had_old) {
            cfire::build::set_process_env(key.c_str(), old_val);
        } else {
            cfire::build::unset_process_env(key.c_str());
        }
    }
};

struct ScopedPortableFlags {
    bool prev_portable = cfire::build::g_portable_mode;
    bool prev_static = cfire::build::g_static_mode;
    bool prev_universal = cfire::build::g_universal_mode;
    std::string prev_macos_min = cfire::build::g_macos_min_version;
    bool prev_no_bin_cache = cfire::build::g_no_binary_cache;
    bool prev_no_shard = cfire::build::g_no_shard;
    cfire::build::BuildProfile prev_profile = cfire::build::g_build_profile;
    bool prev_debug = cfire::build::g_debug_mode;

    ~ScopedPortableFlags() {
        cfire::build::g_portable_mode = prev_portable;
        cfire::build::g_static_mode = prev_static;
        cfire::build::g_universal_mode = prev_universal;
        cfire::build::g_macos_min_version = prev_macos_min;
        cfire::build::g_no_binary_cache = prev_no_bin_cache;
        cfire::build::g_no_shard = prev_no_shard;
        cfire::build::set_build_profile(prev_profile);
        cfire::build::g_debug_mode = prev_debug;
    }
};

inline auto find_files_named(const std::filesystem::path& root, std::string_view name)
    -> std::vector<std::filesystem::path> {
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    if (!std::filesystem::exists(root, ec)) return out;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(
             root, std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (entry.is_regular_file(ec) && entry.path().filename() == name) {
            out.push_back(entry.path());
        }
    }
    return out;
}

inline auto find_files_with_exts(
    const std::filesystem::path& root,
    std::initializer_list<std::string_view> exts
) -> std::vector<std::filesystem::path> {
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    if (!std::filesystem::exists(root, ec)) return out;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(
             root, std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        std::string ext = entry.path().extension().string();
        for (auto wanted : exts) {
            if (ext == wanted) {
                out.push_back(entry.path());
                break;
            }
        }
    }
    return out;
}

inline auto read_text_file(const std::filesystem::path& p) -> std::string {
    std::ifstream in(p, std::ios::binary);
    if (!in) return "";
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

inline auto vector_contains_substr(const std::vector<std::string>& items, std::string_view needle) -> bool {
    for (const auto& item : items) {
        if (item.find(needle) != std::string::npos) return true;
    }
    return false;
}

inline auto create_dummy_static_archive(
    const std::filesystem::path& lib_dir,
    std::string_view lib_name
) -> bool {
    std::error_code ec;
    std::filesystem::create_directories(lib_dir, ec);
    auto c_file = lib_dir / std::format("{}.c", lib_name);
    auto o_file = lib_dir / std::format("{}.o", lib_name);
    auto a_file = lib_dir / std::format("lib{}.a", lib_name);
    {
        std::ofstream ofs(c_file, std::ios::trunc);
        ofs << "int cfire_dummy_sym_" << lib_name << "(void) { return 42; }\n";
    }
    std::string cc = cfire::build::resolve_c_compiler();
    std::string ar = cfire::build::resolve_archiver();
    int rc = 0;
    (void)cfire::bridge::run_command_capture(
        std::format("{} -c \"{}\" -o \"{}\"", cc, c_file.string(), o_file.string()), rc);
    if (rc != 0) return false;
    (void)cfire::bridge::run_command_capture(
        std::format("{} rcs \"{}\" \"{}\"", ar, a_file.string(), o_file.string()), rc);
    return rc == 0 && std::filesystem::is_regular_file(a_file, ec);
}

inline auto copy_dir_recursive(const std::filesystem::path& src, const std::filesystem::path& dst) -> bool {
    std::error_code ec;
    std::filesystem::create_directories(dst, ec);
    if (ec) return false;
    std::filesystem::copy(
        src, dst,
        std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing,
        ec);
    return !ec;
}

} // namespace test_portable_release_detail

void test_vendored_archives_and_portable_releases(const std::filesystem::path& rt_dir) {
    using namespace test_portable_release_detail;
    std::println("=== RUN   test_vendored_archives_and_portable_releases ");

    const std::filesystem::path repo_root = rt_dir.parent_path();

    static constexpr std::string_view kSqliteJsonSrc = R"CF(package main

import "core:json"
import "ext:db/sqlite"

type DocRow struct {
    title: str `sql:"title" json:"title"`
    body:  str `sql:"body" json:"body"`
}

fn main() {
    mut db := try sqlite.open(":memory:")
    defer db.close()
    try db.exec("CREATE VIRTUAL TABLE docs USING fts5(title, body)")
    try db.exec("INSERT INTO docs(title, body) VALUES (?1, ?2)", "cfire", "vendored sqlite fts5 and simdjson")
    rows := try db.query[DocRow]("SELECT title, body FROM docs WHERE docs MATCH 'vendored'")
    raw := json.stringify(rows[0])
    parsed := try json.parse[DocRow](raw)
    println("{}:{}", parsed.title, rows.len())
}
)CF";

    // =========================================================================
    // (1): Clean-room build + FTS5/SIMDJSON exec + archive cache hit
    // =========================================================================
    std::filesystem::path host_sqlite_ar;
    std::filesystem::path host_simdjson_ar;
    {
        ScopedTempDir tmp("portable_cleanroom");
        auto fake_home = tmp.path / "home";
        auto cache_dir = tmp.path / "cache";
        auto proj_dir = tmp.path / "proj";
        std::error_code ec;
        std::filesystem::create_directories(fake_home, ec);
        std::filesystem::create_directories(cache_dir, ec);
        std::filesystem::create_directories(proj_dir, ec);

        ScopedEnvVar env_ignore("CFIRE_IGNORE_SYSTEM_LIBS", "1");
        ScopedEnvVar env_pc_path("PKG_CONFIG_PATH", nullptr);
        ScopedEnvVar env_pc_libdir("PKG_CONFIG_LIBDIR", nullptr);
        ScopedEnvVar env_home("HOME", fake_home.string());
        ScopedEnvVar env_userprofile("USERPROFILE", fake_home.string());
        ScopedEnvVar env_cache("CFIRE_CACHE_DIR", cache_dir.string());
        ScopedPortableFlags flags_guard;
        cfire::build::set_build_profile(cfire::build::BuildProfile::Release);
        cfire::build::g_no_binary_cache = true;
        cfire::build::g_no_shard = true;

        auto cf_file = proj_dir / "main.cf";
        {
            std::ofstream ofs(cf_file, std::ios::trunc);
            ofs << kSqliteJsonSrc;
        }

        auto bin1 = tmp.path / "app1";
        auto res1 = cfire::build::build_native(cf_file, rt_dir, bin1);
        require(res1.has_value(), std::format("clean-room sqlite3+simdjson build succeeds: {}",
                                              res1.has_value() ? "" : res1.error()));
        if (res1) {
            require(res1->use_vendored_sqlite3, "build plan enabled vendored sqlite3 under clean env");
            require(res1->use_vendored_simdjson, "build plan enabled vendored simdjson under clean env");
        }

        auto sq_archives = find_files_named(cache_dir / "libs", "libcfire_sqlite3.a");
        auto sj_archives = find_files_named(cache_dir / "libs", "libcfire_simdjson.a");
        require(sq_archives.size() == 1, "cached libcfire_sqlite3.a created once under libs/<sha256>/");
        require(sj_archives.size() == 1, "cached libcfire_simdjson.a created once under libs/<sha256>/");
        if (!sq_archives.empty() && !sj_archives.empty()) {
            host_sqlite_ar = sq_archives[0];
            host_simdjson_ar = sj_archives[0];
            require(std::filesystem::file_size(host_sqlite_ar, ec) > 0, "libcfire_sqlite3.a non-empty");
            require(std::filesystem::file_size(host_simdjson_ar, ec) > 0, "libcfire_simdjson.a non-empty");

            int run_rc = 0;
            std::string run_out = cfire::bridge::run_command_capture(
                cfire::bridge::shell_quote(bin1.string()), run_rc);
            require(run_rc == 0 && run_out.find("cfire:1") != std::string::npos,
                std::format("clean-room sqlite FTS5 + json output 'cfire:1' (got rc={}, out='{}')",
                            run_rc, run_out));

            auto t_sq_1 = std::filesystem::last_write_time(host_sqlite_ar, ec);
            auto t_sj_1 = std::filesystem::last_write_time(host_simdjson_ar, ec);
            std::this_thread::sleep_for(std::chrono::milliseconds(30));

            auto bin2 = tmp.path / "app2";
            auto res2 = cfire::build::build_native(cf_file, rt_dir, bin2);
            require(res2.has_value(), std::format("second build reuses cached .a archives: {}",
                                                  res2.has_value() ? "" : res2.error()));
            auto sq_archives2 = find_files_named(cache_dir / "libs", "libcfire_sqlite3.a");
            auto sj_archives2 = find_files_named(cache_dir / "libs", "libcfire_simdjson.a");
            require(sq_archives2.size() == 1 && sq_archives2[0] == host_sqlite_ar,
                "second build kept same sqlite3 archive cache path");
            require(sj_archives2.size() == 1 && sj_archives2[0] == host_simdjson_ar,
                "second build kept same simdjson archive cache path");
            auto t_sq_2 = std::filesystem::last_write_time(host_sqlite_ar, ec);
            auto t_sj_2 = std::filesystem::last_write_time(host_simdjson_ar, ec);
            require(t_sq_1 == t_sq_2 && t_sj_1 == t_sj_2,
                "second build reused cached libcfire_{sqlite3,simdjson}.a without rewriting mtime");
        }
    }

    // =========================================================================
    // (2): Guardrail 1 — No blanket vendor -I & automatic override yielding
    // =========================================================================
    {
        ScopedTempDir tmp("portable_guardrail1");
        auto fake_home = tmp.path / "home";
        auto empty_home = tmp.path / "empty_home";
        std::error_code ec;
        std::filesystem::create_directories(fake_home, ec);
        std::filesystem::create_directories(empty_home, ec);

        ScopedEnvVar env_ignore("CFIRE_IGNORE_SYSTEM_LIBS", "1");
        ScopedEnvVar env_pc_path("PKG_CONFIG_PATH", nullptr);
        ScopedEnvVar env_pc_libdir("PKG_CONFIG_LIBDIR", nullptr);
        ScopedEnvVar env_home("HOME", empty_home.string());
        ScopedEnvVar env_userprofile("USERPROFILE", empty_home.string());
        ScopedPortableFlags flags_guard;
        cfire::build::set_build_profile(cfire::build::BuildProfile::Release);

        auto cf_file = tmp.path / "app.cf";
        {
            std::ofstream ofs(cf_file, std::ios::trunc);
            ofs << kSqliteJsonSrc;
        }
        auto base_ast = cfire::frontend::parse_source(cf_file.string(), kSqliteJsonSrc);
        require(base_ast.has_value(), "G1: parse base source");
        auto base_checked = cfire::frontend::analyze(std::move(*base_ast));
        require(base_checked.has_value(), "G1: analyze base source");

        if (base_checked) {
            auto init_plan = cfire::build::init_build_plan_flags(cf_file, rt_dir, false, "", "", "", "", false);
            require(init_plan.has_value(), "G1: init_build_plan_flags succeeds");
            if (init_plan) {
                std::string raw_vendor = (rt_dir / "vendor").string();
                bool has_blanket_vendor = false;
                for (const auto& cf : init_plan->cflags) {
                    if (cf == ("-I" + raw_vendor)) has_blanket_vendor = true;
                }
                require(!has_blanket_vendor,
                    "G1: init_build_plan_flags must NOT add blanket -I<runtime>/vendor");

                // Clean env -> vendored enabled
                cfire::build::BuildPlan clean_plan = *init_plan;
                auto p_ok = cfire::build::populate_checked_build_plan(*base_checked, clean_plan, rt_dir, false);
                require(p_ok.has_value(), "G1: clean populate_checked_build_plan succeeds");
                require(clean_plan.use_vendored_sqlite3 && clean_plan.use_vendored_simdjson,
                    "G1: clean environment enables both vendored sqlite3 & simdjson");

                // Override via --link-lib (extra_link_libs) for sqlcipher & simdjson
                cfire::build::BuildPlan link_override = *init_plan;
                auto p_link = cfire::build::populate_checked_build_plan(
                    *base_checked, link_override, rt_dir, false, {"sqlcipher", "simdjson"});
                require(p_link.has_value(), "G1: link override populate succeeds");
                require(!link_override.use_vendored_sqlite3,
                    "G1: extra_link_libs 'sqlcipher' yields vendored sqlite3");
                require(!link_override.use_vendored_simdjson,
                    "G1: extra_link_libs 'simdjson' yields vendored simdjson");
                require(!vector_contains_substr(link_override.cflags, "vendor/sqlite3") &&
                        !vector_contains_substr(link_override.cflags, "vendor/simdjson"),
                    "G1: yielded plan omits vendor -I flags");

                // Override via build.cf external_deps
                cfire::build::BuildPlan dep_override = *init_plan;
                dep_override.external_deps.push_back(cfire::build::ExternalDepSpec{.name = "sqlite3"});
                dep_override.external_deps.push_back(cfire::build::ExternalDepSpec{.name = "simdjson"});
                auto p_dep = cfire::build::populate_checked_build_plan(*base_checked, dep_override, rt_dir, false);
                require(p_dep.has_value(), "G1: external_deps override populate succeeds");
                require(!dep_override.use_vendored_sqlite3 && !dep_override.use_vendored_simdjson,
                    "G1: build.cf external_deps overrides yield vendored sqlite3 & simdjson");
            }
        }

        // Override via ~/.local/{include,lib}
        {
            std::filesystem::create_directories(fake_home / ".local" / "include", ec);
            std::filesystem::create_directories(fake_home / ".local" / "lib", ec);
            { std::ofstream(fake_home / ".local" / "include" / "sqlite3.h") << "/* local */\n"; }
            { std::ofstream(fake_home / ".local" / "lib" / "libsqlite3.a") << "!<arch>\n"; }
            { std::ofstream(fake_home / ".local" / "include" / "simdjson.h") << "/* local */\n"; }
            { std::ofstream(fake_home / ".local" / "lib" / "libsimdjson.a") << "!<arch>\n"; }

            ScopedEnvVar home_local("HOME", fake_home.string());
            ScopedEnvVar userprofile_local("USERPROFILE", fake_home.string());
            auto init_plan = cfire::build::init_build_plan_flags(cf_file, rt_dir, false, "", "", "", "", false);
            if (init_plan && base_checked) {
                cfire::build::BuildPlan local_plan = *init_plan;
                auto p_loc = cfire::build::populate_checked_build_plan(*base_checked, local_plan, rt_dir, false);
                require(p_loc.has_value(), "G1: ~/.local populate succeeds");
                require(!local_plan.use_vendored_sqlite3 && !local_plan.use_vendored_simdjson,
                    "G1: ~/.local/{include,lib} overrides yield vendored sqlite3 & simdjson");
            }
        }

        // @pkg(...) on imports is retired (cfire/RETIRED-API); overrides come from build.cf PkgConfig or --pkg-config
        {
            auto hdr_sql = tmp.path / "custom_sql.h";
            auto hdr_sj = tmp.path / "custom_sj.h";
            { std::ofstream(hdr_sql) << "int custom_sql_fn(void);\n"; }
            { std::ofstream(hdr_sj) << "int custom_sj_fn(void);\n"; }
            std::string retired_src = std::format(
                "package main\n"
                "import cpp \"{}\" @pkg(\"sqlite3\")\n"
                "fn main() {{ println(\"ok\") }}\n",
                hdr_sql.string());
            auto retired_cf = tmp.path / "pkg_retired.cf";
            { std::ofstream(retired_cf) << retired_src; }
            auto retired_ast = cfire::frontend::parse_source(retired_cf.string(), retired_src);
            require(retired_ast.has_value(), "@pkg still parses for clean diagnostic");
            if (retired_ast) {
                auto retired_checked = cfire::frontend::analyze(std::move(*retired_ast));
                require(!retired_checked.has_value() && retired_checked.error().contains("cfire/RETIRED-API"),
                    std::format("@pkg on import must fail with cfire/RETIRED-API, got: {}",
                                retired_checked.has_value() ? "<ok>" : retired_checked.error()));
            }

            std::string pure_src = std::format(
                "package main\n"
                "import \"core:json\"\n"
                "import \"ext:db/sqlite\"\n"
                "import cpp \"{}\"\n"
                "import cpp \"{}\"\n"
                "fn main() {{ println(\"ok\") }}\n",
                hdr_sql.string(), hdr_sj.string());
            auto pkg_cf = tmp.path / "pkg_override.cf";
            { std::ofstream(pkg_cf) << pure_src; }
            auto pkg_ast = cfire::frontend::parse_source(pkg_cf.string(), pure_src);
            require(pkg_ast.has_value(), "G1: parse override source");
            if (pkg_ast) {
                auto pkg_checked = cfire::frontend::analyze(std::move(*pkg_ast));
                require(pkg_checked.has_value(), std::format("G1: analyze override source: {}",
                                                             pkg_checked.has_value() ? "" : pkg_checked.error()));
                auto init_plan = cfire::build::init_build_plan_flags(
                    pkg_cf, rt_dir, false, "", "", "", "", false, {"sqlite3", "simdjson"});
                if (pkg_checked && init_plan) {
                    cfire::build::BuildPlan pkg_plan = *init_plan;
                    auto p_pkg = cfire::build::populate_checked_build_plan(*pkg_checked, pkg_plan, rt_dir, false);
                    cfire::build::resolve_plan_pkg_configs(pkg_plan, rt_dir);
                    require(p_pkg.has_value(), "G1: --pkg-config populate succeeds");
                    require(!pkg_plan.use_vendored_sqlite3 && !pkg_plan.use_vendored_simdjson,
                        "G1: --pkg-config sqlite3 and simdjson yield vendored archives");
                }
            }
        }
    }

    // =========================================================================
    // (3): Cross-target vendored archive cache (Wasm & iOS Simulator)
    // =========================================================================
    {
        ScopedTempDir tmp("portable_cross_cache");
        auto cache_dir = tmp.path / "cache";
        auto em_cache_dir = tmp.path / "em_cache";
        std::error_code ec;
        std::filesystem::create_directories(em_cache_dir, ec);
        ScopedEnvVar env_cache("CFIRE_CACHE_DIR", cache_dir.string());
        ScopedEnvVar env_em_cache("EM_CACHE", em_cache_dir.string());
        ScopedPortableFlags flags_guard;
        cfire::build::set_build_profile(cfire::build::BuildProfile::Release);

        int em_rc = 0;
        (void)cfire::bridge::run_command_capture("command -v em++ >/dev/null 2>&1", em_rc);
        if (em_rc == 0) {
            cfire::build::BuildPlan wasm_plan;
            wasm_plan.target_triple = "wasm32-unknown-emscripten";
            wasm_plan.use_vendored_sqlite3 = true;
            wasm_plan.use_vendored_simdjson = true;
            auto w_sq = cfire::build::ensure_vendored_sqlite3_archive(rt_dir, wasm_plan);
            auto w_sj = cfire::build::ensure_vendored_simdjson_archive(rt_dir, wasm_plan);
            require(w_sq.has_value(), std::format("wasm: vendored sqlite3 compiles: {}",
                                                  w_sq.has_value() ? "" : w_sq.error()));
            require(w_sj.has_value(), std::format("wasm: vendored simdjson compiles: {}",
                                                  w_sj.has_value() ? "" : w_sj.error()));
            if (w_sq && w_sj && !host_sqlite_ar.empty() && !host_simdjson_ar.empty()) {
                require(w_sq->parent_path().filename() != host_sqlite_ar.parent_path().filename(),
                    "wasm: sqlite3 cache key differs from host key");
                require(w_sj->parent_path().filename() != host_simdjson_ar.parent_path().filename(),
                    "wasm: simdjson cache key differs from host key");
            }
        }

#if defined(__APPLE__)
        auto ios_sdk = cfire::build::resolve_ios_sdk_path(true);
        if (ios_sdk.has_value() && !ios_sdk->empty()) {
            cfire::build::BuildPlan ios_plan;
            ios_plan.is_ios = true;
            ios_plan.is_ios_sim = true;
            ios_plan.target_triple = "arm64-apple-ios16.0-simulator";
            ios_plan.sysroot = *ios_sdk;
            ios_plan.use_vendored_sqlite3 = true;
            ios_plan.use_vendored_simdjson = true;
            auto i_sq = cfire::build::ensure_vendored_sqlite3_archive(rt_dir, ios_plan);
            auto i_sj = cfire::build::ensure_vendored_simdjson_archive(rt_dir, ios_plan);
            require(i_sq.has_value(), std::format("ios-sim: vendored sqlite3 compiles: {}",
                                                  i_sq.has_value() ? "" : i_sq.error()));
            require(i_sj.has_value(), std::format("ios-sim: vendored simdjson (NEON) compiles: {}",
                                                  i_sj.has_value() ? "" : i_sj.error()));
            if (i_sq && i_sj) {
                std::error_code ec;
                require(std::filesystem::file_size(*i_sq, ec) > 0 &&
                        std::filesystem::file_size(*i_sj, ec) > 0,
                    "ios-sim: compiled archives non-empty");
                if (!host_sqlite_ar.empty() && !host_simdjson_ar.empty()) {
                    require(i_sq->parent_path().filename() != host_sqlite_ar.parent_path().filename(),
                        "ios-sim: sqlite3 cache key differs from host macOS key");
                    require(i_sj->parent_path().filename() != host_simdjson_ar.parent_path().filename(),
                        "ios-sim: simdjson cache key differs from host macOS key");
                }
            }
        }
#endif
    }

    // =========================================================================
    // (4): Guardrail 2 — deps.hpp toolchain, triple, sysroot & minOS forwarding
    // =========================================================================
    {
        ScopedTempDir tmp("portable_guardrail2");
        cfire::build::ExternalDepSpec dep;
        dep.name = "sample_cpp_dep";
        dep.driver = "C";

        cfire::build::BuildPlan p_arm13;
        p_arm13.target_triple = "arm64-apple-macos13.3";
        p_arm13.macos_min = "13.3";
        p_arm13.sysroot = "/Fake/MacOSX.sdk";

        cfire::build::BuildPlan p_x86_13;
        p_x86_13.target_triple = "x86_64-apple-macos13.3";
        p_x86_13.macos_min = "13.3";
        p_x86_13.sysroot = "/Fake/MacOSX.sdk";

        cfire::build::BuildPlan p_arm14 = p_arm13;
        p_arm14.target_triple = "arm64-apple-macos14.0";
        p_arm14.macos_min = "14.0";

        cfire::build::BuildPlan p_win;
        p_win.target_triple = "x86_64-w64-mingw32";

        std::string k_arm13 = cfire::build::compute_dep_target_cache_key(dep, p_arm13);
        std::string k_x86_13 = cfire::build::compute_dep_target_cache_key(dep, p_x86_13);
        std::string k_arm14 = cfire::build::compute_dep_target_cache_key(dep, p_arm14);
        std::string k_win = cfire::build::compute_dep_target_cache_key(dep, p_win);
        require(k_arm13 != k_x86_13 && k_arm13 != k_arm14 && k_arm13 != k_win && k_x86_13 != k_win,
            "G2: compute_dep_target_cache_key isolates arch, minOS, and OS targets");

        std::string cmake_arm13 = cfire::build::build_dep_cmake_configure_cmd(
            tmp.path / "src", tmp.path / "bld", dep, p_arm13);
        require(cmake_arm13.find("-DCMAKE_C_COMPILER=") != std::string::npos &&
                cmake_arm13.find("-DCMAKE_CXX_COMPILER=") != std::string::npos,
            "G2: CMake configure forwards C/C++ compilers");
        require(cmake_arm13.find("-DCMAKE_OSX_DEPLOYMENT_TARGET=13.3") != std::string::npos,
            "G2: CMake configure forwards CMAKE_OSX_DEPLOYMENT_TARGET=13.3");
        require(cmake_arm13.find("-DCMAKE_OSX_SYSROOT=/Fake/MacOSX.sdk") != std::string::npos,
            "G2: CMake configure forwards CMAKE_OSX_SYSROOT");
        require(cmake_arm13.find("-DCMAKE_OSX_ARCHITECTURES=arm64") != std::string::npos,
            "G2: CMake configure forwards CMAKE_OSX_ARCHITECTURES=arm64");

        std::string cc_cmd = cfire::build::build_dep_source_compile_cmd(
            tmp.path / "src" / "foo.cpp", tmp.path / "src", tmp.path / "bld" / "foo.o", dep, p_x86_13);
        require(cc_cmd.find("--target=x86_64-apple-macos13.3") != std::string::npos,
            "G2: dep compile cmd forwards --target=");
        require(cc_cmd.find("-mmacosx-version-min=13.3") != std::string::npos,
            "G2: dep compile cmd forwards -mmacosx-version-min=13.3");
        require(cc_cmd.find("-isysroot") != std::string::npos,
            "G2: dep compile cmd forwards -isysroot");
    }

    // =========================================================================
    // (1 & 3): CLI flag validation, Mach-O minOS 13.3 / 14.0 & universal
    // =========================================================================
    {
        ScopedTempDir tmp("portable_cli");
        auto cf_file = tmp.path / "main.cf";
        {
            std::ofstream ofs(cf_file, std::ios::trunc);
            ofs << kSqliteJsonSrc;
        }
        auto bad_out = tmp.path / "bad_out";

        auto r_min12 = run_cfire(std::format(
            "build \"{}\" --macos-min=12.0 -o \"{}\"", cf_file.string(), bad_out.string()));
        require(r_min12.exit_code == 2 && r_min12.output.find("13.3") != std::string::npos,
            std::format("--macos-min=12.0 exits 2 citing 13.3 (got rc={}, out='{}')",
                        r_min12.exit_code, r_min12.output));

        auto r_min13 = run_cfire(std::format(
            "build \"{}\" --macos-min=13.0 -o \"{}\"", cf_file.string(), bad_out.string()));
        require(r_min13.exit_code == 2 && r_min13.output.find("13.3") != std::string::npos,
            std::format("--macos-min=13.0 exits 2 citing 13.3 (got rc={}, out='{}')",
                        r_min13.exit_code, r_min13.output));

        auto r_uni_wasm = run_cfire(std::format(
            "build \"{}\" --universal --target=wasm32-unknown-emscripten -o \"{}\"",
            cf_file.string(), bad_out.string()));
        require(r_uni_wasm.exit_code != 0,
            std::format("--universal --target=wasm fails cleanly (got rc={})", r_uni_wasm.exit_code));

#if defined(__APPLE__)
        {
            ScopedEnvVar env_ignore("CFIRE_IGNORE_SYSTEM_LIBS", "1");
            ScopedEnvVar env_cache("CFIRE_CACHE_DIR", (tmp.path / "cache").string());

            auto port_bin = tmp.path / "app_portable";
            auto r_port = run_cfire(std::format(
                "build \"{}\" --portable --no-cache -o \"{}\"", cf_file.string(), port_bin.string()));
            r_port.assert_ok("macOS: --portable build succeeds");

            int otl_rc = 0;
            std::string otool_l = cfire::bridge::run_command_capture(
                std::format("otool -l \"{}\"", port_bin.string()), otl_rc);
            require(otl_rc == 0 && otool_l.find("minos 13.3") != std::string::npos,
                "macOS: --portable binary embeds minos 13.3");

            int otL_rc = 0;
            std::string otool_L = cfire::bridge::run_command_capture(
                std::format("otool -L \"{}\"", port_bin.string()), otL_rc);
            require(otL_rc == 0 &&
                    otool_L.find("/opt/homebrew") == std::string::npos &&
                    otool_L.find("/usr/local") == std::string::npos,
                std::format("macOS: --portable binary has zero Homebrew/usr-local dylibs:\n{}", otool_L));

            int ex_rc = 0;
            std::string ex_out = cfire::bridge::run_command_capture(
                cfire::bridge::shell_quote(port_bin.string()), ex_rc);
            require(ex_rc == 0 && ex_out.find("cfire:1") != std::string::npos,
                "macOS: --portable binary executes cleanly");

            auto min14_bin = tmp.path / "app_min14";
            auto r_m14 = run_cfire(std::format(
                "build \"{}\" --portable --macos-min=14.0 --no-cache -o \"{}\"",
                cf_file.string(), min14_bin.string()));
            r_m14.assert_ok("macOS: --portable --macos-min=14.0 build succeeds");
            std::string otool_m14 = cfire::bridge::run_command_capture(
                std::format("otool -l \"{}\"", min14_bin.string()), otl_rc);
            require(otl_rc == 0 && otool_m14.find("minos 14.0") != std::string::npos,
                "macOS: --macos-min=14.0 sets minos 14.0");

            auto uni_bin = tmp.path / "app_universal";
            auto r_uni = run_cfire(std::format(
                "build \"{}\" --universal --portable --no-cache -o \"{}\"",
                cf_file.string(), uni_bin.string()));
            r_uni.assert_ok("macOS: --universal --portable build succeeds");
            int lipo_rc = 0;
            std::string lipo_out = cfire::bridge::run_command_capture(
                std::format("/usr/bin/lipo -archs \"{}\"", uni_bin.string()), lipo_rc);
            require(lipo_rc == 0 &&
                    lipo_out.find("x86_64") != std::string::npos &&
                    lipo_out.find("arm64") != std::string::npos,
                std::format("macOS: universal binary contains x86_64 and arm64 (got '{}')", lipo_out));
            std::string uni_out = cfire::bridge::run_command_capture(
                cfire::bridge::shell_quote(uni_bin.string()), ex_rc);
            require(ex_rc == 0 && uni_out.find("cfire:1") != std::string::npos,
                "macOS: universal binary executes cleanly");
        }
#endif
    }

    // =========================================================================
    // (2): Guardrail 3 — Per-library static archive resolution & fallback
    // =========================================================================
    {
        ScopedTempDir tmp("portable_guardrail3");
        auto lib_dir = tmp.path / "custom_libs";
        require(create_dummy_static_archive(lib_dir, "customstatic"),
            "G3: create dummy static archive libcustomstatic.a");

        cfire::build::BuildPlan port_plan;
        port_plan.portable = true;
#if defined(__APPLE__)
        port_plan.target_triple = "";
#elif defined(__linux__)
        port_plan.target_triple = "";
#endif
        port_plan.ldflags = {
            "-L" + lib_dir.string(),
            "-lcustomstatic",
            "-lmissing_shared_only",
        };
        std::string cc_for_rules = cfire::build::resolve_cxx_compiler(port_plan.target_triple);
        auto port_res = cfire::build::apply_portability_and_static_rules(port_plan, cc_for_rules);
        require(port_res.has_value(), std::format("G3: --portable handles shared-only fallback cleanly: {}",
                                                  port_res.has_value() ? "" : port_res.error()));

        std::string expected_ar = (lib_dir / "libcustomstatic.a").string();
        require(vector_contains_substr(port_plan.ldflags, expected_ar) &&
                !vector_contains_substr(port_plan.ldflags, "-lcustomstatic"),
            "G3: --portable replaces -lcustomstatic with full .a path");
        require(vector_contains_substr(port_plan.ldflags, "-lmissing_shared_only"),
            "G3: --portable retains shared-only -lmissing_shared_only");
        require(vector_contains_substr(port_plan.portable_notes, "missing_shared_only"),
            "G3: --portable records concise note for missing_shared_only");
#if defined(__APPLE__)
        require(vector_contains_substr(port_plan.ldflags, "@executable_path"),
            "G3 macOS: --portable adds @executable_path rpath when dynamic lib remains");
#elif defined(__linux__)
        require(vector_contains_substr(port_plan.ldflags, "$ORIGIN"),
            "G3 Linux: --portable adds $ORIGIN rpath when dynamic lib remains");
#endif

        cfire::build::BuildPlan static_plan;
        static_plan.static_link = true;
        static_plan.ldflags = {"-L" + lib_dir.string(), "-lmissing_shared_only"};
        auto static_res = cfire::build::apply_portability_and_static_rules(static_plan, cc_for_rules);
        require(!static_res.has_value(), "G3: --static rejects unavailable static linkage");
        if (!static_res) {
#if defined(__APPLE__)
            require(static_res.error().find("--portable") != std::string::npos,
                std::format("G3 macOS: --static error directs user to --portable (got '{}')",
                            static_res.error()));
#else
            require(static_res.error().find("missing_shared_only") != std::string::npos,
                std::format("G3: --static error names missing_shared_only (got '{}')",
                            static_res.error()));
#endif
        }
    }

    // =========================================================================
    // Portable bundle, install.sh mock file:// & tamper check, contract checks
    // =========================================================================
    {
        ScopedTempDir tmp("release_bundle");
        auto bundle_dir = tmp.path / "portable_bundle";
        auto r_inst = run_cfire(std::format("install --portable \"{}\"", bundle_dir.string()));
        r_inst.assert_ok("cfire install --portable succeeds");

        std::error_code ec;
#if defined(_WIN32)
        require(std::filesystem::is_regular_file(bundle_dir / "cfire.exe", ec),
            "portable bundle contains cfire.exe");
#else
        require(std::filesystem::is_regular_file(bundle_dir / "cfire", ec),
            "portable bundle contains cfire");
#endif
        require(std::filesystem::is_regular_file(bundle_dir / "runtime" / "cfire-runtime.version", ec),
            "portable bundle writes runtime/cfire-runtime.version");
        require(!std::filesystem::exists(bundle_dir / "runtime" / "VERSION", ec),
            "portable bundle excludes VERSION header conflict file");
        require(std::filesystem::is_regular_file(bundle_dir / "runtime" / "vendor" / "sqlite3" / "sqlite3.c", ec),
            "portable bundle includes vendored sqlite3.c");
        require(std::filesystem::is_regular_file(bundle_dir / "runtime" / "vendor" / "simdjson" / "simdjson.cpp", ec),
            "portable bundle includes vendored simdjson.cpp");
        require(!std::filesystem::exists(bundle_dir / "pkg", ec),
            "portable bundle contains no dead pkg directory");

        auto stray_artifacts = find_files_with_exts(bundle_dir, {".pch", ".a", ".o"});
        require(stray_artifacts.empty(),
            std::format("portable bundle contains zero .pch/.a/.o files (found {})",
                        stray_artifacts.size()));

#if !defined(_WIN32)
        // Mock release directory tested with file:// against docs/install.sh
        auto mock_dir = tmp.path / "mock_release";
        std::filesystem::create_directories(mock_dir, ec);

        int u_rc = 0;
        std::string uname_s = cfire::bridge::run_command_capture("uname -s", u_rc);
        std::string uname_m = cfire::bridge::run_command_capture("uname -m", u_rc);
        std::string target_os = (uname_s.find("Darwin") != std::string::npos) ? "macos" : "linux";
        std::string target_arch = (uname_m.find("arm64") != std::string::npos || uname_m.find("aarch64") != std::string::npos)
            ? "arm64"
            : "x86_64";
        std::string bundle_name = std::format("cfire-v0.11.0-{}-{}", target_os, target_arch);
        std::string archive_name = bundle_name + ".tar.gz";

        require(copy_dir_recursive(bundle_dir, mock_dir / bundle_name),
            "stage mock release directory");

        int tar_rc = 0;
        std::string tar_out = cfire::bridge::run_command_capture(
            std::format("tar -czf \"{}\" -C \"{}\" \"{}\"",
                        (mock_dir / archive_name).string(), mock_dir.string(), bundle_name),
            tar_rc);
        require(tar_rc == 0 && std::filesystem::is_regular_file(mock_dir / archive_name, ec),
            std::format("package mock release tarball failed: {}", tar_out));

        std::string archive_bytes = read_text_file(mock_dir / archive_name);
        std::string archive_sha = cfire::sha256::hash_hex(archive_bytes);
        {
            std::ofstream sums(mock_dir / "SHA256SUMS.txt", std::ios::trunc);
            sums << archive_sha << "  " << archive_name << "\n";
        }

        auto prefix_ok = tmp.path / "prefix_ok";
        // Pre-seed a stale runtime header to prove docs/install.sh prunes deleted headers atomically.
        std::filesystem::create_directories(prefix_ok / "share" / "cfire" / "runtime", ec);
        {
            std::ofstream stale(prefix_ok / "share" / "cfire" / "runtime" / "stale_obsolete_header.hpp", std::ios::trunc);
            stale << "// stale header that must be pruned on upgrade\n";
        }

        auto install_sh = repo_root / "docs" / "install.sh";
        int sh_rc = 0;
        std::string sh_out = cfire::bridge::run_command_capture(
            std::format("CFIRE_RELEASE_BASE_URL=\"file://{}\" CFIRE_VERSION=\"v0.11.0\" "
                        "CFIRE_INSTALL_PREFIX=\"{}\" sh \"{}\"",
                        mock_dir.string(), prefix_ok.string(), install_sh.string()),
            sh_rc);
        require(sh_rc == 0,
            std::format("docs/install.sh verified mock binary install succeeded (rc={}):\n{}",
                        sh_rc, sh_out));
        require(std::filesystem::is_regular_file(prefix_ok / "bin" / "cfire", ec),
            "installed prefix contains bin/cfire");
        require(std::filesystem::is_regular_file(prefix_ok / "share" / "cfire" / "runtime" / "cfire-runtime.version", ec),
            "installed prefix contains share/cfire/runtime/cfire-runtime.version");
        require(std::filesystem::is_regular_file(prefix_ok / "share" / "cfire" / "runtime" / "cfire_prelude.hpp", ec),
            "installed prefix contains share/cfire/runtime/cfire_prelude.hpp");
        require(!std::filesystem::exists(prefix_ok / "share" / "cfire" / "runtime" / "stale_obsolete_header.hpp", ec),
            "docs/install.sh atomic runtime swap removes stale obsolete runtime headers");
        int ver_rc = 0;
        std::string ver_out = cfire::bridge::run_command_capture(
            std::format("\"{}\" --version", (prefix_ok / "bin" / "cfire").string()), ver_rc);
        require(ver_rc == 0 && !ver_out.empty(),
            std::format("installed cfire --version works cleanly (rc={}, out='{}')", ver_rc, ver_out));

        // Verify wget redirect tag resolution fallback when curl is bypassed via CFIRE_FORCE_WGET=1.
        {
            auto mock_bin = tmp.path / "mock_bin";
            auto prefix_wget = tmp.path / "prefix_wget";
            std::filesystem::create_directories(mock_bin, ec);
            auto fake_wget = mock_bin / "wget";
            {
                std::ofstream wofs(fake_wget, std::ios::trunc);
                wofs << "#!/bin/sh\n"
                     << "printf 'HTTP/1.1 302 Found\\r\\nLocation: https://github.com/gsandoval/cfire/releases/tag/v0.11.0\\r\\n' >&2\n"
                     << "exit 0\n";
            }
            std::filesystem::permissions(
                fake_wget,
                std::filesystem::perms::owner_all | std::filesystem::perms::group_read | std::filesystem::perms::group_exec,
                ec);
            const char* old_path_env = std::getenv("PATH");
            std::string test_path = mock_bin.string() + ":" + (old_path_env ? old_path_env : "/usr/bin:/bin");
            int wget_rc = 0;
            std::string wget_out = cfire::bridge::run_command_capture(
                std::format("PATH=\"{}\" CFIRE_FORCE_WGET=1 "
                            "CFIRE_LATEST_RELEASE_URL=\"https://mock.invalid/releases/latest\" "
                            "CFIRE_RELEASE_BASE_URL=\"file://{}\" "
                            "CFIRE_INSTALL_PREFIX=\"{}\" sh \"{}\"",
                            test_path, mock_dir.string(), prefix_wget.string(), install_sh.string()),
                wget_rc);
            require(wget_rc == 0 && std::filesystem::is_regular_file(prefix_wget / "bin" / "cfire", ec),
                std::format("docs/install.sh wget spider redirect resolves tag cleanly (rc={}, out='{}')",
                            wget_rc, wget_out));
        }

        // Corrupt checksum and verify install.sh aborts before installing
        {
            std::ofstream sums(mock_dir / "SHA256SUMS.txt", std::ios::trunc);
            sums << "0000000000000000000000000000000000000000000000000000000000000000  " << archive_name << "\n";
        }
        auto prefix_bad = tmp.path / "prefix_bad";
        int bad_rc = 0;
        std::string bad_out = cfire::bridge::run_command_capture(
            std::format("CFIRE_RELEASE_BASE_URL=\"file://{}\" CFIRE_VERSION=\"v0.11.0\" "
                        "CFIRE_INSTALL_PREFIX=\"{}\" sh \"{}\"",
                        mock_dir.string(), prefix_bad.string(), install_sh.string()),
            bad_rc);
        require(bad_rc != 0,
            std::format("docs/install.sh rejects corrupted SHA256SUMS.txt (got rc={}, out='{}')",
                        bad_rc, bad_out));
        require(!std::filesystem::exists(prefix_bad / "bin" / "cfire", ec),
            "rejected install leaves destination clean");

        // Test semantic bump math (patch / minor / major) in scripts/release.sh inside an isolated sandbox.
        {
            auto rel_sandbox = tmp.path / "release_semver_sandbox";
            std::filesystem::create_directories(rel_sandbox / "scripts", ec);
            std::filesystem::create_directories(rel_sandbox / "src", ec);
            std::filesystem::copy_file(repo_root / "scripts" / "release.sh",
                                       rel_sandbox / "scripts" / "release.sh",
                                       std::filesystem::copy_options::overwrite_existing, ec);
            auto reset_ver = [&](std::string_view v) {
                std::ofstream(rel_sandbox / "VERSION", std::ios::trunc) << v << "\n";
                std::ofstream(rel_sandbox / "src" / "version.hpp", std::ios::trunc)
                    << "inline constexpr std::string_view kVersion = \"" << v << "\";\n";
            };
            auto run_bump = [&](std::string_view bump, std::string_view expected) {
                reset_ver("0.11.0");
                int brc = 0;
                std::string bout = cfire::bridge::run_command_capture(
                    std::format("CFIRE_RELEASE_SKIP_BUILD=1 sh \"{}\" {} --dry-run",
                                (rel_sandbox / "scripts" / "release.sh").string(), bump),
                    brc);
                std::string got_ver = read_text_file(rel_sandbox / "VERSION");
                std::string got_hdr = read_text_file(rel_sandbox / "src" / "version.hpp");
                require(brc == 0 &&
                        got_ver == std::format("{}\n", expected) &&
                        got_hdr.contains(std::format("kVersion = \"{}\"", expected)),
                    std::format("scripts/release.sh {} --dry-run advances 0.11.0 -> {} (rc={}, ver='{}', out='{}')",
                                bump, expected, brc, got_ver, bout));
            };
            run_bump("patch", "0.11.1");
            run_bump("minor", "0.12.0");
            run_bump("major", "1.0.0");
        }
#else
        // Real Windows PowerShell E2E test for docs/install.ps1 via file:// release directory.
        {
            auto mock_dir = tmp.path / "mock_win_release";
            std::filesystem::create_directories(mock_dir, ec);
            SYSTEM_INFO si{};
            ::GetNativeSystemInfo(&si);
            std::string win_arch = (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64)
                ? "windows-arm64"
                : "windows-x86_64";
            std::string bundle_name = std::format("cfire-v0.11.0-{}", win_arch);
            std::string archive_name = bundle_name + ".zip";
            require(copy_dir_recursive(bundle_dir, mock_dir / bundle_name),
                "stage mock Windows release bundle");

            auto ps_sq = [](std::string_view s) {
                std::string out = "'";
                for (char c : s) {
                    if (c == '\'') out += "''";
                    else out.push_back(c);
                }
                out.push_back('\'');
                return out;
            };
            int zip_rc = 0;
            std::string zip_cmd = std::format(
                "powershell -NoProfile -ExecutionPolicy Bypass -Command \"$ErrorActionPreference='Stop'; $ProgressPreference='SilentlyContinue'; "
                "Compress-Archive -Path {} -DestinationPath {} -Force\"",
                ps_sq((mock_dir / bundle_name).string()),
                ps_sq((mock_dir / archive_name).string()));
            std::string zip_out = cfire::bridge::run_command_capture(zip_cmd, zip_rc);
            require(zip_rc == 0 && std::filesystem::is_regular_file(mock_dir / archive_name, ec),
                std::format("Compress-Archive packages Windows mock release zip (rc={}): {}", zip_rc, zip_out));

            std::string archive_bytes = read_text_file(mock_dir / archive_name);
            std::string archive_sha = cfire::sha256::hash_hex(archive_bytes);
            {
                std::ofstream sums(mock_dir / "SHA256SUMS.txt", std::ios::trunc);
                sums << archive_sha << "  " << archive_name << "\n";
            }

            auto win_ok = tmp.path / "win_install_ok";
            std::filesystem::create_directories(win_ok / "runtime", ec);
            {
                std::ofstream stale(win_ok / "runtime" / "stale_obsolete_header.hpp", std::ios::trunc);
                stale << "// stale Windows header\n";
            }

            auto install_ps1 = repo_root / "docs" / "install.ps1";
            std::string mock_url = "file:///" + mock_dir.generic_string();
            {
                ScopedEnvVar env_base("CFIRE_RELEASE_BASE_URL", mock_url);
                ScopedEnvVar env_ver("CFIRE_VERSION", "v0.11.0");
                ScopedEnvVar env_dest("CFIRE_INSTALL_DIR", win_ok.string());
                ScopedEnvVar env_nopath("CFIRE_NO_MODIFY_PATH", "1");

                int ps_rc = 0;
                std::string ps_cmd = std::format(
                    "powershell -NoProfile -ExecutionPolicy Bypass -File \"{}\"",
                    install_ps1.string());
                std::string ps_out = cfire::bridge::run_command_capture(ps_cmd, ps_rc);
                require(ps_rc == 0,
                    std::format("docs/install.ps1 mock file:// install succeeded (rc={}):\n{}", ps_rc, ps_out));
            }
            require(std::filesystem::is_regular_file(win_ok / "cfire.exe", ec),
                "docs/install.ps1 installs cfire.exe");
            require(std::filesystem::is_regular_file(win_ok / "runtime" / "cfire_prelude.hpp", ec),
                "docs/install.ps1 installs runtime/cfire_prelude.hpp");
            require(!std::filesystem::exists(win_ok / "runtime" / "stale_obsolete_header.hpp", ec),
                "docs/install.ps1 atomic runtime swap prunes stale obsolete headers");
            int win_ver_rc = 0;
            std::string win_ver_out = cfire::bridge::run_command_capture(
                std::format("\"{}\" --version", (win_ok / "cfire.exe").string()), win_ver_rc);
            require(win_ver_rc == 0 && !win_ver_out.empty(),
                std::format("installed Windows cfire.exe --version runs cleanly (rc={}, out='{}')",
                            win_ver_rc, win_ver_out));

            // Corrupt SHA256SUMS.txt and verify install.ps1 rejects without mutating destination
            {
                std::ofstream sums(mock_dir / "SHA256SUMS.txt", std::ios::trunc);
                sums << "0000000000000000000000000000000000000000000000000000000000000000  " << archive_name << "\n";
            }
            auto win_bad = tmp.path / "win_install_bad";
            {
                ScopedEnvVar env_base("CFIRE_RELEASE_BASE_URL", mock_url);
                ScopedEnvVar env_ver("CFIRE_VERSION", "v0.11.0");
                ScopedEnvVar env_dest("CFIRE_INSTALL_DIR", win_bad.string());
                ScopedEnvVar env_nopath("CFIRE_NO_MODIFY_PATH", "1");

                int bad_ps_rc = 0;
                std::string bad_ps_out = cfire::bridge::run_command_capture(
                    std::format("powershell -NoProfile -ExecutionPolicy Bypass -File \"{}\"",
                                install_ps1.string()),
                    bad_ps_rc);
                require(bad_ps_rc != 0,
                    std::format("docs/install.ps1 rejects corrupted SHA256SUMS.txt (rc={}, out='{}')",
                                bad_ps_rc, bad_ps_out));
                require(!std::filesystem::exists(win_bad / "cfire.exe", ec),
                    "docs/install.ps1 leaves rejected destination clean");
            }
        }
#endif

        // Contract assertions for Dockerfile, .dockerignore, installers, .github/workflows/{release,ci}.yml, and pkg retirement
        std::string dockerignore_txt = read_text_file(repo_root / ".dockerignore");
        require(dockerignore_txt.contains("**/*.o") && dockerignore_txt.contains("**/*.d"),
            ".dockerignore excludes nested host .o and .d build artifacts");

        std::string df_txt = read_text_file(repo_root / "Dockerfile");
        require(!df_txt.contains("COPY pkg") && !df_txt.contains("pkg/"),
            "Dockerfile contains zero dead pkg references");
        {
            std::istringstream df_iss(df_txt);
            std::string df_line;
            while (std::getline(df_iss, df_line)) {
                auto first = df_line.find_first_not_of(" \t\r\n");
                if (first == std::string::npos) continue;
                std::string_view trimmed = std::string_view(df_line).substr(first);
                if (!trimmed.starts_with("COPY ")) continue;
                std::istringstream tok_ss{std::string(trimmed.substr(5))};
                std::vector<std::string> toks;
                std::string tok;
                while (tok_ss >> tok) {
                    if (tok.starts_with("--")) continue;
                    toks.push_back(std::move(tok));
                }
                if (toks.size() >= 2) {
                    for (std::size_t ti = 0; ti + 1 < toks.size(); ++ti) {
                        if (toks[ti].contains('*') || toks[ti].contains('?')) continue;
                        require(std::filesystem::exists(repo_root / toks[ti], ec),
                            std::format("Dockerfile COPY source '{}' exists in repo root", toks[ti]));
                    }
                }
            }
        }

        std::string sh_txt = read_text_file(repo_root / "docs" / "install.sh");
        require(!sh_txt.contains("/pkg\"") && !sh_txt.contains("/pkg/"),
            "docs/install.sh contains no dead pkg copy blocks");

        std::string ps1 = read_text_file(repo_root / "docs" / "install.ps1");
        require(ps1.find("Get-FileHash") != std::string::npos &&
                ps1.find("SHA256SUMS.txt") != std::string::npos &&
                ps1.find("windows-x86_64") != std::string::npos &&
                ps1.find("windows-arm64") != std::string::npos &&
                ps1.find("CFIRE_RELEASE_BASE_URL") != std::string::npos &&
                ps1.find("AllowAutoRedirect") != std::string::npos &&
                !ps1.contains("\"pkg\"") && !ps1.contains("\\pkg"),
            "docs/install.ps1 enforces SHA-256 verification, redirect lookup, x86_64/arm64 targets, and no pkg references");

        auto msys2_install_has_pkg = [](std::string_view yml, std::string_view pkg) -> bool {
            std::size_t search_from = 0;
            while (true) {
                auto msys_pos = yml.find("msys2/setup-msys2@", search_from);
                if (msys_pos == std::string_view::npos) return false;
                auto inst_pos = yml.find("install:", msys_pos);
                if (inst_pos == std::string_view::npos) return false;
                auto line_start = yml.rfind('\n', inst_pos);
                line_start = (line_start == std::string_view::npos) ? 0 : line_start + 1;
                std::size_t base_indent = 0;
                while (line_start + base_indent < inst_pos && yml[line_start + base_indent] == ' ') {
                    ++base_indent;
                }
                auto nl = yml.find('\n', inst_pos);
                if (nl == std::string_view::npos) return false;
                {
                    std::istringstream first_ss{std::string(yml.substr(inst_pos + 8, nl - (inst_pos + 8)))};
                    std::string tok;
                    while (first_ss >> tok) {
                        if (tok == ">-" || tok == ">" || tok == "|-" || tok == "|") continue;
                        if (tok == pkg) return true;
                    }
                }
                std::istringstream iss{std::string(yml.substr(nl + 1))};
                std::string line;
                while (std::getline(iss, line)) {
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    auto first = line.find_first_not_of(' ');
                    if (first == std::string::npos) continue;
                    if (first <= base_indent) break;
                    std::istringstream tok_ss{line};
                    std::string tok;
                    while (tok_ss >> tok) {
                        if (tok == pkg) return true;
                    }
                }
                search_from = nl + 1;
            }
        };

        std::string rel_yml = read_text_file(repo_root / ".github" / "workflows" / "release.yml");
        std::size_t tag_ref_count = 0;
        {
            const std::string needle = "ref: ${{ needs.prepare.outputs.tag }}";
            std::size_t pos = 0;
            while ((pos = rel_yml.find(needle, pos)) != std::string::npos) {
                ++tag_ref_count;
                pos += needle.size();
            }
        }
        require(rel_yml.find("linux-x86_64") != std::string::npos &&
                rel_yml.find("linux-arm64") != std::string::npos &&
                rel_yml.find("macos-arm64") != std::string::npos &&
                rel_yml.find("macos-x86_64") != std::string::npos &&
                rel_yml.find("windows-x86_64") != std::string::npos &&
                rel_yml.find("windows-arm64") != std::string::npos &&
                rel_yml.find("SHA256SUMS.txt") != std::string::npos &&
                rel_yml.find("13.3") != std::string::npos &&
                rel_yml.find("workflow_run") != std::string::npos &&
                rel_yml.find("concurrency:") != std::string::npos &&
                rel_yml.find("libssh-dev") != std::string::npos &&
                rel_yml.find("libwebkit2gtk-4.1-dev") != std::string::npos &&
                rel_yml.find("deploy-pages") != std::string::npos &&
                tag_ref_count >= 4 &&
                !rel_yml.contains("cp -R pkg") &&
                !rel_yml.contains("src/main.cpp") &&
                msys2_install_has_pkg(rel_yml, "make") &&
                msys2_install_has_pkg(rel_yml, "unzip") &&
                msys2_install_has_pkg(rel_yml, "tar") &&
                msys2_install_has_pkg(rel_yml, "mingw-w64-clang-x86_64-lld") &&
                msys2_install_has_pkg(rel_yml, "mingw-w64-clang-aarch64-compiler-rt") &&
                msys2_install_has_pkg(rel_yml, "mingw-w64-clang-aarch64-libunwind") &&
                msys2_install_has_pkg(rel_yml, "mingw-w64-clang-aarch64-headers") &&
                !rel_yml.contains("-headers-git") &&
                !rel_yml.contains("-crt-git") &&
                !rel_yml.contains("-winpthreads-git") &&
                rel_yml.contains("brew install libsodium libssh zstd") &&
                rel_yml.contains("-resource-dir="),
            ".github/workflows/release.yml defines unified auto-patch/dispatch pipeline, 6 targets, tag refs, deps, MSYS2/ARM64/macOS fixes, Pages deploy, and no pkg/main.cpp workarounds");

        std::string ci_yml = read_text_file(repo_root / ".github" / "workflows" / "ci.yml");
        require(ci_yml.contains("docker build --target release-linux-musl") &&
                msys2_install_has_pkg(ci_yml, "make") &&
                msys2_install_has_pkg(ci_yml, "tar") &&
                ci_yml.contains("include-hidden-files: true"),
            ".github/workflows/ci.yml verifies Dockerfile release-linux-musl stage, MSYS2 make/tar, and hidden fuzz crash artifact upload");

        std::string fuzz_yml = read_text_file(repo_root / ".github" / "workflows" / "fuzz-nightly.yml");
        require(fuzz_yml.contains("libssh-dev") &&
                fuzz_yml.contains("update-alternatives --install /usr/bin/clang clang /usr/bin/clang-18") &&
                fuzz_yml.contains("include-hidden-files: true"),
            ".github/workflows/fuzz-nightly.yml installs libssh-dev, registers clang-18, and uploads hidden fuzz artifacts");

        // Verify pkg:tako -> core:tako fix-it, pkg:custom -> RETIRED-API, while relative ./pkg/... stays working
        require_sema_error("package main\nimport \"pkg:tako\" as t\nfn main() {}\n",
            "cfire/INVALID-IMPORT");
        require_sema_error("package main\nimport \"pkg:tako\" as t\nfn main() {}\n",
            "core:tako");
        require_sema_error("package main\nimport \"pkg:custom\"\nfn main() {}\n",
            "cfire/RETIRED-API");
        {
            ScopedTempDir rel_pkg("rel_local_pkg");
            rel_pkg.file("pkg/sub.cf", "package sub\n\nfn answer() -> i32 {\n    return 42\n}\n");
            auto main_cf = rel_pkg.file("main.cf",
                "package main\nimport \"./pkg/sub\" as sub\n\nfn main() {\n    println(\"{}\", sub.answer())\n}\n");
            auto r_pkg = run_cfire(std::format("run \"{}\"", main_cf.string()));
            r_pkg.assert_ok("relative ./pkg/sub local import builds and runs cleanly");
            r_pkg.assert_contains("42", "relative ./pkg/sub outputs 42");
        }
    }

    std::println("--- PASS: test_vendored_archives_and_portable_releases");
}
