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
        cfire::detail::SecureTempDir sec(prefix);
        dir = sec.release();
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

pub type Describable interface {
    fn tag() -> str
}

pub type Counter struct {
    value: i64
}

mut pkg_bonus: i64 = 5

fn (c: Counter) tag() -> str {
    return "counter:{c.value}"
}

fn (c: Counter) doubled() -> i64 {
    return c.value * 2 + pkg_bonus
}

pub fn id_val[T](x: T) -> T {
    return x
}

pub fn bump(n: i64) -> i64 {
    return n + 10
}
)CF");

        test_sharded_codegen_detail::write_text(sub_a / "pkgb" / "pkgb.cf", R"CF(package pkgb

pub fn helper(n: i64) -> i64 {
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

        // Verify per-package header & 1:1 linkage rules on preamble.hpp, pkg_*.hpp, and each unit_*.cpp
        require(sharded.preamble_hpp.contains("#pragma once"), "preamble.hpp starts with #pragma once");
        require(sharded.preamble_hpp.contains("#include \"core/str.hpp\""), "preamble.hpp includes core/str.hpp unconditionally");
        require(sharded.preamble_hpp.contains("#include \"core/builtin_methods.hpp\""), "preamble.hpp includes core/builtin_methods.hpp unconditionally");

        auto find_hdr = [&](std::string_view pkg) -> const cfire::frontend::CppHeader* {
            for (const auto& h : sharded.headers) {
                if (h.pkg_name == pkg) return &h;
            }
            return nullptr;
        };
        const auto* hdr_pa = find_hdr("pa");
        const auto* hdr_pb = find_hdr("pb");
        const auto* hdr_main = find_hdr("main");
        require(hdr_pa != nullptr && hdr_pb != nullptr && hdr_main != nullptr,
            "emit_sharded_cpp emits separate per-package headers for main, pa, and pb");
        if (hdr_pa && hdr_pb && hdr_main) {
            require(hdr_pa->hpp.contains("concept Describable"), "pkg_pa.hpp contains interface concept");
            require(hdr_pa->hpp.contains("inline std::int64_t pkg_bonus"), "pkg_pa.hpp keeps package global inline");
            require(hdr_pa->hpp.contains("auto bump(") && !hdr_pa->hpp.contains("inline auto bump("),
                "pkg_pa.hpp emits non-inline prototype for concrete free function");
            require(hdr_pa->hpp.contains("auto Counter__doubled(") && !hdr_pa->hpp.contains("inline auto Counter__doubled("),
                "pkg_pa.hpp emits non-inline prototype for concrete method wrapper");
            require(hdr_pa->hpp.contains("template <typename T>\n[[nodiscard]] inline auto id_val("),
                "pkg_pa.hpp keeps generic function template inline");
            require(!hdr_pa->hpp.contains("auto helper("), "pkg_pa.hpp omits unrelated pb symbols");
            require(hdr_pb->hpp.contains("auto helper(") && !hdr_pb->hpp.contains("inline auto helper("),
                "pkg_pb.hpp emits non-inline prototype for second package concrete free function");
            require(!hdr_pb->hpp.contains("struct Counter"), "pkg_pb.hpp omits unrelated pa structs");
            require(hdr_main->hpp.contains("#include \"pkg_pa.hpp\"") && hdr_main->hpp.contains("#include \"pkg_pb.hpp\""),
                "pkg_main.hpp includes direct imported package headers");
        }

        for (const auto& u : sharded.units) {
            require(u.cpp.contains(std::format("#include \"{}\"", u.header_file)),
                "each sharded unit includes its own package header");
            require(!u.cpp.contains("inline auto bump(") && !u.cpp.contains("inline auto helper(") &&
                    !u.cpp.contains("inline auto Counter__doubled(") && !u.cpp.contains("inline auto cfire_method_doubled("),
                "concrete definitions in unit_*.cpp omit inline specifier");
        }

        // Write shards & compile manually with -Wundefined-inline -Werror to catch any C++23 [dcl.inline] bugs
        const auto shard_out = sub_a / "manual_shards";
        test_sharded_codegen_detail::write_text(shard_out / "decls.hpp", sharded.decls_hpp);
        for (const auto& h : sharded.headers) {
            test_sharded_codegen_detail::write_text(shard_out / h.filename, h.hpp);
        }
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

pub fn wrap_generic[T](v: T) -> T {
    return v
}

pub fn a_val() -> i64 {
    it := Item{value: 40}
    return it.total()
}
)CF");

        const auto pkg_b_file = proj / "pkg_b" / "pkg_b.cf";
        test_sharded_codegen_detail::write_text(pkg_b_file, R"CF(package pkg_b

pub fn b_val(x: i64) -> i64 {
    return x + 10
}
)CF");

        test_sharded_codegen_detail::write_text(proj / "pkg_c" / "pkg_c.cf", R"CF(package pkg_c

mut c_offset: i64 = 20

pub fn c_val(x: i64) -> i64 {
    return x + c_offset
}
)CF");

        test_sharded_codegen_detail::write_text(proj / "pkg_d" / "pkg_d.cf", R"CF(package pkg_d

pub fn d_val(x: i64) -> i64 {
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
        const auto shard_dir = proj / ".cfire" / "gen" / "shards" / "app";
        const auto decls_path = shard_dir / "preamble.hpp";
        const auto pkg_pa_path = shard_dir / "pkg_pa.hpp";
        const auto pkg_pb_path = shard_dir / "pkg_pb.hpp";

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

pub fn b_val(x: i64) -> i64 {
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

        // Step B4: Struct layout edit in pkg_a updates pkg_pa.hpp and recompiles ONLY dependent units (pa + main = 2 of 5)
        const std::string pkg_pa_content_1 = test_sharded_codegen_detail::read_text(pkg_pa_path);
        const std::string pkg_pb_content_1 = test_sharded_codegen_detail::read_text(pkg_pb_path);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        test_sharded_codegen_detail::write_text(proj / "pkg_a" / "pkg_a.cf", R"CF(package pkg_a

type Item struct {
    value: i64
    bonus: i64 = 7
}

fn (it: Item) total() -> i64 {
    return it.value + it.bonus
}

pub fn wrap_generic[T](v: T) -> T {
    return v
}

pub fn a_val() -> i64 {
    it := Item{value: 40, bonus: 7}
    return it.total()
}
)CF");

        auto b4 = cfire::build::build_native(entry_cf, rt_dir, out_sharded);
        require(b4.has_value(), "struct edit rebuild succeeds: " + (b4 ? "" : b4.error()));
        if (!b4) return;
        require(b4->used_sharded && b4->sharded_total_units == 5, "struct edit rebuild stays sharded");
        require(b4->sharded_recompiled_units == 2,
            std::format("struct edit in pkg_a should recompile ONLY pkg_a and main (2 units), got {}", b4->sharded_recompiled_units));
        require(test_sharded_codegen_detail::read_text(pkg_pa_path) != pkg_pa_content_1, "struct edit updates pkg_pa.hpp");
        require(test_sharded_codegen_detail::read_text(pkg_pb_path) == pkg_pb_content_1, "struct edit in pkg_a preserves unrelated pkg_pb.hpp");

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

    // 3. Subtest C: 1:1 Per-File TU Sharding Inside a Multi-File Package & Body-Only 1-Unit Rebuild
    {
        const auto mproj = root.dir / "multi_file_pkg";
        const auto alpha_cf = mproj / "libm" / "alpha.cf";
        const auto beta_cf = mproj / "libm" / "beta.cf";
        const auto main_cf = mproj / "main.cf";

        test_sharded_codegen_detail::write_text(alpha_cf, R"CF(package libm

type Box struct {
    id: i64
}

fn (b: Box) tag() -> i64 {
    return b.id + 1
}

pub fn alpha_fn(x: i64) -> i64 {
    b := Box{id: x}
    return beta_helper(b.tag())
}
)CF");

        test_sharded_codegen_detail::write_text(beta_cf, R"CF(package libm

pub fn beta_helper(x: i64) -> i64 {
    return x * 10
}
)CF");

        test_sharded_codegen_detail::write_text(main_cf, R"CF(package main

import "./libm" as lm

fn main() {
    println("m={}", lm.alpha_fn(4) + lm.beta_helper(2))
}
)CF");

        const auto out_bin = test_sharded_codegen_detail::exe_path(mproj / "app_multi");
        flags_guard.set(true, false);
        auto m1 = cfire::build::build_native(main_cf, rt_dir, out_bin);
        require(m1.has_value(), "Subtest C multi-file package clean build succeeds: " + (m1 ? "" : m1.error()));
        if (!m1) return;
        require(m1->used_sharded && m1->sharded_total_units == 3 && m1->sharded_recompiled_units == 3,
            std::format("multi-file package produces 1:1 units (expected 3, got total={}, recompiled={})",
                m1->sharded_total_units, m1->sharded_recompiled_units));
        int run_rc = 0;
        std::string out1 = test_sharded_codegen_detail::trim_str(
            cfire::bridge::run_command_capture(cfire::bridge::shell_quote(out_bin.string()) + " 2>&1", run_rc));
        require(run_rc == 0 && out1 == "m=70", "Subtest C initial binary outputs 'm=70', got: '" + out1 + "'");

        const auto pkg_lm_hdr = mproj / ".cfire" / "gen" / "shards" / "main" / "pkg_lm.hpp";
        const std::string hdr_before = test_sharded_codegen_detail::read_text(pkg_lm_hdr);

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        test_sharded_codegen_detail::write_text(alpha_cf, R"CF(package libm

type Box struct {
    id: i64
}

fn (b: Box) tag() -> i64 {
    return b.id + 1
}

pub fn alpha_fn(x: i64) -> i64 {
    b := Box{id: x}
    return beta_helper(b.tag()) + 7
}
)CF");

        auto m2 = cfire::build::build_native(main_cf, rt_dir, out_bin);
        require(m2.has_value(), "Subtest C body-only edit rebuild succeeds: " + (m2 ? "" : m2.error()));
        if (!m2) return;
        require(m2->used_sharded && m2->sharded_total_units == 3 && m2->sharded_recompiled_units == 1,
            std::format("body-only edit in alpha.cf must recompile ONLY 1 unit (not beta.cf or main.cf), got {}",
                m2->sharded_recompiled_units));
        require(test_sharded_codegen_detail::read_text(pkg_lm_hdr) == hdr_before,
            "body-only edit inside multi-file package keeps pkg_lm.hpp identical");
        std::string out2 = test_sharded_codegen_detail::trim_str(
            cfire::bridge::run_command_capture(cfire::bridge::shell_quote(out_bin.string()) + " 2>&1", run_rc));
        require(run_rc == 0 && out2 == "m=77", "Subtest C body edit outputs 'm=77', got: '" + out2 + "'");
    }

    // 4. Subtest D: Incremental & Cached `cfire test` (`(cached) PASS`, `--no-cache`, `--fuzz`, `--changed`)
    {
        const auto tproj = root.dir / "inc_test_ws";
        const auto a_cf = tproj / "pkg_a" / "a.cf";
        const auto a_test_cf = tproj / "pkg_a" / "a_test.cf";
        const auto b_cf = tproj / "pkg_b" / "b.cf";
        const auto b_test_cf = tproj / "pkg_b" / "b_test.cf";
        const auto val_hpp = tproj / "pkg_c" / "val.hpp";
        const auto c_cf = tproj / "pkg_c" / "c.cf";
        const auto c_test_cf = tproj / "pkg_c" / "c_test.cf";

        test_sharded_codegen_detail::write_text(a_cf, R"CF(package pkg_a

pub fn base_num() -> i64 {
    return 10
}
)CF");
        test_sharded_codegen_detail::write_text(a_test_cf, R"CF(package pkg_a

test "a_basic" {
    assert(base_num() == 10)
}
)CF");
        test_sharded_codegen_detail::write_text(b_cf, R"CF(package pkg_b

import "../pkg_a" as pa

pub fn derived_num() -> i64 {
    return pa.base_num() + 5
}
)CF");
        test_sharded_codegen_detail::write_text(b_test_cf, R"CF(package pkg_b

test "b_basic" {
    assert(derived_num() == 15)
}
)CF");
        test_sharded_codegen_detail::write_text(val_hpp, R"CPP(#pragma once
inline long long cfire_test_cpp_val() { return 42; }
)CPP");
        test_sharded_codegen_detail::write_text(c_cf, R"CF(package pkg_c

import cpp "val.hpp"

fn native_num() -> i64 {
    return cpp.cfire_test_cpp_val()
}
)CF");
        test_sharded_codegen_detail::write_text(c_test_cf, R"CF(package pkg_c

test "c_basic" {
    assert(native_num() == 42)
}
)CF");

        const std::string cfire_bin = cfire::bridge::shell_quote(cfire_exe());
        const std::string ws_q = cfire::bridge::shell_quote(tproj.string());
        int rc = 0;

        // Run 1: cold pass (uncached)
        std::string r1 = cfire::bridge::run_command_capture(
            std::format("{} test {} 2>&1", cfire_bin, ws_q), rc);
        require(rc == 0 && r1.contains("All 3 test suite(s) passed") && !r1.contains("(cached) PASS"),
            std::format("Subtest D Run 1 expected 3 uncached suites to pass (rc={}):\n{}", rc, r1));

        // Run 2: warm pass -> cache hits for all 3 suites
        std::string r2 = cfire::bridge::run_command_capture(
            std::format("{} test {} 2>&1", cfire_bin, ws_q), rc);
        require(rc == 0 && r2.contains("(cached) PASS") && r2.contains("All 3 test suite(s) passed"),
            std::format("Subtest D Run 2 expected `(cached) PASS` cache hits (rc={}):\n{}", rc, r2));

        // Run 3: `--no-cache` and `--fuzz` bypass cache
        std::string r_nc = cfire::bridge::run_command_capture(
            std::format("{} test --no-cache {} 2>&1", cfire_bin, ws_q), rc);
        require(rc == 0 && !r_nc.contains("(cached) PASS") && r_nc.contains("All 3 test suite(s) passed"),
            "Subtest D `--no-cache` bypasses test result cache");

        std::string r_fz = cfire::bridge::run_command_capture(
            std::format("{} test --fuzz {} 2>&1", cfire_bin, ws_q), rc);
        require(rc == 0 && !r_fz.contains("(cached) PASS") && r_fz.contains("All 3 test suite(s) passed"),
            "Subtest D `--fuzz` bypasses test result cache");

        // Clean `--changed` in non-git workspace reports 0 affected suites
        std::string r_ch_clean = cfire::bridge::run_command_capture(
            std::format("{} test --changed {} 2>&1", cfire_bin, ws_q), rc);
        require(rc == 0 && r_ch_clean.contains("No test suites affected"),
            std::format("Subtest D clean `--changed` expected no affected suites (rc={}):\n{}", rc, r_ch_clean));

        // Edit `pkg_a/a.cf` (transitive dep of `pkg_b`, unrelated to `pkg_c`): `--changed` runs only pkg_a + pkg_b
        test_sharded_codegen_detail::write_text(a_cf, R"CF(package pkg_a

pub fn base_num() -> i64 {
    return 20
}
)CF");
        test_sharded_codegen_detail::write_text(a_test_cf, R"CF(package pkg_a

test "a_basic" {
    assert(base_num() == 20)
}
)CF");
        test_sharded_codegen_detail::write_text(b_test_cf, R"CF(package pkg_b

test "b_basic" {
    assert(derived_num() == 25)
}
)CF");

        std::string r_ch_dirty = cfire::bridge::run_command_capture(
            std::format("{} test --changed {} 2>&1", cfire_bin, ws_q), rc);
        require(rc == 0 && r_ch_dirty.contains("All 2 test suite(s) passed") && !r_ch_dirty.contains("c_test.cf"),
            std::format("Subtest D `--changed` after pkg_a edit runs only 2 affected suites (rc={}):\n{}", rc, r_ch_dirty));

        // Verify C++ header modification invalidates ONLY pkg_c while pkg_a and pkg_b hit cache
        std::string r_warm = cfire::bridge::run_command_capture(
            std::format("{} test {} 2>&1", cfire_bin, ws_q), rc);
        require(rc == 0 && r_warm.contains("(cached) PASS"), "Subtest D re-warms cache cleanly");

        test_sharded_codegen_detail::write_text(val_hpp, R"CPP(#pragma once
inline long long cfire_test_cpp_val() { return 99; }
)CPP");
        test_sharded_codegen_detail::write_text(c_test_cf, R"CF(package pkg_c

test "c_basic" {
    assert(native_num() == 99)
}
)CF");
        std::string r_hdr = cfire::bridge::run_command_capture(
            std::format("{} test {} 2>&1", cfire_bin, ws_q), rc);
        require(rc == 0 && r_hdr.contains("All 3 test suite(s) passed") && r_hdr.contains("(cached) PASS"),
            std::format("Subtest D C++ header edit keeps pkg_a/pkg_b cached while invalidating pkg_c (rc={}):\n{}", rc, r_hdr));
        // Split output at Suite 3/3 to verify Suite 3 actually ran uncached!
        if (auto s3_pos = r_hdr.find("Suite 3/3:"); s3_pos != std::string::npos) {
            std::string s3_tail = r_hdr.substr(s3_pos);
            require(!s3_tail.contains("(cached) PASS"), "Suite 3 (pkg_c) ran uncached after val.hpp edit");
        } else {
            require(false, "expected Suite 3/3 marker in output");
        }

        // Git-backed `--changed` check inside a temp `git init` workspace
        int git_rc = 0;
        (void)cfire::bridge::run_command_capture(std::format("git -C {} init 2>&1", ws_q), git_rc);
        if (git_rc == 0) {
            (void)cfire::bridge::run_command_capture(
                std::format("git -C {} config user.email test@cfire.local && git -C {} config user.name Tester && git -C {} add -A && git -C {} commit -m init 2>&1",
                    ws_q, ws_q, ws_q, ws_q),
                git_rc);
            if (git_rc == 0) {
                test_sharded_codegen_detail::write_text(a_cf, R"CF(package pkg_a

pub fn base_num() -> i64 {
    return 30
}
)CF");
                test_sharded_codegen_detail::write_text(a_test_cf, R"CF(package pkg_a

test "a_basic" {
    assert(base_num() == 30)
}
)CF");
                test_sharded_codegen_detail::write_text(b_test_cf, R"CF(package pkg_b

test "b_basic" {
    assert(derived_num() == 35)
}
)CF");
                std::string r_git_ch = cfire::bridge::run_command_capture(
                    std::format("{} test --changed {} 2>&1", cfire_bin, ws_q), rc);
                require(rc == 0 && r_git_ch.contains("All 2 test suite(s) passed") && !r_git_ch.contains("c_test.cf"),
                    std::format("Subtest D git-backed `--changed` runs only pkg_a + pkg_b (rc={}):\n{}", rc, r_git_ch));
            }
        }
    }

    // 5. Subtest E: Flint-Scale Sharding Invariants (line-shift header invariance, out-of-line & reachable reflection,
    //    1:1 per-file test sharding, workspace-shared `.cfire/obj` cross-suite reuse, non-test Arbitrary omission)
    {
        const auto ws = root.dir / "flint_scale";
        std::error_code ec;
        std::filesystem::create_directories(ws / ".git", ec);

        const auto dom_types_cf = ws / "domain" / "types.cf";
        const auto dom_alerts_cf = ws / "domain" / "alerts.cf";
        const auto app_service_cf = ws / "app" / "service.cf";
        const auto app_a_test_cf = ws / "app" / "a_test.cf";
        const auto app_b_test_cf = ws / "app" / "b_test.cf";
        const auto cli_cmd_cf = ws / "cli" / "cmd.cf";
        const auto cli_cmd_test_cf = ws / "cli" / "cmd_test.cf";
        const auto bin_main_cf = ws / "binapp" / "main.cf";

        test_sharded_codegen_detail::write_text(dom_types_cf, R"CF(package domain

pub type Status enum {
    Active,
    Paused,
}

pub type PublicRecord struct {
    id: i64 `json:"id"`
    label: str `json:"label"`
    status: Status `json:"status"`
}

pub type InternalScratch struct {
    cache: map[str]i64
    notes: []str
}
)CF");

        test_sharded_codegen_detail::write_text(dom_alerts_cf, R"CF(package domain

pub fn compute_alert(x: i64) -> i64 {
    return x * 2
}

pub type AlertRule struct {
    threshold: i64 `json:"threshold"`
}
)CF");

        test_sharded_codegen_detail::write_text(app_service_cf, R"CF(package app

import "core:json"
import "../domain"

pub fn encode_rule(r: domain.AlertRule) -> str {
    return json.stringify(r)
}

pub fn app_score(x: i64) -> i64 {
    return domain.compute_alert(x) + 1
}
)CF");

        test_sharded_codegen_detail::write_text(app_a_test_cf, R"CF(package app

import "../domain"

test "a_service" {
    assert_eq(app_score(5), i64(11))
    mut rec := domain.InternalScratch{ cache: make(map[str]i64), notes: ["ok"] }
    assert_eq(rec.notes.len(), usize(1))
}
)CF");

        test_sharded_codegen_detail::write_text(app_b_test_cf, R"CF(package app

import "../domain"

test "b_alert" {
    enc := encode_rule(domain.AlertRule{ threshold: 42 })
    assert(enc.contains("42"))
}
)CF");

        test_sharded_codegen_detail::write_text(cli_cmd_cf, R"CF(package cli

import "core:cli"
import "../domain"

pub type CliOpts struct {
    verbose: bool `cli:"--verbose,-v"`
}

pub fn parse_opts() -> CliOpts! {
    return try cli.parse[CliOpts](["--verbose"])
}

pub fn cli_total(x: i64) -> i64 {
    return domain.compute_alert(x) + 100
}
)CF");

        test_sharded_codegen_detail::write_text(cli_cmd_test_cf, R"CF(package cli

test "cli_basic" {
    opts := try parse_opts()
    assert(opts.verbose)
    assert_eq(cli_total(5), i64(110))
}
)CF");

        test_sharded_codegen_detail::write_text(bin_main_cf, R"CF(package main

import "../domain"

fn main() {
    println("bin={}", domain.compute_alert(7))
}
)CF");

        flags_guard.set(true, false);
        cfire::build::set_build_profile(cfire::build::BuildProfile::DevDebug);

        const auto app_test_bin = test_sharded_codegen_detail::exe_path(ws / "app_test_bin");
        auto b1 = cfire::build::build_native(app_service_cf, rt_dir, app_test_bin, true);
        require(b1.has_value(), "Subtest E app cold test build succeeds: " + (b1 ? "" : b1.error()));
        if (!b1) return;
        require(b1->used_sharded, "Subtest E app test build uses sharded codegen");

        // 1. Verify workspace-shared `.cfire/obj` & lean `pkg_domain.hpp` + out-of-line `pkg_domain_reflect.hpp`
        const auto ws_obj_dir = std::filesystem::weakly_canonical(ws / ".cfire" / "obj", ec);
        require(std::filesystem::exists(ws_obj_dir, ec) && !std::filesystem::exists(ws / "app" / ".cfire" / "obj", ec),
            "Subtest E stores shared .o files in workspace root <ws>/.cfire/obj, not app/.cfire/obj");
        for (const auto& obj_p : b1->sharded_object_files) {
            std::filesystem::path p = std::filesystem::weakly_canonical(std::filesystem::path(obj_p).parent_path(), ec);
            require(p == ws_obj_dir && std::filesystem::path(obj_p).filename().string().starts_with("u_"),
                "Subtest E object filename is suite-independent u_<id>_<hash>.o in <ws>/.cfire/obj: " + obj_p);
        }

        const auto app_shard_dir = ws / "app" / ".cfire" / "gen" / "shards" / "service_test";
        const auto pkg_domain_hpp_p = app_shard_dir / "pkg_domain.hpp";
        const auto pkg_domain_refl_p = app_shard_dir / "pkg_domain_reflect.hpp";
        const std::string pkg_domain_hpp_1 = test_sharded_codegen_detail::read_text(pkg_domain_hpp_p);
        const std::string pkg_domain_refl_1 = test_sharded_codegen_detail::read_text(pkg_domain_refl_p);

        require(!pkg_domain_hpp_1.contains("#line "), "pkg_domain.hpp contains zero #line directives");
        require(!pkg_domain_hpp_1.contains("stringify_struct") && !pkg_domain_hpp_1.contains("parse_struct") &&
                !pkg_domain_hpp_1.contains("schema_struct"),
            "pkg_domain.hpp contains zero inline JSON reflection");
        require(!pkg_domain_hpp_1.contains("Arbitrary<"), "pkg_domain.hpp contains zero Arbitrary<T> bodies");
        require(!pkg_domain_hpp_1.contains("core/json.hpp") && !pkg_domain_hpp_1.contains("CFIRE_USE_SIMDJSON"),
            "pkg_domain.hpp does not pull in simdjson/core:json for non-JSON package");

        require(pkg_domain_refl_1.contains("cfire::json::parse_struct<AlertRule>") &&
                pkg_domain_refl_1.contains("cfire::json::parse_struct<PublicRecord>") &&
                pkg_domain_refl_1.contains("cfire::json::stringify_struct<Status>"),
            "pkg_domain_reflect.hpp declares out-of-line specializations for tagged/reachable types");
        require(!pkg_domain_refl_1.contains("cfire::json::parse_struct<InternalScratch>") &&
                !pkg_domain_refl_1.contains("cfire::json::stringify_struct<InternalScratch>"),
            "pkg_domain_reflect.hpp omits JSON reflection for untagged unserialized InternalScratch");
        require(!pkg_domain_refl_1.contains("inline auto cfire::json::"),
            "pkg_domain_reflect.hpp omits inline on out-of-line specializations");

        int run_rc = 0;
        std::string app_out_1 = cfire::bridge::run_command_capture(
            cfire::bridge::shell_quote(app_test_bin.string()) + " 2>&1", run_rc);
        require(run_rc == 0 && app_out_1.contains("PASS: a_service") && app_out_1.contains("PASS: b_alert") &&
                app_out_1.contains("PASS: [auto] json round-trip: PublicRecord") &&
                app_out_1.contains("PASS: [auto] json round-trip: AlertRule"),
            "Subtest E app test binary passes all unit + auto round-trip tests:\n" + app_out_1);

        // 2. Non-test binary build: omits Arbitrary<T>, and line-shift inside domain/alerts.cf recompiles ONLY 1 unit!
        const auto bin_out = test_sharded_codegen_detail::exe_path(ws / "binapp_out");
        auto nb1 = cfire::build::build_native(bin_main_cf, rt_dir, bin_out, false);
        require(nb1.has_value(), "Subtest E non-test binapp build succeeds: " + (nb1 ? "" : nb1.error()));
        if (!nb1) return;
        const auto bin_shard_dir = ws / "binapp" / ".cfire" / "gen" / "shards" / "main";
        const std::string bin_dom_refl = test_sharded_codegen_detail::read_text(bin_shard_dir / "pkg_domain_reflect.hpp");
        require(!bin_dom_refl.contains("Arbitrary<"), "non-test build omits Arbitrary<T> declarations");

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        test_sharded_codegen_detail::write_text(dom_alerts_cf, R"CF(package domain

pub fn compute_alert(x: i64) -> i64 {
    // Shifting line numbers above AlertRule inside a function body:
    tmp := x + 0


    return tmp * 2
}

pub type AlertRule struct {
    threshold: i64 `json:"threshold"`
}
)CF");

        auto nb2 = cfire::build::build_native(bin_main_cf, rt_dir, bin_out, false);
        require(nb2.has_value() && nb2->sharded_recompiled_units == 1,
            std::format("Subtest E non-test line shift in domain/alerts.cf must recompile ONLY 1 unit, got {}",
                nb2 ? nb2->sharded_recompiled_units : 999));

        // Now verify `pkg_domain.hpp` stays byte-for-byte identical in `app` test build after that line shift
        auto b2 = cfire::build::build_native(app_service_cf, rt_dir, app_test_bin, true);
        require(b2.has_value(), "Subtest E app rebuild after domain line shift succeeds");
        if (!b2) return;
        require(test_sharded_codegen_detail::read_text(pkg_domain_hpp_p) == pkg_domain_hpp_1,
            "Subtest E line shift inside function body leaves pkg_domain.hpp byte-for-byte identical");
        require(b2->sharded_recompiled_units <= 2,
            std::format("Subtest E test rebuild after domain line shift must recompile at most 2 units (domain__alerts + __test_main), got {}",
                b2->sharded_recompiled_units));

        // 3. Adding a test to b_test.cf recompiles ONLY b_test + __test_main (2 units)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        test_sharded_codegen_detail::write_text(app_b_test_cf, R"CF(package app

import "../domain"

test "b_alert" {
    enc := encode_rule(domain.AlertRule{ threshold: 42 })
    assert(enc.contains("42"))
}

test "b_extra" {
    assert_eq(domain.compute_alert(10), i64(20))
}
)CF");
        auto b3 = cfire::build::build_native(app_service_cf, rt_dir, app_test_bin, true);
        require(b3.has_value(), "Subtest E rebuild after adding test to b_test.cf succeeds: " + (b3 ? "" : b3.error()));
        if (!b3) return;
        require(b3->sharded_recompiled_units == 2,
            std::format("Subtest E adding test to b_test.cf must recompile ONLY 2 units (app__b_test + __test_main), got {}",
                b3->sharded_recompiled_units));
        std::string app_out_3 = cfire::bridge::run_command_capture(
            cfire::bridge::shell_quote(app_test_bin.string()) + " 2>&1", run_rc);
        require(run_rc == 0 && app_out_3.contains("PASS: b_extra") && app_out_3.contains("PASS: a_service"),
            "Subtest E added test runs and passes cleanly:\n" + app_out_3);

        // 4. Cross-suite `.cfire/obj` reuse: `cli` suite reuses ALL `domain` .o files built by `app`!
        const auto cli_test_bin = test_sharded_codegen_detail::exe_path(ws / "cli_test_bin");
        auto b_cli = cfire::build::build_native(cli_cmd_test_cf, rt_dir, cli_test_bin, true);
        require(b_cli.has_value(), "Subtest E cli test build succeeds: " + (b_cli ? "" : b_cli.error()));
        if (!b_cli) return;
        // cli has: cli__cmd, cli__cmd_test, cli__reflect (for CliOpts), __test_main = 4 cli units,
        // while domain's 2 units (domain__alerts, domain__reflect) MUST be reused from <ws>/.cfire/obj!
        const std::size_t expected_cli_only = b_cli->sharded_total_units - 2;
        require(b_cli->sharded_recompiled_units == expected_cli_only,
            std::format("Subtest E cli suite must reuse all 2 domain .o files (expected {} recompiled of {}, got {})",
                expected_cli_only, b_cli->sharded_total_units, b_cli->sharded_recompiled_units));
        std::string cli_out = cfire::bridge::run_command_capture(
            cfire::bridge::shell_quote(cli_test_bin.string()) + " 2>&1", run_rc);
        require(run_rc == 0 && cli_out.contains("PASS: cli_basic"),
            "Subtest E cli test binary runs and passes:\n" + cli_out);

        // 5. Pure CLI struct with `desc:` tag (and NO `core:json` / `json:` tag) must NOT pull simdjson or emit cfire::json::*
        const auto pure_cli_cf = ws / "pure_cli" / "main.cf";
        test_sharded_codegen_detail::write_text(pure_cli_cf, R"CF(package main

import "core:cli"

type ServerFlags struct {
    host: str `cli:"--host,-h" default:"127.0.0.1" desc:"Bind address"`
    port: u16 `cli:"--port,-p" default:"9090" desc:"Listen port"`
}

fn main() -> void! {
    f := try cli.parse[ServerFlags](["--port", "8088"])
    println("cli={}:{}", f.host, f.port)
}
)CF");
        const auto pure_cli_bin = test_sharded_codegen_detail::exe_path(ws / "pure_cli_bin");
        auto b_pcli = cfire::build::build_native(pure_cli_cf, rt_dir, pure_cli_bin, false);
        require(b_pcli.has_value() && b_pcli->used_sharded,
            "Subtest E pure CLI with desc: sharded build succeeds: " + (b_pcli ? "" : b_pcli.error()));
        if (!b_pcli) return;
        const std::string pcli_refl = test_sharded_codegen_detail::read_text(
            pure_cli_cf.parent_path() / ".cfire" / "gen" / "shards" / "main" / "pkg_main_reflect.hpp");
        require(pcli_refl.contains("cfire::cli::parse") &&
                !pcli_refl.contains("cfire::json::") &&
                !pcli_refl.contains("CFIRE_USE_SIMDJSON"),
            "pure CLI package with desc: emits cli::parse only without json reflection or CFIRE_USE_SIMDJSON:\n" + pcli_refl);
        std::string pcli_out = test_sharded_codegen_detail::trim_str(
            cfire::bridge::run_command_capture(cfire::bridge::shell_quote(pure_cli_bin.string()) + " 2>&1", run_rc));
        require(run_rc == 0 && pcli_out == "cli=127.0.0.1:8088",
            "pure CLI binary with desc: runs cleanly, got: '" + pcli_out + "'");
    }

    std::println("--- PASS: test_sharded_cpp_codegen");
}

void test_cross_package_json_sharded_and_noshard(const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_cross_package_json_sharded_and_noshard");

    test_sharded_codegen_detail::ScopedTempRoot root("cfire_cross_pkg_json");
    const std::string cfire_bin = cfire::bridge::shell_quote(
        std::filesystem::absolute(cfire_exe()).string());
    int rc = 0;

    // 1. Exact Flint 3-file repro across `cfire test ad` (sharded) and `cfire test --no-shard ad`
    {
        auto ws = root.dir / "flint_repro";
        test_sharded_codegen_detail::write_text(ws / "dom" / "dom.cf", R"CF(package dom

pub type Settings struct {
    accent: str `json:"accent"`
    tags: []str `json:"tags"`
}
)CF");
        test_sharded_codegen_detail::write_text(ws / "ad" / "ad.cf", R"CF(package ad

import "core:json"
import "../dom"

type File struct {
    version: i64 `json:"version"`
    settings: dom.Settings `json:"settings"`
    list: []dom.Settings `json:"list"`
}

fn dump(f: File) -> str {
    return json.stringify(f)
}

fn load(t: str) -> File! {
    return try json.parse[File](t)
}
)CF");
        test_sharded_codegen_detail::write_text(ws / "ad" / "ad_test.cf", R"CF(package ad

import "../dom"

test "json across packages" {
    s := dom.Settings{ accent: "moss", tags: ["a"] }
    text := dump(File{ version: 1, settings: s, list: [s] })
    assert(text.contains("\"version\":1"))
    assert(text.contains("\"accent\":\"moss\""))
    back := try load(text)
    assert_eq(back.version, i64(1))
    assert_eq(back.settings.accent, "moss")
    assert_eq(back.list.len(), usize(1))
    assert_eq(back.list[0].tags.len(), usize(1))
    assert_eq(back.list[0].tags[0], "a")
}
)CF");

        const std::string ad_q = cfire::bridge::shell_quote((ws / "ad").string());
        std::string r_sharded = cfire::bridge::run_command_capture(
            std::format("{} test --no-cache {} 2>&1", cfire_bin, ad_q), rc);
        require(rc == 0 &&
                r_sharded.contains("PASS: json across packages") &&
                r_sharded.contains("PASS: [auto] json round-trip: Settings") &&
                r_sharded.contains("PASS: [auto] json round-trip: File"),
            std::format("Flint 3-file repro passes with auto round-trips in sharded mode (rc={}):\n{}", rc, r_sharded));

        std::string r_noshard = cfire::bridge::run_command_capture(
            std::format("{} test --no-cache --no-shard {} 2>&1", cfire_bin, ad_q), rc);
        require(rc == 0 &&
                r_noshard.contains("PASS: json across packages") &&
                r_noshard.contains("PASS: [auto] json round-trip: Settings") &&
                r_noshard.contains("PASS: [auto] json round-trip: File"),
            std::format("Flint 3-file repro passes with auto round-trips in --no-shard mode (rc={}):\n{}", rc, r_noshard));
    }

    // 2. Colliding struct/enum names across packages + narrow ints + `json:"-"` in sharded and `--no-shard`
    {
        auto ws = root.dir / "colliding_cross_pkg";
        test_sharded_codegen_detail::write_text(ws / "dom" / "dom.cf", R"CF(package dom

pub type Mode enum {
    Fast,
    Safe,
}

pub type Item struct {
    label: str `json:"label"`
    mode: Mode `json:"mode"`
    prio: u8 `json:"prio"`
    delta: i8 `json:"delta"`
}
)CF");
        test_sharded_codegen_detail::write_text(ws / "ad" / "ad.cf", R"CF(package ad

import "core:json"
import "../dom"

type Item struct {
    id: u32 `json:"id"`
    port: u16 `json:"port"`
    offset: i16 `json:"offset"`
    primary: dom.Item `json:"primary"`
    extras: []dom.Item `json:"extras"`
    internal_cache: map[str]i64 `json:"-"`
}

fn roundtrip(it: Item) -> Item! {
    s := json.stringify(it)
    return try json.parse[Item](s)
}
)CF");
        test_sharded_codegen_detail::write_text(ws / "ad" / "ad_test.cf", R"CF(package ad

import "../dom"

test "colliding cross-pkg struct and enum" {
    d := dom.Item{ label: "core", mode: dom.Mode::Safe(), prio: u8(9), delta: i8(-5) }
    mut cache: map[str]i64 = make(map[str]i64)
    cache["k"] = 77
    in_it := Item{
        id: u32(42000),
        port: u16(8443),
        offset: i16(-300),
        primary: d,
        extras: [d],
        internal_cache: cache,
    }
    out_it := try roundtrip(in_it)
    assert_eq(out_it.id, u32(42000))
    assert_eq(out_it.port, u16(8443))
    assert_eq(out_it.offset, i16(-300))
    assert_eq(out_it.primary.label, "core")
    assert(out_it.primary.mode == dom.Mode::Safe())
    assert_eq(out_it.primary.prio, u8(9))
    assert_eq(out_it.primary.delta, i8(-5))
    assert_eq(out_it.extras.len(), usize(1))
}
)CF");
        const std::string ad_q = cfire::bridge::shell_quote((ws / "ad").string());
        std::string r_sharded = cfire::bridge::run_command_capture(
            std::format("{} test --no-cache {} 2>&1", cfire_bin, ad_q), rc);
        require(rc == 0 && r_sharded.contains("PASS: colliding cross-pkg struct and enum"),
            std::format("Colliding cross-package JSON test passes in sharded mode (rc={}):\n{}", rc, r_sharded));

        std::string r_noshard = cfire::bridge::run_command_capture(
            std::format("{} test --no-cache --no-shard {} 2>&1", cfire_bin, ad_q), rc);
        require(rc == 0 && r_noshard.contains("PASS: colliding cross-pkg struct and enum"),
            std::format("Colliding cross-package JSON test passes in --no-shard mode (rc={}):\n{}", rc, r_noshard));
    }

    // 3. Cross-package cfire/JSON-UNSUPPORTED-TYPE reports exact nested field path
    {
        auto ws = root.dir / "unsupported_cross_pkg";
        test_sharded_codegen_detail::write_text(ws / "dom" / "dom.cf", R"CF(package dom

pub type BadInner struct {
    meta: map[str]i64 `json:"meta"`
}
)CF");
        test_sharded_codegen_detail::write_text(ws / "app" / "main.cf", R"CF(package main

import "core:json"
import "../dom"

type BadOuter struct {
    inner: dom.BadInner `json:"inner"`
}

fn main() {
    b := BadOuter{ inner: dom.BadInner{ meta: make(map[str]i64) } }
    println("{}", json.stringify(b))
}
)CF");
        const std::string main_q = cfire::bridge::shell_quote((ws / "app" / "main.cf").string());
        std::string r_chk = cfire::bridge::run_command_capture(
            std::format("{} check --json {} 2>&1", cfire_bin, main_q), rc);
        require(rc != 0 && r_chk.contains("cfire/JSON-UNSUPPORTED-TYPE") && r_chk.contains("inner.meta"),
            std::format("cfire check reports cfire/JSON-UNSUPPORTED-TYPE with nested field path 'inner.meta' (rc={}):\n{}", rc, r_chk));
    }

    // 4. Runtime static_assert defense-in-depth on unspecialized C++ struct
    {
        auto cpp_p = root.dir / "unspecialized_json_check.cpp";
        test_sharded_codegen_detail::write_text(cpp_p, R"CPP(#include "core/json.hpp"
struct MissingReflect { int x = 0; };
int main() {
    MissingReflect m{};
    auto s = cfire::json::stringify(m);
    auto p = cfire::json::parse<MissingReflect>("{}");
    return static_cast<int>(s.size()) + (p.has_value() ? 0 : 1);
}
)CPP");
        const char* cxx_env = std::getenv("CXX");
        std::string cxx = (cxx_env != nullptr && *cxx_env != '\0') ? cxx_env : "clang++";
        std::string cmd = std::format("{} -std=c++2b -fsyntax-only -I{} {} 2>&1",
            cxx,
            cfire::bridge::shell_quote(rt_dir.string()),
            cfire::bridge::shell_quote(cpp_p.string()));
        std::string r_cxx = cfire::bridge::run_command_capture(cmd, rc);
        require(rc != 0 && r_cxx.contains("missing generated JSON reflection specialization"),
            std::format("runtime/core/json.hpp static_assert rejects unspecialized struct (rc={}):\n{}", rc, r_cxx));
    }

    // 5. v0.12.7 regressions: shared[T] field access (sharded + --no-shard), aliased `j.Value`, and `[]bool`
    {
        auto ws = root.dir / "regressions_v0127";
        test_sharded_codegen_detail::write_text(ws / "dom" / "dom.cf", R"CF(package dom

pub type St struct {
    now: i64
    items: []str
}
)CF");
        test_sharded_codegen_detail::write_text(ws / "ad" / "ad.cf", R"CF(package ad

import "core:json" as j
import "../dom"

type Clock struct {
    state: shared[dom.St]
}

fn (self: Clock) add(s: str) {
    self.state.items.push(s)
}

fn (self: Clock) bump(v: i64) {
    self.state.now = v
}

fn (self: Clock) now_ms() -> i64 {
    return self.state.now
}

type Holder struct {
    flags: []bool `json:"flags"`
    extra: j.Value `json:"extra"`
}

fn dump_holder(h: Holder) -> str {
    return j.stringify(h)
}

fn load_holder(s: str) -> Holder! {
    return try j.parse[Holder](s)
}
)CF");
        test_sharded_codegen_detail::write_text(ws / "ad" / "ad_test.cf", R"CF(package ad

import "core:json" as j
import "../dom"

test "shared field and aliased j.Value with bool slice" {
    c := Clock{ state: make(shared[dom.St], dom.St{ now: 5, items: [] }) }
    c.add("a")
    assert_eq(c.now_ms(), i64(5))
    c.bump(19)
    assert_eq(c.state.now, i64(19))
    assert_eq(c.state.items.len(), usize(1))

    dv := try j.parse_dynamic(`{"k":9}`)
    h := Holder{ flags: [true, false], extra: dv }
    enc := dump_holder(h)
    assert_eq(enc, `{"flags":[true,false],"extra":{"k":9}}`)
    back := try load_holder(enc)
    assert_eq(back.flags.len(), usize(2))
    assert(back.flags[0] == true && back.flags[1] == false)
    assert(back.extra == dv)
}
)CF");

        const std::string ad_q = cfire::bridge::shell_quote((ws / "ad").string());
        std::string r_sharded = cfire::bridge::run_command_capture(
            std::format("{} test --no-cache {} 2>&1", cfire_bin, ad_q), rc);
        require(rc == 0 && r_sharded.contains("PASS: shared field and aliased j.Value with bool slice"),
            std::format("v0.12.7 regressions test passes in sharded mode (rc={}):\n{}", rc, r_sharded));

        std::string r_noshard = cfire::bridge::run_command_capture(
            std::format("{} test --no-cache --no-shard {} 2>&1", cfire_bin, ad_q), rc);
        require(rc == 0 && r_noshard.contains("PASS: shared field and aliased j.Value with bool slice"),
            std::format("v0.12.7 regressions test passes in --no-shard mode (rc={}):\n{}", rc, r_noshard));
    }

    std::println("--- PASS: test_cross_package_json_sharded_and_noshard");
}
