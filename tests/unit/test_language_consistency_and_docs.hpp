#pragma once
// tests/unit/test_language_consistency_and_docs.hpp — Regression tests for language, build manifest, and documentation consistency

#include "../test_harness.hpp"
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#if __has_include(<print>)
#include <print>
#endif
#include <string>
#include <string_view>

namespace test_consistency_docs_detail {

struct ScopedEnvOverride {
    std::string key;
    bool had_old = false;
    std::string old_val;

    ScopedEnvOverride(std::string k, const char* val) : key(std::move(k)) {
        if (const char* cur = std::getenv(key.c_str())) {
            had_old = true;
            old_val = cur;
        }
        if (val != nullptr) {
            cfire::build::set_process_env(key.c_str(), val);
        } else {
            cfire::build::unset_process_env(key.c_str());
        }
    }

    ~ScopedEnvOverride() {
        if (had_old) {
            cfire::build::set_process_env(key.c_str(), old_val);
        } else {
            cfire::build::unset_process_env(key.c_str());
        }
    }
};

inline auto read_file_str(const std::filesystem::path& p) -> std::string {
    std::ifstream ifs(p, std::ios::in | std::ios::binary);
    if (!ifs.is_open()) return {};
    return std::string(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
}

inline auto run_cfire_in_dir(const std::filesystem::path& workdir, std::string_view args) -> RunResult {
    auto abs_cfire = std::filesystem::absolute(cfire_exe()).string();
    RunResult r;
    r.output = cfire::bridge::run_command_capture(
        std::format("cd \"{}\" && \"{}\" {}", workdir.string(), abs_cfire, args), r.exit_code);
    return r;
}

} // namespace test_consistency_docs_detail

void test_language_consistency_and_docs(const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_language_consistency_and_docs  ===");

    // ---: Retired @pkg(...) on import cpp ---
    {
        ScopedTempDir dir("consistency_pkg_attr");
        auto main_cf = dir.file("main.cf", R"CF(package main
import cpp "math.h" @pkg("m")
fn main() {}
)CF");
        auto res = run_cfire(std::format("check \"{}\"", main_cf.string()));
        res.assert_fail("@pkg on import cpp is rejected")
           .assert_contains("cfire/RETIRED-API", "@pkg emits cfire/RETIRED-API");
    }

    // --- @define retirement & build.cf / CLI defines propagation ---
    {
        ScopedTempDir dir("define_retired");
        auto main_cf = dir.file("main.cf", R"CF(package main
import cpp "math.h" @define("BAD_MACRO", "1")
fn main() {}
)CF");
        auto res = run_cfire(std::format("check \"{}\"", main_cf.string()));
        res.assert_fail("@define on import cpp is rejected")
           .assert_contains("cfire/RETIRED-API", "@define emits cfire/RETIRED-API");

        auto unk_cf = dir.file("unknown_attr.cf", R"CF(package main
import cpp "math.h" @unknown_attr("x")
fn main() {}
)CF");
        auto unk_res = run_cfire(std::format("check \"{}\"", unk_cf.string()));
        unk_res.assert_fail("@unknown_attr on import cpp is rejected")
               .assert_contains("cfire/INVALID-ATTRIBUTE", "unknown attribute emits cfire/INVALID-ATTRIBUTE");
    }
    {
        ScopedTempDir dir("define_bad_manifest");
        dir.file("build.cf", R"CF(package main
import "core:build"

app := build.Executable{
    Name:    "bad_def",
    Entry:   "main.cf",
    Defines: { "1BAD_MACRO": "1" },
}
)CF");
        dir.file("main.cf", "package main\nfn main() {}\n");
        auto chk_res = test_consistency_docs_detail::run_cfire_in_dir(dir.path, "check main.cf");
        chk_res.assert_fail("invalid C macro key in build.cf Defines fails cfire check")
               .assert_contains("cfire/INVALID-ATTRIBUTE", "invalid build.cf macro emits cfire/INVALID-ATTRIBUTE");
        auto res = test_consistency_docs_detail::run_cfire_in_dir(dir.path, "build");
        res.assert_fail("invalid C macro key in build.cf Defines is rejected");

        auto cli_res = run_cfire(std::format("check --define 1BAD_MACRO=1 \"{}\"", (dir.path / "main.cf").string()));
        cli_res.assert_fail("invalid CLI --define macro key is rejected");
        require(cli_res.exit_code == 2,
            std::format("invalid CLI --define exits with usage code 2, got {}", cli_res.exit_code));
    }
    {
        ScopedTempDir dir("define_shard_manifest");
        dir.file("inc/probe.h", R"H(#pragma once
#ifdef CFIRE_SHARD_DEF
static inline int cfire_define_probe(void) { return CFIRE_SHARD_VAL + CFIRE_TOP_VAL + CFIRE_CLI_VAL; }
#else
static inline int cfire_define_probe(void) { return 0; }
#endif
)H");
        dir.file("build.cf", R"CF(package main
import "core:build"

top_def := build.Define("CFIRE_TOP_VAL", "10")

app := build.Executable{
    Name:     "def_shard",
    Entry:    "main.cf",
    Includes: ["inc"],
    Defines:  { CFIRE_SHARD_DEF: true, CFIRE_SHARD_VAL: 30 },
}
)CF");
        dir.file("helper/helper.cf", R"CF(package helper
import cpp "probe.h"
fn ReadProbe() -> i32 {
    return cpp.cfire_define_probe()
}
)CF");
        dir.file("main.cf", R"CF(package main
import "./helper"
fn main() {
    println("probe={}", helper.ReadProbe())
}
)CF");
        test_consistency_docs_detail::run_cfire_in_dir(dir.path, "check -DCFIRE_CLI_VAL=2")
            .assert_ok("cfire check accepts build.cf Defines, build.Define, and CLI -D");
        auto res = test_consistency_docs_detail::run_cfire_in_dir(dir.path, "run -DCFIRE_CLI_VAL=2");
        res.assert_ok("build.cf Defines + build.Define + CLI -D propagate across sharded C++ TUs")
           .assert_contains("probe=42", "all three define sources observed across shards");

        auto eject_res = test_consistency_docs_detail::run_cfire_in_dir(
            dir.path, "eject main.cf ejected -DCFIRE_CLI_VAL=2");
        eject_res.assert_ok("cfire eject succeeds with build.cf and CLI defines");
        std::string cm_text = test_consistency_docs_detail::read_file_str(dir.path / "ejected" / "CMakeLists.txt");
        require(cm_text.contains("target_compile_definitions(app PRIVATE \"CFIRE_SHARD_DEF\")"),
            "ejected CMakeLists.txt emits CFIRE_SHARD_DEF");
        require(cm_text.contains("target_compile_definitions(app PRIVATE \"CFIRE_SHARD_VAL=30\")"),
            "ejected CMakeLists.txt emits CFIRE_SHARD_VAL=30");
        require(cm_text.contains("target_compile_definitions(app PRIVATE \"CFIRE_TOP_VAL=10\")"),
            "ejected CMakeLists.txt emits CFIRE_TOP_VAL=10");
        require(cm_text.contains("target_compile_definitions(app PRIVATE \"CFIRE_CLI_VAL=2\")"),
            "ejected CMakeLists.txt emits CFIRE_CLI_VAL=2");
    }
    {
        ScopedTempDir dir("define_impl_unsharded");
        dir.file("inc/impl_probe.h", R"H(#ifndef CFIRE_IMPL_PROBE_H
#define CFIRE_IMPL_PROBE_H
int cfire_impl_fn(void);
#ifdef CFIRE_TEST_IMPLEMENTATION
int cfire_impl_fn(void) { return 88; }
#endif
#endif
)H");
        dir.file("build.cf", R"CF(package main
import "core:build"

app := build.Executable{
    Name:     "impl_app",
    Entry:    "main.cf",
    Includes: ["inc"],
    Defines:  ["CFIRE_TEST_IMPLEMENTATION"],
}
)CF");
        dir.file("helper/helper.cf", R"CF(package helper
import cpp "impl_probe.h"
fn FromHelper() -> i32 {
    return cpp.cfire_impl_fn()
}
)CF");
        dir.file("main.cf", R"CF(package main
import cpp "impl_probe.h"
import "./helper"
fn main() {
    println("impl={}+{}", cpp.cfire_impl_fn(), helper.FromHelper())
}
)CF");
        auto res = test_consistency_docs_detail::run_cfire_in_dir(dir.path, "run");
        res.assert_ok("IMPLEMENTATION define disables C++ sharding to avoid duplicate symbol collision")
           .assert_contains("impl=88+88", "IMPLEMENTATION symbol resolved cleanly across packages");
    }

    // ---: build.cf Includes & AppName ---
    {
        ScopedTempDir dir("consistency_build_cf");
        dir.file("inc/custom_inc.h", R"H(#pragma once
static inline int custom_inc_val(void) { return 77; }
)H");
        dir.file("build.cf", R"CF(package main
import "core:build"

app := build.Executable{
    Name:     "inc_app",
    Entry:    "main.cf",
    AppName:  "Inc App",
    Includes: ["inc"],
}
)CF");
        dir.file("main.cf", R"CF(package main
import cpp "custom_inc.h"
fn main() {
    println("inc={}", cpp.custom_inc_val())
}
)CF");
        test_consistency_docs_detail::run_cfire_in_dir(dir.path, "check")
            .assert_ok("cfire check typechecks build.cf cleanly");
        auto res = test_consistency_docs_detail::run_cfire_in_dir(dir.path, "run");
        res.assert_ok("build.cf Includes + AppName build & run cleanly")
           .assert_contains("inc=77", "custom include header resolved via build.cf");
    }

    // ---: C++23 reserved keywords in structs and locals ---
    {
        ScopedTempDir dir("cpp23_keywords");
        auto main_cf = dir.file("main.cf", R"CF(package main
type Rec struct {
    concept: i32
    requires: i32
}
fn main() {
    r := Rec{ concept: 2, requires: 3 }
    co_await := 4
    module := 5
    export := 6
    println("kw_sum={}", r.concept + r.requires + co_await + module + export)
}
)CF");
        auto res = run_cfire(std::format("run \"{}\"", main_cf.string()));
        res.assert_ok("C++23 reserved keywords in structs/locals compile and run")
           .assert_contains("kw_sum=20", "keyword sum matches");
    }

    // ---: Receiver generics in sharded build ---
    {
        ScopedTempDir dir("recv_generics");
        dir.file("box.cf", R"CF(package main
type Box[T] struct {
    val: T
}
fn (b: Box[T]) get() -> T {
    return b.val
}
)CF");
        dir.file("main.cf", R"CF(package main
fn main() {
    b := Box[i64]{ val: 99 }
    println("box={}", b.get())
}
)CF");
        auto res = test_consistency_docs_detail::run_cfire_in_dir(dir.path, "run .");
        res.assert_ok("generic receiver methods work across sharded files")
           .assert_contains("box=99", "generic receiver method output matches");
    }

    // ---: cfire/RECURSIVE-TYPE, cfire/UNSATISFIED-CONSTRAINT, cfire/MOVE-REQUIRED ---
    {
        ScopedTempDir dir("sema_type_diags");
        auto rec_cf = dir.file("rec.cf", R"CF(package main
type Node struct {
    next: Node?
}
fn main() {}
)CF");
        run_cfire(std::format("check \"{}\"", rec_cf.string()))
            .assert_fail("direct optional self-recursion rejected")
            .assert_contains("cfire/RECURSIVE-TYPE", "emits cfire/RECURSIVE-TYPE");

        auto unsat_cf = dir.file("unsat.cf", R"CF(package main
type Greeter interface {
    greet() -> str
}
fn invoke_greet[T: Greeter](g: T) -> str {
    return g.greet()
}
fn main() {
    _ = invoke_greet(123)
}
)CF");
        run_cfire(std::format("check \"{}\"", unsat_cf.string()))
            .assert_fail("non-struct violating interface constraint rejected")
            .assert_contains("cfire/UNSATISFIED-CONSTRAINT", "emits cfire/UNSATISFIED-CONSTRAINT");

        auto move_cf = dir.file("move_req.cf", R"CF(package main
type OwnHolder struct {
    p: own[i64]
}
fn take_holder(move h: OwnHolder) -> i64 {
    return 1
}
fn main() {
    h := OwnHolder{ p: make(own[i64], 5) }
    _ = take_holder(h)
}
)CF");
        run_cfire(std::format("check \"{}\"", move_cf.string()))
            .assert_fail("passing move param without move keyword rejected")
            .assert_contains("cfire/MOVE-CALLSITE", "emits cfire/MOVE-CALLSITE");

        auto move_copy_cf = dir.file("move_copy.cf", R"CF(package main
type OwnHolder struct {
    p: own[i64]
}
fn main() {
    h := OwnHolder{ p: make(own[i64], 5) }
    h2 := h
    _ = h2
}
)CF");
        run_cfire(std::format("check \"{}\"", move_copy_cf.string()))
            .assert_fail("copying transitive move-only struct rejected")
            .assert_contains("cfire/MOVE-REQUIRED", "emits cfire/MOVE-REQUIRED");
    }

    // ---: Subpackage global order and root-package resolution ---
    {
        ScopedTempDir dir("subpkg_globals");
        dir.file("sub/sub.cf", R"CF(package sub
LIMIT := 100
fn SubLimit() -> i64 {
    return LIMIT
}
)CF");
        dir.file("main.cf", R"CF(package main
import "./sub"
LIMIT := 5
fn main_limit() -> i64 {
    return LIMIT
}
fn main() {
    println("limits={}:{}", main_limit(), sub.SubLimit())
}
)CF");
        auto res = test_consistency_docs_detail::run_cfire_in_dir(dir.path, "run main.cf");
        res.assert_ok("root package and subpackage identically named globals resolve independently")
           .assert_contains("limits=5:100", "root package LIMIT=5 and subpkg LIMIT=100 preserved");
    }

    // ---: eject_cmake output ---
    {
        ScopedTempDir dir("eject_cmake");
        auto main_cf = dir.file("main.cf", R"CF(package main
fn main() {
    println("eject")
}
)CF");
        auto out_dir = dir.subpath("ejected");
        auto ej = cfire::build::eject_cmake(main_cf, rt_dir, out_dir);
        require(ej.has_value(), ej.has_value() ? "" : ej.error());
        auto cmake_txt = test_consistency_docs_detail::read_file_str(out_dir / "CMakeLists.txt");
        require(!cmake_txt.empty() && cmake_txt.contains("CMAKE_CXX_STANDARD 23"),
            "eject_cmake generates C++23 CMakeLists.txt");
    }

    // ---: Local header edit invalidates build cache ---
    {
        ScopedTempDir dir("hdr_cache");
        dir.file("inc/ver.h", "#pragma once\nstatic inline int hdr_ver(void) { return 10; }\n");
        dir.file("build.cf", R"CF(package main
import "core:build"

app := build.Executable{
    Name:     "hdr_cache",
    Entry:    "main.cf",
    Includes: ["inc"],
}
)CF");
        dir.file("main.cf", R"CF(package main
import cpp "ver.h"
fn main() {
    println("hdr_ver={}", cpp.hdr_ver())
}
)CF");
        test_consistency_docs_detail::run_cfire_in_dir(dir.path, "run")
            .assert_ok("initial header build succeeds")
            .assert_contains("hdr_ver=10", "initial header version is 10");

        dir.file("inc/ver.h", "#pragma once\nstatic inline int hdr_ver(void) { return 25; }\n");
        test_consistency_docs_detail::run_cfire_in_dir(dir.path, "run")
            .assert_ok("rebuild after header edit succeeds")
            .assert_contains("hdr_ver=25", "header modification invalidates cache cleanly");
    }

    // ---: cfire eval, XDG_CONFIG_HOME, multi-token compiler_binary_fingerprint ---
    {
        run_cfire("eval \"println(\\\"eval={}\\\", 6 * 7)\"")
            .assert_ok("cfire eval executes cleanly")
            .assert_contains("eval=42", "cfire eval prints 42");

        ScopedTempDir xdg_dir("xdg_config");
        auto xdg_base = xdg_dir.subpath("xdg_cfg");
        test_consistency_docs_detail::ScopedEnvOverride unset_cfg("CFIRE_CONFIG_PATH", nullptr);
        test_consistency_docs_detail::ScopedEnvOverride set_xdg("XDG_CONFIG_HOME", xdg_base.string().c_str());
        auto cfg_path = cfire::build::config_file_path();
#if !defined(_WIN32)
        require(cfg_path == xdg_base / "cfire" / "config.json",
            std::format("XDG_CONFIG_HOME reflected in config_file_path(), got {}", cfg_path.string()));
#endif

        auto fp_a = cfire::build::compiler_binary_fingerprint("clang++ -O2");
        auto fp_b = cfire::build::compiler_binary_fingerprint("clang++ -O0");
        require(!fp_a.empty() && !fp_b.empty() && fp_a != fp_b,
            "multi-token compiler_binary_fingerprint distinguishes flags");
    }

    // ---: cfire new --template tako uses core:tako and passes cfire test ---
    {
        ScopedTempDir dir("tako_tmpl");
        test_consistency_docs_detail::run_cfire_in_dir(dir.path, "new tako_smoke --template tako")
            .assert_ok("cfire new --template tako succeeds");
        auto tako_dir = dir.path / "tako_smoke";
        auto main_cf_txt = test_consistency_docs_detail::read_file_str(tako_dir / "main.cf");
        require(main_cf_txt.contains("core:tako") && !main_cf_txt.contains("pkg:tako"),
            "tako template imports core:tako (no pkg:tako)");
        test_consistency_docs_detail::run_cfire_in_dir(tako_dir, "test")
            .assert_ok("cfire test passes inside scaffolded --template tako project");
    }

    // ---: docs/LLMS.txt synchronized with compiler catalogs ---
    {
        auto llm_ref = cfire::doc::generate_llm_reference();
        auto disk_llm = test_consistency_docs_detail::read_file_str(std::filesystem::path("docs") / "LLMS.txt");
        if (!disk_llm.empty()) {
            std::erase(disk_llm, '\r');
            auto expected_llm = llm_ref;
            std::erase(expected_llm, '\r');
            require(disk_llm == expected_llm, "docs/LLMS.txt matches generate_llm_reference()");
        }
        for (const auto& mod : cfire::doc::stdlib_catalog()) {
            require(llm_ref.contains(mod.import_path),
                std::format("LLMS reference includes stdlib module {}", mod.import_path));
        }
        for (const auto& diag : cfire::doc::diagnostic_code_registry()) {
            require(llm_ref.contains(diag.code),
                std::format("LLMS reference includes diagnostic {}", diag.code));
        }
        require(llm_ref.contains("core:tako") &&
                !llm_ref.contains("pkg:tako") &&
                !llm_ref.contains("@pkg(") &&
                !llm_ref.contains("build.Tako") &&
                !llm_ref.contains("&str") &&
                !llm_ref.contains("&[]") &&
                !llm_ref.contains("cpp::"),
            "LLMS reference contains core:tako and excludes stale tokens");
    }

    std::println("--- PASS: test_language_consistency_and_docs  ---");
}
