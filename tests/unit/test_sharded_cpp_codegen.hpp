#pragma once
// tests/unit/test_sharded_cpp_codegen.hpp — Tests for progressive per-package sharded C++23 codegen and parallel dirty-unit builds

#include "../test_harness.hpp"
#include "../../src/codegen.hpp"
#include "../../src/build/cache_paths.hpp"
#include "../../src/build/package_loader.hpp"
#include "../../src/build/pch.hpp"
#include "../../src/sema.hpp"

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
#include <thread>
#include <vector>

namespace test_sharded_codegen_detail {

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

struct ScopedBuildFlags {
    bool prev_no_cache = false;
    bool prev_no_shard = false;
    cfire::build::BuildProfile prev_profile = cfire::build::BuildProfile::Release;
    bool prev_debug_mode = false;

    ScopedBuildFlags(bool no_cache, bool no_shard)
        : prev_no_cache(cfire::build::g_no_binary_cache),
          prev_no_shard(cfire::build::g_no_shard),
          prev_profile(cfire::build::g_build_profile),
          prev_debug_mode(cfire::build::g_debug_mode) {
        cfire::build::g_no_binary_cache = no_cache;
        cfire::build::g_no_shard = no_shard;
    }

    void set(bool no_cache, bool no_shard) {
        cfire::build::g_no_binary_cache = no_cache;
        cfire::build::g_no_shard = no_shard;
    }

    ~ScopedBuildFlags() {
        cfire::build::g_no_binary_cache = prev_no_cache;
        cfire::build::g_no_shard = prev_no_shard;
        cfire::build::g_build_profile = prev_profile;
        cfire::build::g_debug_mode = prev_debug_mode;
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

inline void write_text(const std::filesystem::path& p, std::string_view s) {
    std::error_code ec;
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(s.data(), static_cast<std::streamsize>(s.size()));
    f.flush();
}

[[nodiscard]] inline auto read_text(const std::filesystem::path& p) -> std::string {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

[[nodiscard]] inline auto exe_path(const std::filesystem::path& base) -> std::filesystem::path {
#if defined(_WIN32)
    auto p = base;
    if (p.extension() != ".exe") p += ".exe";
    return p;
#else
    return base;
#endif
}

[[nodiscard]] inline auto trim_str(std::string s) -> std::string {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) {
        s.pop_back();
    }
    return s;
}

} // namespace test_sharded_codegen_detail

void test_sharded_cpp_codegen(const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_sharded_cpp_codegen");

    test_sharded_codegen_detail::ScopedTempRoot root("cfire_sharded_codegen");
    const auto cache_root = root.dir / "cache";
    test_sharded_codegen_detail::ScopedEnvVar cache_env("CFIRE_CACHE_DIR", cache_root.string());
    test_sharded_codegen_detail::ScopedBuildFlags flags_guard(false, false);

    // 1. Subtest A: Direct emit_sharded_cpp linkage invariants & -Wundefined-inline -Werror compile/link
    {
        const auto sub_a = root.dir / "sub_a";
        test_sharded_codegen_detail::write_text(sub_a / "pkga" / "pkga.cf", R"CF(package pkga

type Describable interface {
    fn tag() -> str
}

type Counter struct {
    value: i64
}

mut pkg_bonus: i64 = 5

fn (c: Counter) tag() -> str {
    return "counter:{c.value}"
}

fn (c: Counter) doubled() -> i64 {
    return c.value * 2 + pkg_bonus
}

fn id_val[T](x: T) -> T {
    return x
}

fn bump(n: i64) -> i64 {
    return n + 10
}
)CF");

        test_sharded_codegen_detail::write_text(sub_a / "pkgb" / "pkgb.cf", R"CF(package pkgb

fn helper(n: i64) -> i64 {
    return n * 3
}
)CF");

        const auto main_cf = sub_a / "main.cf";
        test_sharded_codegen_detail::write_text(main_cf, R"CF(package main

import "./pkga" as pa
import "./pkgb" as pb

fn main() {
    sc := make(shared[pa.Counter], pa.Counter{value: 7})
    oc := make(own[pa.Counter], pa.Counter{value: 9})
    t := sc.tag()
    d1 := sc.doubled()
    d2 := oc.doubled()
    g := pa.id_val(pa.bump(pb.helper(4)))
    println("{} {} {} {}", t, d1, d2, g)
}
)CF");

        auto raw = cfire::build::read_file_text(main_cf);
        require(raw.has_value(), "Subtest A reads main.cf: " + (raw ? "" : raw.error()));
        if (!raw) return;
        auto ast = cfire::frontend::parse_source(main_cf.string(), *raw);
        require(ast.has_value(), "Subtest A parses main.cf: " + (ast ? "" : ast.error()));
        if (!ast) return;

        auto plan_res = cfire::build::init_build_plan_flags(main_cf, rt_dir, false, "", "", "auto", "auto", false);
        require(plan_res.has_value(), "Subtest A init build plan: " + (plan_res ? "" : plan_res.error()));
        if (!plan_res) return;
        cfire::build::BuildPlan plan = std::move(*plan_res);
        auto imp_res = cfire::build::resolve_local_cf_imports(sub_a, *ast, plan, rt_dir, false);
        require(imp_res.has_value(), "Subtest A resolves local package imports: " + (imp_res ? "" : imp_res.error()));
        if (!imp_res) return;

        auto checked = cfire::frontend::analyze(std::move(*ast));
        require(checked.has_value(), "Subtest A semantic analysis passes: " + (checked ? "" : checked.error()));
        if (!checked) return;

        std::string single_before = cfire::frontend::emit_cpp(*checked, false);
        auto sharded = cfire::frontend::emit_sharded_cpp(*checked, false);
        std::string single_after = cfire::frontend::emit_cpp(*checked, false);
        require(single_before == single_after, "emit_sharded_cpp leaves emit_cpp byte-for-byte deterministic");
        require(sharded.units.size() == 3, "Subtest A produces 3 package units (main, pkga, pkgb)");

        // Verify linkage rules on decls.hpp and each pkg_*.cpp
        require(sharded.decls_hpp.contains("#pragma once"), "decls.hpp starts with #pragma once");
        require(sharded.decls_hpp.contains("#include \"core/str.hpp\""), "decls.hpp includes core/str.hpp unconditionally");
        require(sharded.decls_hpp.contains("#include \"core/builtin_methods.hpp\""), "decls.hpp includes core/builtin_methods.hpp unconditionally");
        require(sharded.decls_hpp.contains("concept Describable"), "decls.hpp contains interface concept");
        require(sharded.decls_hpp.contains("inline std::int64_t pkg_bonus"), "decls.hpp keeps package global inline");
        require(sharded.decls_hpp.contains("auto bump(") && !sharded.decls_hpp.contains("inline auto bump("),
            "decls.hpp emits non-inline prototype for concrete free function");
        require(sharded.decls_hpp.contains("auto helper(") && !sharded.decls_hpp.contains("inline auto helper("),
            "decls.hpp emits non-inline prototype for second package concrete free function");
        require(sharded.decls_hpp.contains("auto Counter__doubled(") && !sharded.decls_hpp.contains("inline auto Counter__doubled("),
            "decls.hpp emits non-inline prototype for concrete method wrapper");
        require(sharded.decls_hpp.contains("template <typename T>\n[[nodiscard]] inline auto id_val("),
            "decls.hpp keeps generic function template inline");

        for (const auto& u : sharded.units) {
            require(u.cpp.contains("#include \"decls.hpp\""), "each pkg unit includes decls.hpp");
            require(!u.cpp.contains("inline auto bump(") && !u.cpp.contains("inline auto helper(") &&
                    !u.cpp.contains("inline auto Counter__doubled(") && !u.cpp.contains("inline auto cfire_method_doubled("),
                "concrete definitions in pkg_*.cpp omit inline specifier");
        }

        // Write shards & compile manually with -Wundefined-inline -Werror to catch any C++23 [dcl.inline] bugs
        const auto shard_out = sub_a / "manual_shards";
        test_sharded_codegen_detail::write_text(shard_out / "decls.hpp", sharded.decls_hpp);
        std::string cxx = cfire::build::resolve_cxx_compiler("");
        std::vector<std::filesystem::path> objs;
        for (const auto& u : sharded.units) {
            auto src_p = shard_out / u.filename;
            auto obj_p = shard_out / (u.id + ".o");
            test_sharded_codegen_detail::write_text(src_p, u.cpp);
            int rc = 0;
            std::string cmd = std::format(
                "{} -std=c++2b -Wall -Wextra -Werror -Wundefined-inline -Wno-unused-parameter -Wno-parentheses-equality -I{} -iquote {} -c {} -o {} 2>&1",
                cxx,
                cfire::bridge::shell_quote(rt_dir.string()),
                cfire::bridge::shell_quote(shard_out.string()),
                cfire::bridge::shell_quote(src_p.string()),
                cfire::bridge::shell_quote(obj_p.string()));
            std::string c_out = cfire::bridge::run_command_capture(cmd, rc);
            require(rc == 0, std::format("-Wundefined-inline -Werror compile failed for {}: {}", u.filename, c_out));
            objs.push_back(obj_p);
        }

        auto manual_bin = test_sharded_codegen_detail::exe_path(shard_out / "manual_bin");
        std::string link_cmd = cxx;
        for (const auto& o : objs) link_cmd += " " + cfire::bridge::shell_quote(o.string());
        link_cmd += " -pthread -o " + cfire::bridge::shell_quote(manual_bin.string()) + " 2>&1";
        int link_rc = 0;
        std::string link_out = cfire::bridge::run_command_capture(link_cmd, link_rc);
        require(link_rc == 0, "manual sharded link succeeded: " + link_out);

        int run_rc = 0;
        std::string run_out = test_sharded_codegen_detail::trim_str(
            cfire::bridge::run_command_capture(cfire::bridge::shell_quote(manual_bin.string()) + " 2>&1", run_rc));
        require(run_rc == 0 && run_out == "counter:7 19 23 22",
            "manual sharded binary produced expected output, got: '" + run_out + "'");
    }

    // 2. Subtest B: 5-Package Incremental Dirty-Unit Rebuild Matrix, decls.hpp Stability, --no-shard & BinaryCache
    {
        const auto proj = root.dir / "five_pkg";
        test_sharded_codegen_detail::write_text(proj / "pkg_a" / "pkg_a.cf", R"CF(package pkg_a

type Item struct {
    value: i64
}

fn (it: Item) total() -> i64 {
    return it.value
}

fn wrap_generic[T](v: T) -> T {
    return v
}

fn a_val() -> i64 {
    it := Item{value: 40}
    return it.total()
}
)CF");

        const auto pkg_b_file = proj / "pkg_b" / "pkg_b.cf";
        test_sharded_codegen_detail::write_text(pkg_b_file, R"CF(package pkg_b

fn b_val(x: i64) -> i64 {
    return x + 10
}
)CF");

        test_sharded_codegen_detail::write_text(proj / "pkg_c" / "pkg_c.cf", R"CF(package pkg_c

mut c_offset: i64 = 20

fn c_val(x: i64) -> i64 {
    return x + c_offset
}
)CF");

        test_sharded_codegen_detail::write_text(proj / "pkg_d" / "pkg_d.cf", R"CF(package pkg_d

fn d_val(x: i64) -> i64 {
    return x + 30
}
)CF");

        const auto entry_cf = proj / "app.cf";
        test_sharded_codegen_detail::write_text(entry_cf, R"CF(package main

import "./pkg_a" as pa
import "./pkg_b" as pb
import "./pkg_c" as pc
import "./pkg_d" as pd

fn local_add(x: i64) -> i64 {
    return x
}

fn main() {
    r := pa.wrap_generic(local_add(pa.a_val()) + pb.b_val(0) + pc.c_val(0) + pd.d_val(0))
    println("res={}", r)
}
)CF");

        const auto out_sharded = test_sharded_codegen_detail::exe_path(proj / "app_sharded");
        const auto decls_path = proj / ".cfire" / "gen" / "shards" / "app" / "decls.hpp";

        // Step B1: Clean build with BinaryCache bypassed -> compiles all 5 units
        flags_guard.set(true, false);
        auto b1 = cfire::build::build_native(entry_cf, rt_dir, out_sharded);
        require(b1.has_value(), "5-pkg clean build succeeds: " + (b1 ? "" : b1.error()));
        if (!b1) return;
        require(b1->used_sharded, "5-pkg build uses sharded compilation");
        require(b1->sharded_total_units == 5, std::format("expected 5 shard units, got {}", b1->sharded_total_units));
        require(b1->sharded_recompiled_units == 5, std::format("clean build recompiles 5 units, got {}", b1->sharded_recompiled_units));
        require(b1->sharded_object_files.size() == 5, "BuildPlan records 5 object files");
        std::error_code ec;
        require(std::filesystem::exists(decls_path, ec), "decls.hpp written in .cfire/gen/shards/app/");

        int run_rc = 0;
        std::string sharded_out = test_sharded_codegen_detail::trim_str(
            cfire::bridge::run_command_capture(cfire::bridge::shell_quote(out_sharded.string()) + " 2>&1", run_rc));
        require(run_rc == 0 && sharded_out == "res=100", "5-pkg clean binary output expected 'res=100', got: '" + sharded_out + "'");

        // Step B2: No-op rebuild -> 0 dirty units recompiled, decls.hpp untouched
        const std::string decls_content_1 = test_sharded_codegen_detail::read_text(decls_path);
        const auto decls_mtime_1 = std::filesystem::last_write_time(decls_path, ec);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        auto b2 = cfire::build::build_native(entry_cf, rt_dir, out_sharded);
        require(b2.has_value(), "5-pkg no-op rebuild succeeds: " + (b2 ? "" : b2.error()));
        if (!b2) return;
        require(b2->used_sharded && b2->sharded_total_units == 5 && b2->sharded_recompiled_units == 0,
            std::format("no-op rebuild expected 0 dirty units, got {}", b2->sharded_recompiled_units));
        require(test_sharded_codegen_detail::read_text(decls_path) == decls_content_1, "no-op rebuild keeps decls.hpp identical");
        require(std::filesystem::last_write_time(decls_path, ec) == decls_mtime_1, "no-op rebuild preserves decls.hpp mtime");

        // Step B3: Single-package body edit in pkg_b (introducing .trim() without dirtying decls.hpp)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        test_sharded_codegen_detail::write_text(pkg_b_file, R"CF(package pkg_b

fn b_val(x: i64) -> i64 {
    bonus := i64("  hello  ".trim().len())
    return x + 15 + bonus
}
)CF");

        auto b3 = cfire::build::build_native(entry_cf, rt_dir, out_sharded);
        require(b3.has_value(), "5-pkg single-body edit rebuild succeeds: " + (b3 ? "" : b3.error()));
        if (!b3) return;
        require(b3->used_sharded && b3->sharded_total_units == 5, "single-body rebuild remains sharded with 5 units");
        require(b3->sharded_recompiled_units == 1,
            std::format("single-package body edit should recompile ONLY 1 unit, got {}", b3->sharded_recompiled_units));
        require(test_sharded_codegen_detail::read_text(decls_path) == decls_content_1,
            "body-only edit keeps decls.hpp content unchanged");
        require(std::filesystem::last_write_time(decls_path, ec) == decls_mtime_1,
            "body-only edit preserves decls.hpp mtime via write_file_if_changed");

        std::string sharded_out_3 = test_sharded_codegen_detail::trim_str(
            cfire::bridge::run_command_capture(cfire::bridge::shell_quote(out_sharded.string()) + " 2>&1", run_rc));
        require(run_rc == 0 && sharded_out_3 == "res=110",
            "body edit binary output expected 'res=110', got: '" + sharded_out_3 + "'");

        // Step B4: Struct layout edit in pkg_a invalidates decls.hpp and cleanly recompiles all 5 units
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        test_sharded_codegen_detail::write_text(proj / "pkg_a" / "pkg_a.cf", R"CF(package pkg_a

type Item struct {
    value: i64
    bonus: i64 = 7
}

fn (it: Item) total() -> i64 {
    return it.value + it.bonus
}

fn wrap_generic[T](v: T) -> T {
    return v
}

fn a_val() -> i64 {
    it := Item{value: 40, bonus: 7}
    return it.total()
}
)CF");

        auto b4 = cfire::build::build_native(entry_cf, rt_dir, out_sharded);
        require(b4.has_value(), "struct edit rebuild succeeds: " + (b4 ? "" : b4.error()));
        if (!b4) return;
        require(b4->used_sharded && b4->sharded_total_units == 5, "struct edit rebuild stays sharded");
        require(b4->sharded_recompiled_units == 5,
            std::format("struct edit in decls.hpp must recompile all 5 units, got {}", b4->sharded_recompiled_units));
        require(test_sharded_codegen_detail::read_text(decls_path) != decls_content_1, "struct edit updates decls.hpp");

        std::string sharded_out_4 = test_sharded_codegen_detail::trim_str(
            cfire::bridge::run_command_capture(cfire::bridge::shell_quote(out_sharded.string()) + " 2>&1", run_rc));
        require(run_rc == 0 && sharded_out_4 == "res=117",
            "struct edit binary output expected 'res=117', got: '" + sharded_out_4 + "'");

        // Step B5: --no-shard parity check
        const auto out_noshard = test_sharded_codegen_detail::exe_path(proj / "app_noshard");
        flags_guard.set(true, true);
        auto b_ns = cfire::build::build_native(entry_cf, rt_dir, out_noshard);
        require(b_ns.has_value() && !b_ns->used_sharded, "--no-shard disables sharded codegen");
        std::string noshard_out = test_sharded_codegen_detail::trim_str(
            cfire::bridge::run_command_capture(cfire::bridge::shell_quote(out_noshard.string()) + " 2>&1", run_rc));
        require(run_rc == 0 && noshard_out == sharded_out_4,
            "--no-shard binary output matches sharded binary identically");

        // Step B6: BinaryCache interaction in Release and DevDebug sharded modes
        const auto out_cached_rel = test_sharded_codegen_detail::exe_path(proj / "app_cached_rel");
        flags_guard.set(false, false);
        cfire::build::set_build_profile(cfire::build::BuildProfile::Release);
        auto b_r1 = cfire::build::build_native(entry_cf, rt_dir, out_cached_rel);
        require(b_r1.has_value() && b_r1->used_sharded, "sharded Release build with BinaryCache enabled succeeds");
        std::filesystem::remove(out_cached_rel, ec);
        std::filesystem::remove_all(proj / ".cfire" / "obj", ec);
        auto b_r2 = cfire::build::build_native(entry_cf, rt_dir, out_cached_rel);
        require(b_r2.has_value() && b_r2->used_sharded && b_r2->sharded_recompiled_units == 0,
            "sharded Release BinaryCache restore hits cleanly without .cfire/obj or recompiling units");
        std::string cached_rel_out = test_sharded_codegen_detail::trim_str(
            cfire::bridge::run_command_capture(cfire::bridge::shell_quote(out_cached_rel.string()) + " 2>&1", run_rc));
        require(run_rc == 0 && cached_rel_out == "res=117",
            "Release BinaryCache-restored sharded binary executes with expected output");

        const auto out_cached_dbg = test_sharded_codegen_detail::exe_path(proj / "app_cached_dbg");
        cfire::build::set_build_profile(cfire::build::BuildProfile::DevDebug);
        auto b_d1 = cfire::build::build_native(entry_cf, rt_dir, out_cached_dbg);
        require(b_d1.has_value() && b_d1->used_sharded, "sharded DevDebug build with BinaryCache enabled succeeds");
        std::filesystem::remove(out_cached_dbg, ec);
        auto b_d2 = cfire::build::build_native(entry_cf, rt_dir, out_cached_dbg);
        require(b_d2.has_value() && b_d2->used_sharded && b_d2->sharded_recompiled_units == 0,
            "sharded DevDebug BinaryCache restore hits cleanly without recompiling any units");
        std::string cached_dbg_out = test_sharded_codegen_detail::trim_str(
            cfire::bridge::run_command_capture(cfire::bridge::shell_quote(out_cached_dbg.string()) + " 2>&1", run_rc));
        require(run_rc == 0 && cached_dbg_out == "res=117",
            "DevDebug BinaryCache-restored sharded binary executes with expected output");

#if defined(__APPLE__)
        // Removing .cfire/obj/*.o must invalidate BinaryCache restore on macOS DevDebug so OSO maps stay valid
        std::filesystem::remove(out_cached_dbg, ec);
        std::filesystem::remove_all(proj / ".cfire" / "obj", ec);
        auto b_d3 = cfire::build::build_native(entry_cf, rt_dir, out_cached_dbg);
        require(b_d3.has_value() && b_d3->used_sharded && b_d3->sharded_recompiled_units == 5,
            "missing .cfire/obj on macOS DevDebug invalidates BinaryCache restore and rebuilds all 5 .o files");
#endif
    }

    std::println("--- PASS: test_sharded_cpp_codegen");
}
