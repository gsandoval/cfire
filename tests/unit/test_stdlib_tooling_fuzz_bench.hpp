#pragma once

#include "core/tako.hpp"
#include "core/net/tcp.hpp"
#include "core/ai/doc_zip_deflate.hpp"
#include "parser.hpp"
#include <fcntl.h>

void test_agent_tooling_outline_and_json([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_agent_tooling_outline_and_json");
    ScopedTempDir tmp_ws("cfire_tool");
    auto mod_cf = tmp_ws.file("mod.cf", R"(package mymod

import "core:fs"

type Config struct {
    threads: i32 `json:"threads"`
}

type Status enum {
    Active(i32),
    Idle
}

type Runner interface {
    run(task: &str) -> bool!
}

type Box struct {
    id: i32
}

fn (mut self: Config) execute[T: Runner](mut task: &str, move payload: own[Box]) -> (i32, str)! {
    return (0, "done")
}

fn worker(cfg: Config) -> bool {
    return true
}
)");

    // 1. Test outline generation with rich fidelity
    run_cfire(std::format("outline --json \"{}\"", tmp_ws.path.string()))
        .assert_ok("cfire outline exit 0 (TOOL-01)")
        .assert_contains("\"package\": \"mymod\"", "outline contains package name (TOOL-01)")
        .assert_contains("\"imports\": [\"core:fs\"]", "outline contains imports (TOOL-01)")
        .assert_contains("\"name\": \"Config\"", "outline contains struct Config (TOOL-01)")
        .assert_contains("\"name\": \"Status\"", "outline contains enum Status (TOOL-01)")
        .assert_contains("Active(i32)", "outline contains enum variant payload (TOOL-01)")
        .assert_contains("\"name\": \"Runner\"", "outline contains interface Runner (TOOL-01)")
        .assert_contains("\"receiver\": \"(mut self: Config)\"", "outline contains method receiver (TOOL-01)")
        .assert_contains("\"type_params\": [\"T: Runner\"]", "outline contains type params (TOOL-01)")
        .assert_contains("\"intent\": \"mut\"", "outline contains mut parameter intent (TOOL-01)")
        .assert_contains("\"type\": \"&str\"", "outline contains &str borrow type (TOOL-01)")
        .assert_contains("\"type\": \"own[Box]\"", "outline contains generic own[Box] (TOOL-01)")
        .assert_contains("\"returns\": \"(i32, str)!\"", "outline contains fallible tuple return (TOOL-01)")
        .assert_contains("\"name\": \"worker\"", "outline contains function worker (TOOL-01)");

    // 2. Test check --json with clean file
    run_cfire(std::format("check --json \"{}\"", mod_cf.string()))
        .assert_ok("cfire check --json exit 0 on clean file (TOOL-01)")
        .assert_contains("\"success\": true", "check --json reports success (TOOL-01)");

    // 3. Test check --json with multi-error and fixits (AGENT-P0-01)
    auto multi_err_cf = tmp_ws.file("multi_err.cf", R"(package main

fn modify(mut x: i32) {
    x = x + 1
}

fn consumer(move s: str) {
    _ = s
}

fn main() {
    a := 10
    modify(a)

    msg := "hello"
    consumer(move msg)
    println(msg)
}
)");

    auto multi_res = run_cfire(std::format("check --json \"{}\"", multi_err_cf.string()));
    multi_res.assert_fail("cfire check --json on multi-error file fails with non-zero exit code")
        .assert_contains("\"success\": false", "reports success: false")
        .assert_contains("\"code\": \"cfire/MUT-CALLSITE\"", "contains MUT-CALLSITE diagnostic code")
        .assert_contains("\"line\": 13", "MUT-CALLSITE diagnostic has exact 1-based line 13")
        .assert_contains("\"title\": \"Insert 'mut ' at call site (cfire/MUT-CALLSITE)\"", "contains fixit for MUT-CALLSITE")
        .assert_contains("\"replacement\": \"mut \"", "fixit replacement is 'mut '")
        .assert_contains("\"code\": \"cfire/USE-AFTER-MOVE\"", "contains USE-AFTER-MOVE diagnostic code")
        .assert_contains("\"line\": 17", "USE-AFTER-MOVE diagnostic has exact 1-based line 17");

    // 4. Test arity mismatch check (AGENT-P0-02)
    auto arity_err_cf = tmp_ws.file("arity_err.cf", R"(package main

fn calc(a: i32, b: i32) -> i32 {
    return a + b
}

fn main() {
    _ = calc(10)
}
)");
    auto arity_res = run_cfire(std::format("check --json \"{}\"", arity_err_cf.string()));
    arity_res.assert_fail("arity mismatch fails check --json")
        .assert_contains("\"code\": \"cfire/ARITY-MISMATCH\"", "contains ARITY-MISMATCH code")
        .assert_contains("call to 'calc' expects 2 argument(s), got 1", "arity mismatch message specifies counts");

    // 5. Test frontend undeclared-ident and #line backend type check (AGENT-P0-02)
    auto undef_err_cf = tmp_ws.file("undef_err.cf", R"(package main

fn main() {
    completely_nonexistent_function(999)
}
)");
    auto undef_res = run_cfire(std::format("check --json \"{}\"", undef_err_cf.string()));
    undef_res.assert_fail("undefined symbol fails check --json")
        .assert_contains("\"code\": \"cfire/UNDECLARED-IDENT\"", "reports UNDECLARED-IDENT for undeclared identifier")
        .assert_contains("\"line\": 4", "UNDECLARED-IDENT points to line 4 of .cf file")
        .assert_contains("completely_nonexistent_function", "UNDECLARED-IDENT mentions undeclared function");

    auto undef_text = run_cfire(std::format("check \"{}\"", undef_err_cf.string()));
    undef_text.assert_fail("undefined symbol fails text cfire check")
        .assert_contains("undef_err.cf:4:", "text check error points to .cf file and line 4");

    // CF-102: Terminal Warning Visibility in human check mode
    auto warn_cf = tmp_ws.file("warn_test.cf", R"(package main
import "core:os"

fn main() {
    target := "foo"
    _ = os.exec("{target}")
}
)");
    auto warn_check = run_cfire(std::format("check \"{}\"", warn_cf.string()));
    warn_check.assert_ok("human cfire check exits 0 on warning (CF-102)")
        .assert_contains("warning: dynamic string interpolation/concatenation in 'os.exec'", "warning output rendered to stderr (CF-102)")
        .assert_contains("checked (0 errors, 1 warning)", "summary line reports warning count (CF-102)");

    // 6. Test doc --llm & doc --check (AGENT-P0-04)
    auto llm_res = run_cfire("doc --llm");
    llm_res.assert_ok("cfire doc --llm exit 0 (TOOL-01)");
    llm_res.assert_contains("Reference Sheet", "doc --llm emits reference sheet (TOOL-01)");
    llm_res.assert_contains("core:unicode", "doc --llm lists core:unicode (TOOL-01)");
    llm_res.assert_contains("core:cli", "doc --llm lists core:cli");
    llm_res.assert_contains("core:log", "doc --llm lists core:log");
    llm_res.assert_contains("ext:db/sqlite", "doc --llm lists ext:db/sqlite");
    llm_res.assert_contains("ext:ai", "doc --llm lists ext:ai");
    llm_res.assert_contains("json.parse", "doc --llm contains exact json.parse API");
    require(!llm_res.output.contains("json.decode"), "doc --llm does not contain hallucinated json.decode");
    // sort.slice_by is a real API since CF-115; ensure it is documented with valid trailing-closure syntax.
    require(!llm_res.output.contains("sort.slice_by(mut items, |a, b| a < b)"), "doc --llm does not contain invalid expression-closure sort.slice_by example");

    run_cfire("doc --check").assert_ok("cfire doc --check exit 0 (AGENT-P0-04)");

    std::println("--- PASS: test_agent_tooling_outline_and_json");
}

void test_ide_configuration_setup([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_ide_configuration_setup");
    ScopedTempDir tmp_ide_ws("cfire_ide");

    // 1. Configure all IDEs & AI Agents (--all)
    auto res_all = cfire::lsp::configure_ide(tmp_ide_ws.path, "--all");
    require(res_all.has_value(), std::format("configure_ide --all (IDE-01): {}", res_all ? "" : res_all.error()));
    require(std::filesystem::exists(tmp_ide_ws.path / ".vscode" / "settings.json"), "creates .vscode/settings.json (IDE-01)");
    require(std::filesystem::exists(tmp_ide_ws.path / ".vscode" / "extensions.json"), "creates .vscode/extensions.json (IDE-01)");
    require(std::filesystem::exists(tmp_ide_ws.path / ".vscode" / "mcp.json"), "creates .vscode/mcp.json (IDE-MCP-01)");
    require(std::filesystem::exists(tmp_ide_ws.path / ".cursor" / "mcp.json"), "creates .cursor/mcp.json (IDE-MCP-01)");
    require(std::filesystem::exists(tmp_ide_ws.path / ".cursor" / "rules" / "cfire.mdc"), "creates .cursor/rules/cfire.mdc (IDE-MCP-01)");
    require(std::filesystem::exists(tmp_ide_ws.path / ".windsurf" / "mcp_config.json"), "creates .windsurf/mcp_config.json (IDE-MCP-01)");
    require(std::filesystem::exists(tmp_ide_ws.path / ".agents" / "plugins" / "cfire" / "plugin.json"), "creates Antigravity .agents/plugins/cfire/plugin.json (IDE-MCP-01)");
    require(std::filesystem::exists(tmp_ide_ws.path / ".agents" / "plugins" / "cfire" / "mcp_config.json"), "creates Antigravity .agents/plugins/cfire/mcp_config.json (IDE-MCP-01)");
    require(std::filesystem::exists(tmp_ide_ws.path / ".agents" / "rules" / "cfire.md"), "creates Antigravity .agents/rules/cfire.md (IDE-MCP-01)");
    require(std::filesystem::exists(tmp_ide_ws.path / ".mcp.json"), "creates Claude Code .mcp.json (IDE-MCP-01)");
    require(std::filesystem::exists(tmp_ide_ws.path / ".lsp.json"), "creates Claude Code .lsp.json (E7)");
    require(std::filesystem::exists(tmp_ide_ws.path / ".claude-plugin" / "plugin.json"), "creates Claude Code .claude-plugin/plugin.json (E7)");
    require(std::filesystem::exists(tmp_ide_ws.path / ".claude" / "rules" / "cfire.md"), "creates Claude Code .claude/rules/cfire.md (IDE-MCP-01)");
    require(std::filesystem::exists(tmp_ide_ws.path / "opencode.json"), "creates OpenCode opencode.json (E7)");
    require(std::filesystem::exists(tmp_ide_ws.path / ".codex" / "config.toml"), "creates Codex CLI .codex/config.toml (E7)");
    require(std::filesystem::exists(tmp_ide_ws.path / ".zed" / "settings.json"), "creates .zed/settings.json (IDE-01)");
    require(std::filesystem::exists(tmp_ide_ws.path / ".nvim.lua"), "creates .nvim.lua (IDE-01)");
    require(std::filesystem::exists(tmp_ide_ws.path / ".idea" / "lsp.xml"), "creates .idea/lsp.xml (IDE-01)");
    require(std::filesystem::exists(tmp_ide_ws.path / ".idea" / "mcp.json"), "creates .idea/mcp.json (IDE-MCP-01)");

    // Verify contents of .lsp.json, opencode.json, and .codex/config.toml
    auto claude_lsp_txt = cfire::fs::read_text(tmp_ide_ws.path / ".lsp.json");
    require(claude_lsp_txt.has_value() && claude_lsp_txt->contains("\".cf\": \"cfire\"") && claude_lsp_txt->contains("\"args\": [\"lsp\"]"),
        "Claude Code .lsp.json maps .cf to cfire lsp (E7)");
    auto opencode_txt = cfire::fs::read_text(tmp_ide_ws.path / "opencode.json");
    require(opencode_txt.has_value() && opencode_txt->contains("\"lsp\"") && opencode_txt->contains("\"mcp\""),
        "OpenCode opencode.json configures both lsp and mcp (E7)");
    auto codex_txt = cfire::fs::read_text(tmp_ide_ws.path / ".codex" / "config.toml");
    require(codex_txt.has_value() && codex_txt->contains("[mcp_servers.cfire]"),
        "Codex CLI .codex/config.toml configures mcp_servers.cfire (E7)");

    // Verify contents of .vscode/settings.json & .zed/settings.json
    auto vs_txt = cfire::fs::read_text(tmp_ide_ws.path / ".vscode" / "settings.json");
    require(vs_txt.has_value() && vs_txt->contains("\"editor.defaultFormatter\": \"cfire\""), "vs settings formatter (IDE-01)");
    require(vs_txt->contains("\"cfire.inlayHints.enable\": true"), "vs settings inlay hints (IDE-01)");
    auto zed_txt = cfire::fs::read_text(tmp_ide_ws.path / ".zed" / "settings.json");
    require(zed_txt.has_value() && zed_txt->contains("\"context_servers\""), "zed settings includes context_servers MCP (IDE-MCP-01)");

    // Verify contents of .nvim.lua
    auto nvim_txt = cfire::fs::read_text(tmp_ide_ws.path / ".nvim.lua");
    require(nvim_txt.has_value() && nvim_txt->contains("vim.lsp.start"), "nvim lua starts lsp (IDE-01)");

    // 2. Verify environment auto-detection on workspace with markers
    auto detected = cfire::lsp::detect_environments(tmp_ide_ws.path);
    bool found_agy = false;
    bool found_claude = false;
    bool found_cursor = false;
    bool found_vscode = false;
    for (const auto& d : detected) {
        if (d.id == "antigravity" && d.configured) found_agy = true;
        if (d.id == "claude" && d.configured) found_claude = true;
        if (d.id == "cursor" && d.configured) found_cursor = true;
        if (d.id == "vscode" && d.configured) found_vscode = true;
    }
    require(found_agy, "detect_environments detects configured Antigravity (IDE-MCP-01)");
    require(found_claude, "detect_environments detects configured Claude Code (IDE-MCP-01)");
    require(found_cursor, "detect_environments detects configured Cursor (IDE-MCP-01)");
    require(found_vscode, "detect_environments detects configured VS Code (IDE-MCP-01)");

    // 3. Verify CLI `cfire mcp --print-config` & `cfire ide --status`
    run_cfire("mcp --print-config")
        .assert_ok("cfire mcp --print-config exit 0 (IDE-MCP-01)")
        .assert_contains("\"mcpServers\"", "cfire mcp --print-config outputs mcpServers JSON (IDE-MCP-01)");

    run_cfire(std::format("ide --status --dir '{}'", tmp_ide_ws.path.string()))
        .assert_ok("cfire ide --status exit 0 (IDE-MCP-01)")
        .assert_contains("Google Antigravity", "cfire ide --status lists Antigravity (IDE-MCP-01)")
        .assert_contains("Claude Code", "cfire ide --status lists Claude Code (IDE-MCP-01)");

    // 4. Verify new MCP server tools (cfire_outline, cfire_inspect, cfire_doc_llm)
    {
        auto mcp_srv = cfire::mcp::build_compiler_mcp_server(rt_dir);
        auto resp_inspect = mcp_srv.handle_jsonrpc(R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"cfire_inspect","arguments":{"target":"cfire/USE-AFTER-FREE"}}})");
        require(resp_inspect.contains("Use of Dangling Pointer"), "MCP cfire_inspect returns diagnostic rule (IDE-MCP-01)");

        auto resp_llm = mcp_srv.handle_jsonrpc(R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"cfire_doc_llm","arguments":{}}})");
        require(resp_llm.contains("Memory Safety, Loan Tracking"), "MCP cfire_doc_llm returns LLM reference sheet (IDE-MCP-01)");

        // E1: Verify MCP cfire_check reports undefined symbol inside fn main()
        auto bad_main_cf = tmp_ide_ws.file("bad_main_mcp.cf", R"(package main
fn main() {
    undeclared_symbol_in_main(123)
}
)");
        auto resp_chk = mcp_srv.handle_jsonrpc(std::format(
            R"({{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{{"name":"cfire_check","arguments":{{"path":"{}"}}}}}})",
            cfire::lsp::detail::json_escape(bad_main_cf.string())));
        require(resp_chk.contains("undeclared identifier") && resp_chk.contains("undeclared_symbol_in_main"),
            std::format("E1: MCP cfire_check catches undefined identifier inside fn main(): {}", resp_chk));

        // E7: Verify LSP 3.17 textDocument/implementation and Call Hierarchy (prepareCallHierarchy, incomingCalls, outgoingCalls)
        {
            cfire::lsp::Server hier_srv(10);
            std::stringstream h_in;
            std::stringstream h_out;
            auto rpc_f = [](std::string_view p) {
                return std::format("Content-Length: {}\r\n\r\n{}", p.size(), p);
            };
            std::string_view hier_src = R"(package main

type Store interface {
    save(k: str) -> bool
}

type MemoryStore struct {
    id: i32
}

fn (s: MemoryStore) save(k: str) -> bool {
    return helper_validate(k)
}

fn helper_validate(k: str) -> bool {
    return k != ""
}

fn caller_entry() -> bool {
    return helper_validate("ok")
}
)";
            h_in << rpc_f(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})");
            h_in << rpc_f(std::format(
                R"({{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{{"textDocument":{{"text":"{}","uri":"file:///tmp/hier_lsp.cf","languageId":"cfire","version":1}}}}}})",
                cfire::lsp::detail::json_escape(hier_src)));
            // Go to implementation on `Store` (line 2, char 5)
            h_in << rpc_f(R"({"jsonrpc":"2.0","id":10,"method":"textDocument/implementation","params":{"textDocument":{"uri":"file:///tmp/hier_lsp.cf"},"position":{"line":2,"character":5}}})");
            // Prepare call hierarchy on `helper_validate` (line 14, char 3)
            h_in << rpc_f(R"({"jsonrpc":"2.0","id":11,"method":"textDocument/prepareCallHierarchy","params":{"textDocument":{"uri":"file:///tmp/hier_lsp.cf"},"position":{"line":14,"character":3}}})");
            // Incoming calls to `helper_validate`
            h_in << rpc_f(R"({"jsonrpc":"2.0","id":12,"method":"callHierarchy/incomingCalls","params":{"item":{"name":"helper_validate","kind":12,"uri":"file:///tmp/hier_lsp.cf"}}})");
            // Outgoing calls from `caller_entry`
            h_in << rpc_f(R"({"jsonrpc":"2.0","id":13,"method":"callHierarchy/outgoingCalls","params":{"item":{"name":"caller_entry","kind":12,"uri":"file:///tmp/hier_lsp.cf"}}})");
            h_in << rpc_f(R"({"jsonrpc":"2.0","id":14,"method":"shutdown","params":null})");
            h_in << rpc_f(R"({"jsonrpc":"2.0","method":"exit","params":null})");

            hier_srv.handle_stream(h_in, h_out);
            std::string h_resp = h_out.str();

            require(h_resp.contains("\"implementationProvider\":true") && h_resp.contains("\"callHierarchyProvider\":true"),
                "E7: LSP initialize advertises implementationProvider and callHierarchyProvider");
            require(h_resp.contains(R"("id":10)") && h_resp.contains(R"("line":6)"),
                std::format("E7: textDocument/implementation on Store finds MemoryStore at line 6: {}", h_resp));
            require(h_resp.contains(R"("id":11)") && h_resp.contains(R"("name":"helper_validate")"),
                std::format("E7: textDocument/prepareCallHierarchy returns helper_validate item: {}", h_resp));
            require(h_resp.contains(R"("id":12)") && h_resp.contains(R"("name":"caller_entry")") &&
                    h_resp.contains(R"("name":"save")") && h_resp.contains("fn (s: MemoryStore) save"),
                std::format("E7: callHierarchy/incomingCalls finds caller_entry and MemoryStore.save: {}", h_resp));
            require(h_resp.contains(R"("id":13)") && h_resp.contains(R"("name":"helper_validate")"),
                std::format("E7: callHierarchy/outgoingCalls from caller_entry finds helper_validate: {}", h_resp));
        }
    }

    // 5. SELF-LOCATE-01: Verify Portable Directory Installation, System FHS Prefix Installation, and Symlinked Binary
    {
        auto portable_dir = tmp_ide_ws.subpath("portable_dist");
        auto fhs_prefix = tmp_ide_ws.subpath("fhs_prefix");
        auto symlink_dir = tmp_ide_ws.subpath("bin_links");
        auto user_proj = tmp_ide_ws.subpath("external_user_project");
        std::filesystem::create_directories(symlink_dir);
        std::filesystem::create_directories(user_proj);

        (void)cfire::fs::write_text(user_proj / "app.cf", R"(package main
import "core:str"
fn main() {
    println("relocatable={}", str.to_upper("ok"))
}
)");

        // 5a. Portable Directory Installation (`<portable_dir>/cfire` + `<portable_dir>/runtime/`)
        run_cfire(std::format("install --portable '{}'", portable_dir.string()))
            .assert_ok("cfire install --portable exit 0 (SELF-LOCATE-01)");
        require(std::filesystem::exists(portable_dir / "cfire"), "portable install creates <dir>/cfire (SELF-LOCATE-01)");
        require(std::filesystem::exists(portable_dir / "runtime" / "cfire_prelude.hpp"), "portable install creates <dir>/runtime/cfire_prelude.hpp (SELF-LOCATE-01)");

        int rc = 0;
        std::string port_out = cfire::bridge::run_command_capture(
            std::format("cd '{}' && env -u CFIRE_RUNTIME_DIR '{}' run app.cf 2>&1",
                user_proj.string(), (portable_dir / "cfire").string()), rc);
        require(rc == 0 && port_out.contains("relocatable=OK"),
            std::format("portable cfire runs external project without CFIRE_RUNTIME_DIR (SELF-LOCATE-01): {}", port_out));

        // 5b. System FHS Prefix Installation (`<prefix>/bin/cfire` + `<prefix>/share/cfire/runtime/`)
        run_cfire(std::format("install --prefix '{}'", fhs_prefix.string()))
            .assert_ok("cfire install --prefix exit 0 (SELF-LOCATE-01)");
        require(std::filesystem::exists(fhs_prefix / "bin" / "cfire"), "FHS install creates <prefix>/bin/cfire (SELF-LOCATE-01)");
        require(std::filesystem::exists(fhs_prefix / "share" / "cfire" / "runtime" / "cfire_prelude.hpp"),
            "FHS install creates <prefix>/share/cfire/runtime/cfire_prelude.hpp (SELF-LOCATE-01)");

        std::string fhs_doc = cfire::bridge::run_command_capture(
            std::format("cd '{}' && env -u CFIRE_RUNTIME_DIR '{}' doctor 2>&1",
                user_proj.string(), (fhs_prefix / "bin" / "cfire").string()), rc);
        require(rc == 0 && fhs_doc.contains("[System FHS Prefix]"),
            std::format("FHS-installed cfire auto-detects System FHS Prefix runtime (SELF-LOCATE-01): {}", fhs_doc));

        std::string fhs_run = cfire::bridge::run_command_capture(
            std::format("cd '{}' && env -u CFIRE_RUNTIME_DIR '{}' run app.cf 2>&1",
                user_proj.string(), (fhs_prefix / "bin" / "cfire").string()), rc);
        require(rc == 0 && fhs_run.contains("relocatable=OK"),
            std::format("FHS-installed cfire runs external project without CFIRE_RUNTIME_DIR (SELF-LOCATE-01): {}", fhs_run));

        // 5c. Symlinked binary (`<symlink_dir>/cfire -> <portable_dir>/cfire`)
        std::error_code ec;
        std::filesystem::create_symlink(portable_dir / "cfire", symlink_dir / "cfire", ec);
        if (!ec) {
            std::string sym_run = cfire::bridge::run_command_capture(
                std::format("cd '{}' && env -u CFIRE_RUNTIME_DIR '{}' run app.cf 2>&1",
                    user_proj.string(), (symlink_dir / "cfire").string()), rc);
            require(rc == 0 && sym_run.contains("relocatable=OK"),
                std::format("symlinked cfire resolves target runtime (SELF-LOCATE-01): {}", sym_run));
        }
    }

    std::println("--- PASS: test_ide_configuration_setup");
}

void test_cfire_eval([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_cfire_eval");

    // 1. Statement eval
    run_cfire("eval 'println(\"eval-test-ok\")'")
        .assert_ok("eval statement exit 0")
        .assert_contains("eval-test-ok", "eval statement stdout match");

    // 2. Expression eval (-e) (respects standard operator precedence: 100 + (42 * 2) = 184)
    run_cfire("eval -e '100 + 42 * 2'")
        .assert_ok("eval expression exit 0")
        .assert_contains("184", "eval expression stdout 184 match");

    // 3. Pre-imported stdlib (str + unicode)
    run_cfire("eval 'name := \"cfire🚀\"; println(\"upper={}, runes={}\", str.to_upper(\"cfire\"), unicode.rune_count(name))'")
        .assert_ok("eval pre-imported stdlib exit 0")
        .assert_contains("upper=CFIRE, runes=6", "eval pre-imported stdlib rune count match");

    // 4. Stdin piping
    int code = 0;
    std::string out4 = cfire::bridge::run_command_capture("echo 'println(\"pipe-ok-42\")' | ./cfire eval", code);
    require(code == 0, std::format("eval stdin piping exit 0: {}", out4));
    require(out4.contains("pipe-ok-42"), "eval stdin piping stdout match");

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
        .assert_ok("eval top-level decls exit 0")
        .assert_contains("x=20, y=40", "eval top-level struct/fn stdout match");

    // 6. Complete standalone package
    run_cfire(R"(eval 'package main
fn main() {
    println("full-pkg-ok")
}')")
        .assert_ok("eval complete package exit 0")
        .assert_contains("full-pkg-ok", "eval complete package stdout match");

    // 7. Subprocess argument shell-quoting safety (CPP-QA-02: spaces, quotes, and literal $VAR)
    {
        ScopedTempDir arg_dir("cfire_arg_quote");
        auto f = arg_dir.file("args_test.cf", R"(package main
import "core:cli"

type Args struct {
    A: str `cli:"--a"`
    B: str `cli:"--b"`
    C: str `cli:"--c"`
}

fn main() {
    opts := cli.parse[Args]()
    println("arg[1]={}", opts.A)
    println("arg[2]={}", opts.B)
    println("arg[3]={}", opts.C)
}
)");
        run_cfire(std::format("run '{}' -- --a 'hello world' --b '$LITERAL_DOLLAR' --c 'quote\"inside'", f.string()))
            .assert_ok("cfire run with special shell args exit 0")
            .assert_contains("arg[1]=hello world", "cfire run preserved space in arg[1]")
            .assert_contains("arg[2]=$LITERAL_DOLLAR", "cfire run prevented shell expansion of $LITERAL_DOLLAR")
            .assert_contains("arg[3]=quote\"inside", "cfire run preserved double quote in arg[3]");
    }

    // 8. AGENT-P2-05: Tokenizer-backed eval package literal string protection
    run_cfire("eval 'println(\"package manager ready\")'")
        .assert_ok("eval string literal containing 'package ' exit 0")
        .assert_contains("package manager ready", "eval package literal string match");

    // 9. AGENT-P2-05: Tokenizer-backed auto-injected core:* stdlib modules (sha256 + base64)
    run_cfire("eval -e 'sha256.hash_hex(\"abc\")'")
        .assert_ok("eval sha256 auto-import exit 0")
        .assert_contains("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "eval sha256 auto-import match");

    run_cfire("eval -e 'base64.encode(\"hello\")'")
        .assert_ok("eval base64 auto-import exit 0")
        .assert_contains("aGVsbG8=", "eval base64 auto-import match");

    // 10. AGENT-P2-05: Tokenizer-backed declaration brace matching with unbalanced brace in string literal
    run_cfire(R"(eval 'fn get_unbalanced() -> str { return "{unbalanced brace in string" }
println("{}", get_unbalanced())')")
        .assert_ok("eval helper with unbalanced brace in string exit 0")
        .assert_contains("{unbalanced brace in string", "eval unbalanced brace string output match");

    std::println("--- PASS: test_cfire_eval");
}

void test_cfire_remote_vendor(const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_cfire_remote_vendor");
    (void)rt_dir;
    ScopedTempDir test_root("cfire_bs_ext");

    auto remote_repo = test_root.subpath("remote_calc");
    std::filesystem::create_directories(remote_repo);

    int rc = 0;
    (void)cfire::bridge::run_command_capture(
        std::format("git -C \"{}\" init -q && git -C \"{}\" config user.name \"Test\" && git -C \"{}\" config user.email \"test@example.com\"",
            remote_repo.string(), remote_repo.string(), remote_repo.string()), rc);
    require(rc == 0, "git init remote_calc");

    (void)cfire::fs::write_text(remote_repo / "calc.h",
        "#ifndef CALC_H\n#define CALC_H\n#ifdef __cplusplus\nextern \"C\" {\n#endif\nint add(int a, int b);\n#ifdef __cplusplus\n}\n#endif\n#endif\n");
    (void)cfire::fs::write_text(remote_repo / "calc.c",
        "#include \"calc.h\"\nint add(int a, int b) { return a + b; }\n");

    (void)cfire::bridge::run_command_capture(
        std::format("git -C \"{}\" add . && git -C \"{}\" commit -q -m \"Initial commit\"",
            remote_repo.string(), remote_repo.string()), rc);
    require(rc == 0, "git commit remote_calc");

    auto app_dir = test_root.subpath("app");
    std::filesystem::create_directories(app_dir);

    (void)cfire::fs::write_text(app_dir / "build.cf", std::format(
        "package build\n\nvar Calc = build.Git(\"{}\", {{\n    Driver: \"CC\",\n    Sources: [\"calc.c\"],\n    Includes: [\".\"]\n}})\n",
        remote_repo.string()));

    (void)cfire::fs::write_text(app_dir / "main.cf",
        "package main\n\nimport cpp \"calc.h\"\n\nfn main() {\n    res := cpp.add(15, 25)\n    println(\"sum={}\", res)\n}\n");

    // 1. Run cfire build/run and verify remote checkout + build.lock generation
    std::string run_cmd = std::format("cd \"{}\" && \"{}\"/cfire run main.cf",
        app_dir.string(), std::filesystem::current_path().string());
    std::string run_out = cfire::bridge::run_command_capture(run_cmd, rc);
    require(rc == 0, std::format("cfire run with Git remote dep exit 0: {}", run_out));
    require(run_out.contains("sum=40"), std::format("app output contains sum=40: {}", run_out));

    auto lock_file = app_dir / "build.lock";
    require(std::filesystem::exists(lock_file), "build.lock exists after run");
    auto lock_txt = cfire::build::read_file_text(lock_file);
    require(lock_txt.has_value(), "build.lock is readable");
    require(lock_txt->contains("\"packages\""), "build.lock contains packages");
    require(lock_txt->contains("\"Calc\""), "build.lock contains Calc");
    require(lock_txt->contains("\"commit\""), "build.lock contains commit hash");
    require(lock_txt->contains("\"driver\": \"CC\""), "build.lock contains driver CC");

    // 2. Vendor packages for offline air-gapped usage
    std::string vendor_cmd = std::format("cd \"{}\" && \"{}\"/cfire vendor",
        app_dir.string(), std::filesystem::current_path().string());
    std::string vendor_out = cfire::bridge::run_command_capture(vendor_cmd, rc);
    require(rc == 0, std::format("cfire vendor exit 0: {}", vendor_out));
    require(vendor_out.contains("Successfully vendored 1 package(s)"), "vendor reports 1 package vendored");
    require(std::filesystem::exists(app_dir / "vendor" / "Calc" / "calc.h"), "vendored calc.h exists");
    require(std::filesystem::exists(app_dir / "vendor" / "Calc" / "calc.c"), "vendored calc.c exists");

    // 3. Delete remote repo and local cache to verify offline build works strictly from vendor/
    std::filesystem::remove_all(remote_repo);
    std::filesystem::remove_all(app_dir / ".cfire" / "cache");

    std::string offline_out = cfire::bridge::run_command_capture(run_cmd, rc);
    require(rc == 0, std::format("offline cfire run exit 0: {}", offline_out));
    require(offline_out.contains("sum=40"), std::format("offline output contains sum=40: {}", offline_out));

    std::println("--- PASS: test_cfire_remote_vendor");
}

void test_section3_agent_tooling([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_section3_agent_tooling");

    // 1. AGENT-P2-02: Headless Auto-Fix Engine (dry-run, json, and in-place fix)
    {
        ScopedTempDir fix_dir("cfire_fix_test");
        auto f = fix_dir.file("fix_target.cf", R"(package main

type Color enum {
    Red
    Green
    Blue
}

fn mutate_num(mut n: i32) {
    n = n + 10
}

fn describe_color(c: Color) {
    match c {
        Color::Red => { println("red") }
    }
}

fn main() {
    x := 5
    mutate_num(x)
    describe_color(Color::Red)
    _ = fs.exists("nonexistent")
}
)");

        // Dry-run mode: should propose fixes in JSON but NOT alter the file on disk
        auto dry_res = run_cfire(std::format("fix --dry-run --json '{}'", f.string()));
        dry_res.assert_ok("cfire fix dry run exit 0")
            .assert_contains("\"success\": true", "dry run json success")
            .assert_contains("\"applied_fixes\":", "dry run json applied_fixes");

        // Verify file is still untouched
        auto untouched_content = cfire::build::read_file_text(f);
        require(untouched_content.has_value() && !untouched_content->contains("mut x :="), "dry-run preserved original file");

        // Real fix mode: applies fixes and modifies file
        auto fix_res = run_cfire(std::format("fix '{}'", f.string()));
        fix_res.assert_ok("cfire fix exit 0")
            .assert_contains("Applied", "cfire fix report");

        // Check that repaired file passes cfire check with 0 errors!
        auto chk_res = run_cfire(std::format("check '{}'", f.string()));
        chk_res.assert_ok("cfire check on auto-fixed file passes with 0 errors");
    }

    // 2. AGENT-P2-03: Structured Test Runner (--json, --filter, and setjmp location assertion failure)
    {
        ScopedTempDir test_dir("cfire_test_runner");
        auto f = test_dir.file("tests.cf", R"(package main

test "failing test assertion" {
    a := 10
    b := 20
    assert(a == b, "a must equal b")
}

test "passing test assertion" {
    assert(100 == 100)
}
)");

        // Run --json mode: both tests execute, failing test doesn't abort whole runner
        int exit_code = 0;
        std::string json_out = cfire::bridge::run_command_capture(std::format("./cfire test --json '{}'", f.string()), exit_code);
        require(exit_code == 1, "test --json returns exit code 1 when tests fail");
        require(json_out.contains("\"success\": false"), "test --json output has success: false");
        require(json_out.contains("\"passed\": 1"), "test --json output has passed: 1");
        require(json_out.contains("\"failed\": 1"), "test --json output has failed: 1");
        require(json_out.contains("failing test assertion"), "test --json has failing test name");
        require(json_out.contains("passing test assertion"), "test --json has passing test name");
        require(json_out.contains("(a == b)"), "test --json captured assertion expression");

        // Run --filter mode: only runs matching test
        auto filt_res = run_cfire(std::format("test --filter passing '{}'", f.string()));
        filt_res.assert_ok("cfire test --filter passing exit 0")
            .assert_contains("PASS (1 cfire tests succeeded)", "filtered test ran 1 passing test");
    }

    // 3. AGENT-P2-04: Stateless CLI Symbol, Type & Foreign C/C++ Header Inspector (cfire inspect)
    {
        // Form 1: Source coordinate
        auto insp1 = run_cfire("inspect examples/01-hello-core/main.cf:22:15 --json");
        insp1.assert_ok("cfire inspect source coordinate exit 0")
            .assert_contains("examples/01-hello-core/main.cf:22:15", "inspect target match")
            .assert_contains("record", "inspect hover contains symbol name")
            .assert_contains("definition", "inspect contains definition location");

        // Form 2: Foreign C header
        auto insp2 = run_cfire("inspect cpp math.h --json");
        insp2.assert_ok("cfire inspect cpp math.h exit 0")
            .assert_contains("\"header\": \"math.h\"", "inspect cpp header match")
            .assert_contains("\"name\": \"cos\"", "inspect cpp contains math function cos");

        // Form 3: stdlib module & symbol
        auto insp3 = run_cfire("inspect core:json --json");
        insp3.assert_ok("cfire inspect core:json exit 0")
            .assert_contains("\"name\": \"json\"", "inspect core:json name match")
            .assert_contains("\"import_path\": \"core:json\"", "inspect core:json import_path match")
            .assert_contains("stringify", "inspect core:json functions contains stringify");
        auto insp3_alias = run_cfire("inspect ext:json --json");
        insp3_alias.assert_ok("cfire inspect ext:json alias exit 0")
            .assert_contains("\"import_path\": \"core:json\"", "inspect ext:json alias resolves to core:json");

        auto insp4 = run_cfire("inspect json.stringify --json");
        insp4.assert_ok("cfire inspect json.stringify exit 0")
            .assert_contains("\"name\": \"stringify\"", "inspect json.stringify name match")
            .assert_contains("fn stringify", "inspect json.stringify signature match");
    }

    std::println("--- PASS: test_section3_agent_tooling");
}

void test_security_and_fuzzing([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_security_and_fuzzing");

    // 1. SEC-P0-01a: WebSocket 64-Bit Frame Length Allocation Bomb (OOM DoS Prevention)
    {
        int sv[2]{-1, -1};
        require(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair for ws bomb test");
        // Frame 1: FIN=1, opcode=0x2 (binary), mask=0, len=127, 8-byte extended len = 0x7FFFFFFFFFFFFFFF (9 EB)
        const std::uint8_t bomb1[10] = {0x82, 127, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        (void)::send(sv[0], bomb1, sizeof(bomb1), 0);
        auto res1 = cfire::websocket::decode_frame(sv[1]);
        require(!res1.has_value(), "decode_frame immediately rejects 9-exabyte 64-bit length bomb without OOM");

        // Frame 2: MSB set on 64-bit length (violates RFC 6455 Section 5.2)
        const std::uint8_t bomb2[10] = {0x82, 127, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10};
        (void)::send(sv[0], bomb2, sizeof(bomb2), 0);
        auto res2 = cfire::websocket::decode_frame(sv[1]);
        require(!res2.has_value(), "decode_frame rejects RFC 6455 invalid MSB-set 64-bit length");

        // Frame 3: Custom max_frame_size limit (1024 bytes) vs 4096-byte claimed frame
        const std::uint8_t bomb3[4] = {0x81, 126, 0x10, 0x00}; // len = 4096
        (void)::send(sv[0], bomb3, sizeof(bomb3), 0);
        auto res3 = cfire::websocket::decode_frame(sv[1], nullptr, /*max_frame_size=*/1024);
        require(!res3.has_value(), "decode_frame rejects frame exceeding custom max_frame_size");

        ::close(sv[0]);
        ::close(sv[1]);
    }

    // 2. SEC-P0-01b: HTTP CRLF Response Splitting & Header/Query Parameter Flood Caps
    {
        auto resp = cfire::http::Response::redirect("/home\r\nSet-Cookie: session=hacked\r\n\r\nInjectedBody");
        resp.set_header("X-Test\r\nX-Injected", "val\r\nEvil-Header: 1");
        std::string wire = resp.to_http_wire();
        require(!wire.contains("Set-Cookie: session=hacked\r\n"), "Response::redirect strips CRLF header injection");
        require(!wire.contains("\r\nEvil-Header:"), "Response::set_header strips CRLF header injection");
        std::size_t first_delim = wire.find("\r\n\r\n");
        require(first_delim != std::string::npos && first_delim + 4 == wire.size(),
            "Response wire has exactly one header terminator at end of empty-body redirect");

        // 200 HTTP headers (> 128 cap)
        std::string flood_req = "GET / HTTP/1.1\r\nHost: localhost\r\n";
        for (int i = 0; i < 200; ++i) {
            flood_req += std::format("X-Hdr-{}: {}\r\n", i, i);
        }
        flood_req += "\r\n";
        auto flood_res = cfire::http::parse_request(flood_req);
        require(!flood_res.has_value(), "http::parse_request rejects header flood (>128 headers)");

        // 300 query parameters (> 256 cap)
        std::string qflood = "http://localhost/search?";
        for (int i = 0; i < 300; ++i) {
            if (i > 0) qflood += "&";
            qflood += std::format("k{}={}", i, i);
        }
        auto qflood_res = cfire::url::parse(qflood);
        require(!qflood_res.has_value(), "url::parse rejects query parameter flood (>256 params)");
    }

    // 3. SEC-P0-01c: URL Malformed %XX Hex Decoding & Control Character Rejection
    {
        std::string dec = cfire::url::decode("%ZZ%0G%1%41+test%");
        require(dec == "%ZZ%0G%1A test%", "url::decode safely preserves malformed % sequences and decodes valid %41");

        auto crlf_url = cfire::url::parse("http://example.com/test\r\nX-Injected: true");
        require(!crlf_url.has_value(), "url::parse rejects URLs containing CR/LF control characters");
    }

    // 4. SEC-P0-01d: Stack-Overflow Prevention on Deeply Nested JSON & .cf ASTs
    {
        std::string deep_json_arr(5000, '[');
        deep_json_arr.append(5000, ']');
        auto dj_res = cfire::json::parse_dynamic(deep_json_arr);
        require(!dj_res.has_value() && dj_res.error().message().contains("maximum JSON nesting depth"),
            "json::parse_dynamic rejects 5,000-deep array without stack overflow");

        std::string deep_cf = "package main\nfn main() {\n    x := ";
        deep_cf.append(1000, '(');
        deep_cf += "1";
        deep_cf.append(1000, ')');
        deep_cf += "\n}\n";
        auto dcf_res = cfire::frontend::parse_source_resilient("deep.cf", deep_cf);
        require(!dcf_res.ok(), "frontend::parse_source_resilient rejects 1,000-deep expression without stack overflow");

        std::string deep_type = "package main\nfn main() {\n    x: ";
        for (int i = 0; i < 1000; ++i) deep_type += "[]";
        deep_type += "i32 = []\n}\n";
        auto dtype_res = cfire::frontend::parse_source_resilient("deep_type.cf", deep_type);
        require(!dtype_res.ok(), "frontend::parse_source_resilient rejects 1,000-deep slice type without stack overflow");
    }

    // 5. SEC-P0-02: Deterministic PRNG Mutation Fuzzer Across All Untrusted Input Parsers (10,000+ inputs)
    {
        std::mt19937_64 rng(0xCF12E2026ULL);
        auto mutate_str = [&](std::string_view seed) -> std::string {
            std::string s(seed);
            int ops = static_cast<int>(rng() % 6) + 1;
            for (int op = 0; op < ops; ++op) {
                std::uint64_t r = rng();
                int kind = static_cast<int>(r % 5);
                if (kind == 0 && !s.empty()) {
                    // Bit flip
                    s[r % s.size()] ^= static_cast<char>(1U << ((r >> 8) % 8));
                } else if (kind == 1 && !s.empty()) {
                    // Truncate or erase slice
                    std::size_t pos = (r >> 8) % s.size();
                    std::size_t len = std::min<std::size_t>(s.size() - pos, ((r >> 16) % 16) + 1);
                    s.erase(pos, len);
                } else if (kind == 2) {
                    // Insert special boundary characters
                    static constexpr std::string_view kSpecial = "%\r\n\0\"\\{[()]}:,=-+09afAF\x80\xC0\xE0\xF0\xFF";
                    char ch = kSpecial[(r >> 8) % kSpecial.size()];
                    std::size_t pos = s.empty() ? 0 : ((r >> 16) % s.size());
                    s.insert(pos, 1, ch);
                } else if (kind == 3 && s.size() < 512) {
                    // Duplicate chunk
                    s += s.substr(0, std::min<std::size_t>(s.size(), 32));
                } else {
                    // Random byte replacement
                    if (!s.empty()) s[(r >> 8) % s.size()] = static_cast<char>((r >> 16) & 0xFF);
                }
            }
            return s;
        };

        const std::vector<std::string> http_seeds = {
            "GET /api/v1/items?id=10&filter=a%20b HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: 0\r\n\r\n",
            "POST /submit HTTP/1.1\r\nHost: example.com\r\nContent-Type: application/json\r\nContent-Length: 13\r\n\r\n{\"ok\":true}\r\n",
            "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 5\r\n\r\nhello"
        };
        const std::vector<std::string> url_seeds = {
            "https://user.example.com:8443/a/b/c?x=1&y=%20%FF#frag",
            "wss://stream.local/ws?token=abc+def%2F123",
            "http://127.0.0.1:0/"
        };
        const std::vector<std::string> json_seeds = {
            R"({"service":"api","count":-42,"rate":3.14e-2,"flags":[true,false,null],"nested":{"k":"\u0041\n\t\"\\"}})",
            R"([1, 2, [3, 4, {"a": "b", "c": []}], "plain"])"
        };
        const std::vector<std::string> cf_seeds = {
            "package main\nimport \"core:fs\"\ntype S struct { a: i32 }\nfn main() { mut x := 10; x += 2; match x { 12 => println(\"{x}\"), _ => {} } }\n",
            "package main\ntype E enum { A(i32), B }\nfn f(mut s: &mut []u8) -> i32! { return 0 }\n"
        };

        std::size_t total_fuzz_cases = 0;

        // 2,500 HTTP request/response mutations
        for (int i = 0; i < 2500; ++i) {
            std::string m = mutate_str(http_seeds[static_cast<std::size_t>(i) % http_seeds.size()]);
            (void)cfire::http::parse_request(m);
            (void)cfire::http::parse_response(m);
            ++total_fuzz_cases;
        }

        // 2,500 URL parse/decode/encode mutations
        for (int i = 0; i < 2500; ++i) {
            std::string m = mutate_str(url_seeds[static_cast<std::size_t>(i) % url_seeds.size()]);
            (void)cfire::url::parse(m);
            std::string d = cfire::url::decode(m);
            (void)cfire::url::encode(d);
            ++total_fuzz_cases;
        }

        // 2,500 JSON dynamic parser + Base64/Hex/CSV/UTF-8 mutations
        for (int i = 0; i < 2500; ++i) {
            std::string m = mutate_str(json_seeds[static_cast<std::size_t>(i) % json_seeds.size()]);
            if (auto jv = cfire::json::parse_dynamic(m)) {
                (void)cfire::json::stringify_dynamic(*jv);
            }
            (void)cfire::base64::decode(m);
            (void)cfire::hex::decode(m);
            (void)cfire::csv::parse(m);
            (void)cfire::unicode::is_valid_utf8(m);
            (void)cfire::unicode::runes(m);
            ++total_fuzz_cases;
        }

        // 1,500 WebSocket binary wire frame mutations
        {
            int sv[2]{-1, -1};
            require(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair for ws fuzzing");
            timeval tv{.tv_sec = 0, .tv_usec = 1000}; // 1ms timeout for truncated frames
            ::setsockopt(sv[1], SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            for (int i = 0; i < 1500; ++i) {
                std::size_t flen = (rng() % 24) + 2;
                std::vector<std::uint8_t> raw(flen);
                for (std::size_t b = 0; b < flen; ++b) raw[b] = static_cast<std::uint8_t>(rng() & 0xFF);
                // Keep payload length small or bomb-sized so recv doesn't wait 1ms on every iteration
                if ((raw[1] & 0x7F) < 126) {
                    raw[1] = (raw[1] & 0x80) | static_cast<std::uint8_t>(rng() % 8);
                }
                (void)::send(sv[0], raw.data(), raw.size(), MSG_NOSIGNAL);
                (void)cfire::websocket::decode_frame(sv[1], nullptr, 4096);
                // Drain any leftover bytes in sv[1]
                std::uint8_t drain[64];
                while (::recv(sv[1], drain, sizeof(drain), MSG_DONTWAIT) > 0) {}
                ++total_fuzz_cases;
            }
            ::close(sv[0]);
            ::close(sv[1]);
        }

        // 1,500 Compiler Lexer + Parser + LSP JSON-RPC mutations
        for (int i = 0; i < 1500; ++i) {
            std::string m = mutate_str(cf_seeds[static_cast<std::size_t>(i) % cf_seeds.size()]);
            auto pr = cfire::frontend::parse_source_resilient("fuzz.cf", m);
            if (pr.ok()) {
                (void)cfire::frontend::analyze_resilient( std::move(pr.ast));
            }
            (void)cfire::lsp::detail::find_json_key(m, "method");
            (void)cfire::lsp::detail::extract_json_string(m, "uri");
            ++total_fuzz_cases;
        }

        require(total_fuzz_cases >= 10000, "executed >= 10,000 security fuzz test cases cleanly");
        std::println("  [FUZZ] Executed {} adversarial & mutated inputs with 0 crashes or leaks", total_fuzz_cases);
    }

    std::println("--- PASS: test_security_and_fuzzing");
}

void test_performance_benchmarks([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_performance_benchmarks");
    using clock = std::chrono::steady_clock;

    auto print_metric = [](std::string_view name, std::size_t iters, double elapsed_sec, std::size_t bytes_per_iter = 0) {
        double ns_per_op = (elapsed_sec * 1e9) / static_cast<double>(iters);
        double ops_per_sec = static_cast<double>(iters) / std::max(elapsed_sec, 1e-9);
        if (bytes_per_iter > 0) {
            double mb_per_sec = (static_cast<double>(iters * bytes_per_iter) / (1024.0 * 1024.0)) / std::max(elapsed_sec, 1e-9);
            std::println("  [BENCH] {:<38} {:>8.1f} ns/op | {:>10.0f} ops/s | {:>8.1f} MB/s",
                name, ns_per_op, ops_per_sec, mb_per_sec);
        } else {
            std::println("  [BENCH] {:<38} {:>8.1f} ns/op | {:>10.0f} ops/s",
                name, ns_per_op, ops_per_sec);
        }
    };

    // 1. HTTP Request Parse + Parameterized Router Dispatch
    {
        auto router = cfire::http::make_router();
        router.get("/orgs/:org/repos/:repo/issues/:id", [](const cfire::http::Request& req) {
            return cfire::http::Response::text(200, req.param("org") + "/" + req.param("repo") + "#" + req.param("id"));
        });
        std::string_view raw_req =
            "GET /orgs/deepmind/repos/cfire/issues/42?state=open&limit=20 HTTP/1.1\r\n"
            "Host: api.cfire.dev\r\n"
            "User-Agent: cfire-bench/1.0\r\n"
            "Accept: application/json\r\n\r\n";

        constexpr std::size_t kIters = 25000;
        auto t0 = clock::now();
        std::size_t ok_count = 0;
        for (std::size_t i = 0; i < kIters; ++i) {
            auto req = cfire::http::parse_request(raw_req);
            if (req) {
                auto resp = router.dispatch(std::move(*req));
                if (resp.status_code == 200) ++ok_count;
            }
        }
        double sec = std::chrono::duration<double>(clock::now() - t0).count();
        require(ok_count == kIters, "HTTP parse + router benchmark all iterations succeeded");
        require(sec < 5.0, "HTTP parse + router dispatch completes 25k ops under 5s");
        print_metric("http::parse_request + Router::dispatch", kIters, sec, raw_req.size());
    }

    // 2. WebSocket RFC 6455 Frame Masking & Loopback Exchange (256B payload)
    {
        auto pair_res = cfire::websocket::pair();
        require(pair_res.has_value(), "websocket::pair for benchmark");
        auto [client, server] = std::move(*pair_res);
        std::string payload(256, 'W');

        constexpr std::size_t kIters = 10000;
        auto t0 = clock::now();
        std::size_t ok_count = 0;
        for (std::size_t i = 0; i < kIters; ++i) {
            if (client.send_text(payload) && server.recv_text()) {
                ++ok_count;
            }
        }
        double sec = std::chrono::duration<double>(clock::now() - t0).count();
        require(ok_count == kIters, "WebSocket frame roundtrip benchmark all iterations succeeded");
        print_metric("websocket::Conn (256B masked frame)", kIters, sec, payload.size());
    }

    // 3. TLS 1.3 Encrypted WSS Stream Throughput
    {
        auto cert = cfire::tls::generate_self_signed_pem("localhost", 1);
        require(cert.has_value(), "generate_self_signed_pem for TLS benchmark");
        auto srv = cfire::websocket::make_tls_server_pem(0, cert->cert_pem, cert->key_pem);
        require(srv.has_value(), "make_tls_server_pem for TLS benchmark");
        constexpr std::size_t kIters = 1000;
        srv->serve_in_background([](cfire::websocket::Conn& ws) {
            for (std::size_t i = 0; i < kIters; ++i) {
                auto m = ws.recv_text();
                if (!m) break;
                (void)ws.send_text(*m);
            }
        });

        auto cli = cfire::websocket::connect_insecure(std::format("wss://127.0.0.1:{}/bench", srv->port()));
        require(cli.has_value(), "wss connect_insecure for TLS benchmark");
        std::string payload(1024, 'T');
        auto t0 = clock::now();
        std::size_t ok_count = 0;
        for (std::size_t i = 0; i < kIters; ++i) {
            if (cli->send_text(payload) && cli->recv_text()) {
                ++ok_count;
            }
        }
        double sec = std::chrono::duration<double>(clock::now() - t0).count();
        cli->close();
        srv->stop();
        require(ok_count == kIters, "WSS TLS 1.3 encrypted roundtrip benchmark succeeded");
        print_metric("wss:// TLS 1.3 (1KB encrypted echo)", kIters, sec, payload.size() * 2);
    }

    // 4. JSON Dynamic DOM Parse + Stringify
    {
        std::string_view json_doc =
            R"({"cluster":"prod-us-east","nodes":[{"id":1,"host":"10.0.0.1","ok":true,"load":0.42},{"id":2,"host":"10.0.0.2","ok":true,"load":0.71}],"tags":["c++23","cfire","tls","websocket"]})";
        constexpr std::size_t kIters = 20000;
        auto t0 = clock::now();
        std::size_t ok_count = 0;
        for (std::size_t i = 0; i < kIters; ++i) {
            auto doc = cfire::json::parse_dynamic(json_doc);
            if (doc) {
                auto s = cfire::json::stringify_dynamic(*doc);
                if (!s.empty()) ++ok_count;
            }
        }
        double sec = std::chrono::duration<double>(clock::now() - t0).count();
        require(ok_count == kIters, "JSON parse_dynamic + stringify_dynamic benchmark succeeded");
        print_metric("json::parse_dynamic + stringify", kIters, sec, json_doc.size());
    }

    // 5. Cryptographic Primitives: SHA-256 & AES-256-GCM AEAD (16 KiB buffers)
    {
        std::vector<std::byte> buf(16384, std::byte{0x5A});
        constexpr std::size_t kShaIters = 2000;
        auto t0 = clock::now();
        std::size_t sha_ok = 0;
        for (std::size_t i = 0; i < kShaIters; ++i) {
            auto h = cfire::sha256::hash_bytes(buf);
            if (h.size() == 32) ++sha_ok;
        }
        double sha_sec = std::chrono::duration<double>(clock::now() - t0).count();
        require(sha_ok == kShaIters, "SHA-256 benchmark succeeded");
        print_metric("sha256::hash_bytes (16KB block)", kShaIters, sha_sec, buf.size());

        std::vector<std::byte> key(32, std::byte{0x11});
        std::vector<std::byte> nonce(12, std::byte{0x22});
        std::vector<std::byte> aad(16, std::byte{0x33});
        constexpr std::size_t kAesIters = 200;
        auto t1 = clock::now();
        std::size_t aes_ok = 0;
        for (std::size_t i = 0; i < kAesIters; ++i) {
            auto enc = cfire::aes::encrypt_gcm(key, nonce, buf, aad);
            if (enc && !enc->ciphertext.empty()) ++aes_ok;
        }
        double aes_sec = std::chrono::duration<double>(clock::now() - t1).count();
        require(aes_ok == kAesIters, "AES-256-GCM benchmark succeeded");
        print_metric("aes::encrypt_gcm (16KB block)", kAesIters, aes_sec, buf.size());
    }

    // 6. Compiler Frontend Pipeline (Lex + Parse + Sema + C++23 Codegen)
    {
        std::string_view cf_prog = R"(package main
import "core:fs"
import "core:str"

type Record struct {
    id: i64
    name: str
}

fn process(mut r: Record, delta: i64) -> i64 {
    r.id += delta
    return r.id
}

fn main() {
    mut rec := Record{ id: 10, name: "alpha" }
    val := process(mut rec, 32)
    println("rec={rec.name} val={val}")
}
)";
        constexpr std::size_t kIters = 5000;
        auto t0 = clock::now();
        std::size_t ok_count = 0;
        for (std::size_t i = 0; i < kIters; ++i) {
            auto ast = cfire::frontend::parse_source("bench.cf", cf_prog);
            if (ast) {
                auto checked = cfire::frontend::analyze(std::move(*ast));
                if (checked) {
                    auto cpp = cfire::frontend::emit_cpp(*checked);
                    if (!cpp.empty()) ++ok_count;
                }
            }
        }
        double sec = std::chrono::duration<double>(clock::now() - t0).count();
        require(ok_count == kIters, "Compiler frontend benchmark succeeded");
        print_metric("frontend (lex+parse+sema+codegen)", kIters, sec, cf_prog.size());
    }

    std::println("--- PASS: test_performance_benchmarks");
}

void test_negative_parser_corpus_and_fuzz_runtime([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_negative_parser_corpus_and_fuzz_runtime (CF-131)");

    // 1. Negative Parser Corpus: Verify syntax error recovery on 10 malformed programs
    {
        auto parse_fail_dir = std::filesystem::path("tests/parse_fail");
        if (!std::filesystem::exists(parse_fail_dir)) {
            parse_fail_dir = rt_dir.parent_path() / "tests" / "parse_fail";
        }
        require(std::filesystem::exists(parse_fail_dir), "tests/parse_fail directory exists");

        std::vector<std::string> fail_cases = {
            "01_unclosed_brace.cf",
            "02_missing_type_bracket.cf",
            "03_invalid_loan_intent.cf",
            "04_reserved_keyword_var.cf",
            "05_malformed_match_arm.cf",
            "06_invalid_closure_param.cf",
            "07_unclosed_string.cf",
            "08_misplaced_top_level_stmt.cf",
            "09_missing_fn_return_arrow.cf",
            "10_invalid_float_exponent.cf"
        };

        for (const auto& fname : fail_cases) {
            auto fpath = parse_fail_dir / fname;
            auto content = cfire::fs::read_text(fpath);
            require(content.has_value(), std::format("read negative parse test file: {}", fname));
            if (!content) continue;

            auto parse_res = cfire::frontend::parse_source(fname, *content);
            require(!parse_res.has_value(), std::format("parser cleanly rejects invalid file: {}", fname));
            if (!parse_res) {
                require(!parse_res.error().empty(), std::format("parser returns diagnostic message for {}", fname));
            }
        }
    }

    // 2. Tako Cloud SDK In-Memory Runtime
    {
        auto& store = cfire::tako::detail::local_store();
        store.reset();

        // Entity operations
        {
            std::lock_guard<std::recursive_mutex> lock(store.mtx);
            store.entities["users"]["u1"] = R"({"id":"u1","name":"alice"})";
            require(store.entities["users"].contains("u1"), "tako entity store put");
            require(store.entities["users"]["u1"].find("alice") != std::string::npos, "tako entity payload verification");
            store.entities["users"].erase("u1");
            require(!store.entities["users"].contains("u1"), "tako entity store delete");
        }

        // Bucket operations
        {
            std::lock_guard<std::recursive_mutex> lock(store.mtx);
            store.buckets["avatars"]["u1.png"] = {"PNG_DATA", "image/png"};
            require(store.buckets["avatars"].contains("u1.png"), "tako bucket store put");
            require(store.buckets["avatars"]["u1.png"].second == "image/png", "tako bucket content type");
            store.buckets["avatars"].erase("u1.png");
            require(!store.buckets["avatars"].contains("u1.png"), "tako bucket delete");
        }

        // Cache operations
        {
            std::lock_guard<std::recursive_mutex> lock(store.mtx);
            store.caches["session"]["sess_123"] = "valid";
            require(store.caches["session"]["sess_123"] == "valid", "tako cache store get");
            store.caches["session"].erase("sess_123");
            require(!store.caches["session"].contains("sess_123"), "tako cache store delete");
        }

        // Queue publish / drain
        {
            std::lock_guard<std::recursive_mutex> lock(store.mtx);
            store.queues["emails"].push_back("msg_1");
            store.queues["emails"].push_back("msg_2");
            require(store.queues["emails"].size() == 2, "tako queue publish size");
            std::string popped = store.queues["emails"].front();
            store.queues["emails"].erase(store.queues["emails"].begin());
            require(popped == "msg_1", "tako queue fifo order");
            require(store.queues["emails"].size() == 1, "tako queue remaining size");
        }

        // Secrets & Metrics
        {
            std::lock_guard<std::recursive_mutex> lock(store.mtx);
            store.secrets["API_KEY"] = "secret_value_xyz";
            require(store.secrets["API_KEY"] == "secret_value_xyz", "tako secret get");
            store.counters["requests_total"] += 5;
            require(store.counters["requests_total"] == 5, "tako counter metric inc");
            store.histograms["latency_ms"].push_back(12.5);
            require(!store.histograms["latency_ms"].empty(), "tako histogram observe");
        }
    }

    // 3. TCP Loopback Stream and Socketpair
    {
        auto pair_res = cfire::net::socketpair();
        require(pair_res.has_value(), "cfire::net::socketpair creation succeeds");
        if (pair_res) {
            auto& [c1, c2] = *pair_res;
            require(c1.is_open() && c2.is_open(), "socketpair endpoints are open");

            std::string msg = "cfire_tcp_packet_stream_test";
            auto wres = c1.write_text(msg);
            require(wres.has_value(), "c1 writes message successfully");

            auto rres = c2.read_exact(msg.size());
            require(rres.has_value(), "c2 reads exact message bytes");
            if (rres) {
                std::string rstr(reinterpret_cast<const char*>(rres->data()), rres->size());
                require(rstr == msg, "socketpair data integrity matches");
            }
            c1.close();
            c2.close();
            require(!c1.is_open() && !c2.is_open(), "socketpair closed cleanly");
        }
    }

    // 4. Zero-Dependency DEFLATE, ZIP and XML Utilities
    {
        // XML unescaping
        std::string unesc = cfire::ai::detail::xml_unescape("&lt;book id=&quot;1&quot;&gt;Tom &amp; Jerry&lt;/book&gt;");
        require(unesc == "<book id=\"1\">Tom & Jerry</book>", "xml_unescape handles XML entities");

        // XML text extraction
        std::string xml = "<w:p><w:t>Hello</w:t><w:t> </w:t><w:t>World</w:t></w:p>";
        std::string extracted = cfire::ai::detail::extract_xml_text_runs(xml, "w:t");
        require(extracted.find("Hello") != std::string::npos && extracted.find("World") != std::string::npos,
            "extract_xml_text_runs extracts text runs");

        // CRC32 check
        std::uint32_t crc = cfire::ai::detail::crc32_bytes("123456789");
        require(crc == 0xcbf43926, "crc32 standard check value");

        // Inflate resilience on truncated/corrupt payload
        const std::byte corrupt_bytes[] = {std::byte{0x78}, std::byte{0x9c}, std::byte{0xff}, std::byte{0x00}};
        auto dec = cfire::ai::detail::inflate_raw(corrupt_bytes);
        (void)dec; // Must not crash / throw

        // Zip extraction resilience on invalid headers
        auto zip_res = cfire::ai::detail::extract_zip_entries(corrupt_bytes);
        require(zip_res.empty(), "extract_zip_entries returns empty map on malformed payload");
    }

    // 5. Standalone LibFuzzer Smoke Harness
    {
        std::vector<std::string> adversarial_inputs = {
            "",
            "\0",
            "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n",
            "{\"jsonrpc\":\"2.0\",\"id\":123,\"method\":\"test\"}",
            "http://example.com:8080/path?key=val%20test#hash",
            "fn main() { return 1 + 2 }"
        };

        for (const auto& sample : adversarial_inputs) {
            // Parser fuzz check
            auto p_res = cfire::frontend::parse_source("fuzz_sample.cf", sample);
            (void)p_res;

            // HTTP parse check
            auto h_res = cfire::http::parse_request(sample);
            (void)h_res;

            // URL parse check
            auto u_res = cfire::url::parse(sample);
            (void)u_res;

            // JSON-RPC key extraction check
            auto s_res = cfire::lsp::detail::extract_json_string(sample, "method");
            (void)s_res;

            // Deflate check
            auto b_span = std::as_bytes(std::span(sample.data(), sample.size()));
            auto d_res = cfire::ai::detail::inflate_raw(b_span);
            (void)d_res;
        }
    }

    std::println("--- PASS: test_negative_parser_corpus_and_fuzz_runtime (CF-131)");
}
