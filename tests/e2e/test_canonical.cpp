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
        require(res->first.contains("#include <raylib.h>") &&
                res->first.contains("#include <raymath.h>") &&
                res->first.contains("InitWindow(640, 360"),
            "raylib codegen check");
        require(res->second.auto_linked.size() == 1 && res->second.auto_linked[0] == "vendor:raylib",
            "vendor:raylib auto-link recorded");
    }

    // Verify full Raylib + Raygui struct literals (rl.Vector2, rl.Rectangle, rl.Color, rl.Camera2D),
    // type annotations, raymath functions (Vector2Add, Vector2Scale, Clamp), dynamic str coercion,
    // and immediate-mode GUI controls (GuiSetStyle, GuiGetStyle, GuiCheckBox, GuiSliderBar, GuiTextBox(&mut str))
    {
        std::string_view rl_src = R"(package main
import "vendor:raylib" as rl
import "vendor:raygui" as rg
import "core:str"

type Entity struct {
    pos: rl.Vector2
    bounds: rl.Rectangle
    tint: rl.Color
}

fn step_entity(e: Entity, vel: rl.Vector2, dt: f32) -> rl.Vector2 {
    scaled := rl.Vector2Scale(vel, dt)
    next := rl.Vector2Add(e.pos, scaled)
    return rl.Vector2{
        x: rl.Clamp(next.x, 0.0, 800.0),
        y: rl.Clamp(next.y, 0.0, 450.0)
    }
}

fn render_hud(title: str, score: i32, cam: rl.Camera2D) {
    rl.SetWindowTitle(title)
    rl.BeginMode2D(cam)
    defer rl.EndMode2D()
    rl.DrawText("Score: {score}", 20, 20, 24, rl.RAYWHITE)
}

fn main() {
    ent := Entity{
        pos: rl.Vector2{x: 100.0, y: 200.0},
        bounds: rl.Rectangle{x: 100.0, y: 200.0, width: 32.0, height: 32.0},
        tint: rl.Color{r: 255, g: 120, b: 0, a: 255}
    }
    p2 := step_entity(ent, rl.Vector2{x: 50.0, y: -20.0}, 0.5)
    assert(p2.x == 125.0)
    assert(p2.y == 190.0)

    // Test vendor:raygui style, state, slider clamping, checkbox, and safe mut str GuiTextBox
    rg.GuiSetStyle(rg.DEFAULT, rg.TEXT_SIZE, 18)
    assert(rg.GuiGetStyle(rg.DEFAULT, rg.TEXT_SIZE) == 18)
    mut vsync := true
    mut volume: f32 = 150.0
    mut quality: i32 = 1
    mut player_name := "Memo"
    box := rl.Rectangle{x: 20.0, y: 20.0, width: 200.0, height: 28.0}
    rg.GuiWindowBox(box, "Settings")
    rg.GuiCheckBox(box, "VSync", &mut vsync)
    rg.GuiSliderBar(box, "Min", "Max: {volume}", &mut volume, 0.0, 100.0)
    assert(volume == 100.0)
    rg.GuiComboBox(box, "Low;Medium;High", &mut quality)
    rg.GuiTextBox(box, &mut player_name, 64, false)
    assert(player_name == "Memo")

    // Test Cfire numeric constructors in .cf code (LANG-P0-01)
    cast_int := i32(volume)
    assert(cast_int == 100)
    cast_flt := f32(cast_int)
    assert(cast_flt == 100.0)

    // Test vendor enum bitwise operations in .cf code (LANG-P1-03)
    mut cfg_flags: rl.ConfigFlags = rl.FLAG_WINDOW_HIGHDPI
    cfg_flags = cfg_flags | rl.FLAG_WINDOW_RESIZABLE
    assert((cfg_flags & rl.FLAG_WINDOW_HIGHDPI) == rl.FLAG_WINDOW_HIGHDPI)
    assert((cfg_flags & rl.FLAG_WINDOW_RESIZABLE) == rl.FLAG_WINDOW_RESIZABLE)
    cfg_flags &= ~rl.FLAG_WINDOW_HIGHDPI
    assert((cfg_flags & rl.FLAG_WINDOW_HIGHDPI) == 0)
    assert((cfg_flags & rl.FLAG_WINDOW_RESIZABLE) == rl.FLAG_WINDOW_RESIZABLE)

    // Test string pointer offsets and C FFI interop (LANG-P1-02)
    ptr_name := player_name.byte_ptr()
    assert(ptr_name != none)
    ptr_off := player_name.byte_ptr_at(2)
    assert(ptr_off != none)
    ptr_mod := str.byte_ptr(player_name)
    assert(ptr_mod != none)
    ptr_mod_off := str.byte_ptr_at(player_name, 1)
    assert(ptr_mod_off != none)

    // Test rebindable pointer aliases and dereference assignment (LANG-P2-05)
    mut target: *mut str = &mut player_name
    *target += " Sandoval"
    assert(player_name == "Memo Sandoval")
    mut title := "Settings"
    target = &mut title
    *target += " Dialog"
    assert(title == "Settings Dialog")

    mut p_quality: *mut i32 = &mut quality
    *p_quality = 2
    assert(quality == 2)
    *p_quality += 3
    assert(quality == 5)

    mut p_null: *mut i32 = none
    assert(p_null == none)
    p_null = &mut quality
    assert(p_null != none)
    assert(*p_null == 5)

    // Test slice and vector buffer pointer access for C FFI interop (LANG-P1-06)
    mut codepoints: []i32 = [32, 65, 66]
    assert(codepoints.len() == 3)
    p_code := codepoints.data()
    assert(p_code != none)
    assert(*p_code == 32)
    p_code_ptr := codepoints.data_ptr()
    assert(p_code_ptr != none)
    assert(*p_code_ptr == 32)
    *p_code = 99
    assert(codepoints[0] == 99)
    codepoints[0] = 32
}
)";
        auto rl_dir = std::filesystem::temp_directory_path() / "cfire_raylib_full_test_dir";
        std::filesystem::remove_all(rl_dir);
        std::filesystem::create_directories(rl_dir);
        auto rl_tmp = rl_dir / "main.cf";
        {
            std::ofstream ofs(rl_tmp);
            ofs << rl_src;
        }
        auto rl_emit = cfire::build::analyze_and_emit(rl_tmp, rt_dir, false);
        require(rl_emit.has_value(), "analyze_and_emit full raylib structs/raymath/str coercion");
        if (rl_emit) {
            require(rl_emit->first.contains("Vector2{.x =") &&
                    rl_emit->first.contains("Rectangle{.x =") &&
                    rl_emit->first.contains("Color{.r =") &&
                    rl_emit->first.contains("cfire::c_arg(title)") &&
                    rl_emit->first.contains("cfire::str::byte_ptr") &&
                    rl_emit->first.contains("cfire::str::byte_ptr_at") &&
                    rl_emit->first.contains("(codepoints).data()") &&
                    rl_emit->first.contains("(*target) +="),
                "raylib struct literal, c_arg string coercion, and pointer interop emitted");
        }
        // Execute the headless Raylib + Raymath vector/collision test end-to-end
        auto rl_bin = rl_dir / "raylib_full_test_bin";
        auto rl_build = cfire::build::build_native(rl_tmp, rt_dir, rl_bin, false, false);
        require(rl_build.has_value(), "build_native raylib + raymath struct/vector test");
        if (rl_build) {
            int rl_rc = 0;
            std::string rl_out = cfire::bridge::run_command_capture("\"" + rl_bin.string() + "\"", rl_rc);
            require(rl_rc == 0, std::format("raylib_full_test exited with 0 (got {}): {}", rl_rc, rl_out));
        }
        std::filesystem::remove_all(rl_dir);
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



void test_wasm_and_gui_subsystem(const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_wasm_and_gui_subsystem");
    ScopedTempDir tmp("cfire_wasm_gui");

    // 1. WebAssembly CLI profile auto-detection on non-GUI code
    auto cli_cf = tmp.file("cli.cf", "package main\n\nfn main() {\n    println(\"hello wasm cli\")\n}\n");
    auto cli_res = cfire::build::analyze_and_emit(cli_cf, rt_dir, false, false, "wasm32-unknown-emscripten");
    require(cli_res.has_value(), "analyze_and_emit for wasm CLI");
    if (cli_res) {
        require(!cli_res->second.is_gui, "cli.cf is not marked as GUI");
        bool has_platform_web = false;
        for (const auto& f : cli_res->second.cflags) {
            if (f == "-DPLATFORM_WEB") has_platform_web = true;
        }
        require(has_platform_web, "wasm target defines -DPLATFORM_WEB");
    }

    // 2. WebAssembly Canvas profile auto-detection on Raylib code
    auto gui_cf = tmp.file("gui.cf", "package main\nimport \"vendor:raylib\" as rl\n\nfn main() {\n    rl.InitWindow(320, 240, \"test\")\n}\n");
    auto gui_res = cfire::build::analyze_and_emit(gui_cf, rt_dir, false, false, "wasm32-unknown-emscripten");
    require(gui_res.has_value(), "analyze_and_emit for wasm GUI (Canvas)");
    if (gui_res) {
        require(gui_res->second.is_gui, "gui.cf is marked as GUI");
        bool has_glfw = false;
        bool has_asyncify = false;
        for (const auto& l : gui_res->second.ldflags) {
            if (l == "-sUSE_GLFW=3") has_glfw = true;
            if (l == "-sASYNCIFY=1") has_asyncify = true;
        }
        require(has_glfw, "wasm canvas profile links with -sUSE_GLFW=3");
        require(has_asyncify, "wasm canvas profile links with -sASYNCIFY=1");
    }

    // 3. Toolchain detection & friendly diagnostics when em++ is absent
    auto build_cli_out = run_cfire(std::format("build --target=wasm \"{}\"", cli_cf.string()));
    require(build_cli_out.exit_code != 0, "build --target=wasm exits non-zero when em++ is not in PATH");
    require(build_cli_out.output.contains("em++ (Emscripten compiler) not found"),
        "helpful error diagnostic when em++ is missing");

    // 4. Native GUI Subsystem: Windows -mwindows flag
    auto win_gui_res = cfire::build::analyze_and_emit(gui_cf, rt_dir, false, false, "x86_64-w64-mingw32");
    require(win_gui_res.has_value(), "analyze_and_emit Windows GUI");
    if (win_gui_res) {
        require(win_gui_res->second.is_gui, "win_gui_res marked as GUI");
    }

    // 5. Native GUI Subsystem: macOS .app bundle creation for Raylib
#if defined(__APPLE__)
    auto rl_dir = tmp.path / "app_bundle_test";
    std::filesystem::create_directories(rl_dir);
    auto app_cf = rl_dir / "main.cf";
    {
        std::ofstream ofs(app_cf);
        ofs << "package main\nimport \"vendor:raylib\" as rl\n\nfn main() {\n    rl.InitWindow(100, 100, \"App\")\n}\n";
    }
    auto out_bin = rl_dir / "my_gui_app";
    auto app_build = cfire::build::build_native(app_cf, rt_dir, out_bin, false, false, "", "", {}, {}, "auto", "gui");
    if (app_build) {
        require(!app_build->macos_app_bundle.empty(), "macos_app_bundle path recorded in plan");
        auto bundle_path = std::filesystem::path(app_build->macos_app_bundle);
        require(std::filesystem::exists(bundle_path), "macOS .app bundle directory created");
        require(std::filesystem::exists(bundle_path / "Contents" / "Info.plist"), "Info.plist exists inside bundle");
        require(std::filesystem::exists(bundle_path / "Contents" / "MacOS" / "my_gui_app"), "executable copied into Contents/MacOS/");
        auto plist_content = cfire::build::read_file_text(bundle_path / "Contents" / "Info.plist");
        require(plist_content.has_value() && plist_content->contains("CFBundlePackageType") && plist_content->contains("APPL"),
            "Info.plist contains APPL package type");
    }

    // 6. Subsystem console override: --subsystem=console suppresses .app bundle
    auto out_bin_console = rl_dir / "my_console_app";
    auto console_build = cfire::build::build_native(app_cf, rt_dir, out_bin_console, false, false, "", "", {}, {}, "auto", "console");
    if (console_build) {
        require(console_build->macos_app_bundle.empty(), "--subsystem=console suppresses macOS .app bundle creation");
        require(!std::filesystem::exists(rl_dir / "my_console_app.app"), "no .app bundle created when console subsystem is forced");
    }
#endif

    std::println("--- PASS: test_wasm_and_gui_subsystem");
}



