#include "../test_harness.hpp"

void test_end_to_end_canonical_examples(const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_end_to_end_canonical_examples");

    std::vector<ExampleCase> cases = {
        {
            .name = "01-hello-core",
            .cf_path = "examples/01-hello-core/main.cf",
            .args = "",
            .expected_stdout = {
                "Recorded #1 -> [INFO] boot sequence complete",
                "[native C++23] vector capacity >= true",
                "Audit session closed (1 entries).",
            },
            .expected_cpp = {
                "std::string_view prefix, AuditLog& log, std::string&& msg",
                "std::format(\"[{}] {}\", prefix, std::move(msg))",
                "cfire::scope_exit",
            },
        },
        {
            .name = "02-log-compressor",
            .cf_path = "examples/02-log-compressor/main.cf",
            .args = "--input README.md --level 3",
            .expected_stdout = {
                "Compressed README.md ->",
                "zstd bytes (verified roundtrip)",
            },
            .expected_cpp = {
                "// @cfire-link: -lzstd",
                "#define CFIRE_USE_ZSTD 1",
                "cfire::cli::parse<Options>",
            },
        },
        {
            .name = "03-pkg-cache",
            .cf_path = "examples/03-pkg-cache/main.cf",
            .args = "",
            .expected_stdout = {
                "Indexed ripgrep@14.1.0 (rows=1, size=4096)",
            },
            .expected_cpp = {
                "// @cfire-link: -lsimdjson -lsqlite3",
                "cfire::json::parse_struct<PackageMeta>",
                "cfire::sqlite::map_row<PackageMeta>",
            },
        },
        {
            .name = "04-async-workers",
            .cf_path = "examples/04-async-workers/main.cf",
            .args = "",
            .expected_stdout = {
                "Cluster health codes: [200, 503, 200]",
            },
            .expected_cpp = {
                "cfire::async::Scope scope(4);",
                "std::span<const std::string> hosts",
            },
        },
        {
            .name = "05-cmake-interop",
            .cf_path = "examples/05-cmake-interop/main.cf",
            .args = "",
            .expected_stdout = {
                "Ingested native <zstd.h> via Clang AST Bridge: version=",
            },
            .expected_cpp = {
                "#include <zstd.h>",
                "ZSTD_versionNumber()",
            },
        },
        {
            .name = "07-types-match-map",
            .cf_path = "examples/07-types-match-map/main.cf",
            .args = "",
            .expected_stdout = {
                "Title slice: CAD",
                "Areas -> circ=314, rect=40, missing=-1",
                "Scene ready",
            },
            .expected_cpp = {
                "concept Renderable = true;",
                "std::visit(cfire::overloaded{",
                "cfire::Map<std::string, double>",
                "cfire::slice(",
                "template <Renderable T>",
            },
        },
        {
            .name = "08-multi-pkg-cc-build",
            .cf_path = "examples/08-multi-pkg-cc-build/main.cf",
            .args = "",
            .expected_stdout = {
                "Multi-package + build.CC archive checksum: 1764876467",
            },
            .expected_cpp = {
                "#include <fastcrc.h>",
                "fastcrc_compute(cfire::c_str(label), label.size())",
            },
        },
        {
            .name = "09-stdlib-test-pointers",
            .cf_path = "examples/09-stdlib-test-pointers/main.cf",
            .args = "",
            .expected_stdout = {
                "Stdlib: env=PRODUCTION service=alpha-beta-gamma count=128 healthy=true ptr_sum=100 arr_sum=100 tcp=PING_CFIRE ch=777",
            },
            .expected_cpp = {
                "cfire::make_own<Box>",
                "cfire::make_shared_ptr<Box>",
                "std::array<std::int32_t, 4>",
                "cfire::json::stringify_struct<Metric>",
                "cfire::sync::make_channel<std::int64_t>",
                "__cfire_test_0()",
            },
        },
        {
            .name = "10-cmake-and-make-deps",
            .cf_path = "examples/10-cmake-and-make-deps/main.cf",
            .args = "",
            .expected_stdout = {
                "CMake kvstore slot for 'user:sessions:42' -> 0",
                "Make vecmath scaled dot product -> 64",
            },
            .expected_cpp = {
                "#include <kvstore.h>",
                "#include <vecmath.h>",
                "kv_compute_slot(\"user:sessions:42\", 16)",
                "vec3_scaled_dot(1.0, 2.0, 3.0",
            },
        },
    };

    auto tmp_dir = std::filesystem::temp_directory_path() / "cfire_conformance_bins";
    std::filesystem::create_directories(tmp_dir);

    for (const auto& tc : cases) {
        std::println("=== RUN   test_end_to_end_canonical_examples/{}", tc.name);
        auto out_bin = tmp_dir / tc.name;
        auto plan = cfire::build::build_native(tc.cf_path, rt_dir, out_bin);
        if (!plan) {
            require(false, std::format("build_native({}) failed: {}", tc.name, plan.error()));
            continue;
        }

        auto gen_cpp = cfire::build::read_file_text(plan->generated_cpp);
        require(gen_cpp.has_value(), "read generated cpp for " + tc.name);
        for (const auto& snippet : tc.expected_cpp) {
            require(gen_cpp->contains(snippet),
                std::format("{} generated C++23 missing snippet '{}'\nGenerated:\n{}", tc.name, snippet, *gen_cpp));
        }

        if (tc.name == "05-cmake-interop") {
            require(plan->external_deps.size() == 2, "expected 2 external_deps from build.cf");
            auto it = plan->cpp_headers.find("zstd.h");
            require(it != plan->cpp_headers.end(), "expected Clang AST index for zstd.h");
            if (it != plan->cpp_headers.end()) {
                require(it->second.symbols.contains("ZSTD_versionNumber"),
                    "expected ZSTD_versionNumber in Clang AST index");
            }
        }

        if (tc.name == "08-multi-pkg-cc-build") {
            require(plan->local_cf_packages.size() == 1 && plan->local_cf_packages[0] == "./pkg/checksum",
                "expected ./pkg/checksum in local_cf_packages");
            require(plan->external_deps.size() == 1 && plan->external_deps[0].built_archive.ends_with("libFastCRC.a"),
                "expected built static archive libFastCRC.a");
            require(plan->cpp_headers.contains("fastcrc.h") &&
                    plan->cpp_headers["fastcrc.h"].symbols.contains("fastcrc_compute"),
                "expected fastcrc_compute in Clang AST index for fastcrc.h");
            const auto& sym = plan->cpp_headers["fastcrc.h"].symbols["fastcrc_compute"];
            require(sym.param_types.size() == 2 && sym.param_types[0] == "const char *",
                "expected Clang AST ParmVarDecl param_types[0] == 'const char *' for fastcrc_compute");

            // Verify .cfire/cache/ast/ persistent index cache hit on second analysis
            auto second_res = cfire::build::analyze_and_emit(tc.cf_path, rt_dir, true);
            require(second_res.has_value() && second_res->second.cpp_headers["fastcrc.h"].from_cache,
                "expected .cfire/cache/ast/ hit on second analyze_and_emit");
        }

        int rc = 0;
        std::string run_cmd = "\"" + out_bin.string() + "\" " + tc.args;
        std::string out = cfire::bridge::run_command_capture(run_cmd, rc);
        require(rc == 0, std::format("{} exited with code {}: {}", tc.name, rc, out));
        for (const auto& sub : tc.expected_stdout) {
            require(out.contains(sub),
                std::format("{} output missing '{}'. Got:\n{}", tc.name, sub, out));
        }

        // For 09-stdlib-test-pointers, verify PCH acceleration and compile+run in `cfire test --sanitize` mode
        require(plan->used_pch, std::format("expected PCH cache acceleration on {}", tc.name));
        if (tc.name == "09-stdlib-test-pointers") {
            auto test_bin = tmp_dir / "09_test_runner";
            auto test_plan = cfire::build::build_native(tc.cf_path, rt_dir, test_bin, true, true);
            require(test_plan.has_value() && test_plan->sanitized && test_plan->used_pch,
                "build_native(..., test_mode=true, sanitize=true) for 09-stdlib-test-pointers");
            if (test_plan) {
                int trc = 0;
                std::string tout = cfire::bridge::run_command_capture("\"" + test_bin.string() + "\"", trc);
                require(trc == 0 && tout.contains("PASS (2 cfire tests succeeded)"),
                    std::format("expected 'PASS (2 cfire tests succeeded)' from cfire test --sanitize mode, got:\n{}", tout));
            }
        }
    }

    std::filesystem::remove_all(tmp_dir);
    std::println("--- PASS: test_end_to_end_canonical_examples");
}



void test_raylib_vendor_codegen_and_eject(const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_raylib_vendor_codegen_and_eject");
    auto res = cfire::build::analyze_and_emit("examples/06-raylib-viz/main.cf", rt_dir, false);
    require(res.has_value(), "analyze_and_emit(06-raylib-viz)");
    if (res) {
        require(res->first.contains("#include <raylib.h>") && res->first.contains("InitWindow(640, 360"),
            "raylib codegen check");
        require(res->second.auto_linked.size() == 1 && res->second.auto_linked[0] == "vendor:raylib",
            "vendor:raylib auto-link recorded");
    }

    auto eject_dir = std::filesystem::temp_directory_path() / "cfire_eject_test";
    std::filesystem::remove_all(eject_dir);
    auto ej = cfire::build::eject_cmake("examples/02-log-compressor/main.cf", rt_dir, eject_dir);
    require(ej.has_value(), "eject_cmake(02-log-compressor)");
    auto cmake_txt = cfire::build::read_file_text(eject_dir / "CMakeLists.txt");
    require(cmake_txt.has_value() &&
            cmake_txt->contains("set(CMAKE_CXX_STANDARD 23)") &&
            cmake_txt->contains("libzstd"),
        "ejected CMakeLists.txt content check");
    std::filesystem::remove_all(eject_dir);

    auto eject_cc_dir = std::filesystem::temp_directory_path() / "cfire_eject_cc_test";
    std::filesystem::remove_all(eject_cc_dir);
    auto ej_cc = cfire::build::eject_cmake("examples/08-multi-pkg-cc-build/main.cf", rt_dir, eject_cc_dir);
    require(ej_cc.has_value(), "eject_cmake(08-multi-pkg-cc-build)");
    auto cmake_cc_txt = cfire::build::read_file_text(eject_cc_dir / "CMakeLists.txt");
    require(cmake_cc_txt.has_value() &&
            cmake_cc_txt->contains("add_library(FastCRC STATIC") &&
            cmake_cc_txt->contains("FASTCRC_SEED=42"),
        "ejected CMakeLists.txt build.CC static library check");
    std::filesystem::remove_all(eject_cc_dir);
    std::println("--- PASS: test_raylib_vendor_codegen_and_eject");
}



void test_cfire_cross_compile(const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_cfire_cross_compile");
    (void)rt_dir;
    ScopedTempDir tmp("cfire_cross");
    auto main_cf = tmp.file("main.cf", "package main\n\nfn main() {\n    println(\"cross target test ok\")\n}\n");

    // 1. Compile with target flag
    auto out_bin = tmp.path / "cross_bin";
    run_cfire(std::format("build \"{}\" -o \"{}\" --target arm64-apple-darwin", main_cf.string(), out_bin.string()))
        .assert_ok("cfire build --target arm64-apple-darwin exit 0 (BS-EXT-02)")
        .assert_contains("target=arm64-apple-darwin", "output reports target=arm64-apple-darwin (BS-EXT-02)");
    require(std::filesystem::exists(out_bin), "cfire build -o created output binary at requested path");

    // 2. Check emit-cpp with target
    run_cfire(std::format("emit-cpp \"{}\" --target x86_64-apple-darwin", main_cf.string()))
        .assert_ok("cfire emit-cpp --target exit 0 (BS-EXT-02)")
        .assert_contains("auto main(", "emit-cpp contains main (BS-EXT-02)");

    std::println("--- PASS: test_cfire_cross_compile");
}


