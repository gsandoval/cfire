#pragma once
// tests/unit/test_cf201.hpp — Tests for CF-201 (Single-Pass In-Memory check, Pure-.cf Short-Circuit & Stdin Clang Gating)

#include "../test_harness.hpp"
#include "../../src/build/cache_paths.hpp"

#include <algorithm>
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
#include <vector>

namespace test_cf201_detail {

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

inline auto lsp_frame(std::string_view body) -> std::string {
    return std::format("Content-Length: {}\r\n\r\n{}", body.size(), body);
}

inline auto is_sanitizer_build() -> bool {
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
    return true;
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
    return true;
#else
    return false;
#endif
#else
    return false;
#endif
}

} // namespace test_cf201_detail

void test_cf201_single_pass_in_memory_check(const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_cf201_single_pass_in_memory_check (CF-201)");

    // 1. needs_cpp_syntax_check classification (pure .cf vs import cpp vs nested cpp { ... })
    {
        auto pure_ast = cfire::frontend::parse_source("pure.cf",
            "package main\n"
            "import \"core:fs\"\n"
            "fn main() {\n"
            "    nums := [1, 2, 3]\n"
            "    println(\"{}\", nums.len())\n"
            "}\n");
        require(pure_ast.has_value(), "CF-201: parse pure .cf");
        require(!cfire::analysis::needs_cpp_syntax_check(*pure_ast),
            "CF-201: pure .cf must NOT require C++ syntax check");

        auto imp_cpp_ast = cfire::frontend::parse_source("cimp.cf",
            "package main\n"
            "import cpp \"math.h\"\n"
            "fn main() {}\n");
        require(imp_cpp_ast.has_value(), "CF-201: parse import cpp");
        require(cfire::analysis::needs_cpp_syntax_check(*imp_cpp_ast),
            "CF-201: import cpp must require C++ syntax check");

        auto nested_cpp_ast = cfire::frontend::parse_source("nested.cf",
            "package main\n"
            "fn run_cb(cb: fn()) { cb() }\n"
            "fn main() {\n"
            "    if true {\n"
            "        run_cb(|| {\n"
            "            cpp { int v = 42; (void)v; }\n"
            "        })\n"
            "    }\n"
            "}\n");
        require(nested_cpp_ast.has_value(), "CF-201: parse nested cpp {}");
        require(cfire::analysis::needs_cpp_syntax_check(*nested_cpp_ast),
            "CF-201: nested cpp {} inside if/closure must require C++ syntax check");
    }

    // 2. Stale-disk vs in-memory content & multi-file source_overlays regression
    {
        ScopedTempDir dir("cfire_cf201_mem");
        auto trace_file = dir.path / "clang_trace.log";
        test_cf201_detail::ScopedEnvVar trace_env("CFIRE_TRACE_CLANG_CHECK", trace_file.string());

        auto main_cf = dir.path / "main.cf";
        auto pkg_dir = dir.path / "pkg" / "mathutil";
        std::error_code ec;
        std::filesystem::create_directories(pkg_dir, ec);
        auto mod_cf = pkg_dir / "mathutil.cf";

        // 2a. Disk has broken C++ block, in-memory buffer is valid pure .cf -> 0 diagnostics, 0 Clang spawns
        require(cfire::fs::write_text(main_cf,
            "package main\nfn main() { cpp { SYNTAX@@@ERROR } }\n").has_value(),
            "CF-201: write broken disk file");
        cfire::analysis::CheckOptions opts;
        opts.runtime_dir = rt_dir;
        auto d_valid_mem = cfire::analysis::check_source(
            main_cf.string(),
            "package main\nfn main() {\n    x := 10 + 20\n    println(\"{}\", x)\n}\n",
            opts);
        require(d_valid_mem.empty(),
            std::format("CF-201: valid in-memory buffer must ignore broken disk file, got {} diag(s)", d_valid_mem.size()));
        require(!std::filesystem::exists(trace_file, ec),
            "CF-201: pure .cf in-memory check must spawn 0 Clang processes even when disk has cpp {}");
        require(!std::filesystem::exists(dir.path / ".cfire", ec),
            "CF-201: check_source must not create <proj>/.cfire");

        // 2b. Disk is valid pure .cf, in-memory buffer has broken cpp {} -> cfire/TYPE-ERROR on main.cf (never <stdin>)
        require(cfire::fs::write_text(main_cf,
            "package main\nfn main() {\n    println(\"ok\")\n}\n").has_value(),
            "CF-201: write clean disk file");
        std::filesystem::remove(trace_file, ec);
        auto d_bad_mem = cfire::analysis::check_source(
            main_cf.string(),
            "package main\nfn main() {\n    cpp {\n        int broken_cf201 = \"not_an_int\";\n    }\n}\n",
            opts);
        bool saw_type_err = false;
        bool saw_stdin_leak = false;
        for (const auto& d : d_bad_mem) {
            if (d.code == "cfire/TYPE-ERROR") saw_type_err = true;
            if (d.file.contains("<stdin>") || d.file.contains("_check.cpp")) saw_stdin_leak = true;
        }
        require(saw_type_err, "CF-201: broken in-memory cpp {} must report cfire/TYPE-ERROR even when disk is valid");
        require(!saw_stdin_leak, "CF-201: stdin Clang diagnostics must never leak <stdin> filename");
        require(!std::filesystem::exists(dir.path / ".cfire", ec),
            "CF-201: stdin Clang check must create 0 <proj>/.cfire artifacts");

        // 2c. Multi-file source_overlays: disk sibling takes str, in-memory sibling overlay takes i64 -> clean check
        require(cfire::fs::write_text(mod_cf,
            "package mathutil\nfn double_it(s: str) -> str { return s }\n").has_value(),
            "CF-201: write stale disk sibling");
        const std::string caller_src =
            "package main\nimport \"./pkg/mathutil\"\nfn main() {\n    v := mathutil.double_it(21)\n    println(\"{}\", v)\n}\n";
        require(cfire::fs::write_text(main_cf, caller_src).has_value(),
            "CF-201: write caller main.cf");
        cfire::analysis::CheckOptions ov_opts;
        ov_opts.runtime_dir = rt_dir;
        ov_opts.source_overlays[mod_cf.string()] =
            "package mathutil\nfn double_it(n: i64) -> i64 { return n * 2 }\n";
        auto d_ov = cfire::analysis::check_source(main_cf.string(), caller_src, ov_opts);
        require(d_ov.empty(),
            std::format("CF-201: in-memory source_overlays must override stale disk sibling, got {} diag(s)", d_ov.size()));
    }

    // 3. Pure-.cf short-circuit, zero subprocess / zero .cfire/gen, and warm latency <= 30ms
    {
        ScopedTempDir dir("cfire_cf201_perf");
        auto trace_file = dir.path / "clang_trace.log";
        test_cf201_detail::ScopedEnvVar trace_env("CFIRE_TRACE_CLANG_CHECK", trace_file.string());

        auto main_cf = dir.path / "main.cf";
        const std::string src =
            "package main\n"
            "type Point struct { x: i64, y: i64 }\n"
            "fn sum_points(xs: []Point) -> i64 {\n"
            "    mut acc: i64 = 0\n"
            "    for p in xs {\n"
            "        acc += p.x + p.y\n"
            "    }\n"
            "    return acc\n"
            "}\n"
            "fn main() {\n"
            "    pts := [Point{x: 1, y: 2}, Point{x: 3, y: 4}]\n"
            "    println(\"{}\", sum_points(pts))\n"
            "}\n";
        require(cfire::fs::write_text(main_cf, src).has_value(), "CF-201: write pure perf file");

        auto cli_res = run_cfire(std::format("check \"{}\"", main_cf.string()));
        cli_res.assert_ok("CF-201: CLI cfire check on pure .cf exits 0");
        std::error_code ec;
        require(!std::filesystem::exists(trace_file, ec),
            "CF-201: CLI cfire check on pure .cf must spawn 0 Clang processes");
        require(!std::filesystem::exists(dir.path / ".cfire", ec),
            "CF-201: CLI cfire check must not create <proj>/.cfire");

        cfire::analysis::CheckOptions opts;
        opts.runtime_dir = rt_dir;
        // Warm call (caches toolchain layout lookup, etc.)
        auto warm = cfire::analysis::check_source(main_cf.string(), src, opts);
        require(warm.empty(), "CF-201: warm pure check clean");

        std::vector<double> ms_runs;
        for (int i = 0; i < 5; ++i) {
            auto t0 = std::chrono::steady_clock::now();
            auto d = cfire::analysis::check_source(main_cf.string(), src, opts);
            auto t1 = std::chrono::steady_clock::now();
            require(d.empty(), "CF-201: timed pure check clean");
            ms_runs.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
        }
        std::sort(ms_runs.begin(), ms_runs.end());
        double median_ms = ms_runs[2];
        double budget_ms = test_cf201_detail::is_sanitizer_build() ? 150.0 : 30.0;
        require(median_ms <= budget_ms,
            std::format("CF-201: warm pure-.cf check_source median {:.2f}ms exceeded {:.1f}ms", median_ms, budget_ms));

        // 4. --verify-cpp / --full forces Clang verification on pure .cf over stdin with 0 .cfire/gen files
        std::filesystem::remove(trace_file, ec);
        auto full_cli = run_cfire(std::format("check --verify-cpp \"{}\"", main_cf.string()));
        full_cli.assert_ok("CF-201: CLI cfire check --verify-cpp exits 0 on valid pure .cf");
        auto tr_txt = cfire::fs::read_text(trace_file).value_or("");
        require(!tr_txt.empty(), "CF-201: --verify-cpp must invoke Clang syntax verify");
        require(!std::filesystem::exists(dir.path / ".cfire", ec),
            "CF-201: --verify-cpp must still create 0 <proj>/.cfire files");

        std::filesystem::remove(trace_file, ec);
        auto alias_cli = run_cfire(std::format("check --full \"{}\"", main_cf.string()));
        alias_cli.assert_ok("CF-201: CLI cfire check --full exits 0");
        require(!cfire::fs::read_text(trace_file).value_or("").empty(),
            "CF-201: --full alias must invoke Clang syntax verify");
    }

    // 4b. Automatic stdin Clang check on missing import cpp header + broken cpp {} without --verify-cpp
    {
        ScopedTempDir dir("cfire_cf201_cpp_auto");
        auto trace_file = dir.path / "clang_trace.log";
        test_cf201_detail::ScopedEnvVar trace_env("CFIRE_TRACE_CLANG_CHECK", trace_file.string());

        auto missing_hdr_cf = dir.path / "missing_hdr.cf";
        require(cfire::fs::write_text(missing_hdr_cf,
            "package main\n"
            "import cpp \"missing_cf201_nonexistent_header.h\"\n"
            "fn main() {}\n").has_value(),
            "CF-201: write missing import cpp test file");

        auto r = run_cfire(std::format("check --json \"{}\"", missing_hdr_cf.string()));
        r.assert_fail("CF-201: cfire check on missing import cpp header must fail automatically");
        require(r.output.contains("cfire/TYPE-ERROR"),
            std::format("CF-201: expected cfire/TYPE-ERROR on missing header, got: {}", r.output));
        require(!r.output.contains("<stdin>"),
            std::format("CF-201: JSON diagnostics must normalize <stdin> to file path, got: {}", r.output));
        require(!cfire::fs::read_text(trace_file).value_or("").empty(),
            "CF-201: import cpp must automatically trigger Clang syntax check");
        std::error_code ec;
        require(!std::filesystem::exists(dir.path / ".cfire", ec),
            "CF-201: automatic import cpp check must not create <proj>/.cfire");
    }

    // 5. LSP didChange (in-memory Sema only, < 50ms, 0 Clang spawns) vs didSave (stdin Clang enabled)
    {
        ScopedTempDir ws("cfire_cf201_lsp");
        std::error_code ec;
        std::filesystem::create_directory(ws.path / ".git", ec);
        auto trace_file = ws.path / "clang_trace.log";
        test_cf201_detail::ScopedEnvVar trace_env("CFIRE_TRACE_CLANG_CHECK", trace_file.string());

        auto main_cf = ws.path / "main.cf";
        const std::string init_src = "package main\nfn main() {}\n";
        require(cfire::fs::write_text(main_cf, init_src).has_value(), "CF-201: write LSP main.cf");
        std::string uri = cfire::lsp::detail::path_to_uri(main_cf);
        std::string ws_uri = cfire::lsp::detail::path_to_uri(ws.path);

        // Warm once outside timed didChange so toolchain layout discovery is warm
        {
            cfire::analysis::CheckOptions warm_opts;
            warm_opts.runtime_dir = rt_dir;
            (void)cfire::analysis::check_source(main_cf.string(), init_src, warm_opts);
        }
        std::filesystem::remove(trace_file, ec);

        // 5a. didChange with both cpp {} bug AND sema error -> reports sema error quickly, 0 Clang spawns
        {
            const std::string changed_src =
                "package main\n"
                "fn main() {\n"
                "    missing_fn_cf201()\n"
                "    cpp { int broken = \"bad\"; }\n"
                "}\n";
            std::stringstream in, out;
            in << test_cf201_detail::lsp_frame(std::format(
                R"({{"jsonrpc":"2.0","id":1,"method":"initialize","params":{{"rootUri":"{}","capabilities":{{}}}}}})",
                cfire::lsp::detail::json_escape(ws_uri)));
            in << test_cf201_detail::lsp_frame(std::format(
                R"({{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{{"textDocument":{{"uri":"{}","languageId":"cfire","version":1,"text":"{}"}}}}}})",
                cfire::lsp::detail::json_escape(uri), cfire::lsp::detail::json_escape(init_src)));
            in << test_cf201_detail::lsp_frame(std::format(
                R"({{"jsonrpc":"2.0","method":"textDocument/didChange","params":{{"textDocument":{{"uri":"{}","version":2}},"contentChanges":[{{"text":"{}"}}]}}}})",
                cfire::lsp::detail::json_escape(uri), cfire::lsp::detail::json_escape(changed_src)));
            in << test_cf201_detail::lsp_frame(R"({"jsonrpc":"2.0","id":2,"method":"shutdown","params":null})");
            in << test_cf201_detail::lsp_frame(R"({"jsonrpc":"2.0","method":"exit","params":null})");

            cfire::lsp::Server srv(15);
            auto t0 = std::chrono::steady_clock::now();
            srv.handle_stream(in, out);
            auto t1 = std::chrono::steady_clock::now();
            double elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

            std::string resp = out.str();
            require(resp.contains("textDocument/publishDiagnostics") && resp.contains("missing_fn_cf201"),
                std::format("CF-201: LSP didChange must publish Sema diagnostics, got: {}", resp));
            require(!std::filesystem::exists(trace_file, ec),
                "CF-201: LSP didChange must spawn 0 Clang processes even with cpp {}");
            double lsp_budget = test_cf201_detail::is_sanitizer_build() ? 200.0 : 50.0;
            require(elapsed_ms <= lsp_budget,
                std::format("CF-201: LSP didChange stream took {:.2f}ms (budget {:.1f}ms)", elapsed_ms, lsp_budget));
        }

        // 5b. didSave with valid Sema + broken cpp {} -> runs stdin Clang check & publishes cfire/TYPE-ERROR
        {
            std::filesystem::remove(trace_file, ec);
            const std::string saved_src =
                "package main\n"
                "fn main() {\n"
                "    cpp {\n"
                "        int broken = \"bad\";\n"
                "    }\n"
                "}\n";
            require(cfire::fs::write_text(main_cf, saved_src).has_value(), "CF-201: write saved main.cf");

            std::stringstream in, out;
            in << test_cf201_detail::lsp_frame(std::format(
                R"({{"jsonrpc":"2.0","id":1,"method":"initialize","params":{{"rootUri":"{}","capabilities":{{}}}}}})",
                cfire::lsp::detail::json_escape(ws_uri)));
            in << test_cf201_detail::lsp_frame(std::format(
                R"({{"jsonrpc":"2.0","method":"textDocument/didSave","params":{{"textDocument":{{"uri":"{}"}},"text":"{}"}}}})",
                cfire::lsp::detail::json_escape(uri), cfire::lsp::detail::json_escape(saved_src)));
            in << test_cf201_detail::lsp_frame(R"({"jsonrpc":"2.0","id":2,"method":"shutdown","params":null})");
            in << test_cf201_detail::lsp_frame(R"({"jsonrpc":"2.0","method":"exit","params":null})");

            cfire::lsp::Server srv(15);
            srv.handle_stream(in, out);

            std::string resp = out.str();
            require(!cfire::fs::read_text(trace_file).value_or("").empty(),
                "CF-201: LSP didSave with cpp {} must trigger stdin Clang check");
            require(resp.contains("textDocument/publishDiagnostics") && resp.contains("cfire/TYPE-ERROR"),
                std::format("CF-201: LSP didSave must publish cfire/TYPE-ERROR from stdin Clang, got: {}", resp));
            require(!std::filesystem::exists(ws.path / ".cfire", ec),
                "CF-201: LSP didSave must not create <proj>/.cfire");
        }
    }

    std::println("--- PASS: test_cf201_single_pass_in_memory_check (CF-201)");
}
