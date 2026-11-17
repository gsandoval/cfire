#pragma once
// tests/unit/test_package_fixes_and_generics.hpp — Cross-package bounds/matching, package-aware cfire fix,
// fix applicability filtering, copy hints, and formatter/outline/MCP/LSP parity tests.

#include "../test_harness.hpp"
#include "../../src/mcp.hpp"

void test_package_fixes_and_generics(const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_package_fixes_and_generics");

    // 1. Cross-package qualified bounds ([T: Local + pkg.Bound]), mut interface methods, and pkg.Enum::Variant matching
    {
        ScopedTempDir ws("cfire_pkg_bounds_match");
        ws.file("io_ports/io_ports.cf", R"(package io_ports

type Mode enum {
    Read(i32),
    Write(str),
    Idle,
}

type Codec interface {
    fn encode() -> str
    fn mut reset()
}

fn load_names(ok: bool) -> []str! {
    if !ok {
        return error("cannot load names")
    }
    return ["eth0", "lo"]
}

fn fixed_label() -> str {
    return "steady"
}
)");

        const std::string main_src = R"(package main

import "./io_ports"

type LocalLabel interface {
    fn label() -> str
}

type Channel struct {
    tag:   str
    count: i64
}

fn (c: Channel) label() -> str {
    return c.tag
}

fn (c: Channel) encode() -> str {
    return format("{}#{}", c.tag, c.count)
}

fn (mut c: Channel) reset() {
    c.count = 0
}

fn summarize[T: LocalLabel + io_ports.Codec](mut item: T, m: io_ports.Mode) -> str {
    head := item.label() + ":" + item.encode()
    item.reset()
    return match m {
        io_ports.Mode::Read(n) if n > 0 => format("{}@+{}", head, n),
        io_ports.Mode::Read(_) => head + "@<=0",
        io_ports.Mode::Write(s) => format("{}:{}", head, s),
        io_ports.Mode::Idle => head + ":idle",
    }
}

fn main() {
    mut ch := Channel{tag: "tty", count: 7}
    a := summarize(mut ch, io_ports.Mode::Read(5))
    b := summarize(mut ch, io_ports.Mode::Read(-1))
    c := summarize(mut ch, io_ports.Mode::Write("tx"))
    d := summarize(mut ch, io_ports.Mode::Idle)
    names := io_ports.load_names(true) or []
    oct := 0o600
    msg := "a\x00b\u{1F600}"
    println("{}|{}|{}|{}|{}|{}|{}", a, b, c, d, names.len(), oct, msg.len())
}
)";
        auto main_cf = ws.file("main.cf", main_src);

        auto chk = run_cfire(std::format("check --verify-cpp \"{}\"", main_cf.string()));
        require(chk.ok(), std::format("cross-pkg check ok, got:\n{}", chk.output));

        auto run = run_cfire(std::format("run \"{}\"", main_cf.string()));
        require(run.ok() && run.output.contains("tty:tty#7@+5|tty:tty#0@<=0|tty:tty#0:tx|tty:tty#0:idle|2|384|7"),
            std::format("cross-pkg run output mismatch, got:\n{}", run.output));

        // Formatter idempotency on Wave F2 constructs
        auto fmt_res = cfire::fmt::format_source(main_src, main_cf.string());
        require(fmt_res.has_value(), "format cross-pkg main.cf succeeded");
        if (fmt_res) {
            auto fmt_again = cfire::fmt::format_source(*fmt_res, main_cf.string());
            require(fmt_again.has_value() && *fmt_again == *fmt_res, "cfire fmt idempotent on Wave F2 constructs");
            require(fmt_res->contains("[T: LocalLabel + io_ports.Codec]") &&
                    fmt_res->contains("io_ports.Mode::Read(n) if n > 0") &&
                    fmt_res->contains("oct := 0o600"),
                std::format("cfire fmt preserved compound bounds/guard/octal, got:\n{}", *fmt_res));
        }

        // CLI `cfire outline --json` parity
        auto outline_cli = run_cfire(std::format("outline --json \"{}\"", ws.path.string()));
        require(outline_cli.ok() &&
                outline_cli.output.contains("T: LocalLabel + io_ports.Codec") &&
                outline_cli.output.contains("fn mut reset()"),
            std::format("cfire outline --json includes compound bounds & fn mut, got:\n{}", outline_cli.output));

        // MCP `cfire_outline` parity
        auto mcp_srv = cfire::mcp::build_compiler_mcp_server(rt_dir, cfire::mcp::McpOptions{ws.path, false});
        std::string mcp_req = std::format(
            R"({{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{{"name":"cfire_outline","arguments":{{"path":"{}"}}}}}})",
            cfire::lsp::detail::json_escape(ws.path.string()));
        std::string mcp_resp = mcp_srv.handle_jsonrpc(mcp_req);
        require(mcp_resp.contains("T: LocalLabel + io_ports.Codec") &&
                mcp_resp.contains("fn mut reset()"),
            std::format("MCP cfire_outline includes compound bounds & fn mut, got:\n{}", mcp_resp));

        // LSP navigation, hover, and \u{...} string literal non-interpolation check
        auto locate_pos = [](std::string_view text, std::string_view needle) -> std::pair<int, int> {
            auto idx = text.find(needle);
            if (idx == std::string_view::npos) return {0, 0};
            int ln = 0;
            std::size_t line_start = 0;
            for (std::size_t i = 0; i < idx; ++i) {
                if (text[i] == '\n') { ++ln; line_start = i + 1; }
            }
            return {ln, static_cast<int>(idx - line_start)};
        };
        auto [codec_ln, codec_col] = locate_pos(main_src, "Codec");
        auto [read_ln, read_col] = locate_pos(main_src, "Read(n)");
        auto [u_ln, u_col] = locate_pos(main_src, "1F600");
        std::filesystem::create_directory(ws.path / ".git");
        const auto main_uri = cfire::lsp::detail::path_to_uri(main_cf);
        const auto pkg_uri = cfire::lsp::detail::path_to_uri(ws.path / "io_ports" / "io_ports.cf");
        auto rpc_frame = [](std::string_view body) {
            return std::format("Content-Length: {}\r\n\r\n{}", body.size(), body);
        };
        std::stringstream lsp_in, lsp_out;
        lsp_in << rpc_frame(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})");
        lsp_in << rpc_frame(std::format(
            R"({{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{{"textDocument":{{"uri":"{}","text":"{}"}}}}}})",
            cfire::lsp::detail::json_escape(main_uri), cfire::lsp::detail::json_escape(main_src)));
        lsp_in << rpc_frame(std::format(
            R"({{"jsonrpc":"2.0","id":2,"method":"textDocument/hover","params":{{"textDocument":{{"uri":"{}"}},"position":{{"line":{},"character":{}}}}}}})",
            cfire::lsp::detail::json_escape(main_uri), codec_ln, codec_col));
        lsp_in << rpc_frame(std::format(
            R"({{"jsonrpc":"2.0","id":3,"method":"textDocument/definition","params":{{"textDocument":{{"uri":"{}"}},"position":{{"line":{},"character":{}}}}}}})",
            cfire::lsp::detail::json_escape(main_uri), codec_ln, codec_col));
        lsp_in << rpc_frame(std::format(
            R"({{"jsonrpc":"2.0","id":4,"method":"textDocument/hover","params":{{"textDocument":{{"uri":"{}"}},"position":{{"line":{},"character":{}}}}}}})",
            cfire::lsp::detail::json_escape(main_uri), read_ln, read_col));
        lsp_in << rpc_frame(std::format(
            R"({{"jsonrpc":"2.0","id":5,"method":"textDocument/definition","params":{{"textDocument":{{"uri":"{}"}},"position":{{"line":{},"character":{}}}}}}})",
            cfire::lsp::detail::json_escape(main_uri), read_ln, read_col));
        lsp_in << rpc_frame(std::format(
            R"({{"jsonrpc":"2.0","id":6,"method":"textDocument/hover","params":{{"textDocument":{{"uri":"{}"}},"position":{{"line":{},"character":{}}}}}}})",
            cfire::lsp::detail::json_escape(main_uri), u_ln, u_col));
        lsp_in << rpc_frame(R"({"jsonrpc":"2.0","id":99,"method":"shutdown","params":null})");
        lsp_in << rpc_frame(R"({"jsonrpc":"2.0","method":"exit","params":null})");

        cfire::lsp::Server srv;
        srv.handle_stream(lsp_in, lsp_out);
        std::string rpc_resp = lsp_out.str();
        require(rpc_resp.contains("Interface `io_ports.Codec`") && rpc_resp.contains("fn mut reset()"),
            std::format("LSP hover on io_ports.Codec expected Interface doc with fn mut reset(), got:\n{}", rpc_resp));
        require(rpc_resp.contains("Enum Variant `io_ports.Mode::Read`"),
            std::format("LSP hover on io_ports.Mode::Read expected Enum Variant doc, got:\n{}", rpc_resp));
        require(rpc_resp.contains(cfire::lsp::detail::json_escape(pkg_uri)),
            std::format("LSP definition on io_ports.Codec / Mode::Read expected target {}, got:\n{}", pkg_uri, rpc_resp));
        require(!rpc_resp.contains("UNDECLARED-IDENT") && !rpc_resp.contains("`1F600`"),
            std::format("LSP must not misread \\u{{1F600}} as interpolation, got:\n{}", rpc_resp));

        // Deprecated separator diagnostics & `cfire fix` rewrite (`pkg::Bound`, `pkg.Enum.Variant`, `pkg::Enum::Variant`)
        auto sep_cf = ws.file("sep_bad.cf", R"(package main

import "./io_ports"

fn show[T: io_ports::Codec](mut x: T, m: io_ports.Mode) -> str {
    x.reset()
    return match m {
        io_ports.Mode.Read(n) => format("{}:{}", x.encode(), n),
        io_ports::Mode::Write(s) => s,
        io_ports.Mode::Idle => "idle",
    }
}

fn main() {
    println("ok")
}
)");
        auto sep_chk = run_cfire(std::format("check --json \"{}\"", sep_cf.string()));
        require(sep_chk.output.contains("cfire/DEPRECATED-SEPARATOR"),
            std::format("expected cfire/DEPRECATED-SEPARATOR, got:\n{}", sep_chk.output));
        auto sep_fix = run_cfire(std::format("fix \"{}\"", sep_cf.string()));
        require(sep_fix.ok(), std::format("cfire fix on deprecated separators ok, got:\n{}", sep_fix.output));
        std::string fixed_sep = cfire::fs::read_text(sep_cf).value_or("");
        require(fixed_sep.contains("[T: io_ports.Codec]") &&
                fixed_sep.contains("io_ports.Mode::Read(n)") &&
                fixed_sep.contains("io_ports.Mode::Write(s)"),
            std::format("expected cfire fix to repair deprecated separators, got:\n{}", fixed_sep));

        // Package-aware `cfire fix`: valid `try io_ports.load_names(true)` is preserved while genuine redundant `try` is removed
        auto valid_try_cf = ws.file("valid_try_fix.cf", R"(package main

import "./io_ports"

fn run_demo() -> str! {
    xs := try io_ports.load_names(true)
    lbl := try io_ports.fixed_label()
    return format("{}:{}", xs.len(), lbl)
}

fn main() {
    println("ok")
}
)");
        auto valid_try_fix = run_cfire(std::format("fix \"{}\"", valid_try_cf.string()));
        require(valid_try_fix.ok(), std::format("cfire fix on valid cross-pkg try ok, got:\n{}", valid_try_fix.output));
        std::string fixed_valid_try = cfire::fs::read_text(valid_try_cf).value_or("");
        require(fixed_valid_try.contains("xs := try io_ports.load_names(true)") &&
                fixed_valid_try.contains("lbl := io_ports.fixed_label()"),
            std::format("expected package-aware fix to keep fallible try and remove genuine redundant try, got:\n{}", fixed_valid_try));
    }

    // 2. False REDUNDANT-TRY / REDUNDANT-OR suppression on broken cross-package import + `cfire fix` safety
    {
        ScopedTempDir ws("cfire_broken_import_fix");
        const std::string broken_src = R"(package main

import "./missing_ports"

fn load() -> str! {
    a := try missing_ports.open_port()
    b := missing_ports.read_name() or "fallback"
    return a + b
}

fn main() {
    println("hi")
}
)";
        auto broken_cf = ws.file("main.cf", broken_src);
        cfire::analysis::CheckOptions opts;
        opts.verify_cpp_backend = false;
        opts.resolve_sibling_packages = true;
        auto diags = cfire::analysis::check_source(broken_cf.string(), broken_src, opts);

        bool saw_unresolved = false;
        bool saw_false_redundant = false;
        for (const auto& d : diags) {
            if (d.code == "cfire/UNRESOLVED-PACKAGE") saw_unresolved = true;
            if (d.code == "cfire/REDUNDANT-TRY" || d.code == "cfire/REDUNDANT-OR") saw_false_redundant = true;
        }
        require(saw_unresolved, "broken import must report cfire/UNRESOLVED-PACKAGE");
        require(!saw_false_redundant, "unresolved import must suppress cfire/REDUNDANT-TRY and cfire/REDUNDANT-OR");

        auto fix_res = run_cfire(std::format("fix \"{}\"", broken_cf.string()));
        (void)fix_res;
        std::string after_fix = cfire::fs::read_text(broken_cf).value_or("");
        require(after_fix == broken_src,
            std::format("cfire fix must not delete try/or when unresolved import errors exist; got:\n{}", after_fix));
    }

    // 3. FixApplicability::EditorOnly (cfire/PREFER-ENUM): skipped by plain `cfire fix`, applied with `--only`, offered in LSP codeAction
    {
        ScopedTempDir ws("cfire_prefer_enum_applicability");
        std::filesystem::create_directory(ws.path / ".git");
        const std::string src = R"(package main

STATUS_OK := 1
STATUS_ERR := 2
STATUS_PENDING := 3

fn handle(s: i32) -> i32 {
    if s == STATUS_OK {
        return 10
    }
    if s == STATUS_ERR {
        return 20
    }
    if s == STATUS_PENDING {
        return 30
    }
    return 0
}

fn bump(mut x: i32) {
    x += 1
}

fn main() {
    mut v: i32 = 1
    bump(v)
    println("{}", handle(1))
}
)";
        auto main_cf = ws.file("main.cf", src);

        // Plain `cfire fix` applies MachineApplicable (`bump(mut v)`) and leaves EditorOnly (`PREFER-ENUM`) alone
        auto plain_fix = run_cfire(std::format("fix \"{}\"", main_cf.string()));
        require(plain_fix.ok(), std::format("plain cfire fix ok, got:\n{}", plain_fix.output));
        std::string after_plain = cfire::fs::read_text(main_cf).value_or("");
        require(after_plain.contains("bump(mut v)") && !after_plain.contains("enum {"),
            std::format("plain cfire fix must apply MUT-CALLSITE and skip PREFER-ENUM, got:\n{}", after_plain));

        // LSP codeAction still offers PREFER-ENUM as non-preferred quickfix
        int status_ln = 0;
        if (auto p = after_plain.find("STATUS_OK"); p != std::string::npos) {
            status_ln = static_cast<int>(std::count(after_plain.begin(), after_plain.begin() + static_cast<std::ptrdiff_t>(p), '\n'));
        }
        const auto main_uri = cfire::lsp::detail::path_to_uri(main_cf);
        auto rpc_frame = [](std::string_view body) {
            return std::format("Content-Length: {}\r\n\r\n{}", body.size(), body);
        };
        std::stringstream lsp_in, lsp_out;
        lsp_in << rpc_frame(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})");
        lsp_in << rpc_frame(std::format(
            R"({{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{{"textDocument":{{"uri":"{}","text":"{}"}}}}}})",
            cfire::lsp::detail::json_escape(main_uri), cfire::lsp::detail::json_escape(after_plain)));
        lsp_in << rpc_frame(std::format(
            R"({{"jsonrpc":"2.0","id":2,"method":"textDocument/codeAction","params":{{"textDocument":{{"uri":"{}"}},"range":{{"start":{{"line":{},"character":0}},"end":{{"line":{},"character":15}}}}}}}})",
            cfire::lsp::detail::json_escape(main_uri), status_ln, status_ln));
        lsp_in << rpc_frame(R"({"jsonrpc":"2.0","id":99,"method":"shutdown","params":null})");
        lsp_in << rpc_frame(R"({"jsonrpc":"2.0","method":"exit","params":null})");
        cfire::lsp::Server srv;
        srv.handle_stream(lsp_in, lsp_out);
        std::string lsp_resp = lsp_out.str();
        require(lsp_resp.contains("cfire/PREFER-ENUM") && lsp_resp.contains("\"isPreferred\":false"),
            std::format("LSP codeAction expected non-preferred cfire/PREFER-ENUM quickfix, got:\n{}", lsp_resp));

        // Explicit `cfire fix --only cfire/PREFER-ENUM` applies it
        auto only_fix = run_cfire(std::format("fix --only cfire/PREFER-ENUM \"{}\"", main_cf.string()));
        require(only_fix.ok(), std::format("cfire fix --only cfire/PREFER-ENUM ok, got:\n{}", only_fix.output));
        std::string after_only = cfire::fs::read_text(main_cf).value_or("");
        require(after_only.contains("enum {"),
            std::format("expected --only cfire/PREFER-ENUM to apply enum stub, got:\n{}", after_only));
    }

    // 4. `.clone()` UNKNOWN-METHOD idiomatic copy hint
    {
        cfire::analysis::CheckOptions opts;
        opts.verify_cpp_backend = false;
        opts.resolve_sibling_packages = false;
        const std::string src = "package main\nfn main() {\n    xs := [1, 2]\n    ys := xs.clone()\n    println(\"{}\", ys.len())\n}\n";
        auto diags = cfire::analysis::check_source("clone_hint.cf", src, opts);
        bool saw_hint = false;
        for (const auto& d : diags) {
            if (d.code == "cfire/UNKNOWN-METHOD" && d.message.contains("mut copy := xs")) {
                saw_hint = true;
            }
        }
        require(saw_hint, "expected cfire/UNKNOWN-METHOD on .clone() to hint \"mut copy := xs\"");
    }

    std::println("--- PASS: test_package_fixes_and_generics");
}
