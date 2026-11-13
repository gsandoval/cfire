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
                "cfire::str_ref prefix, AuditLog& log, std::string&& msg",
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
                "std::get_if<Shape::",
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
        {
            .name = "14-ai-rag-sqlite",
            .cf_path = "examples/14-ai-rag-sqlite/main.cf",
            .args = "",
            .expected_stdout = {
                "Top RAG Match -> [#1] Transpiler Architecture: cfire lowers 1-to-1 to ISO C++23 with zero hidden runtime overhead.",
            },
            .expected_cpp = {
                "cfire::json::schema<QueryPlan>()",
                "cfire::ai::local()",
                "client.embed(doc)",
                "cfire::sqlite::open(\":memory:\")",
            },
        },
        {
            .name = "15-mcp-server",
            .cf_path = "examples/15-mcp-server/main.cf",
            .args = "",
            .expected_stdout = {
                "Tool 'calculate' invoked with expr: '6 * 7', precision: 2",
                "MCP Server response -> {\"jsonrpc\":\"2.0\",\"id\":3,\"result\":{\"content\":[{\"type\":\"text\",\"text\":\"42.0\"}],\"isError\":false}}",
            },
            .expected_cpp = {
                "cfire::ai::make_mcp_server(\"calculator-mcp\", \"1.0.0\")",
                "srv.register_tool<CalculateArgs>(\"calculate\"",
                "srv.handle_jsonrpc(call_req)",
            },
        },
        {
            .name = "16-audio-synth",
            .cf_path = "examples/16-audio-synth/main.cf",
            .args = "",
            .expected_stdout = {
                "Synthesized WAV image: 48044 bytes, duration: 0.25s",
                "Loaded WAV: rate=48000 channels=2 frames=12000",
            },
            .expected_cpp = {
                "cfire::audio::init(48000, 2)",
                "cfire::audio::gen_sfx(cfire::audio::SfxPreset::Coin)",
                "cfire::audio::encode_wav(synth_wave)",
            },
        },
        {
            .name = "17-webui-studio",
            .cf_path = "examples/17-webui-studio/main.cf",
            .args = "",
            .expected_stdout = {
                "Studio RPC ready -> track={\"channel\":1,\"effective_db\":-3,\"headroom_db\":3,\"status\":\"active\"} preset=\"loaded:Punchy Bus\" state=Punchy Bus",
            },
            .expected_cpp = {
                "cfire::webui::new_app(\"Pocket Mastering Studio\", 980, 640)",
                "app.bind<TrackMixReq>(\"set_track\"",
                "app.bind_action(\"reset_desk\"",
            },
        },
    };

    ScopedTempDir tmp_dir_scope("cfire_tmp_dir");  // hermetic, mkdtemp-backed (CF-153)
    auto tmp_dir = tmp_dir_scope.path / "cfire_conformance_bins";
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
        std::string run_cmd = "CFIRE_HEADLESS_TEST=1 \"" + out_bin.string() + "\" " + tc.args;
        std::string out = cfire::bridge::run_command_capture(run_cmd, rc);
        require(rc == 0, std::format("{} exited with code {}: {}", tc.name, rc, out));
        for (const auto& sub : tc.expected_stdout) {
            require(out.contains(sub),
                std::format("{} output missing '{}'. Got:\n{}", tc.name, sub, out));
        }

        // For 09-stdlib-test-pointers, verify PCH acceleration and compile+run in `cfire test --sanitize` mode
        if (!plan->uses_objc) {
            require(plan->used_pch, std::format("expected PCH cache acceleration on {}", tc.name));
        }
        if (tc.name == "09-stdlib-test-pointers") {
            auto test_bin = tmp_dir / "09_test_runner";
            auto test_plan = cfire::build::build_native(tc.cf_path, rt_dir, test_bin, true, true);
            require(test_plan.has_value() && test_plan->sanitized && test_plan->used_pch,
                "build_native(..., test_mode=true, sanitize=true) for 09-stdlib-test-pointers");
            if (test_plan) {
                int trc = 0;
                std::string tout = cfire::bridge::run_command_capture("\"" + test_bin.string() + "\"", trc);
                require(trc == 0 &&
                        tout.contains("[doctest] compute_fixed_sum (#1)") &&
                        tout.contains("math.clamp_i64 stays within bounds") &&
                        tout.contains("str.split and str.join throughput") &&
                        tout.contains("[auto] json round-trip: Metric") &&
                        tout.contains("PASS (6 cfire tests succeeded)"),
                    std::format("expected 'PASS (6 cfire tests succeeded)' from cfire test --sanitize mode, got:\n{}", tout));
            }
        }
    }

    std::filesystem::remove_all(tmp_dir);
    std::println("--- PASS: test_end_to_end_canonical_examples");
}



// CF-185: the C header bridge against a bundled C library (tests/fixtures/cinterop) instead of raylib: C structs as
// literals and fields, struct returns, `str` -> `const char*`, slice data pointers, and bitflag enums.
void test_cinterop_codegen_and_eject(const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_cinterop_codegen_and_eject");
    {
        ScopedTempDir fx_scope("cfire_cinterop");  // hermetic, mkdtemp-backed (CF-153)
        auto fx = fx_scope.path / "cinterop";
        std::error_code cec;
        std::filesystem::copy("tests/fixtures/cinterop", fx,
            std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, cec);
        std::filesystem::remove_all(fx / ".cfire", cec);
        require(!cec, std::format("copy the cinterop fixture: {}", cec.message()));
        auto emit = cfire::build::analyze_and_emit(fx / "main.cf", rt_dir, false);
        require(emit.has_value(), std::format("analyze_and_emit(cinterop): {}", emit ? "" : emit.error()));
        if (emit) {
            require(emit->first.contains("Vec2{.x =") &&
                    emit->first.contains("Rect{.x =") &&
                    emit->first.contains("Color{.r =") &&
                    emit->first.contains("cfire::c_arg(title)") &&
                    emit->first.contains("(values).data()"),
                "C struct literals, c_arg string coercion, and slice data pointers emitted");
        }
        auto run = run_cfire(std::format("run \"{}\"", fx.string()));
        require(run.ok() && run.output.contains("cinterop ok ff7800ff"),
            std::format("cinterop fixture builds with build.CC and runs: {}", run.output));
    }

    ScopedTempDir eject_dir_scope("cfire_eject_dir");  // hermetic, mkdtemp-backed (CF-153)
    auto eject_dir = eject_dir_scope.path / "cfire_eject_test";
    std::filesystem::remove_all(eject_dir);
    auto ej = cfire::build::eject_cmake("examples/02-log-compressor/main.cf", rt_dir, eject_dir);
    require(ej.has_value(), "eject_cmake(02-log-compressor)");
    auto cmake_txt = cfire::build::read_file_text(eject_dir / "CMakeLists.txt");
    require(cmake_txt.has_value() &&
            cmake_txt->contains("set(CMAKE_CXX_STANDARD 23)") &&
            cmake_txt->contains("libzstd"),
        "ejected CMakeLists.txt content check");
    std::filesystem::remove_all(eject_dir);

    ScopedTempDir eject_cc_dir_scope("cfire_eject_cc_dir");  // hermetic, mkdtemp-backed (CF-153)
    auto eject_cc_dir = eject_cc_dir_scope.path / "cfire_eject_cc_test";
    std::filesystem::remove_all(eject_cc_dir);
    auto ej_cc = cfire::build::eject_cmake("examples/08-multi-pkg-cc-build/main.cf", rt_dir, eject_cc_dir);
    require(ej_cc.has_value(), "eject_cmake(08-multi-pkg-cc-build)");
    auto cmake_cc_txt = cfire::build::read_file_text(eject_cc_dir / "CMakeLists.txt");
    require(cmake_cc_txt.has_value() &&
            cmake_cc_txt->contains("add_library(FastCRC STATIC") &&
            cmake_cc_txt->contains("FASTCRC_SEED=42"),
        "ejected CMakeLists.txt build.CC static library check");
    std::filesystem::remove_all(eject_cc_dir);
    std::println("--- PASS: test_cinterop_codegen_and_eject");
}



void test_cfire_cross_compile(const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_cfire_cross_compile");
    ScopedTempDir tmp("cfire_cross");
    auto main_cf = tmp.file("main.cf", "package main\n\nfn main() {\n    println(\"cross target test ok\")\n}\n");

    // 1. Compile with target flag
    auto out_bin = tmp.path / "cross_bin";
#if defined(__APPLE__)
    std::string test_target = "arm64-apple-darwin";
#elif defined(_WIN32)
    std::string test_target = "x86_64-w64-mingw32";
#elif defined(__aarch64__)
    std::string test_target = "aarch64-unknown-linux-gnu";
#else
    std::string test_target = "x86_64-unknown-linux-gnu";
#endif
    run_cfire(std::format("build \"{}\" -o \"{}\" --target {}", main_cf.string(), out_bin.string(), test_target))
        .assert_ok(std::format("cfire build --target {} exit 0 (BS-EXT-02)", test_target))
        .assert_contains(std::format("target={}", test_target), "output reports target (BS-EXT-02)");
    require(std::filesystem::exists(out_bin), "cfire build -o created output binary at requested path");

    // 2. Check emit-cpp with target
    run_cfire(std::format("emit-cpp \"{}\" --target x86_64-apple-darwin", main_cf.string()))
        .assert_ok("cfire emit-cpp --target exit 0 (BS-EXT-02)")
        .assert_contains("auto main(", "emit-cpp contains main (BS-EXT-02)");

    // 3. Windows target resolution (*_windows.cf / *_unix.cf file selection & Win32 linker flags)
    ScopedTempDir win_tmp("cfire_win_cross");
    (void)win_tmp.file("plat_windows.cf", "package main\nfn target_os_tag() -> str { return \"win32-target\" }\n");
    (void)win_tmp.file("plat_unix.cf", "package main\nfn target_os_tag() -> str { return \"unix-target\" }\n");
    auto win_main = win_tmp.file("main.cf", R"(package main
import "core:os"
import "core:fs"
import "core:cli"
import "core:crypto/aes"
import "core:keychain"
import "core:net"
import "ext:webui"

fn main() {
    tag := target_os_tag()
    pid := os.getpid()
    cwd := os.cwd()
    key := aes.generate_key()
    app := make(webui.App, "Win32 Cross", 800, 600)
    app.set_hide_on_close(true)
    app.set_dark_mode(false)
    app.center()
    app.fullscreen()
    app.emit("ping", "pong")
    println("{tag}:{pid}:{cwd.len()}:{key.len()}")
}
)");

    require(cfire::build::is_windows_target("x86_64-w64-mingw32"), "is_windows_target(x86_64-w64-mingw32)");
    require(!cfire::build::is_windows_target("wasm32-unknown-emscripten"), "!is_windows_target(wasm32)");
    auto make_plan = [](std::string_view t) {
        cfire::build::BuildPlan p{};
        p.target_triple = std::string(t);
        return p;
    };
    require(!cfire::build::is_platform_file_enabled("foo_unix.cf", make_plan("x86_64-w64-mingw32")), "_unix excluded on Windows target");
    require(!cfire::build::is_platform_file_enabled("foo_unix.cf", make_plan("wasm32-unknown-emscripten")), "_unix excluded on WASM target");
    require(cfire::build::is_platform_file_enabled("foo_unix.cf", make_plan("x86_64-unknown-linux-gnu")), "_unix enabled on Linux target");
    require(cfire::build::is_platform_file_enabled("foo_wasm.cf", make_plan("wasm32-unknown-emscripten")), "_wasm enabled on WASM target");
    require(!cfire::build::is_platform_file_enabled("foo_wasm.cf", make_plan("x86_64-w64-mingw32")), "_wasm excluded on Windows target");

    run_cfire("build --help")
        .assert_ok("cfire build --help exits 0 (NW-03)")
        .assert_contains("Usage:", "cfire build --help prints usage");

    for (const char* triple : {"x86_64-pc-windows-gnu", "x86_64-pc-windows-msvc", "x86_64-w64-mingw32"}) {
        auto win_emit = cfire::build::analyze_and_emit(win_main, rt_dir, false, false, triple);
        require(win_emit.has_value(), std::format("analyze_and_emit for {}", triple));
        if (win_emit) {
            require(win_emit->first.contains("\"win32-target\"") && !win_emit->first.contains("\"unix-target\""),
                std::format("{} selects *_windows.cf and excludes *_unix.cf", triple));
            bool has_ws2 = false;
            bool has_comdlg = false;
            bool has_shlwapi = false;
            bool has_shell32 = false;
            bool has_static = false;
            for (const auto& lf : win_emit->second.ldflags) {
                if (lf == "-lws2_32") has_ws2 = true;
                if (lf == "-lcomdlg32") has_comdlg = true;
                if (lf == "-lshlwapi") has_shlwapi = true;
                if (lf == "-lshell32") has_shell32 = true;
                if (lf == "-static") has_static = true;
            }
            require(has_ws2 && has_comdlg && has_shlwapi && has_shell32 && has_static,
                std::format("{} includes -lws2_32, -lcomdlg32, -lshlwapi, -lshell32, and -static linker flags", triple));
        }
    }

    // 4. Real MinGW-w64 cross-compilation & PE32+ .exe linking (including PNG->ICO windres resource & self-contained WebView2 COM)
    int has_mingw = 0;
    (void)cfire::bridge::run_command_capture("which x86_64-w64-mingw32-g++ 2>/dev/null", has_mingw);
    if (has_mingw == 0) {
        auto win_emit = cfire::build::analyze_and_emit(win_main, rt_dir, false, false, "x86_64-w64-mingw32");
        require(win_emit.has_value(), "analyze_and_emit for x86_64-w64-mingw32");
        if (win_emit) {
            auto cpp_path = win_tmp.path / "win_cross.cpp";
            auto exe_path = win_tmp.path / "win_cross.exe";
            {
                std::ofstream ofs(cpp_path);
                ofs << win_emit->first;
            }
            // Write a valid 1x1 PNG icon to verify wrap_png_as_ico + compile_windows_icon via windres
            auto png_path = win_tmp.path / "icon.png";
            {
                static constexpr std::uint8_t kMinPng[] = {
                    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
                    0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
                    0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x20,
                    0x08, 0x06, 0x00, 0x00, 0x00, 0x73, 0x7A, 0x7A, 0xF4,
                    0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82
                };
                std::ofstream pofs(png_path, std::ios::binary);
                pofs.write(reinterpret_cast<const char*>(kMinPng), sizeof(kMinPng));
            }
            auto res_obj = cfire::build::compile_windows_icon(png_path, exe_path, "x86_64-w64-mingw32");
            std::string res_arg = res_obj.empty() ? "" : std::format(" \"{}\"", res_obj);
            std::string cmd = std::format(
                "x86_64-w64-mingw32-g++ -std=c++23 -O2 -Wall -Wextra -I\"{}\" -I\"{}\" \"{}\" -o \"{}\"{} "
                "-lws2_32 -ladvapi32 -luser32 -lgdi32 -lshell32 -lole32 -ldwmapi -lcomdlg32 -lshlwapi -lstdc++exp",
                rt_dir.string(), (rt_dir / "vendor").string(), cpp_path.string(), exe_path.string(), res_arg);
            int mrc = 0;
            std::string mout = cfire::bridge::run_command_capture(cmd, mrc);
            require(mrc == 0 && std::filesystem::exists(exe_path),
                std::format("x86_64-w64-mingw32-g++ cross-compiled and linked win_cross.exe cleanly (rc={}): {}", mrc, mout));
        }
    }

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

    // 2. With the raylib canvas gone (CF-185, Q49), a webui program on wasm gets no GLFW link flags.
    auto gui_cf = tmp.file("gui.cf", "package main\nimport \"ext:webui\"\n\nfn main() {\n    println(\"gui\")\n}\n");
    auto gui_res = cfire::build::analyze_and_emit(gui_cf, rt_dir, false, false, "wasm32-unknown-emscripten");
    require(gui_res.has_value(), "analyze_and_emit for a wasm webui program");
    if (gui_res) {
        for (const auto& l : gui_res->second.ldflags) {
            require(l != "-sUSE_GLFW=3", "no raylib GLFW link flags on wasm");
        }
    }

    // 3. Toolchain detection & wasm build verification
    auto out_wasm_html = tmp.path / "cli.html";
    auto build_cli_out = run_cfire(std::format("build --target=wasm \"{}\" -o \"{}\"", cli_cf.string(), out_wasm_html.string()));
    int has_empp = 0;
    (void)cfire::bridge::run_command_capture("which em++ 2>/dev/null", has_empp);
    if (has_empp == 0) {
        require(build_cli_out.exit_code == 0, "build --target=wasm succeeds when em++ is in PATH");
    } else {
        require(build_cli_out.exit_code != 0, "build --target=wasm exits non-zero when em++ is not in PATH");
        require(build_cli_out.output.contains("em++ (Emscripten compiler) not found"),
            "helpful error diagnostic when em++ is missing");
    }

    // 4. Native GUI Subsystem: Windows -mwindows flag
    auto win_gui_res = cfire::build::analyze_and_emit(gui_cf, rt_dir, false, false, "x86_64-w64-mingw32");
    require(win_gui_res.has_value(), "analyze_and_emit Windows GUI");
    if (win_gui_res) {
        require(win_gui_res->second.is_gui, "win_gui_res marked as GUI");
    }

    // 5. Native GUI Subsystem: macOS .app bundle creation (`--subsystem gui`)
#if defined(__APPLE__)
    auto rl_dir = tmp.path / "app_bundle_test";
    std::filesystem::create_directories(rl_dir);
    auto app_cf = rl_dir / "main.cf";
    {
        std::ofstream ofs(app_cf);
        ofs << "package main\n\nfn main() {\n    println(\"App\")\n}\n";
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

    // 7. Native iOS & Simulator Target (`OPP-01`: `ios-sim` & `ios` target triples, flat .app bundle)
    require(cfire::build::is_ios_target("ios") && cfire::build::is_ios_target("ios-sim"), "is_ios_target detects ios and ios-sim");
    require(cfire::build::is_ios_simulator_target("ios-sim") && !cfire::build::is_ios_simulator_target("ios"),
        "is_ios_simulator_target distinguishes simulator from physical device");
    require(cfire::build::normalize_ios_triple("ios-sim") == "arm64-apple-ios17.0-simulator",
        "normalize_ios_triple(ios-sim) returns arm64-apple-ios17.0-simulator");
    require(cfire::build::normalize_ios_triple("ios") == "arm64-apple-ios17.0",
        "normalize_ios_triple(ios) returns arm64-apple-ios17.0");

    auto ios_dir = tmp.path / "ios_bundle_test";
    std::filesystem::create_directories(ios_dir);
    auto ios_cf = ios_dir / "main.cf";
    {
        std::ofstream ofs(ios_cf);
        ofs << R"(package main
import "core:fs"
import "core:json"
import "ext:db/sqlite"
import "ext:compress/zstd"

type SaveState struct {
    Pilot: str `json:"pilot"`
    Score: i64 `json:"score"`
}

fn main() {
    s := SaveState{Pilot: "Memo", Score: 42}
    raw := json.stringify(s)
    _ = fs.write_text("save.json", raw)
    _ = zstd.compress("hello ios")
    _ = sqlite.open("game.db")
}
)";
    }
    auto ios_emit = cfire::build::analyze_and_emit(ios_cf, rt_dir, false, false, "ios-sim");
    require(ios_emit.has_value(), "analyze_and_emit for ios-sim with fs, json, sqlite, zstd");
    if (ios_emit) {
        bool has_platform_ios = false;
        for (const auto& cf : ios_emit->second.cflags) {
            if (cf == "-DPLATFORM_IOS=1") has_platform_ios = true;
        }
        require(has_platform_ios, "ios-sim cflags include -DPLATFORM_IOS=1");
    }

    if (cfire::build::resolve_ios_sdk_path(true).has_value()) {
        auto ios_bin = ios_dir / "ios_game";
        auto ios_build = cfire::build::build_native(ios_cf, rt_dir, ios_bin, false, false, "ios-sim");
        require(ios_build.has_value(), std::format("build_native for ios-sim succeeded: {}", ios_build ? "" : ios_build.error()));
        if (ios_build) {
            auto ios_bundle = std::filesystem::path(ios_build->ios_app_bundle);
            require(std::filesystem::exists(ios_bundle / "ios_game"), "flat iOS .app bundle contains executable at root");
            require(std::filesystem::exists(ios_bundle / "Info.plist"), "flat iOS .app bundle contains Info.plist at root");
            auto ios_plist = cfire::build::read_file_text(ios_bundle / "Info.plist");
            require(ios_plist.has_value() &&
                    ios_plist->contains("UIDeviceFamily") &&
                    ios_plist->contains("MinimumOSVersion") &&
                    ios_plist->contains("UIInterfaceOrientationPortrait") &&
                    ios_plist->contains("UIInterfaceOrientationLandscapeLeft"),
                "iOS Info.plist contains UIDeviceFamily, MinimumOSVersion, and portrait and landscape orientations");
        }
    }
#endif

    std::println("--- PASS: test_wasm_and_gui_subsystem");
}



