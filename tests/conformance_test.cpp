// tests/conformance_test.cpp — Native C++23 Conformance Suite for cfire (.cf)

#include "../src/buildsys.hpp"
#include "../src/docgen.hpp"
#include "../src/formatter.hpp"
#include "../src/lsp.hpp"
#include "../runtime/cfire_core_stdlib.hpp"
#include <cstdlib>
#include <filesystem>
#include <print>
#include <sstream>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void require(bool cond, std::string_view msg) {
    if (!cond) {
        std::println(stderr, "  [FAIL] {}", msg);
        ++g_failures;
    }
}

// RAII Hermetic Test Directory (thread-safe and PID-isolated)
struct ScopedTempDir {
    std::filesystem::path path;

    explicit ScopedTempDir(std::string_view prefix = "cfire_test") {
        static std::atomic<std::uint64_t> s_id{0};
        path = std::filesystem::temp_directory_path() / std::format("{}_{}_{}", prefix, ::getpid(), s_id++);
        std::error_code ec;
        std::filesystem::create_directories(path, ec);
    }

    ~ScopedTempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }

    ScopedTempDir(const ScopedTempDir&) = delete;
    ScopedTempDir& operator=(const ScopedTempDir&) = delete;
    ScopedTempDir(ScopedTempDir&&) = default;
    ScopedTempDir& operator=(ScopedTempDir&&) = default;

    auto file(std::string_view filename, std::string_view content) const -> std::filesystem::path {
        auto p = path / filename;
        std::error_code ec;
        if (p.has_parent_path()) {
            std::filesystem::create_directories(p.parent_path(), ec);
        }
        (void)cfire::fs::write_text(p, content);
        return p;
    }

    auto subpath(std::string_view sub) const -> std::filesystem::path {
        auto p = path / sub;
        std::error_code ec;
        std::filesystem::create_directories(p, ec);
        return p;
    }
};

// Fluent Subprocess Result Assertion Wrapper
struct RunResult {
    int exit_code = 0;
    std::string output{};

    [[nodiscard]] auto ok() const noexcept -> bool { return exit_code == 0; }

    auto assert_ok(std::string_view label = "") const -> const RunResult& {
        require(exit_code == 0, label.empty() ? std::format("command exit 0, got {}: {}", exit_code, output)
                                              : std::format("{}: exit 0, got {}: {}", label, exit_code, output));
        return *this;
    }

    auto assert_fail(std::string_view label = "") const -> const RunResult& {
        require(exit_code != 0, label.empty() ? "expected command failure (non-zero exit code)"
                                              : std::format("{}: expected failure (non-zero exit code)", label));
        return *this;
    }

    auto assert_contains(std::string_view needle, std::string_view label = "") const -> const RunResult& {
        require(output.contains(needle),
            label.empty() ? std::format("output contains '{}', got: {}", needle, output)
                          : std::format("{}: output contains '{}', got: {}", label, needle, output));
        return *this;
    }
};

inline auto run_cfire(std::string_view args) -> RunResult {
    RunResult r;
    r.output = cfire::bridge::run_command_capture(std::format("./cfire {}", args), r.exit_code);
    return r;
}

inline auto run_cfire_source(std::string_view source, std::string_view prefix = "cfire_run") -> RunResult {
    ScopedTempDir dir(prefix);
    auto file = dir.file("main.cf", source);
    return run_cfire(std::format("run \"{}\"", file.string()));
}

inline auto compile_and_run_cpp(std::string_view cpp_source, const std::filesystem::path& rt_dir, std::string_view test_name = "test") -> RunResult {
    ScopedTempDir dir(test_name);
    auto cpp_file = dir.file("test.cpp", cpp_source);
    auto bin_file = dir.path / "test_bin";
    std::string cmd = std::format("clang++ -std=c++2b -O2 -I\"{}\" \"{}\" -o \"{}\"",
        rt_dir.string(), cpp_file.string(), bin_file.string());
    int rc = 0;
    std::string comp_out = cfire::bridge::run_command_capture(cmd, rc);
    if (rc != 0) {
        return RunResult{.exit_code = rc, .output = std::format("compile error: {}", comp_out)};
    }
    int run_rc = 0;
    std::string run_out = cfire::bridge::run_command_capture(bin_file.string(), run_rc);
    return RunResult{.exit_code = run_rc, .output = std::move(run_out)};
}

inline void require_sema_error(std::string_view src, std::string_view expected_diag, std::string_view filename = "test.cf") {
    auto ast = cfire::frontend::parse_source(filename, src);
    require(ast.has_value(), std::format("parse {}", filename));
    if (!ast) return;
    auto checked = cfire::frontend::analyze(std::move(*ast));
    require(!checked.has_value(), std::format("expected diagnostic {}", expected_diag));
    if (!checked) {
        require(checked.error().contains(expected_diag),
            std::format("expected diagnostic {}, got: {}", expected_diag, checked.error()));
    }
}

inline void require_sema_ok(std::string_view src, std::string_view filename = "test.cf") {
    auto ast = cfire::frontend::parse_source(filename, src);
    require(ast.has_value(), std::format("parse {}", filename));
    if (!ast) return;
    auto checked = cfire::frontend::analyze(std::move(*ast));
    require(checked.has_value(), std::format("sema ok for {}", filename));
}

void test_sema_intent_and_move_diagnostics() {
    std::println("=== RUN   test_sema_intent_and_move_diagnostics");

    require_sema_error(R"(package main
fn bump(mut x: i32) { x += 1 }
fn main() { mut val: i32 = 10; bump(val) }
)", "cfire/MUT-CALLSITE", "bad_mut.cf");

    require_sema_error(R"(package main
fn consume(move s: str) { println("{}", s) }
fn main() { msg := "secret"; consume(move msg); println("{}", msg) }
)", "cfire/USE-AFTER-MOVE", "use_after_move.cf");

    require_sema_error(R"(package main
type Shape enum { Circle(f64), Rect(f64, f64), Empty }
fn main() {
    s := Shape.Circle(1.0)
    match s {
        Circle(r) => { println("{}", r) }
        Rect(w, h) => { println("{}", w) }
    }
}
)", "cfire/NON-EXHAUSTIVE-MATCH", "non_exhaustive.cf");

    require_sema_error(R"(package main
type Counter struct { Val: i32 }
fn (mut c: Counter) inc() { c.Val += 1 }
fn main() { imm_c := Counter{Val: 0}; imm_c.inc() }
)", "cfire/MUT-RECEIVER", "mut_receiver.cf");

    require_sema_error(R"(package main
fn write_out(out target: i32) { target = 42 }
fn main() { mut v: i32 = 0; write_out(v) }
)", "cfire/OUT-CALLSITE", "out_callsite.cf");

    require_sema_error(R"(package main
fn mutate_val(mut target: i32) { target += 1 }
fn main() { imm_v: i32 = 10; mutate_val(mut imm_v) }
)", "cfire/IMMUTABLE-MUT-ARG", "imm_mut_arg.cf");

    require_sema_error(R"(package main
fn transfer(mut from: i32, mut to: i32) { from -= 10; to += 10 }
fn main() { mut acc: i32 = 100; transfer(mut acc, mut acc) }
)", "cfire/MUT-ALIAS", "mut_alias.cf");

    std::println("--- PASS: test_sema_intent_and_move_diagnostics");
}

struct ExampleCase {
    std::string name;
    std::string cf_path;
    std::string args;
    std::vector<std::string> expected_stdout;
    std::vector<std::string> expected_cpp;
};

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

void test_formatter_lsp_and_docgen(const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_formatter_lsp_and_docgen");

    // 1. Test `cfire fmt` (alignment of struct fields/tags, indentation, comment preservation, idempotency)
    std::string messy_cf = R"(package main
import "core:fs"
type Item struct {
Id: i64 `json:"id"`
Label: str `json:"label"`
}
fn bump(mut x: i32) {
// increment in-place
x += 1
}
)";
    auto fmt1 = cfire::fmt::format_source(messy_cf, "messy.cf");
    require(fmt1.has_value(), "cfire::fmt::format_source(messy.cf)");
    if (fmt1) {
        require(fmt1->contains("    Id:    i64 `json:\"id\"`") &&
                fmt1->contains("    Label: str `json:\"label\"`") &&
                fmt1->contains("    // increment in-place\n    x += 1"),
            std::format("expected aligned struct tags and 4-space indentation, got:\n{}", *fmt1));
        auto fmt2 = cfire::fmt::format_source(*fmt1, "messy.cf");
        require(fmt2.has_value() && *fmt2 == *fmt1, "expected cfire fmt to be idempotent");
    }

    // Regression test: struct with standalone comment (`// `Entries` values`) must never duplicate `type ... struct {`
    std::string struct_with_comment_cf = R"(package main

type AuditLog struct {
    // `Entries` values
    Entries: []str
}
)";
    auto fmt_comment = cfire::fmt::format_source(struct_with_comment_cf, "audit.cf");
    require(fmt_comment.has_value() && *fmt_comment == struct_with_comment_cf,
        std::format("expected struct with standalone comment to format identically without duplicating header, got:\n{}",
            fmt_comment.value_or("<error>")));

    // Test format_range (LSP-10): only selected lines are formatted with proper block indentation
    std::string range_cf =
        "package main\n\n"
        "fn calculate(a: i32) -> i32 {\n"
        "x := a * 2\n"
        "y := x + 10\n"
        "z := y / 2\n"
        "return z\n"
        "}\n";
    auto fmt_range_res = cfire::fmt::format_range(range_cf, 3, 5, "range.cf");
    require(fmt_range_res.has_value(), "cfire::fmt::format_range ok");
    std::string expected_range_str =
        "    x := a * 2\n"
        "    y := x + 10\n"
        "    z := y / 2";
    require(fmt_range_res.value() == expected_range_str,
        std::format("expected 3-line formatted range, got:\n{}", fmt_range_res.value_or("<error>")));

    // 2. Test `cfire lsp` JSON-RPC 2.0 server (initialize, didOpen diagnostics, hover, completion, signatureHelp, inlayHint, definition)
    auto rpc_frame = [](std::string_view body) {
        return std::format("Content-Length: {}\r\n\r\n{}", body.size(), body);
    };
    std::stringstream lsp_in;
    std::stringstream lsp_out;
    lsp_in << rpc_frame(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})");
    lsp_in << rpc_frame(R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp_test.cf","text":"package main\nimport \"core:fs\"\ntype Metric struct {\n    Service: str `json:\"service\"`\n    Count: i64\n}\nfn bump(mut x: i32) {\n    x += 1\n}\nfn main() {\n    m := Metric{Service: \"api\", Count: 1}\n    mut a: i32 = 5\n    bump(a)\n    _ := fs.read_all(m.Service)\n}\n"}}})");
    lsp_in << rpc_frame(R"({"jsonrpc":"2.0","id":2,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///tmp/lsp_test.cf"},"position":{"line":13,"character":14}}})");
    lsp_in << rpc_frame(R"({"jsonrpc":"2.0","id":3,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp_test.cf"},"position":{"line":13,"character":12}}})");
    lsp_in << rpc_frame(R"({"jsonrpc":"2.0","id":4,"method":"textDocument/definition","params":{"textDocument":{"uri":"file:///tmp/lsp_test.cf"},"position":{"line":12,"character":5}}})");
    lsp_in << rpc_frame(R"({"jsonrpc":"2.0","id":5,"method":"textDocument/documentSymbol","params":{"textDocument":{"uri":"file:///tmp/lsp_test.cf"}}})");
    lsp_in << rpc_frame(R"({"jsonrpc":"2.0","id":6,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///tmp/lsp_test.cf"},"position":{"line":13,"character":24}}})");
    lsp_in << rpc_frame(R"({"jsonrpc":"2.0","id":7,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp_test.cf"},"position":{"line":13,"character":23}}})");
    lsp_in << rpc_frame(R"({"jsonrpc":"2.0","id":8,"method":"textDocument/signatureHelp","params":{"textDocument":{"uri":"file:///tmp/lsp_test.cf"},"position":{"line":12,"character":10}}})");
    lsp_in << rpc_frame(R"({"jsonrpc":"2.0","id":9,"method":"textDocument/inlayHint","params":{"textDocument":{"uri":"file:///tmp/lsp_test.cf"}}})");
    lsp_in << rpc_frame(R"({"jsonrpc":"2.0","id":10,"method":"textDocument/codeAction","params":{"textDocument":{"uri":"file:///tmp/lsp_test.cf"},"range":{"start":{"line":12,"character":9},"end":{"line":12,"character":10}}}})");
    lsp_in << rpc_frame(R"({"jsonrpc":"2.0","id":11,"method":"textDocument/references","params":{"textDocument":{"uri":"file:///tmp/lsp_test.cf"},"position":{"line":12,"character":5}}})");
    lsp_in << rpc_frame(R"({"jsonrpc":"2.0","id":12,"method":"textDocument/prepareRename","params":{"textDocument":{"uri":"file:///tmp/lsp_test.cf"},"position":{"line":12,"character":5}}})");
    lsp_in << rpc_frame(R"({"jsonrpc":"2.0","id":13,"method":"textDocument/rename","params":{"textDocument":{"uri":"file:///tmp/lsp_test.cf"},"position":{"line":12,"character":5},"newName":"increment_in_place"}})");
    lsp_in << rpc_frame(R"({"jsonrpc":"2.0","id":14,"method":"workspace/symbol","params":{"query":"Metric"}})");
    // Cross-package LSP test on examples/08-multi-pkg-cc-build/main.cf (`checksum.compute_tag`)
    auto pkg_main_uri = "file://" + (rt_dir.parent_path() / "examples/08-multi-pkg-cc-build/main.cf").string();
    lsp_in << rpc_frame(std::format(
        R"({{"jsonrpc":"2.0","id":15,"method":"textDocument/hover","params":{{"textDocument":{{"uri":"{}"}},"position":{{"line":5,"character":24}}}}}})",
        pkg_main_uri));
    lsp_in << rpc_frame(std::format(
        R"({{"jsonrpc":"2.0","id":16,"method":"textDocument/inlayHint","params":{{"textDocument":{{"uri":"{}"}}}}}})",
        pkg_main_uri));
    lsp_in << rpc_frame(std::format(
        R"({{"jsonrpc":"2.0","id":17,"method":"textDocument/definition","params":{{"textDocument":{{"uri":"{}"}},"position":{{"line":5,"character":24}}}}}})",
        pkg_main_uri));
    // Trailing dot member completion regression test on examples/01-hello-core/main.cf (`audit.` -> Entries, `audit.Entries.` -> push/capacity)
    std::string hello_core_incomplete = "package main\n\ntype AuditLog struct {\n    Entries: []str\n}\n\nfn main() {\n    mut audit := AuditLog{}\n    audit.\n    audit.Entries.\n}\n";
    lsp_in << rpc_frame(std::format(
        R"({{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{{"textDocument":{{"uri":"file:///tmp/audit_test.cf","text":"{}"}}}}}})",
        cfire::lsp::detail::json_escape(hello_core_incomplete)));
    lsp_in << rpc_frame(R"({"jsonrpc":"2.0","id":18,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/audit_test.cf"},"position":{"line":8,"character":10}}})");
    lsp_in << rpc_frame(R"({"jsonrpc":"2.0","id":19,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/audit_test.cf"},"position":{"line":9,"character":18}}})");
    lsp_in << rpc_frame(R"({"jsonrpc":"2.0","id":20,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/audit_test.cf"},"position":{"line":5,"character":0}}})");
    lsp_in << rpc_frame(R"({"jsonrpc":"2.0","id":22,"method":"textDocument/typeDefinition","params":{"textDocument":{"uri":"file:///tmp/audit_test.cf"},"position":{"line":7,"character":10}}})");
    lsp_in << rpc_frame(R"({"jsonrpc":"2.0","id":23,"method":"textDocument/typeDefinition","params":{"textDocument":{"uri":"file:///tmp/lsp_test.cf"},"position":{"line":10,"character":4}}})");
    lsp_in << rpc_frame(R"({"jsonrpc":"2.0","id":24,"method":"textDocument/typeDefinition","params":{"textDocument":{"uri":"file:///tmp/lsp_test.cf"},"position":{"line":11,"character":8}}})");
    lsp_in << rpc_frame(R"({"jsonrpc":"2.0","id":26,"method":"textDocument/semanticTokens/full","params":{"textDocument":{"uri":"file:///tmp/audit_test.cf"}}})");
    // Cross-package references and rename tests (LSP-04)
    auto pkg_checksum_uri = "file://" + (rt_dir.parent_path() / "examples/08-multi-pkg-cc-build/pkg/checksum/checksum.cf").string();
    lsp_in << rpc_frame(std::format(
        R"({{"jsonrpc":"2.0","id":28,"method":"textDocument/references","params":{{"textDocument":{{"uri":"{}"}},"position":{{"line":4,"character":5}}}}}})",
        pkg_checksum_uri));
    lsp_in << rpc_frame(std::format(
        R"({{"jsonrpc":"2.0","id":29,"method":"textDocument/rename","params":{{"textDocument":{{"uri":"{}"}},"position":{{"line":4,"character":5}},"newName":"calc_tag"}}}})",
        pkg_checksum_uri));
    lsp_in << rpc_frame(std::format(
        R"({{"jsonrpc":"2.0","id":30,"method":"textDocument/rename","params":{{"textDocument":{{"uri":"{}"}},"position":{{"line":5,"character":4}},"newName":"my_tag"}}}})",
        pkg_main_uri));
    lsp_in << rpc_frame(std::format(
        R"({{"jsonrpc":"2.0","id":31,"method":"textDocument/prepareRename","params":{{"textDocument":{{"uri":"{}"}},"position":{{"line":4,"character":1}}}}}})",
        pkg_checksum_uri));
    // Auto-import stdlib completions (LSP-05)
    lsp_in << rpc_frame(R"({"jsonrpc":"2.0","id":32,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/audit_test.cf"},"position":{"line":7,"character":4}}})");
    lsp_in << rpc_frame(R"({"jsonrpc":"2.0","id":33,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp_test.cf"},"position":{"line":13,"character":4}}})");
    // Match arm generation code actions (LSP-06)
    std::string match_action_cf = "package main\n\ntype Shape enum {\n    Circle(f64),\n    Rect(f64, f64),\n    Empty,\n}\n\nfn test_match(s: Shape) {\n    match s {\n        Circle(r) => {\n            println(\"circle {}\", r)\n        }\n    }\n}\n";
    lsp_in << rpc_frame(std::format(
        R"({{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{{"textDocument":{{"uri":"file:///tmp/match_action_test.cf","text":"{}"}}}}}})",
        cfire::lsp::detail::json_escape(match_action_cf)));
    lsp_in << rpc_frame(R"({"jsonrpc":"2.0","id":34,"method":"textDocument/codeAction","params":{"textDocument":{"uri":"file:///tmp/match_action_test.cf"},"range":{"start":{"line":9,"character":4},"end":{"line":9,"character":4}}}})");
    // Struct layout and expression hover inspection (LSP-07)
    lsp_in << rpc_frame(R"({"jsonrpc":"2.0","id":35,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///tmp/audit_test.cf"},"position":{"line":2,"character":6}}})");
    lsp_in << rpc_frame(R"({"jsonrpc":"2.0","id":36,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///tmp/lsp_test.cf"},"position":{"line":2,"character":6}}})");
    std::string hover_expr_cf = "package main\n\nfn test_expr(a: i32, b: i32) -> i32 {\n    res := a + b\n    return res\n}\n";
    lsp_in << rpc_frame(std::format(
        R"({{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{{"textDocument":{{"uri":"file:///tmp/hover_expr_test.cf","text":"{}"}}}}}})",
        cfire::lsp::detail::json_escape(hover_expr_cf)));
    lsp_in << rpc_frame(R"({"jsonrpc":"2.0","id":37,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///tmp/hover_expr_test.cf"},"position":{"line":3,"character":13}}})");
    // Selection Range Formatting (LSP-10)
    std::string range_fmt_cf =
        "package main\n\n"
        "fn calculate(a: i32) -> i32 {\n"
        "x := a * 2\n"
        "y := x + 10\n"
        "z := y / 2\n"
        "return z\n"
        "}\n";
    lsp_in << rpc_frame(std::format(
        R"({{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{{"textDocument":{{"uri":"file:///tmp/range_fmt_test.cf","text":"{}"}}}}}})",
        cfire::lsp::detail::json_escape(range_fmt_cf)));
    lsp_in << rpc_frame(R"({"jsonrpc":"2.0","id":38,"method":"textDocument/rangeFormatting","params":{"textDocument":{"uri":"file:///tmp/range_fmt_test.cf"},"range":{"start":{"line":3,"character":0},"end":{"line":5,"character":10}},"options":{"tabSize":4,"insertSpaces":true}}})");
    lsp_in << rpc_frame(R"({"jsonrpc":"2.0","id":27,"method":"shutdown","params":null})");
    lsp_in << rpc_frame(R"({"jsonrpc":"2.0","method":"exit","params":null})");

    cfire::lsp::Server srv;
    srv.handle_stream(lsp_in, lsp_out);
    std::string rpc_resp = lsp_out.str();
    require(rpc_resp.contains("\"hoverProvider\":true") &&
            rpc_resp.contains("\"signatureHelpProvider\"") &&
            rpc_resp.contains("\"inlayHintProvider\":true") &&
            rpc_resp.contains("\"referencesProvider\":true") &&
            rpc_resp.contains("\"typeDefinitionProvider\":true") &&
            rpc_resp.contains("\"semanticTokensProvider\":") &&
            rpc_resp.contains("\"codeActionProvider\":true") &&
            rpc_resp.contains("\"documentRangeFormattingProvider\":true") &&
            rpc_resp.contains("\"workspaceSymbolProvider\":true"),
        "LSP initialize capabilities check");
    require(rpc_resp.contains("cfire/MUT-CALLSITE"),
        "LSP publishDiagnostics expected cfire/MUT-CALLSITE");
    require(rpc_resp.contains("cfire::fs::read_all"),
        "LSP hover on fs.read_all expected C++23 lowering");
    require(rpc_resp.contains("\"label\":\"write_all\""),
        "LSP completion on fs. expected write_all");
    require(rpc_resp.contains("\"line\":6"),
        "LSP go-to-definition on bump(a) expected line 6");
    require(rpc_resp.contains("Field `Metric.Service`") && rpc_resp.contains("std::string Service"),
        "LSP hover on m.Service expected struct field type and C++23 lowering");
    require(rpc_resp.contains("\"label\":\"Service\"") && rpc_resp.contains("\"label\":\"Count\""),
        "LSP completion on m. expected struct fields Service and Count");
    require(rpc_resp.contains("\"signatures\":[{\"label\":\"fn bump(mut x: i32)\""),
        "LSP signatureHelp inside bump(...) expected parameter signature");
    require(rpc_resp.contains("\"label\":\": Metric\""),
        "LSP inlayHint expected `: Metric` on inferred m := Metric{...}");
    require(rpc_resp.contains("\"label\":\"x:\"") && rpc_resp.contains("\"kind\":2"),
        "LSP inlayHint expected parameter hint `x:` with kind:2 on bump(a) (IDE-EXT-01)");
    require(rpc_resp.contains("\"label\":\"path:\""),
        "LSP inlayHint expected stdlib parameter hint `path:` on fs.read_all(m.Service) (IDE-EXT-01)");
    require(rpc_resp.contains("Insert 'mut ' at call site (cfire/MUT-CALLSITE)"),
        "LSP codeAction expected quickfix for cfire/MUT-CALLSITE");
    require(rpc_resp.contains("\"placeholder\":\"bump\""),
        "LSP prepareRename on bump expected placeholder");
    require(rpc_resp.contains("\"newText\":\"increment_in_place\""),
        "LSP rename on bump expected WorkspaceEdit with increment_in_place");
    require(rpc_resp.contains("\"containerName\":\"main\""),
        "LSP workspace/symbol expected Metric symbol in container main");
    require(rpc_resp.contains("checksum.compute_tag") && rpc_resp.contains("fn compute_tag(label: str) -> u32"),
        "LSP cross-package hover on checksum.compute_tag expected signature from pkg/checksum/checksum.cf");
    require(rpc_resp.contains("\"label\":\": u32\""),
        "LSP cross-package inlayHint expected `: u32` for `tag := checksum.compute_tag(...)`");
    require(rpc_resp.contains("pkg/checksum/checksum.cf"),
        "LSP cross-package definition on checksum.compute_tag expected pkg/checksum/checksum.cf URI");
    require(rpc_resp.contains("\"label\":\"Entries\"") && rpc_resp.contains("Field of AuditLog"),
        "LSP resilient completion on audit. expected field Entries");
    require(rpc_resp.contains("\"label\":\"push\"") && rpc_resp.contains("\"label\":\"capacity\""),
        "LSP chained completion on audit.Entries. expected vector methods push and capacity");
    require(rpc_resp.contains("\"label\":\"fn\"") &&
            rpc_resp.contains("\"insertTextFormat\":2") &&
            rpc_resp.contains("fn ${1:name}(${2:params}) -> ${3:void} {\\n\\t${0}\\n}") &&
            rpc_resp.contains("\"label\":\"type struct\"") &&
            rpc_resp.contains("type ${1:Name} struct {\\n\\t${0}\\n}") &&
            rpc_resp.contains("\"label\":\"match\"") &&
            rpc_resp.contains("\"label\":\"for\""),
        "LSP statement snippet completions check (LSP-01)");
    require(rpc_resp.contains(R"("id":22)") && rpc_resp.contains("audit_test.cf") && rpc_resp.contains(R"("line":2)"),
        "LSP typeDefinition on audit expected line 2 (type AuditLog struct)");
    require(rpc_resp.contains(R"("id":23)") && rpc_resp.contains("lsp_test.cf") && rpc_resp.contains(R"("line":2)"),
        "LSP typeDefinition on m expected line 2 (type Metric struct)");
    require(rpc_resp.contains(R"({"jsonrpc":"2.0","id":24,"result":null})"),
        "LSP typeDefinition on primitive a expected null result");
    require(rpc_resp.contains(R"("id":26)") && rpc_resp.contains(R"("data":[)") && !rpc_resp.contains(R"("data":[])"),
        "LSP semanticTokens/full expected non-empty delta array (LSP-03)");
    require(rpc_resp.contains(R"("id":28)") &&
            rpc_resp.contains("main.cf") &&
            rpc_resp.contains("checksum.cf"),
        "LSP cross-package references on compute_tag expected locations in both main.cf and checksum.cf (LSP-04)");
    require(rpc_resp.contains(R"("id":29)") &&
            rpc_resp.contains("checksum.cf") &&
            rpc_resp.contains("main.cf") &&
            rpc_resp.contains(R"("newText":"calc_tag")"),
        "LSP cross-package rename on compute_tag expected WorkspaceEdit for both checksum.cf and main.cf (LSP-04)");
    require(rpc_resp.contains(R"("id":30)") &&
            rpc_resp.contains("main.cf") &&
            rpc_resp.contains(R"("newText":"my_tag")"),
        "LSP local variable rename on tag expected WorkspaceEdit strictly for main.cf (LSP-04)");
    require(rpc_resp.contains(R"({"jsonrpc":"2.0","id":31,"result":null})"),
        "LSP prepareRename on keyword fn expected null result (LSP-04)");
    require(rpc_resp.contains(R"("id":32)") &&
            rpc_resp.contains(R"("label":"read_all")") &&
            rpc_resp.contains(R"("insertText":"fs.read_all")") &&
            rpc_resp.contains("core:fs (auto-import)") &&
            rpc_resp.contains(R"("additionalTextEdits":[)") &&
            rpc_resp.contains("import \\\"core:fs\\\""),
        "LSP auto-import completion for unimported stdlib module expected additionalTextEdits (LSP-05)");
    require(rpc_resp.contains(R"("id":33)") &&
            rpc_resp.contains(R"("label":"read_all")") &&
            rpc_resp.contains(R"("insertText":"fs.read_all")") &&
            rpc_resp.contains("fs: fn read_all"),
        "LSP completion for already imported stdlib module expected no auto-import edit (LSP-05)");
    require(rpc_resp.contains(R"("id":34)") &&
            rpc_resp.contains("Fill missing match arms for 'Shape' (cfire/NON-EXHAUSTIVE-MATCH)") &&
            rpc_resp.contains("Rect(v0, v1) => {") &&
            rpc_resp.contains("Empty => {") &&
            rpc_resp.contains(R"("isPreferred":true)") &&
            rpc_resp.contains("Add wildcard pattern '_ => { ... }' (cfire/NON-EXHAUSTIVE-MATCH)") &&
            rpc_resp.contains(R"("isPreferred":false)"),
        "LSP exhaustive match arm and wildcard code actions (LSP-06)");
    require(rpc_resp.contains(R"("id":35)") &&
            rpc_resp.contains("Size: 24 bytes | Align: 8 bytes") &&
            rpc_resp.contains("+0 (24B)"),
        "LSP struct memory layout inspection for AuditLog (LSP-07)");
    require(rpc_resp.contains(R"("id":36)") &&
            rpc_resp.contains("Size: 32 bytes | Align: 8 bytes") &&
            rpc_resp.contains("+0 (24B)") &&
            rpc_resp.contains("+24 (8B)"),
        "LSP struct memory layout inspection for Metric (LSP-07)");
    require(rpc_resp.contains(R"("id":6)") &&
            rpc_resp.contains("Offset: +0 bytes | Size: 24 bytes"),
        "LSP struct field layout inspection for m.Service (LSP-07)");
    require(rpc_resp.contains(R"("id":37)") &&
            rpc_resp.contains("### Expression `a + b`") &&
            rpc_resp.contains("a + b: i32") &&
            rpc_resp.contains("std::int32_t"),
        "LSP sub-expression type hover on operator (LSP-07)");
    require(rpc_resp.contains("\"id\":38") &&
            rpc_resp.contains("\"start\":{\"line\":3,\"character\":0}") &&
            rpc_resp.contains("\"end\":{\"line\":5,\"character\":10}") &&
            rpc_resp.contains("    x := a * 2\\n    y := x + 10\\n    z := y / 2"),
        "LSP selection range formatting on 3-line block (LSP-10)");

    // Debounced background diagnostics worker thread verification (LSP-08)
    {
        std::stringstream debounce_in;
        std::stringstream debounce_out;
        cfire::lsp::Server d_srv(60); // 60ms debounce window

        std::string init_cf = "package main\n\nfn valid_func() -> i32 {\n    return 42\n}\n";
        debounce_in << rpc_frame(std::format(
            R"({{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{{"textDocument":{{"uri":"file:///tmp/debounce_test.cf","text":"{}"}}}}}})",
            cfire::lsp::detail::json_escape(init_cf)));

        // Rapid typing bursts: 3 intermediate syntax errors in quick succession (< 20ms)
        for (int i = 1; i <= 3; ++i) {
            std::string burst_cf = std::format("package main\n\nfn valid_func() -> i32 {{\n    return error_syntax_{}\n", i);
            debounce_in << rpc_frame(std::format(
                R"({{"jsonrpc":"2.0","method":"textDocument/didChange","params":{{"textDocument":{{"uri":"file:///tmp/debounce_test.cf"}},"text":"{}"}}}})",
                cfire::lsp::detail::json_escape(burst_cf)));
        }

        // Final typed code that is completely valid:
        std::string final_cf = "package main\n\nfn valid_func() -> i32 {\n    return 100\n}\n";
        debounce_in << rpc_frame(std::format(
            R"({{"jsonrpc":"2.0","method":"textDocument/didChange","params":{{"textDocument":{{"uri":"file:///tmp/debounce_test.cf"}},"text":"{}"}}}})",
            cfire::lsp::detail::json_escape(final_cf)));

        // Immediate hover request reflecting the final code without waiting for diagnostics
        debounce_in << rpc_frame(R"({"jsonrpc":"2.0","id":100,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///tmp/debounce_test.cf"},"position":{"line":2,"character":5}}})");
        debounce_in << rpc_frame(R"({"jsonrpc":"2.0","id":101,"method":"shutdown","params":null})");
        debounce_in << rpc_frame(R"({"jsonrpc":"2.0","method":"exit","params":null})");

        d_srv.handle_stream(debounce_in, debounce_out);
        std::string d_resp = debounce_out.str();

        require(d_resp.contains(R"("id":100)") && d_resp.contains("fn valid_func() -> i32"),
            "LSP debounce: hover response processed immediately during typing burst (LSP-08)");
        require(d_resp.contains("textDocument/publishDiagnostics"),
            "LSP debounce: diagnostics published after debounce drainage (LSP-08)");
        require(!d_resp.contains("error_syntax_1") && !d_resp.contains("error_syntax_2"),
            "LSP debounce: intermediate typing states debounced and coalesced (LSP-08)");
    }

    // Resilient statement-level AST recovery in parser & LSP (LSP-09)
    {
        // 1. Direct parser recovery unit check
        std::string broken_src =
            "package main\n\n"
            "fn broken_first(a: i32) -> i32 {\n"
            "    res := a + ;\n"
            "    return res\n"
            "}\n\n"
            "type HealthyStruct struct {\n"
            "    Tag: str\n"
            "}\n\n"
            "fn intact_second(s: HealthyStruct) -> str {\n"
            "    return s.Tag\n"
            "}\n";

        auto pres = cfire::frontend::parse_source_resilient("resilient_test.cf", broken_src);
        require(!pres.ok(), "Parser recovery expected errors list non-empty on syntax error (LSP-09)");
        require(!pres.errors.empty(), "Parser recovery expected recorded syntax error (LSP-09)");
        require(pres.ast.functions.size() >= 1, "Parser recovery expected intact_second in recovered AST (LSP-09)");
        bool found_intact = false;
        for (const auto& fn : pres.ast.functions) {
            if (fn.name == "intact_second") found_intact = true;
        }
        require(found_intact, "Parser recovery found intact_second function despite earlier syntax error (LSP-09)");
        require(!pres.ast.structs.empty() && pres.ast.structs[0].name == "HealthyStruct",
            "Parser recovery found HealthyStruct in recovered AST (LSP-09)");

        // 2. LSP protocol integration check with document symbols and hover
        std::stringstream r_in;
        std::stringstream r_out;
        cfire::lsp::Server r_srv(10);

        r_in << rpc_frame(std::format(
            R"({{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{{"textDocument":{{"uri":"file:///tmp/resilient_test.cf","text":"{}"}}}}}})",
            cfire::lsp::detail::json_escape(broken_src)));
        r_in << rpc_frame(R"({"jsonrpc":"2.0","id":200,"method":"textDocument/documentSymbol","params":{"textDocument":{"uri":"file:///tmp/resilient_test.cf"}}})");
        r_in << rpc_frame(R"({"jsonrpc":"2.0","id":201,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///tmp/resilient_test.cf"},"position":{"line":10,"character":5}}})");
        r_in << rpc_frame(R"({"jsonrpc":"2.0","id":202,"method":"shutdown","params":null})");
        r_in << rpc_frame(R"({"jsonrpc":"2.0","method":"exit","params":null})");

        r_srv.handle_stream(r_in, r_out);
        std::string r_resp = r_out.str();

        require(r_resp.contains("cfire/SYNTAX"), "LSP publishDiagnostics emitted syntax error squiggly (LSP-09)");
        require(r_resp.contains(R"("id":200)") && r_resp.contains("\"name\":\"intact_second\"") && r_resp.contains("\"name\":\"HealthyStruct\""),
            "LSP documentSymbol successfully outlined intact_second and HealthyStruct despite syntax error (LSP-09)");
        require(r_resp.contains(R"("id":201)") && r_resp.contains("fn intact_second(s: HealthyStruct) -> str"),
            "LSP hover on intact_second succeeded despite earlier syntax error (LSP-09)");
    }

    auto ide_res = cfire::lsp::configure_ide(rt_dir.parent_path(), "zed");
    require(ide_res.has_value(), "cfire::lsp::configure_ide(zed)");

    // 3. Test `cfire doc` always-up-to-date documentation generation & verification
    auto docs_dir = rt_dir.parent_path() / "docs";
    auto gen_docs = cfire::doc::sync_docs_directory(docs_dir, false);
    require(gen_docs.has_value(), "cfire::doc::sync_docs_directory write");
    auto check_docs = cfire::doc::sync_docs_directory(docs_dir, true);
    require(check_docs.has_value(), "cfire::doc::sync_docs_directory --check verification");

    auto pkg_md = cfire::doc::extract_file_markdown_doc("examples/09-stdlib-test-pointers/main.cf");
    require(pkg_md.has_value() && pkg_md->contains("compute_fixed_sum") && pkg_md->contains("type Metric struct"),
        "cfire::doc::extract_file_markdown_doc check");

    // 4. Test Core Library additions and catalog alignment (LIB-12, BS-01)
    {
        require(cfire::str::contains("cfire language", "fire"), "cfire::str::contains check (LIB-12)");
        require(!cfire::str::contains("cfire language", "rust"), "cfire::str::contains false check (LIB-12)");
        require(cfire::str::replace("hello world", "world", "cfire") == "hello cfire", "cfire::str::replace check (LIB-12)");
        require(cfire::math::clamp_f64(1.5, 0.0, 1.0) == 1.0, "cfire::math::clamp_f64 check (LIB-12)");
        require(cfire::math::sqrt(16.0) == 4.0, "cfire::math::sqrt check (LIB-12)");
        auto r_u64 = cfire::rand::u64();
        auto r_unit = cfire::rand::f64_unit();
        require(r_unit >= 0.0 && r_unit <= 1.0, "cfire::rand::f64_unit check (LIB-12)");
        (void)r_u64;

        auto test_tmp_file = std::filesystem::temp_directory_path() / "cfire_io_test.bin";
        std::vector<std::byte> payload = {std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE}, std::byte{0xEF}};
        auto w_res = cfire::fs::write_all(test_tmp_file, payload);
        require(w_res.has_value(), "cfire::fs::write_all check (LIB-12)");
        auto r_res = cfire::fs::read_all(test_tmp_file);
        require(r_res.has_value() && *r_res == payload, "cfire::fs::read_all check (LIB-12)");
        std::filesystem::remove(test_tmp_file);

        auto exec_res = cfire::os::exec_output("echo 'cfire_test'");
        require(exec_res.has_value() && exec_res->contains("cfire_test"), "cfire::os::exec_output check (LIB-12)");

        // Verify Guide Chapter 8 declarative build.cf content
        auto guide_path = docs_dir / "GUIDE.md";
        auto guide_txt = cfire::build::read_file_text(guide_path);
        require(guide_txt.has_value() &&
                guide_txt->contains("FastHash := build.CC(") &&
                guide_txt->contains("KVStore := build.CMake("),
            "docs/GUIDE.md Chapter 8 declarative build.cf reconciliation (BS-01)");
    }

    std::println("--- PASS: test_formatter_lsp_and_docgen");
}

void test_p1_features(const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_p1_features (BS-02, LIB-03, LD-11)");

    // 1. LIB-03: Cryptographic Suite & True CSPRNG
    {
        // Standard NIST FIPS 180-4 SHA-256 test vectors:
        auto h_empty = cfire::sha256::hash_hex("");
        require(h_empty == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
            "sha256 empty string test vector");

        auto h_abc = cfire::sha256::hash_hex("abc");
        require(h_abc == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            "sha256 'abc' NIST test vector");

        auto b_abc = cfire::sha256::hash_bytes(std::span<const std::byte>(reinterpret_cast<const std::byte*>("abc"), 3));
        require(b_abc.size() == 32 && static_cast<unsigned char>(b_abc[0]) == 0xba,
            "sha256 hash_bytes vector");

        // blake3 compatibility alias
        require(cfire::blake3::hash_hex("abc") == h_abc,
            "blake3 backward-compat alias matching sha256");

        // RFC 4231 HMAC-SHA256 test vector
        auto hmac_res = cfire::hmac::sha256_hex("key", "The quick brown fox jumps over the lazy dog");
        require(hmac_res == "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8",
            "RFC 4231 HMAC-SHA256 test vector");

        // CSPRNG bytes from OS entropy
        auto rand_b = cfire::rand::bytes(32);
        require(rand_b.size() == 32, "rand.bytes length check");
        bool all_zero = true;
        for (auto b : rand_b) {
            if (static_cast<unsigned char>(b) != 0) { all_zero = false; break; }
        }
        require(!all_zero, "rand.bytes entropy check");
    }

    // 2. LD-11: Composable Expression-Level `try` Lowering
    {
        std::string_view src = R"(package main

fn fail_or_num(fail: bool) -> i32! {
    if fail {
        return error("failure triggered")
    }
    return 25
}

fn compute(fail: bool) -> i32! {
    val := 100 + try fail_or_num(fail)
    return val
}

fn main() {
    ok_val := try compute(false)
    assert(ok_val == 125, "expression try success unwrapping")
}
)";
        auto ast = cfire::frontend::parse_source("test_expr_try.cf", src);
        require(ast.has_value(), ast.has_value() ? "parse test_expr_try.cf" : ast.error());
        if (!ast.has_value()) return;
        auto checked = cfire::frontend::analyze(std::move(*ast));
        require(checked.has_value(), checked.has_value() ? "analyze test_expr_try.cf" : checked.error());
        if (!checked.has_value()) return;
        std::string cpp = cfire::frontend::emit_cpp(*checked);
        require(cpp.contains("cfire::unwrap_val"), "codegen contains statement-expression cfire::unwrap_val");
        require(cpp.contains("std::unexpected"), "codegen contains early return std::unexpected");

        // Compile and execute the generated C++ to verify end-to-end correctness
        auto res = compile_and_run_cpp(cpp, rt_dir, "cfire_expr_try");
        res.assert_ok("expr-try C++ execution");
    }

    // 3. BS-02: Incremental build caching for build.CC
    {
        ScopedTempDir test_dir("cfire_bs02");
        auto c_file = test_dir.subpath("src") / "fast_calc.c";
        std::ofstream(c_file) << "int fast_add(int a, int b) { return a + b; }\n";

        cfire::build::BuildPlan plan;
        cfire::build::ExternalDepSpec dep;
        dep.name = "fastcalc";
        dep.driver = "CC";
        dep.source = test_dir.path.string();

        auto res1 = cfire::build::build_local_dep(test_dir.path, dep, plan);
        require(res1.has_value(), "first build_local_dep");
        require(std::filesystem::exists(dep.built_archive), "built archive exists");

        // Verify object file stem was disambiguated with relative path hash
        auto obj_dir = test_dir.path / ".cfire" / "cache" / "objs" / "fastcalc";
        bool found_disambiguated_obj = false;
        for (const auto& entry : std::filesystem::directory_iterator(obj_dir)) {
            if (entry.path().extension() == ".o" && entry.path().stem().string().starts_with("fast_calc_")) {
                found_disambiguated_obj = true;
                break;
            }
        }
        require(found_disambiguated_obj, "object file disambiguated with relative path hash (BS-02)");

        // Record archive write time
        auto ar_time1 = std::filesystem::last_write_time(dep.built_archive);

        // Run second build immediately without modifying source
        cfire::build::BuildPlan plan2;
        auto res2 = cfire::build::build_local_dep(test_dir.path, dep, plan2);
        require(res2.has_value(), "second build_local_dep");
        auto ar_time2 = std::filesystem::last_write_time(dep.built_archive);
        require(ar_time1 == ar_time2, "unmodified build.CC dependency skips re-compilation and re-archiving (BS-02)");
    }

    // 4. LD-12: Enforce Divergence in `or err { ... }` Block on Value Binding
    {
        require_sema_error(R"(package main
fn fallible() -> i32! { return 42 }
fn main() {
    val := fallible() or err {
        println("logging error: {}", err)
    }
    println("val is: {}", val)
}
)", "cfire/NON-DIVERGING-OR-BLOCK", "test_bad_or_diverge.cf");

        require_sema_ok(R"(package main
fn fallible() -> i32! { return 42 }
fn run() -> i32 {
    val := fallible() or err { return -1 }
    return val
}
fn main() {
    res := run()
    assert(res == 42, "diverging or block valid")
}
)", "test_good_or_diverge.cf");
    }

    // 5. LIB-02: Safe Subprocess Spawning (`cfire::os::command`)
    {
        // Arguments with spaces must be preserved without shell escaping issues
        auto res = cfire::os::command("echo", {"hello world", "arg with spaces"});
        require(res.has_value(), "cfire::os::command echo execution");
        if (res.has_value()) {
            require(res->exit_code == 0, "echo exit code 0");
            require(res->stdout_str.contains("hello world arg with spaces"),
                "stdout contains literal argument contents without shell splitting");
        }

        // Spawning nonexistent executable returns error
        auto bad_res = cfire::os::command("/bin/nonexistent_command_xyz", {"foo"});
        require(!bad_res.has_value(), "nonexistent command returns unexpected");
    }

    // 6. BS-03: Namespaced C++ AST Ingestion and Overload Disambiguation
    {
        std::string_view ast_json = R"json({
  "kind": "TranslationUnitDecl",
  "inner": [
    {
      "kind": "NamespaceDecl",
      "name": "Math",
      "inner": [
        {
          "kind": "FunctionDecl",
          "name": "add",
          "type": { "qualType": "int (int, int)" }
        },
        {
          "kind": "FunctionDecl",
          "name": "add",
          "type": { "qualType": "double (double, double)" }
        }
      ]
    }
  ]
})json";
        std::unordered_map<std::string, cfire::bridge::CppSymbol> syms;
        cfire::bridge::extract_clang_json_symbols(ast_json, syms);

        require(syms.contains("Math::add"), "ClangAstParser extracts namespaced symbol Math::add");
        if (syms.contains("Math::add")) {
            const auto& sym = syms["Math::add"];
            require(sym.name == "add", "symbol name is add");
            require(sym.return_type == "int", "primary overload return type is int");
            require(sym.param_types.size() == 2, "primary overload has 2 params");
            require(sym.overloads.size() == 1, "Math::add captured secondary overload (BS-03)");
            if (!sym.overloads.empty()) {
                require(sym.overloads[0].return_type == "double", "secondary overload return type is double");
            }
        }

        // Verify cache serialization and deserialization preserves overloads
        cfire::bridge::HeaderIndex idx;
        idx.header = "<test_math.h>";
        idx.symbols = syms;
        auto cache_tmp = std::filesystem::temp_directory_path() / "cfire_bs03_test.idx";
        cfire::bridge::save_cached_index(cache_tmp, idx);

        auto loaded = cfire::bridge::load_cached_index(cache_tmp, "<test_math.h>");
        require(loaded.has_value(), "load_cached_index loaded saved index");
        if (loaded.has_value()) {
            require(loaded->symbols.contains("Math::add"), "loaded cache contains Math::add");
            if (loaded->symbols.contains("Math::add")) {
                const auto& loaded_sym = loaded->symbols.at("Math::add");
                require(loaded_sym.overloads.size() == 1, "loaded cache preserves overloads (BS-03)");
            }
        }
        std::filesystem::remove(cache_tmp);
    }

    // 7. LD-14: Parameter Intent `forward` Removal
    {
        std::string_view bad_src = R"(package main
fn compute(forward x: i32) -> i32 {
    return x + 1
}
fn main() {}
)";
        auto bad_ast = cfire::frontend::parse_source("test_bad_forward.cf", bad_src);
        require(!bad_ast.has_value(), "expected parser to reject 'forward' parameter intent (LD-14)");
    }

    // 8. LD-08: Standardized Escape Hatch to `cpp { ... }`
    {
        std::string_view src = R"(package main
fn main() {
    cpp {
        int val = 42;
        std::println("cpp escape hatch: {}", val);
    }
}
)";
        auto ast = cfire::frontend::parse_source("test_cpp_escape.cf", src);
        require(ast.has_value(), "parse test_cpp_escape.cf with cpp { ... } (LD-08)");
        if (ast.has_value()) {
            auto checked = cfire::frontend::analyze(std::move(*ast));
            require(checked.has_value(), "analyze test_cpp_escape.cf");
            if (checked.has_value()) {
                std::string cpp_code = cfire::frontend::emit_cpp(*checked);
                require(cpp_code.contains("int val = 42;"), "codegen preserves cpp block verbatim (LD-08)");
            }
        }
    }

    // 9. LIB-01: High-Level HTTP Client, Server & URL Parsing
    {
        // A. URL parsing
        auto u_res = cfire::url::parse("http://127.0.0.1:8080/api/items?category=shoes&limit=10#details");
        require(u_res.has_value(), "url.parse valid RFC 3986 URL");
        if (u_res.has_value()) {
            require(u_res->scheme == "http", "url scheme is http");
            require(u_res->host == "127.0.0.1", "url host is 127.0.0.1");
            require(u_res->port == 8080, "url port is 8080");
            require(u_res->path == "/api/items", "url path is /api/items");
            require(u_res->query == "category=shoes&limit=10", "url query parsed");
            require(u_res->fragment == "details", "url fragment is details");
            require(u_res->query_params.size() == 2, "query_params has 2 entries");
            require(u_res->query_params["category"] == "shoes", "query_param category is shoes");
            require(u_res->query_params["limit"] == "10", "query_param limit is 10");
        }

        // B. URL encode / decode
        std::string original = "hello world & foo=bar/baz+100%";
        std::string enc = cfire::url::encode(original);
        require(!enc.contains(" "), "encoded URL does not contain spaces");
        std::string dec = cfire::url::decode(enc);
        require(dec == original, "url.decode roundtrip matches original");

        // C. HTTP Server and Client roundtrip over loopback
        auto srv_res = cfire::http::make_server(0);
        require(srv_res.has_value(), "cfire::http::make_server creation");
        if (srv_res.has_value()) {
            auto port = srv_res->port();
            std::thread srv_th([&]() {
                srv_res->serve_once([](const cfire::http::Request& req) -> cfire::http::Response {
                    cfire::http::Response resp;
                    resp.status_code = 200;
                    resp.status_text = "OK";
                    resp.headers["content-type"] = "application/json";
                    resp.body = std::format("{{\"method\":\"{}\",\"path\":\"{}\"}}", req.method, req.path);
                    return resp;
                });
            });

            std::string target_url = std::format("http://127.0.0.1:{}/api/test", port);
            auto resp_res = cfire::http::get(target_url);
            require(resp_res.has_value(), "cfire::http::get loopback request");
            if (resp_res.has_value()) {
                require(resp_res->status_code == 200, "http response status code 200");
                require(resp_res->is_ok(), "http response is_ok() true");
                require(resp_res->body.contains("\"method\":\"GET\""), "response body reflects GET method");
                require(resp_res->body.contains("\"path\":\"/api/test\""), "response body reflects path");
            }
            if (srv_th.joinable()) {
                srv_th.join();
            }
        }
    }

    // 10. LD-01: Recursive Monadic Type Orthogonality (T! and T? in Containers & Struct Fields)
    {
        std::string_view src = R"(package main

type Item struct {
    Name: str?
    Score: i32!
    Tags: []str?
}

fn main() {
    println("monadic types struct check")
}
)";
        auto ast = cfire::frontend::parse_source("test_monadic_types.cf", src);
        require(ast.has_value(), "parse test_monadic_types.cf (LD-01)");
        if (ast.has_value()) {
            auto checked = cfire::frontend::analyze(std::move(*ast));
            require(checked.has_value(), "analyze test_monadic_types.cf");
            if (checked.has_value()) {
                std::string cpp = cfire::frontend::emit_cpp(*checked);
                require(cpp.contains("std::optional<std::string> Name"),
                    "struct field 'Name: str?' lowers to std::optional<std::string> (LD-01)");
                require(cpp.contains("std::expected<std::int32_t, cfire::Error> Score"),
                    "struct field 'Score: i32!' lowers to std::expected<std::int32_t, cfire::Error> (LD-01)");
                require(cpp.contains("std::vector<std::optional<std::string>> Tags"),
                    "struct field 'Tags: []str?' lowers to std::vector<std::optional<std::string>> (LD-01)");

                // End-to-end compile with clang++ to verify C++23 validity
                auto res = compile_and_run_cpp(cpp, rt_dir, "cfire_monadic");
                res.assert_ok("monadic C++ execution");
            }
        }
    }

    // -------------------------------------------------------------
    // LD-03: Structured Concurrency Integrity & Destruction Ordering in async.scope
    // -------------------------------------------------------------
    {
        std::string_view src = R"(package main

fn set_val(v: *mut i32, n: i32) {
    *v = n
}

fn test_scope_defer() -> i32 {
    mut val := 10
    ptr := &mut val
    async.scope(workers: 2) |scope| {
        defer set_val(ptr, 99)
        scope.spawn || {
            *ptr = 42
        }
    }
    return val
}

fn main() {
    v := test_scope_defer()
    println("final val: {}", v)
}
)";
        auto ast = cfire::frontend::parse_source("test_scope_raii.cf", src);
        require(ast.has_value(), "parse test_scope_raii.cf");
        if (ast.has_value()) {
            auto checked = cfire::frontend::analyze(std::move(*ast));
            require(checked.has_value(), "analyze test_scope_raii.cf");
            if (checked.has_value()) {
                std::string cpp = cfire::frontend::emit_cpp(*checked);
                require(cpp.contains("cfire::async::Scope scope(2);"),
                    "async.scope lowers to RAII Scope declaration (LD-03)");
                require(cpp.contains("scope.defer([&] {"),
                    "defer inside async.scope routes to scope.defer(...) (LD-03)");
                require(cpp.contains("scope.join_all();"),
                    "async.scope block explicitly joins on exit (LD-03)");

                auto res = compile_and_run_cpp(cpp, rt_dir, "cfire_scope");
                res.assert_ok("async.scope binary execution");
                res.assert_contains("final val: 99", "defer in async.scope executed strictly after worker thread task completed (LD-03)");
            }
        }
    }

    // -------------------------------------------------------------
    // LD-04: Explicit Borrow References (&str, &[]T) vs Owned Parameters
    // -------------------------------------------------------------
    {
        std::string_view src = R"(package main

fn inspect_borrows(borrow_str: &str, owned_str: str, borrow_slice: &[]i32, owned_slice: []i32, mut_slice: &mut []i32) {
}

fn main() {
}
)";
        auto ast = cfire::frontend::parse_source("test_borrows.cf", src);
        require(ast.has_value(), "parse test_borrows.cf (LD-04)");
        if (ast.has_value()) {
            auto checked = cfire::frontend::analyze(std::move(*ast));
            require(checked.has_value(), "analyze test_borrows.cf");
            if (checked.has_value()) {
                std::string cpp = cfire::frontend::emit_cpp(*checked);
                require(cpp.contains("std::string_view borrow_str"),
                    "&str parameter lowers to std::string_view (LD-04)");
                require(cpp.contains("const std::string& owned_str"),
                    "owned str parameter lowers to const std::string& (LD-04)");
                require(cpp.contains("std::span<const std::int32_t> borrow_slice"),
                    "&[]i32 parameter lowers to std::span<const std::int32_t> (LD-04)");
                require(cpp.contains("const std::vector<std::int32_t>& owned_slice"),
                    "owned []i32 parameter lowers to const std::vector<std::int32_t>& (LD-04)");
                require(cpp.contains("std::span<std::int32_t> mut_slice"),
                    "&mut []i32 parameter lowers to std::span<std::int32_t> (LD-04)");
            }
        }
    }

    // -------------------------------------------------------------
    // LD-05: CFG Move Analysis (Re-assignment, Loops, and Branches)
    // -------------------------------------------------------------
    {
        // 1. Re-assignment after move is allowed
        std::string_view src_reassign = R"(package main

fn consume(move s: str) {}

fn test_reassign() {
    mut s := "hello"
    consume(move s)
    s = "world"
    consume(move s)
}
)";
        auto ast1 = cfire::frontend::parse_source("test_reassign.cf", src_reassign);
        require(ast1.has_value(), "parse test_reassign.cf");
        if (ast1.has_value()) {
            auto checked = cfire::frontend::analyze(std::move(*ast1));
            require(checked.has_value(), "reassignment after move is permitted (LD-05)");
        }

        // 2. Move in loop body without reassignment is rejected
        std::string_view src_loop_move = R"(package main

fn consume(move s: str) {}

fn test_loop_move() {
    s := "loop_item"
    for i in [1, 2, 3] {
        consume(move s)
    }
}
)";
        auto ast2 = cfire::frontend::parse_source("test_loop_move.cf", src_loop_move);
        require(ast2.has_value(), "parse test_loop_move.cf");
        if (ast2.has_value()) {
            auto checked = cfire::frontend::analyze(std::move(*ast2));
            require(!checked.has_value(), "move in loop body without reassignment must fail");
            if (!checked.has_value()) {
                require(checked.error().contains("cfire/USE-AFTER-MOVE"),
                    "loop move error reports cfire/USE-AFTER-MOVE (LD-05)");
            }
        }

        // 3. Move in loop body WITH reassignment is permitted
        std::string_view src_loop_reassign = R"(package main

fn consume(move s: str) {}

fn test_loop_reassign() {
    mut s := "initial"
    for i in [1, 2, 3] {
        consume(move s)
        s = "reset"
    }
}
)";
        auto ast3 = cfire::frontend::parse_source("test_loop_reassign.cf", src_loop_reassign);
        require(ast3.has_value(), "parse test_loop_reassign.cf");
        if (ast3.has_value()) {
            auto checked = cfire::frontend::analyze(std::move(*ast3));
            require(checked.has_value(), "move in loop body with reassignment is permitted (LD-05)");
        }

        // 4. Branch move: move in only one if branch does NOT mark variable moved after if
        std::string_view src_branch_single = R"(package main

fn consume(move s: str) {}

fn test_branch(cond: bool) {
    s := "branch"
    if cond {
        consume(move s)
    }
    // s is not definitely moved because else path was not taken
}
)";
        auto ast4 = cfire::frontend::parse_source("test_branch.cf", src_branch_single);
        require(ast4.has_value(), "parse test_branch.cf");
        if (ast4.has_value()) {
            auto checked = cfire::frontend::analyze(std::move(*ast4));
            require(checked.has_value(), "move in only one if branch does not mark variable moved after if (LD-05)");
        }

        // 5. Branch move: move in BOTH branches marks variable moved after if
        std::string_view src_branch_both = R"(package main

fn consume(move s: str) {}

fn test_branch_both(cond: bool) {
    s := "branch"
    if cond {
        consume(move s)
    } else {
        consume(move s)
    }
    consume(move s) // Error: definitely moved along both branches
}
)";
        auto ast5 = cfire::frontend::parse_source("test_branch_both.cf", src_branch_both);
        require(ast5.has_value(), "parse test_branch_both.cf");
        if (ast5.has_value()) {
            auto checked = cfire::frontend::analyze(std::move(*ast5));
            require(!checked.has_value(), "use after move when moved along both branches must fail");
            if (!checked.has_value()) {
                require(checked.error().contains("cfire/USE-AFTER-MOVE"),
                    "move along all divergent branches triggers cfire/USE-AFTER-MOVE (LD-05)");
            }
        }
    }

    std::println("--- PASS: test_p1_features (BS-02, LIB-03, LD-11, LD-01, LD-03, LD-04, LD-05)");
}

void test_p2_features(const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_p2_features (LIB-04, BS-04, LD-02, LD-06)");

    // -------------------------------------------------------------
    // LIB-04: Standard Data Encodings (base64, hex, csv)
    // -------------------------------------------------------------
    {
        // 1. Base64
        std::string plain = "hello, cfire!";
        std::string b64 = cfire::base64::encode_str(plain);
        auto dec = cfire::base64::decode_str(b64);
        require(dec.has_value() && *dec == plain, "base64 encode/decode roundtrip (LIB-04)");

        // Base64 URL-safe test
        std::string url_plain = "https://example.com/api?q=hello&id=123";
        std::string url_b64 = cfire::base64::encode_str(url_plain, true);
        require(!url_b64.contains('+') && !url_b64.contains('/'), "url_safe base64 contains no + or / (LIB-04)");
        auto url_dec = cfire::base64::decode_str(url_b64);
        require(url_dec.has_value() && *url_dec == url_plain, "url_safe base64 decode roundtrip (LIB-04)");

        // 2. Hex
        std::string hex_str = cfire::hex::encode_str("cfire");
        require(hex_str == "6366697265", "hex encoding of 'cfire' (LIB-04)");
        auto hex_dec = cfire::hex::decode_str(hex_str);
        require(hex_dec.has_value() && *hex_dec == "cfire", "hex decoding roundtrip (LIB-04)");

        // 3. CSV RFC 4180 parsing and stringification
        std::string raw_csv = "id,name,notes\n1,\"Alice, Smith\",\"Multi-line\nNote\"\n2,Bob,\"Special \"\"Quotes\"\"\"";
        auto table = cfire::csv::parse(raw_csv);
        require(table.size() == 3, "csv parsed 3 rows (LIB-04)");
        require(table[1][1] == "Alice, Smith", "csv parsed comma within quotes (LIB-04)");
        require(table[1][2] == "Multi-line\nNote", "csv parsed embedded newline (LIB-04)");
        require(table[2][2] == "Special \"Quotes\"", "csv parsed escaped double quotes (LIB-04)");

        std::string serialized = cfire::csv::stringify(table);
        auto table2 = cfire::csv::parse(serialized);
        require(table2 == table, "csv stringify and re-parse roundtrip (LIB-04)");
    }

    // -------------------------------------------------------------
    // BS-04: Hermetic & Relocatable CMake Ejection
    // -------------------------------------------------------------
    {
        auto tmp_eject = std::filesystem::temp_directory_path() / "cfire_relocatable_eject";
        std::filesystem::remove_all(tmp_eject);
        auto ej = cfire::build::eject_cmake("examples/08-multi-pkg-cc-build/main.cf", rt_dir, tmp_eject);
        require(ej.has_value(), "eject_cmake for 08-multi-pkg-cc-build (BS-04)");
        require(std::filesystem::exists(tmp_eject / "vendor" / "FastCRC" / "fastcrc.c"),
            "vendor companion tree FastCRC mirrored into ejected folder (BS-04)");
        auto cmake_content = cfire::build::read_file_text(tmp_eject / "CMakeLists.txt");
        require(cmake_content.has_value() && cmake_content->contains("vendor/FastCRC/fastcrc.c"),
            "CMakeLists.txt references relocatable vendor relative path (BS-04)");

        // Test CMake configure if cmake binary exists on system
        int cmake_check = 0;
        auto _cmake_out = cfire::bridge::run_command_capture("which cmake", cmake_check);
        (void)_cmake_out;
        if (cmake_check == 0) {
            int build_rc = 0;
            std::string cmd = std::format("cmake -S \"{}\" -B \"{}/build\" && cmake --build \"{}/build\"",
                tmp_eject.string(), tmp_eject.string(), tmp_eject.string());
            std::string build_out = cfire::bridge::run_command_capture(cmd, build_rc);
            require(build_rc == 0, std::format("ejected CMake project builds standalone: {}", build_out));
        }
        std::filesystem::remove_all(tmp_eject);
    }

    // -------------------------------------------------------------
    // LD-02: Value-Category-Aware Variant Matching
    // -------------------------------------------------------------
    {
        std::string_view src = R"(package main

type Item enum {
    Named(str)
    Empty
}

fn consume_variant(move it: Item) -> str {
    match move it {
        Named(val) => {
            return move val
        }
        Empty => {
            return "empty"
        }
    }
}

fn main() {
}
)";
        auto ast = cfire::frontend::parse_source("test_match_move.cf", src);
        require(ast.has_value(), std::format("parse match move item (LD-02): {}", ast ? "" : ast.error()));
        if (ast) {
            auto checked = cfire::frontend::analyze(std::move(*ast));
            require(checked.has_value(), std::format("analyze match move item (LD-02): {}", checked ? "" : checked.error()));
            if (checked) {
                std::string cpp = cfire::frontend::emit_cpp(*checked);
                require(cpp.contains("[&](Item::_Named&& _m)"),
                    "match move emits rvalue reference visitor (LD-02)");
                require(cpp.contains("auto&& val = std::move(_m._0);"),
                    "match move binds payload as rvalue reference with std::move (LD-02)");
                require(cpp.contains("std::move(it._v)"),
                    "std::visit receives std::move(it._v) (LD-02)");
            }
        }
    }

    // -------------------------------------------------------------
    // LD-06: Path-Based Mutation Exclusivity Verification (cfire/MUT-ALIAS)
    // -------------------------------------------------------------
    {
        // 1. Conflicting projection path: mut pt.x, pt.x -> ERROR
        std::string_view src_conflict = R"(package main

type Point struct {
    x: i32
    y: i32
}

fn update(mut a: i32, b: i32) {}

fn run_test() {
    mut pt := Point{x: 1, y: 2}
    update(mut pt.x, pt.x)
}
)";
        auto ast1 = cfire::frontend::parse_source("test_alias_conflict.cf", src_conflict);
        require(ast1.has_value(), std::format("parse test_alias_conflict.cf: {}", ast1 ? "" : ast1.error()));
        if (ast1) {
            auto chk = cfire::frontend::analyze(std::move(*ast1));
            require(!chk.has_value(), "aliasing mut pt.x with pt.x must fail");
            if (!chk) {
                require(chk.error().contains("cfire/MUT-ALIAS"),
                    "projection aliasing reports cfire/MUT-ALIAS (LD-06)");
            }
        }

        // 2. Conflicting root with child: mut pt, pt.x -> ERROR
        std::string_view src_root_conflict = R"(package main

type Point struct {
    x: i32
    y: i32
}

fn update_pt(mut p: Point, v: i32) {}

fn run_test() {
    mut pt := Point{x: 1, y: 2}
    update_pt(mut pt, pt.x)
}
)";
        auto ast2 = cfire::frontend::parse_source("test_root_conflict.cf", src_root_conflict);
        require(ast2.has_value(), std::format("parse test_root_conflict.cf: {}", ast2 ? "" : ast2.error()));
        if (ast2) {
            auto chk = cfire::frontend::analyze(std::move(*ast2));
            require(!chk.has_value(), "aliasing mut pt with pt.x must fail");
            if (!chk) {
                require(chk.error().contains("cfire/MUT-ALIAS"),
                    "root with child projection reports cfire/MUT-ALIAS (LD-06)");
            }
        }

        // 3. Sibling non-overlapping paths: mut pt.x, pt.y -> OK
        std::string_view src_siblings = R"(package main

type Point struct {
    x: i32
    y: i32
}

fn swap_coords(mut a: i32, b: i32) {}

fn run_test() {
    mut pt := Point{x: 1, y: 2}
    swap_coords(mut pt.x, pt.y)
}
)";
        auto ast3 = cfire::frontend::parse_source("test_siblings.cf", src_siblings);
        require(ast3.has_value(), std::format("parse test_siblings.cf: {}", ast3 ? "" : ast3.error()));
        if (ast3) {
            auto chk = cfire::frontend::analyze(std::move(*ast3));
            require(chk.has_value(), "distinct non-overlapping sibling projections are permitted (LD-06)");
        }
    }

    std::println("--- PASS: test_p2_features (LIB-04, BS-04, LD-02, LD-06)");
}

void test_p2_batch2_features([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_p2_batch2_features (BS-05, LIB-05, LIB-07, LD-09)");

    // -------------------------------------------------------------
    // BS-05: Preprocessor Macro Injection for Header-Only C Libraries
    // -------------------------------------------------------------
    {
        std::string_view src = R"(package main

import cpp "stb_image.h" @define("STB_IMAGE_IMPLEMENTATION") @define("BUFFER_SIZE", "4096")

fn main() {}
)";
        auto ast = cfire::frontend::parse_source("test_macro_def.cf", src);
        require(ast.has_value(), std::format("parse @define attributes (BS-05): {}", ast ? "" : ast.error()));
        if (ast) {
            auto checked = cfire::frontend::analyze(std::move(*ast));
            require(checked.has_value(), "analyze @define attributes (BS-05)");
            if (checked) {
                std::string cpp = cfire::frontend::emit_cpp(*checked);
                require(cpp.contains("#define STB_IMAGE_IMPLEMENTATION\n#define BUFFER_SIZE 4096\n#include <stb_image.h>"),
                    "macro definitions emitted immediately before C++ #include (BS-05)");
            }
        }
    }

    // -------------------------------------------------------------
    // LIB-05: Regular Expressions Engine (`core:regex`)
    // -------------------------------------------------------------
    {
        auto re = cfire::regex::compile(R"(\d+)");
        require(re.has_value(), "compile valid regex (LIB-05)");
        if (re) {
            require(re->is_match("user 1234 active"), "regex is_match positive (LIB-05)");
            require(!re->is_match("user inactive"), "regex is_match negative (LIB-05)");
            auto first = re->find("id: 42, count: 99");
            require(first.has_value() && *first == "42", "regex find first match (LIB-05)");
            auto all = re->find_all("10 items, 20 boxes, 30 pallets");
            require(all.size() == 3 && all[0] == "10" && all[1] == "20" && all[2] == "30",
                "regex find_all matches (LIB-05)");
            auto replaced = re->replace_all("width: 100, height: 200", "0");
            require(replaced == "width: 0, height: 0", "regex replace_all (LIB-05)");
        }
        auto bad = cfire::regex::compile("[unclosed-group");
        require(!bad.has_value(), "compile invalid regex returns error (LIB-05)");

        auto m_quick = cfire::regex::is_match(R"(^[a-zA-Z]+$)", "CfireLanguage");
        require(m_quick.has_value() && *m_quick, "regex package-level is_match helper (LIB-05)");

        auto r_quick = cfire::regex::replace_all(R"(\s+)", "apple   banana   orange", " ");
        require(r_quick.has_value() && *r_quick == "apple banana orange", "regex package-level replace_all helper (LIB-05)");
    }

    // -------------------------------------------------------------
    // LIB-07: Data Structures & Algorithms (`core:collections`, `core:sort`)
    // -------------------------------------------------------------
    {
        // 1. Set[T]
        cfire::collections::Set<std::string> set;
        require(set.empty(), "set starts empty (LIB-07)");
        require(set.insert("alpha"), "set insert new key (LIB-07)");
        require(!set.insert("alpha"), "set insert duplicate returns false (LIB-07)");
        require(set.size() == 1, "set size is 1 (LIB-07)");
        require(set.contains("alpha"), "set contains existing key (LIB-07)");
        require(!set.contains("beta"), "set does not contain missing key (LIB-07)");
        require(set.remove("alpha"), "set remove key returns true (LIB-07)");
        require(!set.contains("alpha"), "set removed key no longer present (LIB-07)");

        // 2. PriorityQueue[T] (Max-Heap)
        cfire::collections::PriorityQueue<int> pq;
        pq.push(20);
        pq.push(50);
        pq.push(10);
        pq.push(30);
        require(pq.size() == 4, "priority queue size is 4 (LIB-07)");
        require(pq.peek() == 50, "priority queue top is max element (LIB-07)");
        require(pq.pop() == 50, "pop max element 50 (LIB-07)");
        require(pq.pop() == 30, "pop next element 30 (LIB-07)");
        require(pq.pop() == 20, "pop next element 20 (LIB-07)");
        require(pq.pop() == 10, "pop final element 10 (LIB-07)");
        require(pq.pop() == std::nullopt, "pop from empty queue returns nullopt (LIB-07)");

        // 3. Deque[T] (Double-ended queue)
        cfire::collections::Deque<int> dq;
        dq.push_back(20);
        dq.push_front(10);
        dq.push_back(30);
        require(dq.size() == 3, "deque size is 3 (LIB-07)");
        require(dq.at(0) == 10 && dq.at(1) == 20 && dq.at(2) == 30, "deque indexing order (LIB-07)");
        require(dq.pop_front() == 10, "pop_front yields 10 (LIB-07)");
        require(dq.pop_back() == 30, "pop_back yields 30 (LIB-07)");
        require(dq.pop_back() == 20, "pop_back yields 20 (LIB-07)");
        require(dq.empty(), "deque empty after draining (LIB-07)");

        // 4. In-Place Sorting & Binary Search
        std::vector<int> vals = {42, 12, 88, 3, 55, 19};
        cfire::sort::slice(vals);
        require(vals == std::vector<int>{3, 12, 19, 42, 55, 88}, "sort.slice sorts in ascending order (LIB-07)");

        auto pos = cfire::sort::binary_search(std::span<const int>(vals), 42);
        require(pos.has_value() && *pos == 3, "sort.binary_search finds existing key (LIB-07)");
        auto missing = cfire::sort::binary_search(std::span<const int>(vals), 50);
        require(!missing.has_value(), "sort.binary_search returns nullopt on missing element (LIB-07)");
    }

    // -------------------------------------------------------------
    // LD-09: Unambiguous Generic Call Disambiguation (`f[T, U](...)`)
    // -------------------------------------------------------------
    {
        std::string_view src = R"(package main

fn pair[T, U](a: T, b: U) -> T {
    return a
}

fn test_generics() {
    p := pair[i32, str](42, "hello")
    s := pair[[]i32, bool]([1, 2], true)
}

fn main() {}
)";
        auto ast = cfire::frontend::parse_source("test_multi_generics.cf", src);
        require(ast.has_value(), std::format("parse multi-argument generic calls f[T, U](args) (LD-09): {}", ast ? "" : ast.error()));
        if (ast) {
            auto checked = cfire::frontend::analyze(std::move(*ast));
            require(checked.has_value(), "analyze multi-argument generic calls (LD-09)");
            if (checked) {
                std::string cpp = cfire::frontend::emit_cpp(*checked);
                require(cpp.contains("pair<std::int32_t, std::string>(42, \"hello\")"),
                    "codegen lowers multi-type generic call f<T1, T2>(args) (LD-09)");
                require(cpp.contains("pair<std::vector<std::int32_t>, bool>("),
                    "codegen lowers compound-type generic call with slice type arg (LD-09)");
            }
        }
    }

    std::println("--- PASS: test_p2_batch2_features (BS-05, LIB-05, LIB-07, LD-09)");
}

void test_p2_batch3_features([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_p2_batch3_features (LIB-06, LIB-08, BS-06, LD-13)");

    // -------------------------------------------------------------
    // LIB-06: Time, Date & Duration Modernization (`core:time`)
    // -------------------------------------------------------------
    {
        auto d5 = cfire::time::Duration::from_secs(5);
        require(d5.millis() == 5000, "Duration millis (LIB-06)");
        require(d5.micros() == 5000000, "Duration micros (LIB-06)");
        require(d5.nanos == 5000000000LL, "Duration nanos (LIB-06)");

        auto d_sum = cfire::time::Duration::from_millis(250) + cfire::time::Duration::from_millis(750);
        require(d_sum.millis() == 1000, "Duration operator+ (LIB-06)");
        require(d_sum == cfire::time::Duration::from_secs(1), "Duration equality (LIB-06)");

        auto now = cfire::time::now();
        require(now.unix_seconds() > 1700000000LL, "time.now() unix_seconds (LIB-06)");

        auto t1 = cfire::time::from_unix_seconds(1700000000LL);
        auto formatted = t1.format_rfc3339();
        require(formatted == "2023-11-14T22:13:20Z", "format_rfc3339 matches expected (LIB-06)");

        auto parsed = cfire::time::parse_rfc3339("2023-11-14T22:13:20Z");
        require(parsed.has_value(), "parse_rfc3339 roundtrip succeeds (LIB-06)");
        if (parsed) {
            require(parsed->unix_seconds() == 1700000000LL, "parsed timestamp matches original (LIB-06)");
            require(parsed->year() == 2023, "calendar breakdown year (LIB-06)");
            require(parsed->month() == 11, "calendar breakdown month (LIB-06)");
            require(parsed->day() == 14, "calendar breakdown day (LIB-06)");
            require(parsed->hour() == 22, "calendar breakdown hour (LIB-06)");
            require(parsed->minute() == 13, "calendar breakdown minute (LIB-06)");
            require(parsed->second() == 20, "calendar breakdown second (LIB-06)");
        }

        auto bad_parse = cfire::time::parse_rfc3339("invalid-timestamp-string");
        require(!bad_parse.has_value(), "parse_rfc3339 returns error on malformed string (LIB-06)");
    }

    // -------------------------------------------------------------
    // LIB-08: Leveled Structured Logging (`core:log`)
    // -------------------------------------------------------------
    {
        cfire::log::set_level(cfire::log::Level::Debug);
        require(cfire::log::get_level() == cfire::log::Level::Debug, "log get/set level (LIB-08)");

        cfire::log::set_format(cfire::log::Format::Json);
        require(cfire::log::get_format() == cfire::log::Format::Json, "log get/set format JSON (LIB-08)");
        cfire::log::info("user logged in", "user_id", "42", "tenant", "prod");

        cfire::log::set_format(cfire::log::Format::Text);
        require(cfire::log::get_format() == cfire::log::Format::Text, "log get/set format Text (LIB-08)");
        cfire::log::debug("debugging details", "status", "ok");
        cfire::log::warn("resource approaching limit", "usage_pct", "85");
        cfire::log::error("service unreachable", "host", "backend.local");
    }

    // -------------------------------------------------------------
    // BS-06: Shared Library (.dylib / .so) & Dynamic Link Support
    // -------------------------------------------------------------
    {
        auto tmp_bs06 = std::filesystem::temp_directory_path() / "cfire_bs06_test";
        std::filesystem::remove_all(tmp_bs06);
        std::filesystem::create_directories(tmp_bs06 / "vendor" / "sharedlib");

        {
            std::ofstream c_out(tmp_bs06 / "vendor" / "sharedlib" / "shared_math.c");
            c_out << "int add_numbers(int a, int b) { return a + b; }\n";
        }

        cfire::build::BuildPlan plan;
        cfire::build::ExternalDepSpec dep;
        dep.name = "shared_math";
        dep.driver = "CC";
        dep.source = "vendor/sharedlib";
        dep.is_shared = true;

        auto res = cfire::build::build_local_dep(tmp_bs06, dep, plan);
        require(res.has_value(), std::format("build_local_dep produces shared library (BS-06): {}", res ? "" : res.error()));
        require(dep.is_shared, "BS-06: dep.is_shared confirmed");
        require(!dep.built_archive.empty(), "BS-06: built_archive path populated");
#if defined(__APPLE__)
        require(dep.built_archive.ends_with(".dylib"), "BS-06: macOS produces .dylib");
#else
        require(dep.built_archive.ends_with(".so"), "BS-06: Linux produces .so");
#endif
        bool has_rpath = false;
        for (const auto& f : plan.ldflags) {
            if (f.starts_with("-Wl,-rpath,")) {
                has_rpath = true;
                break;
            }
        }
        require(has_rpath, "BS-06: automatic -Wl,-rpath added to plan.ldflags for shared library");
    }

    // -------------------------------------------------------------
    // LD-13: Multi-Return Tuple Syntax & 'out' parameter deprecation
    // -------------------------------------------------------------
    {
        // 1. Parsing & Codegen of Multi-Return Tuple and Destructuring
        std::string_view src = R"(package main

fn coords() -> (f64, f64)! {
    return (10.5, 20.5)
}

fn main() {
    (x, y) := try coords()
    mut a := 0.0
    mut b := 0.0
    (a, b) = (x, y)
}
)";
        auto ast = cfire::frontend::parse_source("test_tuples.cf", src);
        require(ast.has_value(), std::format("parse tuple syntax (LD-13): {}", ast ? "" : ast.error()));
        if (ast) {
            auto checked = cfire::frontend::analyze(std::move(*ast));
            require(checked.has_value(), "analyze tuple syntax (LD-13)");
            if (checked) {
                std::string cpp = cfire::frontend::emit_cpp(*checked);
                require(cpp.contains("auto coords() -> std::expected<std::tuple<double, double>, cfire::Error>"),
                    "codegen lowers multi-return tuple to std::expected<std::tuple<...>> (LD-13)");
                require(cpp.contains("return std::make_tuple(10.5, 20.5);"),
                    "codegen lowers tuple literal to std::make_tuple (LD-13)");
                require(cpp.contains("auto [x, y] ="),
                    "codegen lowers deconstructing var decl to C++23 structured binding (LD-13)");
                require(cpp.contains("std::tie(a, b) = std::make_tuple(x, y);"),
                    "codegen lowers deconstructing assign to std::tie (LD-13)");
            }
        }

        // 2. Deprecation warning for 'out' parameters
        std::string_view src_out = R"(package main

fn legacy_div(num: i32, denom: i32, out rem: i32) -> i32 {
    rem = num % denom
    return num / denom
}

fn main() {
    mut r := 0
    q := legacy_div(10, 3, out r)
}
)";
        auto ast_out = cfire::frontend::parse_source("test_out_param.cf", src_out);
        require(ast_out.has_value(), "parse out parameter code");
        if (ast_out) {
            auto checked = cfire::frontend::analyze(std::move(*ast_out));
            require(checked.has_value(), "analyze out parameter code succeeds with warnings");
            if (checked) {
                bool has_dep_warn = std::ranges::any_of(checked->warnings, [](const auto& w) {
                    return w.contains("cfire/DEPRECATED-OUT-PARAM");
                });
                require(has_dep_warn, "deprecation warning emitted for 'out' parameter (LD-13)");
            }
        }
    }

    std::println("--- PASS: test_p2_batch3_features (LIB-06, LIB-08, BS-06, LD-13)");
}

void test_p2_batch4_features(const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_p2_batch4_features (LIB-09, LIB-10, BS-07, BS-08, LD-07)");

    // -------------------------------------------------------------
    // LIB-09: Streaming Filesystem I/O (core:fs Modernization)
    // -------------------------------------------------------------
    {
        // 1. Path decomposition helpers
        require(cfire::fs::dirname("/usr/local/bin/cfire") == "/usr/local/bin", "fs.dirname (LIB-09)");
        require(cfire::fs::basename("/usr/local/bin/cfire") == "cfire", "fs.basename (LIB-09)");
        require(cfire::fs::extension("/usr/local/bin/cfire.tar.gz") == ".gz", "fs.extension (LIB-09)");
        require(cfire::fs::extension("source.cf") == ".cf", "fs.extension .cf (LIB-09)");

        // 2. Streaming File open, write, seek, tell, read, flush, close
        auto tmp_file = std::filesystem::temp_directory_path() / "cfire_stream_test.txt";
        std::filesystem::remove(tmp_file);

        auto file_res = cfire::fs::open(tmp_file, cfire::fs::OpenMode::Write);
        require(file_res.has_value(), "fs.open for write (LIB-09)");
        if (file_res) {
            auto& f = *file_res;
            require(f.is_open(), "file is open (LIB-09)");
            auto w1 = f.write_str("Hello Streaming World!\nLine 2.");
            require(w1.has_value() && *w1 > 0, "file.write_str (LIB-09)");
            auto fl = f.flush();
            require(fl.has_value(), "file.flush (LIB-09)");
            f.close();
            require(!f.is_open(), "file closed (LIB-09)");
        }

        // Reopen for read and seek
        auto r_res = cfire::fs::open(tmp_file, cfire::fs::OpenMode::Read);
        require(r_res.has_value(), "fs.open for read (LIB-09)");
        if (r_res) {
            auto& f = *r_res;
            auto t0 = f.tell();
            require(t0.has_value() && *t0 == 0, "file.tell at start (LIB-09)");

            // Seek to offset 6 ("Streaming")
            auto s1 = f.seek(6, cfire::fs::SeekWhence::Start);
            require(s1.has_value() && *s1 == 6, "file.seek Start (LIB-09)");

            std::vector<uint8_t> chunk(9);
            auto n_read = f.read(chunk);
            require(n_read.has_value() && *n_read == 9, "file.read 9 bytes (LIB-09)");
            std::string word(reinterpret_cast<const char*>(chunk.data()), chunk.size());
            require(word == "Streaming", "file chunk content matches (LIB-09)");

            // Seek back to start and read_all
            auto s0 = f.seek(0, cfire::fs::SeekWhence::Start);
            require(s0.has_value() && *s0 == 0, "file.seek to 0 (LIB-09)");
            auto all_bytes = f.read_all();
            require(all_bytes.has_value(), "file.read_all (LIB-09)");
            std::string all_str(reinterpret_cast<const char*>(all_bytes->data()), all_bytes->size());
            require(all_str == "Hello Streaming World!\nLine 2.", "file.read_all content matches (LIB-09)");
            f.close();
        }
        std::filesystem::remove(tmp_file);
    }

    // -------------------------------------------------------------
    // LIB-10: JSON Dynamic DOM & Structured Serializer Fixes
    // -------------------------------------------------------------
    {
        // 1. String escaping
        std::string raw_esc = "quote: \", backslash: \\, newline: \n, tab: \t";
        std::string escaped = cfire::json::escape_string(raw_esc);
        require(escaped.contains("\\\""), "escaped quote (LIB-10)");
        require(escaped.contains("\\\\"), "escaped backslash (LIB-10)");
        require(escaped.contains("\\n"), "escaped newline (LIB-10)");
        require(escaped.contains("\\t"), "escaped tab (LIB-10)");

        // 2. Dynamic JSON DOM parsing
        std::string json_input = R"({
            "service": "cfire-auth",
            "version": 2,
            "rate_limit": 1500.5,
            "active": true,
            "metadata": null,
            "tags": ["core", "security", "p2"],
            "config": {
                "timeout_ms": 5000,
                "domain": "auth.internal"
            }
        })";

        auto dom_res = cfire::json::parse_dynamic(json_input);
        require(dom_res.has_value(), std::format("parse_dynamic (LIB-10): {}", dom_res ? "" : dom_res.error().message()));
        if (dom_res) {
            const auto& root = *dom_res;
            require(root.is_object(), "root is object (LIB-10)");
            require(root["service"].is_string() && root["service"].as_str() == "cfire-auth", "get string (LIB-10)");
            require(root["version"].is_int() && root["version"].as_int() == 2, "get int (LIB-10)");
            require(root["rate_limit"].is_float() && std::abs(root["rate_limit"].as_float() - 1500.5) < 0.001, "get float (LIB-10)");
            require(root["active"].is_bool() && root["active"].as_bool() == true, "get bool (LIB-10)");
            require(root["metadata"].is_null(), "get null (LIB-10)");

            auto tags = root["tags"];
            require(tags.is_array() && tags.size() == 3, "tags is array of size 3 (LIB-10)");
            require(tags[0].as_str() == "core", "tags[0] == core (LIB-10)");
            require(tags[2].as_str() == "p2", "tags[2] == p2 (LIB-10)");

            auto cfg = root["config"];
            require(cfg.is_object(), "nested config is object (LIB-10)");
            require(cfg["timeout_ms"].as_int() == 5000, "nested config timeout_ms (LIB-10)");
            require(cfg["domain"].as_str() == "auth.internal", "nested config domain (LIB-10)");

            // Dynamic stringify roundtrip
            std::string serialized = cfire::json::stringify_dynamic(root);
            auto re_parsed = cfire::json::parse_dynamic(serialized);
            require(re_parsed.has_value(), "re-parse serialized dynamic json (LIB-10)");
            if (re_parsed) {
                require((*re_parsed)["service"].as_str() == "cfire-auth", "roundtrip value match (LIB-10)");
                require((*re_parsed)["tags"].size() == 3, "roundtrip array size match (LIB-10)");
            }
        }
    }

    // -------------------------------------------------------------
    // LD-07: Unify Variable Declarations & Deprecate `var`
    // -------------------------------------------------------------
    {
        // 1. Canonical package-level & local declarations
        std::string_view src_canonical = R"(package main

app_name := "SapphireServer"
mut port := 8080
max_conn: i32 = 1000
mut is_ready: bool = true

fn test_vars() {
    x := 10
    mut y := 20
    name: str = "cfire"
    mut count: i64 = 99
}
)";
        auto ast = cfire::frontend::parse_source("test_canonical_vars.cf", src_canonical);
        require(ast.has_value(), std::format("parse canonical package and local vars (LD-07): {}", ast ? "" : ast.error()));
        if (ast) {
            auto checked = cfire::frontend::analyze(std::move(*ast));
            require(checked.has_value(), "analyze canonical vars (LD-07)");
            if (checked) {
                require(checked->warnings.empty(), "canonical declarations emit zero warnings (LD-07)");
                std::string cpp = cfire::frontend::emit_cpp(*checked);
                require(cpp.contains("inline const auto app_name = \"SapphireServer\";"), "codegen package const inferred var (LD-07)");
                require(cpp.contains("inline auto port = 8080;"), "codegen package mut inferred var (LD-07)");
                require(cpp.contains("inline const std::int32_t max_conn = 1000;"), "codegen package const typed var (LD-07)");
                require(cpp.contains("inline bool is_ready = true;"), "codegen package mut typed var (LD-07)");
            }
        }

        // 2. Deprecation warning for 'var'
        std::string_view src_var = R"(package main

var global_legacy = 123
var mut global_typed: i32 = 456

fn legacy_local() {
    var a = 1
    var mut b: str = "old"
}
)";
        auto ast_var = cfire::frontend::parse_source("test_deprecated_var.cf", src_var);
        require(ast_var.has_value(), "parse legacy var code (LD-07)");
        if (ast_var) {
            auto checked = cfire::frontend::analyze(std::move(*ast_var));
            require(checked.has_value(), "analyze legacy var code succeeds with warnings (LD-07)");
            if (checked) {
                int warn_count = 0;
                for (const auto& w : checked->warnings) {
                    if (w.contains("cfire/DEPRECATED-VAR")) ++warn_count;
                }
                require(warn_count >= 4, "emits cfire/DEPRECATED-VAR warning for each var declaration (LD-07)");
            }
        }
    }

    // -------------------------------------------------------------
    // BS-07: Recursive Local Package Resolution & Subpackage build.cf
    // -------------------------------------------------------------
    {
        ScopedTempDir tmp_ws("cfire_recursive_pkg");

        // pkg_b contains a C library dependency in its sibling build.cf
        tmp_ws.file("pkg_b/pkg_b.cf", R"(package pkg_b
fn helper_b() -> i32 { return 42 }
)");
        tmp_ws.file("pkg_b/build.cf", R"(package build
helper_lib := build.CC("helper_c", {
    Sources: ["helper.c"]
})
)");
        tmp_ws.file("pkg_b/helper.c", "int helper_c_fn() { return 100; }\n");

        // pkg_a imports pkg_b
        tmp_ws.file("pkg_a/pkg_a.cf", R"(package pkg_a
import "../pkg_b"
fn helper_a() -> i32 { return pkg_b.helper_b() + 1 }
)");

        // root main.cf imports pkg_a
        auto main_cf = tmp_ws.file("main.cf", R"(package main
import "./pkg_a"
fn main() -> i32 { return pkg_a.helper_a() }
)");

        auto res = cfire::build::analyze_and_emit(main_cf, rt_dir, false);
        require(res.has_value(), std::format("analyze_and_emit recursive packages (BS-07): {}", res ? "" : res.error()));
        if (res) {
            const auto& plan = res->second;
            bool found_b_dep = false;
            for (const auto& dep : plan.external_deps) {
                if (dep.name == "helper_lib") found_b_dep = true;
            }
            require(found_b_dep, "subpackage build.cf external dependency aggregated into root build plan (BS-07)");
        }
    }

    // -------------------------------------------------------------
    // BS-08: Pre-Build Code Generation Hooks (build.Command)
    // -------------------------------------------------------------
    {
        ScopedTempDir tmp_hook_ws("cfire_prebuild_hook");

        // build.cf declaring pre-build Command
        tmp_hook_ws.file("build.cf", R"(package build
gen_hook := build.Command("/bin/sh", {
    Args: ["-c", "echo 'package main' > generated.cf && echo 'fn gen_code() -> i32 { return 777 }' >> generated.cf"],
    Outputs: ["generated.cf"]
})
)");

        auto main_cf = tmp_hook_ws.file("main.cf", R"(package main
import "./generated.cf"
fn main() -> i32 { return gen_code() }
)");

        auto res = cfire::build::analyze_and_emit(main_cf, rt_dir, false);
        require(res.has_value(), std::format("analyze_and_emit with pre-build command hook (BS-08): {}", res ? "" : res.error()));
        require(std::filesystem::exists(tmp_hook_ws.path / "generated.cf"), "pre-build hook generated target file (BS-08)");
    }

    std::println("--- PASS: test_p2_batch4_features (LIB-09, LIB-10, BS-07, BS-08, LD-07)");
}

void test_p2_batch5_features([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_p2_batch5_features (LIB-11, LD-10, TOOL-01, IDE-01)");

    // -------------------------------------------------------------
    // LIB-11: Multi-Byte UTF-8 & Unicode Support (core:unicode)
    // -------------------------------------------------------------
    {
        std::string ascii = "Hello, world!";
        require(cfire::unicode::rune_count(ascii) == 13, "ascii rune count (LIB-11)");
        require(cfire::unicode::is_valid_utf8(ascii), "ascii is valid utf8 (LIB-11)");

        std::string multi = "Hello, 世界! 🚀";
        // "Hello, " (7) + "世界" (2) + "! " (2) + "🚀" (1) = 12 runes, but 19 bytes!
        require(cfire::unicode::rune_count(multi) == 12, "multibyte utf8 rune count (LIB-11)");
        require(multi.size() == 19, "byte size is 19 (LIB-11)");
        require(cfire::unicode::is_valid_utf8(multi), "multibyte is valid utf8 (LIB-11)");

        auto cps = cfire::unicode::runes(multi);
        require(cps.size() == 12, "runes vector size matches rune count (LIB-11)");
        require(cps[7] == 0x4E16, "codepoint for 世 (LIB-11)");
        require(cps[8] == 0x754C, "codepoint for 界 (LIB-11)");
        require(cps[11] == 0x1F680, "codepoint for rocket emoji 🚀 (LIB-11)");

        // Encoding rune
        std::string encoded = cfire::unicode::encode_rune(0x1F680);
        require(encoded == "🚀", "encode_rune roundtrip (LIB-11)");

        // Invalid byte sequence rejection
        std::string invalid_bytes = "abc\xFF\xFE" "def";
        require(!cfire::unicode::is_valid_utf8(invalid_bytes), "invalid utf8 sequence rejected (LIB-11)");

        // Case transformation (ASCII + Cyrillic)
        std::string mixed_cyr = "Hello, МИР!";
        std::string lower_cyr = cfire::unicode::to_lower(mixed_cyr);
        require(lower_cyr == "hello, мир!", "unicode to_lower (LIB-11)");
        std::string upper_cyr = cfire::unicode::to_upper(lower_cyr);
        require(upper_cyr == "HELLO, МИР!", "unicode to_upper (LIB-11)");
    }

    // -------------------------------------------------------------
    // LD-10: Decouple Struct Tags & Generalize Tag Metadata
    // -------------------------------------------------------------
    {
        std::string_view src = R"(package main

type ServerConfig struct {
    port: i32       `json:"port" env:"PORT" cli:"--port,-p" help:"Listening port"`
    host: str       `json:"host" env:"HOST" yaml:"server_host"`
    max_conn: i64   `json:"max_conn" db:"max_connections" min:"1" max:"10000"`
}

fn main() {}
)";
        auto ast = cfire::frontend::parse_source("test_tags.cf", src);
        require(ast.has_value(), std::format("parse generalized struct tags (LD-10): {}", ast ? "" : ast.error()));
        if (ast) {
            require(!ast->structs.empty(), "parsed struct (LD-10)");
            const auto& st = ast->structs[0];
            require(st.fields.size() == 3, "struct has 3 fields (LD-10)");
            
            // Check field 0 tags
            const auto& f0 = st.fields[0];
            require(f0.tags.size() == 4, "field 0 has 4 parsed tags (LD-10)");
            require(f0.get_tag("json") == "port", "get_tag json (LD-10)");
            require(f0.get_tag("env") == "PORT", "get_tag env (LD-10)");
            require(f0.get_tag("cli") == "--port,-p", "get_tag cli (LD-10)");
            require(f0.get_tag("help") == "Listening port", "get_tag help (LD-10)");

            // Check field 1 custom yaml tag
            const auto& f1 = st.fields[1];
            require(f1.get_tag("yaml") == "server_host", "get_tag yaml custom tag (LD-10)");

            // Check field 2 custom min/max tags
            const auto& f2 = st.fields[2];
            require(f2.get_tag("min") == "1" && f2.get_tag("max") == "10000", "get_tag custom min/max (LD-10)");

            // Verify codegen emits reflection metadata
            auto chk = cfire::frontend::analyze(std::move(*ast));
            require(chk.has_value(), "analyze struct with custom tags (LD-10)");
            if (chk) {
                std::string cpp = cfire::frontend::emit_cpp(*chk);
                require(cpp.contains("template <>"), "emits template specialization (LD-10)");
                require(cpp.contains("struct cfire::reflect_struct<ServerConfig> {"), "emits reflect_struct specialization (LD-10)");
                require(cpp.contains("static constexpr std::string_view name = \"ServerConfig\";"), "reflect_struct name (LD-10)");
                require(cpp.contains("static constexpr std::size_t field_count = 3;"), "reflect_struct field_count 3 (LD-10)");
                require(cpp.contains("\"port\","), "reflect_struct field_names port (LD-10)");
                require(cpp.contains("\"host\","), "reflect_struct field_names host (LD-10)");
            }
        }
    }

    // -------------------------------------------------------------
    // TOOL-01: Agent Tooling Suite (--json, outline, --llm)
    // -------------------------------------------------------------
    {
        ScopedTempDir tmp_ws("cfire_tool");
        auto mod_cf = tmp_ws.file("mod.cf", R"(package mymod

import "core:fs"

type Config struct {
    threads: i32 `json:"threads"`
}

type Status enum {
    Active,
    Idle
}

fn worker(cfg: Config) -> bool {
    return true
}
)");

        // 1. Test outline generation
        run_cfire(std::format("outline --json \"{}\"", tmp_ws.path.string()))
            .assert_ok("cfire outline exit 0 (TOOL-01)")
            .assert_contains("\"package\": \"mymod\"", "outline contains package name (TOOL-01)")
            .assert_contains("\"imports\": [\"core:fs\"]", "outline contains imports (TOOL-01)")
            .assert_contains("\"name\": \"Config\"", "outline contains struct Config (TOOL-01)")
            .assert_contains("\"name\": \"Status\"", "outline contains enum Status (TOOL-01)")
            .assert_contains("\"name\": \"worker\"", "outline contains function worker (TOOL-01)");

        // 2. Test check --json
        run_cfire(std::format("check --json \"{}\"", mod_cf.string()))
            .assert_ok("cfire check --json exit 0 (TOOL-01)")
            .assert_contains("\"success\": true", "check --json reports success (TOOL-01)");

        // 3. Test doc --llm
        run_cfire("doc --llm")
            .assert_ok("cfire doc --llm exit 0 (TOOL-01)")
            .assert_contains("Reference Sheet", "doc --llm emits reference sheet (TOOL-01)")
            .assert_contains("core:unicode", "doc --llm lists core:unicode (TOOL-01)");
    }

    // -------------------------------------------------------------
    // IDE-01: Universal Multi-IDE Auto-Detection & Setup (cfire ide)
    // -------------------------------------------------------------
    {
        ScopedTempDir tmp_ide_ws("cfire_ide");

        // Configure all IDEs
        auto res_all = cfire::lsp::configure_ide(tmp_ide_ws.path, "--all");
        require(res_all.has_value(), std::format("configure_ide --all (IDE-01): {}", res_all ? "" : res_all.error()));
        require(std::filesystem::exists(tmp_ide_ws.path / ".vscode" / "settings.json"), "creates .vscode/settings.json (IDE-01)");
        require(std::filesystem::exists(tmp_ide_ws.path / ".vscode" / "extensions.json"), "creates .vscode/extensions.json (IDE-01)");
        require(std::filesystem::exists(tmp_ide_ws.path / ".zed" / "settings.json"), "creates .zed/settings.json (IDE-01)");
        require(std::filesystem::exists(tmp_ide_ws.path / ".nvim.lua"), "creates .nvim.lua (IDE-01)");
        require(std::filesystem::exists(tmp_ide_ws.path / ".idea" / "lsp.xml"), "creates .idea/lsp.xml (IDE-01)");

        // Verify contents of .vscode/settings.json
        auto vs_txt = cfire::fs::read_text(tmp_ide_ws.path / ".vscode" / "settings.json");
        require(vs_txt.has_value() && vs_txt->contains("\"editor.defaultFormatter\": \"cfire\""), "vs settings formatter (IDE-01)");
        require(vs_txt->contains("\"cfire.inlayHints.enable\": true"), "vs settings inlay hints (IDE-01)");

        // Verify contents of .nvim.lua
        auto nvim_txt = cfire::fs::read_text(tmp_ide_ws.path / ".nvim.lua");
        require(nvim_txt.has_value() && nvim_txt->contains("vim.lsp.start"), "nvim lua starts lsp (IDE-01)");
    }

    std::println("--- PASS: test_p2_batch5_features (LIB-11, LD-10, TOOL-01, IDE-01)");
}

void test_cfire_eval([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_cfire_eval (TOOL-02: cfire eval One-Shot REPL & CLI Evaluator)");

    // 1. Statement eval
    run_cfire("eval 'println(\"eval-test-ok\")'")
        .assert_ok("eval statement exit 0 (TOOL-02)")
        .assert_contains("eval-test-ok", "eval statement stdout match (TOOL-02)");

    // 2. Expression eval (-e) (respects standard operator precedence: 100 + (42 * 2) = 184)
    run_cfire("eval -e '100 + 42 * 2'")
        .assert_ok("eval expression exit 0 (TOOL-02)")
        .assert_contains("184", "eval expression stdout 184 match (TOOL-02)");

    // 3. Pre-imported stdlib (str + unicode)
    run_cfire("eval 'name := \"cfire🚀\"; println(\"upper={}, runes={}\", str.to_upper(\"cfire\"), unicode.rune_count(name))'")
        .assert_ok("eval pre-imported stdlib exit 0 (TOOL-02)")
        .assert_contains("upper=CFIRE, runes=6", "eval pre-imported stdlib rune count match (TOOL-02)");

    // 4. Stdin piping
    int code = 0;
    std::string out4 = cfire::bridge::run_command_capture("echo 'println(\"pipe-ok-42\")' | ./cfire eval", code);
    require(code == 0, std::format("eval stdin piping exit 0 (TOOL-02): {}", out4));
    require(out4.contains("pipe-ok-42"), "eval stdin piping stdout match (TOOL-02)");

    // 5. Multi-line declarations (struct + helper function)
    run_cfire(R"(eval 'type Point struct {
    x: i32
    y: i32
}
fn double_point(p: Point) -> Point {
    return Point{ x: p.x * 2, y: p.y * 2 }
}
pt := double_point(Point{ x: 10, y: 20 })
println("x={}, y={}", pt.x, pt.y)')")
        .assert_ok("eval top-level decls exit 0 (TOOL-02)")
        .assert_contains("x=20, y=40", "eval top-level struct/fn stdout match (TOOL-02)");

    // 6. Complete standalone package
    run_cfire(R"(eval 'package main
fn main() {
    println("full-pkg-ok")
}')")
        .assert_ok("eval complete package exit 0 (TOOL-02)")
        .assert_contains("full-pkg-ok", "eval complete package stdout match (TOOL-02)");

    std::println("--- PASS: test_cfire_eval (TOOL-02)");
}

void test_cfire_refactor_rename([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_cfire_refactor_rename (TOOL-03: cfire refactor rename)");

    ScopedTempDir tmp("cfire_refactor");
    auto main_cf = tmp.file("main.cf", R"(package main

fn calculate_total(base: i32, multiplier: i32) -> i32 {
    result := base * multiplier
    return result + 10
}

fn main() {
    v := calculate_total(5, 4)
    println("total={}", v)
}
)");

    // 1. Unified diff preview
    run_cfire(std::format("refactor rename \"{}:4:5\" final_amount --diff", main_cf.string()))
        .assert_ok("refactor diff exit 0 (TOOL-03)")
        .assert_contains("--- a/", "diff contains source header (TOOL-03)")
        .assert_contains("+++ b/", "diff contains target header (TOOL-03)")
        .assert_contains("-    result := base * multiplier", "diff contains original line deletion (TOOL-03)")
        .assert_contains("+    final_amount := base * multiplier", "diff contains modified line addition (TOOL-03)");

    // 2. Structured JSON output
    run_cfire(std::format("refactor rename \"{}:4:5\" final_amount --json", main_cf.string()))
        .assert_ok("refactor json exit 0 (TOOL-03)")
        .assert_contains("\"success\": true", "json reports success (TOOL-03)")
        .assert_contains("\"old_name\": \"result\"", "json reports old_name (TOOL-03)")
        .assert_contains("\"new_name\": \"final_amount\"", "json reports new_name (TOOL-03)")
        .assert_contains("\"total_occurrences\": 2", "json reports 2 occurrences (TOOL-03)");

    // 3. Collision safety: cannot rename to existing identifier in scope
    run_cfire(std::format("refactor rename \"{}:4:5\" multiplier --diff", main_cf.string()))
        .assert_fail("refactor collision exits non-zero (TOOL-03)")
        .assert_contains("cfire/REFACTOR-COLLISION", "refactor reports collision diagnostic (TOOL-03)");

    // 4. Invalid identifier rejection
    run_cfire(std::format("refactor rename \"{}:4:5\" '9invalid' --diff", main_cf.string()))
        .assert_fail("refactor invalid name exits non-zero (TOOL-03)")
        .assert_contains("cfire/REFACTOR-INVALID-NAME", "refactor reports invalid name diagnostic (TOOL-03)");

    // 5. Apply modification and run
    run_cfire(std::format("refactor rename \"{}:4:5\" final_amount --apply", main_cf.string()))
        .assert_ok("refactor apply exit 0 (TOOL-03)")
        .assert_contains("Successfully renamed 'result' to 'final_amount'", "apply confirmation message (TOOL-03)");

    // Verify modified file compiles and runs with expected output
    run_cfire(std::format("run \"{}\"", main_cf.string()))
        .assert_ok("refactored file runs cleanly (TOOL-03)")
        .assert_contains("total=30", "refactored output is total=30 (TOOL-03)");

    // 6. Cross-package function rename
    tmp.file("build.cf", "package build\n");
    auto util_cf = tmp.file("pkg/mathutil/mathutil.cf", R"(package mathutil

fn double_it(n: i32) -> i32 {
    return n * 2
}
)");

    tmp.file("caller.cf", R"(package main

import "./pkg/mathutil"

fn main() {
    res := mathutil.double_it(21)
    println("res={}", res)
}
)");

    run_cfire(std::format("refactor rename \"{}:3:4\" multiply_two --json", util_cf.string()))
        .assert_ok("cross-pkg rename exit 0 (TOOL-03)")
        .assert_contains("\"files_modified\": 2", "cross-pkg modified 2 files (TOOL-03)");

    std::println("--- PASS: test_cfire_refactor_rename (TOOL-03)");
}

void test_cfire_remote_vendor(const std::filesystem::path& rt_dir) {
    (void)rt_dir;
    ScopedTempDir test_root("cfire_bs_ext");

    auto remote_repo = test_root.subpath("remote_calc");
    std::filesystem::create_directories(remote_repo);

    int rc = 0;
    (void)cfire::bridge::run_command_capture(
        std::format("git -C \"{}\" init -q && git -C \"{}\" config user.name \"Test\" && git -C \"{}\" config user.email \"test@example.com\"",
            remote_repo.string(), remote_repo.string(), remote_repo.string()), rc);
    require(rc == 0, "git init remote_calc (BS-EXT-01)");

    (void)cfire::fs::write_text(remote_repo / "calc.h",
        "#ifndef CALC_H\n#define CALC_H\n#ifdef __cplusplus\nextern \"C\" {\n#endif\nint add(int a, int b);\n#ifdef __cplusplus\n}\n#endif\n#endif\n");
    (void)cfire::fs::write_text(remote_repo / "calc.c",
        "#include \"calc.h\"\nint add(int a, int b) { return a + b; }\n");

    (void)cfire::bridge::run_command_capture(
        std::format("git -C \"{}\" add . && git -C \"{}\" commit -q -m \"Initial commit\"",
            remote_repo.string(), remote_repo.string()), rc);
    require(rc == 0, "git commit remote_calc (BS-EXT-01)");

    auto app_dir = test_root.subpath("app");
    std::filesystem::create_directories(app_dir);

    (void)cfire::fs::write_text(app_dir / "build.cf", std::format(
        "package build\n\nvar Calc = build.Git(\"{}\", {{\n    Driver: \"CC\",\n    Sources: [\"calc.c\"],\n    Includes: [\".\"]\n}})\n",
        remote_repo.string()));

    (void)cfire::fs::write_text(app_dir / "main.cf",
        "package main\n\nimport cpp \"calc.h\"\n\nfn main() {\n    res := cpp::add(15, 25)\n    println(\"sum={}\", res)\n}\n");

    // 1. Run cfire build/run and verify remote checkout + build.lock generation
    std::string run_cmd = std::format("cd \"{}\" && \"{}\"/cfire run main.cf",
        app_dir.string(), std::filesystem::current_path().string());
    std::string run_out = cfire::bridge::run_command_capture(run_cmd, rc);
    require(rc == 0, std::format("cfire run with Git remote dep exit 0 (BS-EXT-01): {}", run_out));
    require(run_out.contains("sum=40"), std::format("app output contains sum=40 (BS-EXT-01): {}", run_out));

    auto lock_file = app_dir / "build.lock";
    require(std::filesystem::exists(lock_file), "build.lock exists after run (BS-EXT-01)");
    auto lock_txt = cfire::build::read_file_text(lock_file);
    require(lock_txt.has_value(), "build.lock is readable (BS-EXT-01)");
    require(lock_txt->contains("\"packages\""), "build.lock contains packages (BS-EXT-01)");
    require(lock_txt->contains("\"Calc\""), "build.lock contains Calc (BS-EXT-01)");
    require(lock_txt->contains("\"commit\""), "build.lock contains commit hash (BS-EXT-01)");
    require(lock_txt->contains("\"driver\": \"CC\""), "build.lock contains driver CC (BS-EXT-01)");

    // 2. Vendor packages for offline air-gapped usage
    std::string vendor_cmd = std::format("cd \"{}\" && \"{}\"/cfire vendor",
        app_dir.string(), std::filesystem::current_path().string());
    std::string vendor_out = cfire::bridge::run_command_capture(vendor_cmd, rc);
    require(rc == 0, std::format("cfire vendor exit 0 (BS-EXT-01): {}", vendor_out));
    require(vendor_out.contains("Successfully vendored 1 package(s)"), "vendor reports 1 package vendored (BS-EXT-01)");
    require(std::filesystem::exists(app_dir / "vendor" / "Calc" / "calc.h"), "vendored calc.h exists (BS-EXT-01)");
    require(std::filesystem::exists(app_dir / "vendor" / "Calc" / "calc.c"), "vendored calc.c exists (BS-EXT-01)");

    // 3. Delete remote repo and local cache to verify offline build works strictly from vendor/
    std::filesystem::remove_all(remote_repo);
    std::filesystem::remove_all(app_dir / ".cfire" / "cache");

    std::string offline_out = cfire::bridge::run_command_capture(run_cmd, rc);
    require(rc == 0, std::format("offline cfire run exit 0 (BS-EXT-01): {}", offline_out));
    require(offline_out.contains("sum=40"), std::format("offline output contains sum=40 (BS-EXT-01): {}", offline_out));

    std::println("--- PASS: test_cfire_remote_vendor (BS-EXT-01)");
}

void test_cfire_cross_compile(const std::filesystem::path& rt_dir) {
    (void)rt_dir;
    ScopedTempDir tmp("cfire_cross");
    auto main_cf = tmp.file("main.cf", "package main\n\nfn main() {\n    println(\"cross target test ok\")\n}\n");

    // 1. Compile with target flag
    auto out_bin = tmp.path / "cross_bin";
    run_cfire(std::format("build \"{}\" -o \"{}\" --target arm64-apple-darwin", main_cf.string(), out_bin.string()))
        .assert_ok("cfire build --target arm64-apple-darwin exit 0 (BS-EXT-02)")
        .assert_contains("target=arm64-apple-darwin", "output reports target=arm64-apple-darwin (BS-EXT-02)");

    // 2. Check emit-cpp with target
    run_cfire(std::format("emit-cpp \"{}\" --target x86_64-apple-darwin", main_cf.string()))
        .assert_ok("cfire emit-cpp --target exit 0 (BS-EXT-02)")
        .assert_contains("auto main(", "emit-cpp contains main (BS-EXT-02)");

    std::println("--- PASS: test_cfire_cross_compile (BS-EXT-02)");
}

void test_crypto_hmac_and_aes(const std::filesystem::path& rt_dir) {
    (void)rt_dir;
    // 1. In-process C++ unit tests
    // hmac::equal
    require(cfire::hmac::equal("abcdef123456", "abcdef123456"), "hmac::equal identical strings");
    require(!cfire::hmac::equal("abcdef123456", "abcdef123457"), "hmac::equal different strings");
    require(!cfire::hmac::equal("abcdef123456", "abcdef"), "hmac::equal different lengths");

    std::string key_str = "01234567890123456789012345678901"; // 32 bytes
    std::string iv_str  = "1234567890123456";                 // 16 bytes
    std::string pt_str  = "Hello Systems Programming in Cfire!";

    // AES-256 CTR
    auto ct_res = cfire::aes::encrypt_ctr(key_str, iv_str, pt_str);
    require(ct_res.has_value(), "aes::encrypt_ctr success");
    auto dec_ctr = cfire::aes::decrypt_ctr(key_str, iv_str, *ct_res);
    require(dec_ctr.has_value(), "aes::decrypt_ctr success");
    std::string dec_ctr_str(reinterpret_cast<const char*>(dec_ctr->data()), dec_ctr->size());
    require(dec_ctr_str == pt_str, "aes::decrypt_ctr roundtrip match");

    // AES-256 GCM
    std::string nonce_str = "123456789012"; // 12 bytes
    std::string aad_str = "auth_data_v1";
    auto gcm_res = cfire::aes::encrypt_gcm(key_str, nonce_str, pt_str, aad_str);
    require(gcm_res.has_value(), "aes::encrypt_gcm success");
    require(gcm_res->tag.size() == 16, "aes::encrypt_gcm tag size 16");

    auto dec_gcm_str = cfire::aes::decrypt_gcm_str(key_str, nonce_str, gcm_res->ciphertext, gcm_res->tag, aad_str);
    require(dec_gcm_str.has_value(), "aes::decrypt_gcm_str success");
    require(*dec_gcm_str == pt_str, "aes::decrypt_gcm_str roundtrip match");

    // GCM Tampering detection
    auto tampered_tag = gcm_res->tag;
    tampered_tag[0] = static_cast<std::byte>(static_cast<unsigned char>(tampered_tag[0]) ^ 0x01);
    auto tamper_res = cfire::aes::decrypt_gcm_str(key_str, nonce_str, gcm_res->ciphertext, tampered_tag, aad_str);
    require(!tamper_res.has_value(), "aes::decrypt_gcm rejects corrupted tag");

    auto tampered_ct = gcm_res->ciphertext;
    tampered_ct[0] = static_cast<std::byte>(static_cast<unsigned char>(tampered_ct[0]) ^ 0x01);
    auto tamper_ct_res = cfire::aes::decrypt_gcm_str(key_str, nonce_str, tampered_ct, gcm_res->tag, aad_str);
    require(!tamper_ct_res.has_value(), "aes::decrypt_gcm rejects corrupted ciphertext");

    auto tamper_aad_res = cfire::aes::decrypt_gcm_str(key_str, nonce_str, gcm_res->ciphertext, gcm_res->tag, "wrong_aad");
    require(!tamper_aad_res.has_value(), "aes::decrypt_gcm rejects mismatched AAD");

    // 2. End-to-end cfire run invocation
    run_cfire_source(R"(package main

import "core:crypto/aes"
import "core:crypto/hmac"

fn main() {
    key := "01234567890123456789012345678901"
    nonce := "012345678901"
    msg := "cfire aes-gcm authenticated encryption secret"
    aad := "meta-header"

    res := try aes.encrypt_gcm(key, nonce, msg, aad)
    dec := try aes.decrypt_gcm_str(key, nonce, res.ciphertext, res.tag, aad)
    println("decrypted={}", dec)

    eq1 := hmac.equal("hash123456", "hash123456")
    eq2 := hmac.equal("hash123456", "hash999999")
    println("eq1={} eq2={}", eq1, eq2)
}
)", "cfire_crypto")
        .assert_ok("cfire run crypto main.cf exit 0 (LIB-EXT-01)")
        .assert_contains("decrypted=cfire aes-gcm authenticated encryption secret", "output contains decrypted secret (LIB-EXT-01)")
        .assert_contains("eq1=true eq2=false", "output contains eq1=true eq2=false (LIB-EXT-01)");

    std::println("--- PASS: test_crypto_hmac_and_aes (LIB-EXT-01)");
}

void test_slice_pattern_matching(const std::filesystem::path& rt_dir) {
    (void)rt_dir;
    // 1. In-process AST and Codegen verification
    std::string_view cf_src = R"(package main

fn sum_slice(s: &[]i32) -> i32 {
    match s {
        [] => {
            return 0
        }
        [head, ..tail] => {
            return head + sum_slice(tail)
        }
    }
    return 0
}

fn inspect(s: &[]str) -> str {
    match s {
        [] => {
            return "empty"
        }
        [single] => {
            return "single"
        }
        [a, b] => {
            return "pair"
        }
        [first, ..middle, last] => {
            return "sandwich"
        }
    }
    return "unknown"
}

fn main() {
}
)";

    auto ast = cfire::frontend::parse_source("test_slice.cf", cf_src);
    require(ast.has_value(), "parse_source with slice patterns (LD-EXT-01)");
    if (ast) {
        auto checked = cfire::frontend::analyze(std::move(*ast));
        require(checked.has_value(), "analyze slice patterns (LD-EXT-01)");
        if (checked) {
            std::string cpp = cfire::frontend::emit_cpp(*checked);
            require(cpp.contains("cfire::slice(_target, 1, _target.size())"),
                "codegen lowers tail destructuring with cfire::slice (LD-EXT-01)");
            require(cpp.contains("_target.size() == 0"),
                "codegen lowers empty slice pattern condition (LD-EXT-01)");
            require(cpp.contains("_target.size() == 2"),
                "codegen lowers exact length slice pattern condition (LD-EXT-01)");
        }
    }

    // 2. End-to-end cfire run invocation
    run_cfire_source(R"(package main

fn sum_slice(s: &[]i32) -> i32 {
    match s {
        [] => {
            return 0
        }
        [head, ..tail] => {
            return head + sum_slice(tail)
        }
    }
    return 0
}

fn classify(s: &[]str) -> str {
    match s {
        [] => {
            return "empty"
        }
        [single] => {
            return "single:" + single
        }
        [a, b] => {
            return "pair:" + a + "+" + b
        }
        [first, ..mid, last] => {
            return "sandwich:" + first + ".." + last
        }
    }
    return "other"
}

fn main() {
    nums: []i32 = [10, 20, 30, 40]
    println("sum={}", sum_slice(nums))

    e: []str = []
    o: []str = ["solo"]
    p: []str = ["cat", "dog"]
    m: []str = ["alpha", "beta", "gamma", "omega"]

    println("c0={}", classify(e))
    println("c1={}", classify(o))
    println("c2={}", classify(p))
    println("c3={}", classify(m))
}
)", "cfire_slice")
        .assert_ok("cfire run slice destructuring exit 0 (LD-EXT-01)")
        .assert_contains("sum=100", "slice recursive sum output (LD-EXT-01)")
        .assert_contains("c0=empty", "empty slice pattern output (LD-EXT-01)")
        .assert_contains("c1=single:solo", "single slice pattern output (LD-EXT-01)")
        .assert_contains("c2=pair:cat+dog", "pair slice pattern output (LD-EXT-01)")
        .assert_contains("c3=sandwich:alpha..omega", "sandwich slice pattern output (LD-EXT-01)");

    std::println("--- PASS: test_slice_pattern_matching (LD-EXT-01)");
}

void test_sync_context([[maybe_unused]] const std::filesystem::path& rt_dir) {
    // 1. C++ runtime assertions
    auto bg = cfire::sync::context::background();
    require(!bg.is_cancelled(), "background context is not cancelled");
    require(!bg.done(), "background context done() is false");

    // with_cancel manual cancellation
    auto [child1, cancel1] = cfire::sync::context::with_cancel(bg);
    require(!child1.is_cancelled(), "child context not cancelled initially");
    cancel1("request aborted");
    require(child1.is_cancelled(), "child context cancelled after cancel()");
    require(child1.reason() == "request aborted", "child context records cancellation reason");
    require(!bg.is_cancelled(), "parent background context remains uncancelled");

    // Parent cancellation cascading to multiple children
    auto [parent_ctx, parent_cancel] = cfire::sync::context::with_cancel(bg);
    auto [sub_child1, _c1] = cfire::sync::context::with_cancel(parent_ctx);
    auto [sub_child2, _c2] = cfire::sync::context::with_cancel(parent_ctx);
    require(!sub_child1.is_cancelled() && !sub_child2.is_cancelled(), "sub-children active");
    parent_cancel("parent terminated");
    require(sub_child1.is_cancelled(), "sub_child1 cancelled via cascade");
    require(sub_child2.is_cancelled(), "sub_child2 cancelled via cascade");
    require(sub_child1.reason() == "parent terminated", "sub_child1 inherited parent reason");

    // with_timeout automatic expiration
    auto [timeout_ctx, timeout_cancel] = cfire::sync::context::with_timeout(bg, 50);
    require(!timeout_ctx.is_cancelled(), "timeout context active initially");
    bool timeout_w = timeout_ctx.wait_for(150);
    require(timeout_w, "timeout_ctx wait_for unblocks on deadline expiration");
    require(timeout_ctx.is_cancelled(), "timeout_ctx is cancelled after expiration");
    require(timeout_ctx.reason() == "deadline exceeded", "timeout_ctx reason is deadline exceeded");

    // sleep_ctx interrupted by cancellation
    auto [sleep_ctx_handle, sleep_cancel] = cfire::sync::context::with_cancel(bg);
    std::thread cancel_thread([sleep_cancel]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        sleep_cancel("interrupted");
    });
    auto t0 = std::chrono::steady_clock::now();
    bool sleep_res = cfire::time::sleep_ctx(sleep_ctx_handle, 1000);
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    cancel_thread.join();
    require(!sleep_res, "sleep_ctx returns false when cancelled early");
    require(elapsed_ms < 500, "sleep_ctx unblocks promptly upon cancellation");

    // Channel.recv_ctx unblocks immediately on cancellation
    cfire::sync::Channel<int> ch;
    auto [ch_ctx, ch_cancel] = cfire::sync::context::with_cancel(bg);
    std::atomic<bool> unblocked = false;
    std::string err_msg;
    std::thread recv_thread([&ch, &ch_ctx, &unblocked, &err_msg]() {
        auto res = ch.recv_ctx(ch_ctx);
        unblocked = true;
        if (!res) err_msg = std::string(res.error().message());
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    require(!unblocked, "receiver blocks on empty channel before cancellation");
    ch_cancel("client disconnected");
    recv_thread.join();
    require(unblocked, "receiver unblocks promptly when context cancelled");
    require(err_msg.contains("context cancelled: client disconnected"), "recv_ctx returns error with cancellation reason");

    // 2. End-to-end .cf script execution
    run_cfire_source(R"(package main

import "core:sync/context"
import "core:sync"
import "core:time"

fn main() {
    bg := context.background()
    (ctx1, cancel1) := context.with_cancel(bg)
    println("ctx1.done={}", ctx1.done())
    cancel1("stop")
    println("ctx1.cancelled={}", ctx1.is_cancelled())
    println("ctx1.reason={}", ctx1.reason())

    (tctx, tcancel) := context.with_timeout(bg, 30)
    time.sleep_ms(60)
    println("tctx.done={}", tctx.done())
    println("tctx.reason={}", tctx.reason())
}
)", "cfire_ctx")
        .assert_ok("cfire run context exit 0 (LIB-EXT-04)")
        .assert_contains("ctx1.done=false", "ctx1 initial state (LIB-EXT-04)")
        .assert_contains("ctx1.cancelled=true", "ctx1 cancelled state (LIB-EXT-04)")
        .assert_contains("ctx1.reason=stop", "ctx1 reason output (LIB-EXT-04)")
        .assert_contains("tctx.done=true", "tctx expired state (LIB-EXT-04)")
        .assert_contains("tctx.reason=deadline exceeded", "tctx reason output (LIB-EXT-04)");

    std::println("--- PASS: test_sync_context (LIB-EXT-04)");
}

void test_net_websocket([[maybe_unused]] const std::filesystem::path& rt_dir) {
    // 1. C++ runtime assertions
    // RFC 6455 Handshake vector test:
    std::string rfc_key = "dGhlIHNhbXBsZSBub25jZQ==";
    std::string accept = cfire::websocket::compute_accept_key(rfc_key);
    require(accept == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", "RFC 6455 Sec-WebSocket-Accept test vector match");

    // Loopback pair test
    auto pair_res = cfire::websocket::pair();
    require(pair_res.has_value(), "cfire::websocket::pair() succeeds");
    auto [client, server] = std::move(*pair_res);
    require(client.is_open() && server.is_open(), "both sockets open");

    // Text frame client -> server
    auto send_txt = client.send_text("Hello RFC 6455 WebSockets!");
    require(send_txt.has_value(), "client.send_text succeeds");
    auto recv_txt = server.recv_text();
    require(recv_txt.has_value(), "server.recv_text succeeds");
    require(*recv_txt == "Hello RFC 6455 WebSockets!", "text frame content matches exactly");

    // Text frame server -> client
    auto reply_txt = server.send_text("Server Ack");
    require(reply_txt.has_value(), "server.send_text succeeds");
    auto got_reply = client.recv_text();
    require(got_reply.has_value() && *got_reply == "Server Ack", "client receives unmasked server text frame");

    // Binary frame client -> server
    std::vector<std::uint8_t> bin_data = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04};
    auto send_bin = client.send_binary(bin_data);
    require(send_bin.has_value(), "client.send_binary succeeds");
    auto recv_bin = server.recv_binary();
    require(recv_bin.has_value() && *recv_bin == bin_data, "server receives binary frame matching payload");

    // Ping / Pong handling
    auto ping_res = client.ping("ping-heartbeat");
    require(ping_res.has_value(), "client.ping succeeds");
    auto srv_post_ping = server.send_text("alive");
    require(srv_post_ping.has_value(), "server sends alive");
    auto cl_got_alive = client.recv_text();
    require(cl_got_alive.has_value() && *cl_got_alive == "alive", "client receives text after ping");

    // Clean close
    client.close(1000, "done");
    require(!client.is_open(), "client closed");
    auto srv_recv_closed = server.recv_text();
    require(!srv_recv_closed.has_value(), "server recognizes closed peer");

    // 2. End-to-end .cf script execution
    run_cfire_source(R"(package main

import "core:net/websocket"

fn main() {
    (mut client, mut server) := try websocket.pair()
    try client.send_text("cf-websocket-hello")
    msg := try server.recv_text()
    println("ws.server.msg={}", msg)

    try server.send_text("cf-websocket-ack")
    ack := try client.recv_text()
    println("ws.client.ack={}", ack)
    client.close()
}
)", "cfire_ws")
        .assert_ok("cfire run websocket exit 0 (LIB-EXT-02)")
        .assert_contains("ws.server.msg=cf-websocket-hello", "websocket server receive output (LIB-EXT-02)")
        .assert_contains("ws.client.ack=cf-websocket-ack", "websocket client ack output (LIB-EXT-02)");

    std::println("--- PASS: test_net_websocket (LIB-EXT-02)");
}

void test_net_http_and_router(const std::filesystem::path& /*rt_dir*/) {
    std::println("=== RUN   test_net_http_and_router (HTTP Server, Parser, Response Writer & Router)");

    // 1. Request Parser Testing
    std::string raw_get = "GET /api/users/42?sort=desc&limit=10 HTTP/1.1\r\n"
                          "Host: 127.0.0.1:8080\r\n"
                          "User-Agent: test-client/1.0\r\n"
                          "Accept: application/json\r\n\r\n";
    auto req_res = cfire::http::parse_request(raw_get);
    require(req_res.has_value(), "parse_request succeeds for valid GET");
    if (req_res.has_value()) {
        const auto& req = *req_res;
        require(req.method == "GET", "parsed method is GET");
        require(req.path == "/api/users/42", "parsed path is /api/users/42");
        require(req.raw_query == "sort=desc&limit=10", "parsed raw query");
        require(req.query("sort") == "desc", "parsed query param sort");
        require(req.query("limit") == "10", "parsed query param limit");
        require(req.query("missing", "default") == "default", "query fallback value");
        require(req.header("host") == "127.0.0.1:8080", "header lookup lower-case");
        require(req.header("Host") == "127.0.0.1:8080", "header lookup case-insensitive");
        require(req.header("Accept") == "application/json", "header lookup Accept");
    }

    std::string raw_post = "POST /submit HTTP/1.1\r\n"
                           "Content-Type: application/json\r\n"
                           "Content-Length: 15\r\n\r\n"
                           "{\"status\":\"ok\"}";
    auto post_res = cfire::http::parse_request(raw_post);
    require(post_res.has_value(), "parse_request succeeds for valid POST with body");
    if (post_res.has_value()) {
        require(post_res->method == "POST", "parsed method is POST");
        require(post_res->path == "/submit", "parsed path is /submit");
        require(post_res->body == "{\"status\":\"ok\"}", "parsed body matches Content-Length");
    }

    auto bad_res = cfire::http::parse_request("invalid without crlfcrlf");
    require(!bad_res.has_value(), "parse_request returns error for malformed request");

    // 2. Response & Wire Formatting Testing
    auto resp_txt = cfire::http::Response::text(200, "hello world");
    require(resp_txt.status_code == 200, "Response::text status code 200");
    require(resp_txt.header("content-type").contains("text/plain"), "Response::text content-type");
    require(resp_txt.body == "hello world", "Response::text body");

    auto resp_json = cfire::http::Response::json(201, "{\"id\":99}");
    require(resp_json.status_code == 201, "Response::json status code 201");
    require(resp_json.header("content-type") == "application/json", "Response::json content-type");

    auto resp_wire = resp_json.to_http_wire();
    require(resp_wire.starts_with("HTTP/1.1 201 Created\r\n"), "to_http_wire status line");
    require(resp_wire.contains("Content-Length: 9\r\n"), "to_http_wire Content-Length");
    require(resp_wire.ends_with("{\"id\":99}"), "to_http_wire payload suffix");

    // 3. Independent Router Testing (pure in-memory dispatch, zero sockets)
    auto router = cfire::http::make_router();

    router.get("/health", [](const cfire::http::Request&) {
        return cfire::http::Response::json(200, "{\"healthy\":true}");
    });

    router.get("/users/:id", [](const cfire::http::Request& req) {
        return cfire::http::Response::text(200, "user_id=" + req.param("id"));
    });

    router.get("/orgs/:org/repos/:repo", [](const cfire::http::Request& req) {
        return cfire::http::Response::text(200, req.param("org") + "/" + req.param("repo"));
    });

    router.get("/static/*filepath", [](const cfire::http::Request& req) {
        return cfire::http::Response::text(200, "file=" + req.param("filepath"));
    });

    router.post("/items", [](const cfire::http::Request& req) {
        return cfire::http::Response::json(201, "created:" + req.body);
    });

    // Exact match
    cfire::http::Request r1{.method = "GET", .path = "/health"};
    auto out1 = router.dispatch(r1);
    require(out1.status_code == 200 && out1.body == "{\"healthy\":true}", "router exact match /health");

    // Parameter match :id
    cfire::http::Request r2{.method = "GET", .path = "/users/456"};
    auto out2 = router.dispatch(r2);
    require(out2.status_code == 200 && out2.body == "user_id=456", "router parameter match :id");

    // Multi-parameter match :org and :repo
    cfire::http::Request r3{.method = "GET", .path = "/orgs/google/repos/cfire"};
    auto out3 = router.dispatch(r3);
    require(out3.status_code == 200 && out3.body == "google/cfire", "router multi-param match :org/:repo");

    // Wildcard match *filepath
    cfire::http::Request r4{.method = "GET", .path = "/static/assets/css/main.css"};
    auto out4 = router.dispatch(r4);
    require(out4.status_code == 200 && out4.body == "file=assets/css/main.css", "router wildcard match *filepath");

    // POST dispatch
    cfire::http::Request r5{.method = "POST", .path = "/items", .body = "gadget"};
    auto out5 = router.dispatch(r5);
    require(out5.status_code == 201 && out5.body == "created:gadget", "router POST dispatch with body");

    // 405 Method Not Allowed (path matches POST /items but method is GET)
    cfire::http::Request r6{.method = "GET", .path = "/items"};
    auto out6 = router.dispatch(r6);
    require(out6.status_code == 405, "router returns 405 Method Not Allowed for unmatched verb");
    require(out6.header("allow") == "POST", "router sets Allow header for 405 response");

    // 404 Not Found
    cfire::http::Request r7{.method = "GET", .path = "/nonexistent"};
    auto out7 = router.dispatch(r7);
    require(out7.status_code == 404, "router returns 404 for unmatched path");

    // Middleware test
    auto mw_router = cfire::http::make_router();
    mw_router.use([](cfire::http::Request& req) -> std::optional<cfire::http::Response> {
        if (req.header("authorization") != "Bearer secret") {
            return cfire::http::Response::text(401, "Unauthorized");
        }
        return std::nullopt;
    });
    mw_router.get("/protected", [](const cfire::http::Request&) {
        return cfire::http::Response::text(200, "SecretData");
    });

    cfire::http::Request unauth_req{.method = "GET", .path = "/protected"};
    require(mw_router.dispatch(unauth_req).status_code == 401, "middleware rejects missing auth");

    cfire::http::Request auth_req{.method = "GET", .path = "/protected", .headers = {{"authorization", "Bearer secret"}}};
    require(mw_router.dispatch(auth_req).status_code == 200, "middleware allows authenticated request");

    // 4. Server + Router Loopback End-to-End
    auto srv_res = cfire::http::make_server(0);
    require(srv_res.has_value(), "make_server creates listener on ephemeral port");
    if (srv_res.has_value()) {
        auto port = srv_res->port();
        srv_res->serve_in_background(router);

        std::string target_get = std::format("http://127.0.0.1:{}/users/888", port);
        auto client_get = cfire::http::get(target_get);
        require(client_get.has_value(), "client GET against live server with router succeeds");
        if (client_get.has_value()) {
            require(client_get->status_code == 200, "live GET status code 200");
            require(client_get->body == "user_id=888", "live GET received router output");
        }

        std::string target_post = std::format("http://127.0.0.1:{}/items", port);
        auto client_post = cfire::http::post(target_post, "gizmo");
        require(client_post.has_value(), "client POST against live server succeeds");
        if (client_post.has_value()) {
            require(client_post->status_code == 201, "live POST status code 201");
            require(client_post->body == "created:gizmo", "live POST received router output");
        }

        srv_res->stop();
    }

    // 5. End-to-end .cf script execution
    run_cfire_source(R"(package main

import "core:net/http"

fn main() {
    mut router := http.make_router()
    router.get("/ping", |req| {
        return http.Response.text(200, "pong")
    })
    router.get("/users/:id", |req| {
        return http.Response.text(200, "uid:" + req.param("id"))
    })

    // 1. In-memory standalone dispatch test
    req1 := http.Request{ method: "GET", path: "/ping" }
    res1 := router.dispatch(req1)
    println("cf.router.ping={}", res1.body)

    req2 := http.Request{ method: "GET", path: "/users/99" }
    res2 := router.dispatch(req2)
    println("cf.router.user={}", res2.body)
}
)", "cfire_http_router")
        .assert_ok("cfire run http router exit 0")
        .assert_contains("cf.router.ping=pong", "http router ping dispatch output")
        .assert_contains("cf.router.user=uid:99", "http router user parameter dispatch output");

    std::println("--- PASS: test_net_http_and_router (HTTP Server & Router)");
}

void test_selfhost_lexer_parity_and_benchmark(const std::filesystem::path& rt_dir) {
    (void)rt_dir;
    std::println("=== RUN   test_selfhost_lexer_parity_and_benchmark (Self-Hosted Lexer Dogfooding & Measurable Parity)");

    // 1. Files to test
    std::vector<std::string> test_files = {
        "examples/01-hello-core/main.cf",
        "examples/02-log-compressor/main.cf",
        "examples/03-pkg-cache/main.cf",
        "examples/04-async-workers/main.cf",
        "examples/05-cmake-interop/main.cf",
        "examples/06-raylib-viz/main.cf",
        "examples/07-types-match-map/main.cf",
        "examples/08-multi-pkg-cc-build/main.cf",
        "examples/09-stdlib-test-pointers/main.cf",
        "examples/10-cmake-and-make-deps/main.cf",
        "examples/11-http-server-router/main.cf",
        "src/lexer.cf",
    };

    auto check_parity = [](std::string_view name, std::string_view src) {
        auto cxx_res = cfire::frontend::tokenize_cxx(name, src);
        auto cf_res = cfire::frontend::selfhost_tokenize_adapter(name, src);

        require(cxx_res.has_value() == cf_res.has_value(),
            std::format("{}: outcome match (cxx={}, cf={})", name, cxx_res.has_value(), cf_res.has_value()));
        if (!cxx_res.has_value() && !cf_res.has_value()) {
            require(cxx_res.error() == cf_res.error(),
                std::format("{}: error message match ('{}' vs '{}')", name, cxx_res.error(), cf_res.error()));
            return;
        }
        if (!cxx_res || !cf_res) return;

        const auto& cxx_toks = *cxx_res;
        const auto& cf_toks = *cf_res;
        require(cxx_toks.size() == cf_toks.size(),
            std::format("{}: token count match (cxx={}, cf={})", name, cxx_toks.size(), cf_toks.size()));

        std::size_t min_sz = std::min(cxx_toks.size(), cf_toks.size());
        for (std::size_t i = 0; i < min_sz; ++i) {
            const auto& t1 = cxx_toks[i];
            const auto& t2 = cf_toks[i];
            if (t1.kind != t2.kind || t1.lexeme != t2.lexeme || t1.line != t2.line || t1.col != t2.col) {
                require(false, std::format("{}[{}]: token mismatch: C++({} '{}' L{}:C{}) vs cfire({} '{}' L{}:C{})",
                    name, i,
                    cfire::frontend::token_kind_name(t1.kind), t1.lexeme, t1.line, t1.col,
                    cfire::frontend::token_kind_name(t2.kind), t2.lexeme, t2.line, t2.col));
                break;
            }
        }
    };

    // Parity check across all real repository files
    std::string large_workload;
    for (const auto& path : test_files) {
        auto content = cfire::build::read_file_text(path);
        if (content.has_value()) {
            check_parity(path, *content);
            large_workload += *content;
            large_workload += "\n\n";
        }
    }

    // Parity check on synthetic edge-case snippets
    check_parity("synthetic_escape", R"cf(
        str_val := "hello\nworld\t\r\"escaped\"\\"
        raw_val := `multiline
        raw "string" with {braces}`
    )cf");

    check_parity("synthetic_ops_and_nums", R"cf(
        a := 10 + 20 - 3 * 4 / 5 % 2
        f := 3.14159 + 0.005 + 1_000_000
        cond := (a == 10 && f != 0.0) || (a <= 5 && f >= 2.0)
        range := arr[1..10]
        assign := x := y :: z -> w => q += 1 -= 2
        symbols := () {} [] : , . = + - * / % & | ! ? @ < > ;
    )cf");

    check_parity("synthetic_cpp_block", R"cf(
        cpp {
            int a = 1;
            for (int i = 0; i < 10; ++i) {
                if (i == 5) { break; }
            }
        }
        word := "regular_ident"
    )cf");

    check_parity("synthetic_unterminated_str", "unterminated := \"hello world");
    check_parity("synthetic_unterminated_raw", "raw := `unterminated");
    check_parity("synthetic_unexpected_char", "bad := ~");

    std::println("  [PASS] 100% token-for-token parity verified across all files & edge cases");

    // 2. High-Precision Performance Benchmark
    // Replicate workload to ~1.5 MB of code
    std::string benchmark_payload;
    while (benchmark_payload.size() < 1'500'000) {
        benchmark_payload += large_workload;
    }

    // Warm-up
    (void)cfire::frontend::tokenize_cxx("warmup.cf", benchmark_payload);
    (void)cfire::frontend::selfhost_tokenize_adapter("warmup.cf", benchmark_payload);

    constexpr int kIters = 20;

    // Benchmark C++ Lexer
    auto t0_cxx = std::chrono::high_resolution_clock::now();
    std::size_t total_cxx_tokens = 0;
    for (int i = 0; i < kIters; ++i) {
        auto res = cfire::frontend::tokenize_cxx("bench.cf", benchmark_payload);
        total_cxx_tokens += res->size();
    }
    auto t1_cxx = std::chrono::high_resolution_clock::now();
    double ms_cxx = std::chrono::duration<double, std::milli>(t1_cxx - t0_cxx).count();
    double total_bytes = static_cast<double>(benchmark_payload.size()) * kIters;
    double mbs_cxx = (total_bytes / (1024.0 * 1024.0)) / (ms_cxx / 1000.0);
    double mtokens_cxx = (static_cast<double>(total_cxx_tokens) / 1'000'000.0) / (ms_cxx / 1000.0);

    // Benchmark Self-Hosted cfire Lexer
    auto t0_cf = std::chrono::high_resolution_clock::now();
    std::size_t total_cf_tokens = 0;
    for (int i = 0; i < kIters; ++i) {
        auto res = cfire::frontend::selfhost_tokenize_adapter("bench.cf", benchmark_payload);
        total_cf_tokens += res->size();
    }
    auto t1_cf = std::chrono::high_resolution_clock::now();
    double ms_cf = std::chrono::duration<double, std::milli>(t1_cf - t0_cf).count();
    double mbs_cf = (total_bytes / (1024.0 * 1024.0)) / (ms_cf / 1000.0);
    double mtokens_cf = (static_cast<double>(total_cf_tokens) / 1'000'000.0) / (ms_cf / 1000.0);

    double parity_pct = (mbs_cf / mbs_cxx) * 100.0;

    std::println("  [BENCHMARK] Lexer Throughput Benchmark (Payload: {:.2f} MB, {} iterations):",
        static_cast<double>(benchmark_payload.size()) / (1024.0 * 1024.0), kIters);
    std::println("    - C++ Native Lexer:        {:>7.2f} MB/s ({:.2f}M tokens/s, avg {:.2f}ms/run)",
        mbs_cxx, mtokens_cxx, ms_cxx / kIters);
    std::println("    - cfire Self-Hosted Lexer: {:>7.2f} MB/s ({:.2f}M tokens/s, avg {:.2f}ms/run)",
        mbs_cf, mtokens_cf, ms_cf / kIters);
    std::println("    - Performance Ratio:       {:>7.1f}% of native C++ speed", parity_pct);

    // Assert that the self-hosted lexer achieves at least 80% of native C++ speed
    require(mbs_cf >= 0.80 * mbs_cxx,
        std::format("Self-hosted lexer performance regression ({:.1f} MB/s vs {:.1f} MB/s C++ baseline)", mbs_cf, mbs_cxx));

    std::println("--- PASS: test_selfhost_lexer_parity_and_benchmark");
}

void test_selfhost_formatter_parity_and_benchmark(const std::filesystem::path& rt_dir) {
    (void)rt_dir;
    std::println("=== RUN   test_selfhost_formatter_parity_and_benchmark (Self-Hosted Formatter Dogfooding & Measurable Parity)");

    // 1. Files to test
    std::vector<std::string> test_files = {
        "examples/01-hello-core/main.cf",
        "examples/02-log-compressor/main.cf",
        "examples/03-pkg-cache/main.cf",
        "examples/04-async-workers/main.cf",
        "examples/05-cmake-interop/main.cf",
        "examples/06-raylib-viz/main.cf",
        "examples/07-types-match-map/main.cf",
        "examples/08-multi-pkg-cc-build/main.cf",
        "examples/09-stdlib-test-pointers/main.cf",
        "examples/10-cmake-and-make-deps/main.cf",
        "examples/11-http-server-router/main.cf",
        "src/lexer.cf",
        "src/formatter.cf",
    };

    auto check_parity_and_idempotency = [](std::string_view name, std::string_view src) {
        auto cxx_res = cfire::fmt::format_source_cxx(src, name);
        auto cf_res = cfire::fmt::selfhost_format_source(src, name);

        require(cxx_res.has_value() == cf_res.has_value(),
            std::format("{}: outcome match (cxx={}, cf={})", name, cxx_res.has_value(), cf_res.has_value()));
        if (!cxx_res.has_value() && !cf_res.has_value()) {
            require(cxx_res.error() == cf_res.error(),
                std::format("{}: error match ('{}' vs '{}')", name, cxx_res.error(), cf_res.error()));
            return;
        }
        if (!cxx_res || !cf_res) return;

        // Character-for-character parity check
        require(*cxx_res == *cf_res,
            std::format("{}: character parity match between C++ and cfire formatters", name));

        // Idempotency check: fmt(fmt(code)) == fmt(code)
        auto cf_idem = cfire::fmt::selfhost_format_source(*cf_res, name);
        require(cf_idem.has_value() && *cf_idem == *cf_res,
            std::format("{}: self-hosted formatter idempotency check", name));
    };

    std::string large_workload;
    for (const auto& path : test_files) {
        auto content = cfire::build::read_file_text(path);
        if (content.has_value()) {
            check_parity_and_idempotency(path, *content);
            large_workload += *content;
            large_workload += "\n\n";
        }
    }

    // Synthetic edge cases
    check_parity_and_idempotency("synthetic_struct_align", R"cf(
package demo

type Config struct {
id: i32 `json:"id"`
database_url: str `json:"db_url"` // primary connection string
timeout_ms: u64 `json:"timeout"`
// Internal flags
is_active: bool
}

fn test_fn(c: Config) -> bool {
return c.is_active
}
)cf");

    check_parity_and_idempotency("synthetic_raw_strings_and_comments", R"cf(
package demo

fn complex_blocks() {
raw := `first line
    second line with {braces}
third line`
// comment with unbalanced { and ( and [
if true {
a := 10
}
}
)cf");

    std::println("  [PASS] 100% character parity & idempotency verified across all files & edge cases");

    // 2. High-Precision Performance Benchmark
    std::string benchmark_payload;
    while (benchmark_payload.size() < 1'200'000) {
        benchmark_payload += large_workload;
    }

    // Warm-up
    (void)cfire::fmt::format_source_cxx(benchmark_payload, "warmup.cf");
    (void)cfire::fmt::selfhost_format_source(benchmark_payload, "warmup.cf");

    constexpr int kIters = 10;

    // Benchmark C++ Formatter
    auto t0_cxx = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < kIters; ++i) {
        auto res = cfire::fmt::format_source_cxx(benchmark_payload, "bench.cf");
        (void)res;
    }
    auto t1_cxx = std::chrono::high_resolution_clock::now();
    double ms_cxx = std::chrono::duration<double, std::milli>(t1_cxx - t0_cxx).count();
    double total_bytes = static_cast<double>(benchmark_payload.size()) * kIters;
    double mbs_cxx = (total_bytes / (1024.0 * 1024.0)) / (ms_cxx / 1000.0);

    // Benchmark Self-Hosted cfire Formatter
    auto t0_cf = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < kIters; ++i) {
        auto res = cfire::fmt::selfhost_format_source(benchmark_payload, "bench.cf");
        (void)res;
    }
    auto t1_cf = std::chrono::high_resolution_clock::now();
    double ms_cf = std::chrono::duration<double, std::milli>(t1_cf - t0_cf).count();
    double mbs_cf = (total_bytes / (1024.0 * 1024.0)) / (ms_cf / 1000.0);

    double parity_pct = (mbs_cf / mbs_cxx) * 100.0;

    std::println("  [BENCHMARK] Formatter Throughput Benchmark (Payload: {:.2f} MB, {} iterations):",
        static_cast<double>(benchmark_payload.size()) / (1024.0 * 1024.0), kIters);
    std::println("    - C++ Native Formatter:        {:>7.2f} MB/s (avg {:.2f}ms/run)",
        mbs_cxx, ms_cxx / kIters);
    std::println("    - cfire Self-Hosted Formatter: {:>7.2f} MB/s (avg {:.2f}ms/run)",
        mbs_cf, ms_cf / kIters);
    std::println("    - Performance Ratio:           {:>7.1f}% of native C++ speed", parity_pct);

    require(mbs_cf >= 0.70 * mbs_cxx,
        std::format("Self-hosted formatter performance regression ({:.1f} MB/s vs {:.1f} MB/s C++ baseline)", mbs_cf, mbs_cxx));

    std::println("--- PASS: test_selfhost_formatter_parity_and_benchmark");
}

void test_selfhost_ast_parity_and_benchmark(const std::filesystem::path& rt_dir) {
    (void)rt_dir;
    std::println("=== RUN   test_selfhost_ast_parity_and_benchmark (Self-Hosted AST Definitions & TypeRef Serialization)");

    // 1. Structural Parity: Compare format_typeref_cf with cfire::fmt::format_type_ref
    auto check_type_parity = [](const cfire::frontend::TypeRef& cxx_t, const ::TypeRef& cf_t, std::string_view label) {
        std::string cxx_str = cfire::fmt::format_type_ref(cxx_t);
        std::string cf_str = ::format_typeref_cf(cf_t);
        require(cxx_str == cf_str,
            std::format("{}: TypeRef serialization mismatch: C++('{}') vs cfire('{}')", label, cxx_str, cf_str));
    };

    // Primitive
    {
        cfire::frontend::TypeRef cxx{.name = "i32"};
        ::TypeRef cf = ::make_typeref("i32");
        check_type_parity(cxx, cf, "primitive_i32");
    }

    // Optional and Fallible
    {
        cfire::frontend::TypeRef cxx{.name = "str", .is_fallible = true, .is_optional = true};
        ::TypeRef cf = ::make_typeref("str");
        cf.is_fallible = true;
        cf.is_optional = true;
        check_type_parity(cxx, cf, "str_opt_fallible");
    }

    // Slices & Arrays
    {
        auto elem_cxx = std::make_shared<cfire::frontend::TypeRef>(cfire::frontend::TypeRef{.name = "u8"});
        cfire::frontend::TypeRef cxx{.is_slice = true, .elem_type = elem_cxx};

        auto elem_cf = std::make_shared<::TypeRef>(::make_typeref("u8"));
        ::TypeRef cf = ::make_typeref("");
        cf.is_slice = true;
        cf.elem_type = elem_cf;
        check_type_parity(cxx, cf, "slice_u8");
    }
    {
        auto elem_cxx = std::make_shared<cfire::frontend::TypeRef>(cfire::frontend::TypeRef{.name = "f64"});
        cfire::frontend::TypeRef cxx{.is_array = true, .array_len = "16", .elem_type = elem_cxx};

        auto elem_cf = std::make_shared<::TypeRef>(::make_typeref("f64"));
        ::TypeRef cf = ::make_typeref("");
        cf.is_array = true;
        cf.array_len = "16";
        cf.elem_type = elem_cf;
        check_type_parity(cxx, cf, "array_16_f64");
    }

    // Maps
    {
        auto key_cxx = std::make_shared<cfire::frontend::TypeRef>(cfire::frontend::TypeRef{.name = "str"});
        auto val_cxx = std::make_shared<cfire::frontend::TypeRef>(cfire::frontend::TypeRef{.name = "i64"});
        cfire::frontend::TypeRef cxx{.is_map = true, .key_type = key_cxx, .elem_type = val_cxx};

        auto key_cf = std::make_shared<::TypeRef>(::make_typeref("str"));
        auto val_cf = std::make_shared<::TypeRef>(::make_typeref("i64"));
        ::TypeRef cf = ::make_typeref("");
        cf.is_map = true;
        cf.key_type = key_cf;
        cf.elem_type = val_cf;
        check_type_parity(cxx, cf, "map_str_i64");
    }

    // Pointers & Borrows
    {
        auto elem_cxx = std::make_shared<cfire::frontend::TypeRef>(cfire::frontend::TypeRef{.name = "Scene"});
        cfire::frontend::TypeRef cxx{.elem_type = elem_cxx, .is_mut_ptr = true};

        auto elem_cf = std::make_shared<::TypeRef>(::make_typeref("Scene"));
        ::TypeRef cf = ::make_typeref("");
        cf.is_mut_ptr = true;
        cf.elem_type = elem_cf;
        check_type_parity(cxx, cf, "mut_ptr_scene");
    }

    // Smart wrappers
    {
        auto elem_cxx = std::make_shared<cfire::frontend::TypeRef>(cfire::frontend::TypeRef{.name = "Config"});
        cfire::frontend::TypeRef cxx{.elem_type = elem_cxx, .smart_wrap = "own"};

        auto elem_cf = std::make_shared<::TypeRef>(::make_typeref("Config"));
        ::TypeRef cf = ::make_typeref("");
        cf.smart_wrap = "own";
        cf.elem_type = elem_cf;
        check_type_parity(cxx, cf, "own_config");
    }

    // Tuples
    {
        cfire::frontend::TypeRef cxx{.is_tuple = true, .tuple_types = {cfire::frontend::TypeRef{.name = "i32"}, cfire::frontend::TypeRef{.name = "str"}}};
        ::TypeRef cf = ::make_typeref("");
        cf.is_tuple = true;
        cf.tuple_types = {::make_typeref("i32"), ::make_typeref("str")};
        check_type_parity(cxx, cf, "tuple_i32_str");
    }

    // 2. Declaration Counting Invariant
    {
        ::FileAST ast{
            .path = "main.cf",
            .package_name = "main",
            .structs = {::StructDecl{.name = "Foo"}, ::StructDecl{.name = "Bar"}},
            .enums = {::EnumDecl{.name = "Color"}},
            .functions = {::FnDecl{.name = "main"}, ::FnDecl{.name = "helper"}},
        };
        auto [num_structs, num_enums, num_fns] = ::count_ast_declarations(ast);
        require(num_structs == 2 && num_enums == 1 && num_fns == 2,
            std::format("count_ast_declarations mismatch: ({}, {}, {}) expected (2, 1, 2)", num_structs, num_enums, num_fns));
    }

    std::println("  [PASS] 100% TypeRef serialization & AST declaration parity verified");

    // 3. High-Precision Performance Benchmark
    auto test_cf_typeref = ::make_typeref("");
    test_cf_typeref.is_map = true;
    test_cf_typeref.key_type = std::make_shared<::TypeRef>(::make_typeref("str"));
    auto val_cf = ::make_typeref("");
    val_cf.is_slice = true;
    val_cf.elem_type = std::make_shared<::TypeRef>(::make_typeref("i32"));
    val_cf.is_fallible = true;
    test_cf_typeref.elem_type = std::make_shared<::TypeRef>(val_cf);

    auto test_cxx_typeref = cfire::frontend::TypeRef{
        .is_map = true,
        .key_type = std::make_shared<cfire::frontend::TypeRef>(cfire::frontend::TypeRef{.name = "str"}),
        .elem_type = std::make_shared<cfire::frontend::TypeRef>(cfire::frontend::TypeRef{
            .is_slice = true,
            .elem_type = std::make_shared<cfire::frontend::TypeRef>(cfire::frontend::TypeRef{.name = "i32"}),
            .is_fallible = true,
        }),
    };

    constexpr int kIters = 100'000;

    // Benchmark C++ format_type_ref
    auto t0_cxx = std::chrono::high_resolution_clock::now();
    std::size_t total_cxx_len = 0;
    for (int i = 0; i < kIters; ++i) {
        auto s = cfire::fmt::format_type_ref(test_cxx_typeref);
        total_cxx_len += s.size();
    }
    (void)total_cxx_len;
    auto t1_cxx = std::chrono::high_resolution_clock::now();
    double ms_cxx = std::chrono::duration<double, std::milli>(t1_cxx - t0_cxx).count();
    double mtypes_cxx = (static_cast<double>(kIters) / 1'000'000.0) / (ms_cxx / 1000.0);

    // Benchmark cfire format_typeref_cf
    auto t0_cf = std::chrono::high_resolution_clock::now();
    std::size_t total_cf_len = 0;
    for (int i = 0; i < kIters; ++i) {
        auto s = ::format_typeref_cf(test_cf_typeref);
        total_cf_len += s.size();
    }
    (void)total_cf_len;
    auto t1_cf = std::chrono::high_resolution_clock::now();
    double ms_cf = std::chrono::duration<double, std::milli>(t1_cf - t0_cf).count();
    double mtypes_cf = (static_cast<double>(kIters) / 1'000'000.0) / (ms_cf / 1000.0);

    double parity_pct = (mtypes_cf / mtypes_cxx) * 100.0;

    std::println("  [BENCHMARK] AST TypeRef Formatting Benchmark ({} iterations):", kIters);
    std::println("    - C++ Native format_type_ref: {:>7.2f}M types/s (avg {:.4f}µs/type)",
        mtypes_cxx, (ms_cxx * 1000.0) / kIters);
    std::println("    - cfire format_typeref_cf:    {:>7.2f}M types/s (avg {:.4f}µs/type)",
        mtypes_cf, (ms_cf * 1000.0) / kIters);
    std::println("    - Performance Ratio:          {:>7.1f}% of native C++ speed", parity_pct);

    require(mtypes_cf >= 0.70 * mtypes_cxx,
        std::format("Self-hosted AST serializer performance regression ({:.1f}M vs {:.1f}M baseline)", mtypes_cf, mtypes_cxx));

    std::println("--- PASS: test_selfhost_ast_parity_and_benchmark");
}

} // namespace

auto main() -> int {
    ::signal(SIGPIPE, SIG_IGN);
    auto rt_dir = std::filesystem::current_path() / "runtime";
    test_sema_intent_and_move_diagnostics();
    test_end_to_end_canonical_examples(rt_dir);
    test_raylib_vendor_codegen_and_eject(rt_dir);
    test_formatter_lsp_and_docgen(rt_dir);
    test_p1_features(rt_dir);
    test_p2_features(rt_dir);
    test_p2_batch2_features(rt_dir);
    test_p2_batch3_features(rt_dir);
    test_p2_batch4_features(rt_dir);
    test_p2_batch5_features(rt_dir);
    test_cfire_eval(rt_dir);
    test_cfire_refactor_rename(rt_dir);
    test_cfire_remote_vendor(rt_dir);
    test_cfire_cross_compile(rt_dir);
    test_crypto_hmac_and_aes(rt_dir);
    test_slice_pattern_matching(rt_dir);
    test_sync_context(rt_dir);
    test_net_websocket(rt_dir);
    test_net_http_and_router(rt_dir);
    test_selfhost_lexer_parity_and_benchmark(rt_dir);
    test_selfhost_formatter_parity_and_benchmark(rt_dir);
    test_selfhost_ast_parity_and_benchmark(rt_dir);

    if (g_failures > 0) {
        std::println(stderr, "FAIL ({} failures)", g_failures);
        return 1;
    }
    std::println("PASS (all C++23 cfire conformance tests succeeded)");
    return 0;
}

