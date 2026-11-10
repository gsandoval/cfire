#pragma once
// tests/unit/test_lsp_docgen.hpp — Formatter, LSP 3.17, Docgen & Refactoring Unit Tests

#include "../test_harness.hpp"
#include "../../src/mcp.hpp"

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

    // CF-096: Formatter string-awareness (URL with // and trailing comment) and token invariance
    std::string struct_with_url_cf = R"(package main

type Config struct {
    url:      str = "http://x.com"
    endpoint: str = "https://api.example.com/v1" // api base
}
)";
    auto fmt_url = cfire::fmt::format_source(struct_with_url_cf, "config.cf");
    require(fmt_url.has_value() && *fmt_url == struct_with_url_cf,
        std::format("expected struct with URL default and comment to format identically, got:\n{}",
            fmt_url.value_or("<error>")));

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
    lsp_in << rpc_frame(R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp_test.cf","text":"package main\nimport \"core:fs\"\ntype Metric struct {\n    Service: str `json:\"service\"`\n    Count: i64\n}\nfn bump(mut x: i32) {\n    x += 1\n}\nfn main() {\n    m := Metric{Service: \"api\", Count: 1}\n    mut a: i32 = 5\n    bump(a)\n    _ = fs.read_bytes(m.Service)\n}\n"}}})");
    lsp_in << rpc_frame(R"({"jsonrpc":"2.0","id":2,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///tmp/lsp_test.cf"},"position":{"line":13,"character":14}}})");
    lsp_in << rpc_frame(R"({"jsonrpc":"2.0","id":3,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp_test.cf"},"position":{"line":13,"character":12}}})");
    lsp_in << rpc_frame(R"({"jsonrpc":"2.0","id":4,"method":"textDocument/definition","params":{"textDocument":{"uri":"file:///tmp/lsp_test.cf"},"position":{"line":12,"character":5}}})");
    lsp_in << rpc_frame(R"({"jsonrpc":"2.0","id":5,"method":"textDocument/documentSymbol","params":{"textDocument":{"uri":"file:///tmp/lsp_test.cf"}}})");
    lsp_in << rpc_frame(R"({"jsonrpc":"2.0","id":6,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///tmp/lsp_test.cf"},"position":{"line":13,"character":26}}})");
    lsp_in << rpc_frame(R"({"jsonrpc":"2.0","id":7,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp_test.cf"},"position":{"line":13,"character":25}}})");
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
    require(rpc_resp.contains("cfire::fs::read_bytes"),
        "LSP hover on fs.read_bytes expected C++23 lowering");
    require(rpc_resp.contains("\"label\":\"write_bytes\""),
        "LSP completion on fs. expected write_bytes");
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
        "LSP inlayHint expected stdlib parameter hint `path:` on fs.read_bytes(m.Service) (IDE-EXT-01)");
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
            rpc_resp.contains(R"("label":"read_bytes")") &&
            rpc_resp.contains(R"("insertText":"fs.read_bytes(${1:path})$0")") &&
            rpc_resp.contains(R"("insertTextFormat":2)") &&
            rpc_resp.contains("core:fs (auto-import)") &&
            rpc_resp.contains(R"("additionalTextEdits":[)") &&
            rpc_resp.contains("import \\\"core:fs\\\""),
        "LSP auto-import completion for unimported stdlib module expected additionalTextEdits (LSP-05)");
    require(rpc_resp.contains(R"("id":33)") &&
            rpc_resp.contains(R"("label":"read_bytes")") &&
            rpc_resp.contains(R"("insertText":"fs.read_bytes(${1:path})$0")") &&
            rpc_resp.contains("fs: fn read_bytes"),
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

    // 2f. Test LSP structure-aware JSON-RPC key extraction (CPP-QA-03: embedded JSON strings with "id": / "method": / "uri": / "line":)
    {
        cfire::lsp::Server qa_srv(10);
        std::stringstream qa_in;
        std::stringstream qa_out;

        std::string_view qa_src = R"(package main

fn main() -> i32 {
    msg := "{\"id\":999,\"method\":\"shutdown\",\"uri\":\"file:///tmp/wrong.cf\",\"line\":99,\"character\":88}"
    val := 42
    return val
}
)";
        qa_in << rpc_frame(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})");
        qa_in << rpc_frame(std::format(
            R"({{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{{"textDocument":{{"text":"{}","uri":"file:///tmp/qa03.cf","languageId":"cfire","version":1}}}}}})",
            cfire::lsp::detail::json_escape(qa_src)));
        qa_in << rpc_frame(R"({"jsonrpc":"2.0","id":301,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///tmp/qa03.cf"},"position":{"line":4,"character":4}}})");
        qa_in << rpc_frame(R"({"jsonrpc":"2.0","id":302,"method":"shutdown","params":null})");
        qa_in << rpc_frame(R"({"jsonrpc":"2.0","method":"exit","params":null})");

        qa_srv.handle_stream(qa_in, qa_out);
        std::string qa_resp = qa_out.str();

        require(qa_resp.contains(R"("id":301)") && qa_resp.contains("val: i64"),
            "LSP structure-aware JSON key extraction ignores embedded JSON keys inside source strings (CPP-QA-03)");
        require(!qa_resp.contains(R"("id":999)"),
            "LSP did not confuse embedded \"id\":999 in string literal with RPC message id (CPP-QA-03)");
    }

    // 2g. Test LSP completion & hover for ext:webui (App handle methods and typed bind[T] block parameter)
    {
        cfire::lsp::Server webui_srv(10);
        std::stringstream w_in;
        std::stringstream w_out;

        std::string_view webui_src = R"(package main

import "ext:webui"

type TrackMix struct {
    Channel: i64 `json:"channel"`
    Gain: f64 `json:"gain"`
}

fn main() {
    webui.
    mut app := webui.new_app("Studio", 800, 600)
    app.
    app.bind[TrackMix]("set_track") |req| {
        req.
    }
}
)";
        w_in << rpc_frame(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})");
        w_in << rpc_frame(std::format(
            R"({{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{{"textDocument":{{"text":"{}","uri":"file:///tmp/webui_lsp.cf","languageId":"cfire","version":1}}}}}})",
            cfire::lsp::detail::json_escape(webui_src)));
        w_in << rpc_frame(R"({"jsonrpc":"2.0","id":401,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/webui_lsp.cf"},"position":{"line":10,"character":10}}})");
        w_in << rpc_frame(R"({"jsonrpc":"2.0","id":402,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/webui_lsp.cf"},"position":{"line":12,"character":8}}})");
        w_in << rpc_frame(R"({"jsonrpc":"2.0","id":403,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/webui_lsp.cf"},"position":{"line":14,"character":12}}})");
        w_in << rpc_frame(R"({"jsonrpc":"2.0","id":404,"method":"shutdown","params":null})");
        w_in << rpc_frame(R"({"jsonrpc":"2.0","method":"exit","params":null})");

        webui_srv.handle_stream(w_in, w_out);
        std::string w_resp = w_out.str();

        require(w_resp.contains(R"("id":401)") && w_resp.contains(R"("label":"new_app")"),
            "LSP completion on webui. expected new_app (WEBUI-P0-06)");
        require(w_resp.contains(R"("id":402)") &&
                w_resp.contains(R"("label":"bind")") &&
                w_resp.contains(R"("label":"bind_action")") &&
                w_resp.contains(R"("label":"on")") &&
                w_resp.contains(R"("label":"emit")") &&
                w_resp.contains(R"("label":"serve_dir")") &&
                w_resp.contains(R"("label":"run")"),
            "LSP completion on app. expected webui.App handle methods (WEBUI-P0-06)");
        require(w_resp.contains(R"("id":403)") &&
                w_resp.contains(R"("label":"Channel")") &&
                w_resp.contains(R"("label":"Gain")"),
            "LSP completion on req. inside app.bind[TrackMix] block expected struct fields Channel and Gain (WEBUI-P0-06)");
    }

    // 2f. Chained method completion (`try os.stdin().`), `: []str` inlay hints on chained calls, parameter snippets, and onTypeFormatting
    {
        cfire::lsp::Server chain_srv;
        std::stringstream c_in;
        std::stringstream c_out;

        std::string chain_src = R"(package main
import "core:os"
import "core:str"

fn main() {
    lines := try os.stdin().read_lines()
    for line in lines {
    v := str.
    }
    x := try os.stdin().
}
)";
        c_in << rpc_frame(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})");
        c_in << rpc_frame(std::format(
            R"({{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{{"textDocument":{{"text":"{}","uri":"file:///tmp/chain_lsp.cf","languageId":"cfire","version":1}}}}}})",
            cfire::lsp::detail::json_escape(chain_src)));
        // 501: completion on `try os.stdin().` at line 9, character 24
        c_in << rpc_frame(R"({"jsonrpc":"2.0","id":501,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/chain_lsp.cf"},"position":{"line":9,"character":24}}})");
        // 502: completion on `str.` at line 7, character 13
        c_in << rpc_frame(R"({"jsonrpc":"2.0","id":502,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/chain_lsp.cf"},"position":{"line":7,"character":13}}})");
        // 503: inlayHint for `lines := try os.stdin().read_lines()` -> `: []str`
        c_in << rpc_frame(R"({"jsonrpc":"2.0","id":503,"method":"textDocument/inlayHint","params":{"textDocument":{"uri":"file:///tmp/chain_lsp.cf"}}})");
        // 504: hover on `line` in `for line in lines` at line 6, character 9 -> `line: str`
        c_in << rpc_frame(R"({"jsonrpc":"2.0","id":504,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///tmp/chain_lsp.cf"},"position":{"line":6,"character":9}}})");
        // 505: onTypeFormatting on `\n` after `for line in lines {` (line 7, character 0)
        c_in << rpc_frame(R"({"jsonrpc":"2.0","id":505,"method":"textDocument/onTypeFormatting","params":{"textDocument":{"uri":"file:///tmp/chain_lsp.cf"},"position":{"line":7,"character":0},"ch":"\n"}})");
        c_in << rpc_frame(R"({"jsonrpc":"2.0","id":506,"method":"shutdown","params":null})");
        c_in << rpc_frame(R"({"jsonrpc":"2.0","method":"exit","params":null})");

        chain_srv.handle_stream(c_in, c_out);
        std::string c_resp = c_out.str();

        require(c_resp.contains(R"("documentOnTypeFormattingProvider")"),
            "LSP initialize advertises documentOnTypeFormattingProvider");
        require(c_resp.contains(R"("id":501)") &&
                c_resp.contains(R"("label":"read_lines")") &&
                c_resp.contains(R"("insertText":"read_lines()$0")") &&
                c_resp.contains(R"("label":"read_text")"),
            "LSP second-level completion on 'try os.stdin().' returns fs.File methods with snippet tabstops");
        require(c_resp.contains(R"("id":502)") &&
                c_resp.contains(R"("label":"parse_i32")") &&
                c_resp.contains(R"("insertText":"parse_i32(${1:s})$0")"),
            "LSP completion on 'str.' emits parameter tabstop snippet for parse_i32");
        require(c_resp.contains(R"("id":503)") &&
                c_resp.contains(R"("label":": []str")"),
            "LSP inlayHint infers ': []str' for 'lines := try os.stdin().read_lines()'");
        require(c_resp.contains(R"("id":504)") &&
                c_resp.contains("line: str"),
            "LSP hover infers 'line: str' for 'for line in lines' where lines is []str");
        require(c_resp.contains(R"("id":505)") &&
                c_resp.contains(R"("newText":"        ")"),
            "LSP onTypeFormatting on newline after 'for line in lines {' indents by 8 spaces");
    }

    auto ide_res = cfire::lsp::configure_ide(rt_dir.parent_path(), "zed");
    require(ide_res.has_value(), "cfire::lsp::configure_ide(zed)");

    // 3. Test `cfire doc` always-up-to-date documentation generation & verification
    auto docs_dir = rt_dir.parent_path() / "docs";
    auto gen_docs = cfire::doc::sync_docs_directory(docs_dir, false, rt_dir);
    require(gen_docs.has_value(), "cfire::doc::sync_docs_directory write");
    auto check_docs = cfire::doc::sync_docs_directory(docs_dir, true, rt_dir);
    require(check_docs.has_value(), "cfire::doc::sync_docs_directory --check verification");

    auto audit_res = cfire::doc::audit_documentation_catalog(rt_dir);
    require(audit_res.has_value(), "cfire::doc::audit_documentation_catalog verification: " + (audit_res.has_value() ? "" : audit_res.error()));

    auto discovered = cfire::doc::discover_runtime_modules(rt_dir);
    auto has_module = [&](std::string_view name) {
        return std::ranges::any_of(discovered, [&](const auto& m) { return m.import_path == name; });
    };
    require(has_module("vendor:raylib"), "catalog discovered vendor:raylib");
    require(has_module("vendor:raygui"), "catalog discovered vendor:raygui");
    require(has_module("core:math"), "catalog discovered core:math");
    require(has_module("ext:ai"), "catalog discovered ext:ai");
    require(has_module("ext:db/sqlite"), "catalog discovered ext:db/sqlite");
    require(has_module("ext:webui"), "catalog discovered ext:webui");

    auto pkg_md = cfire::doc::extract_file_markdown_doc("examples/09-stdlib-test-pointers/main.cf");
    require(pkg_md.has_value() && pkg_md->contains("compute_fixed_sum") && pkg_md->contains("type Metric struct"),
        "cfire::doc::extract_file_markdown_doc check");

    // 4. Test Core Library additions and catalog alignment
    {
        require(cfire::str::contains("cfire language", "fire"), "cfire::str::contains check");
        require(!cfire::str::contains("cfire language", "rust"), "cfire::str::contains false check");
        require(cfire::str::replace("hello world", "world", "cfire") == "hello cfire", "cfire::str::replace check");
        require(cfire::math::clamp_f64(1.5, 0.0, 1.0) == 1.0, "cfire::math::clamp_f64 check");
        require(cfire::math::sqrt(16.0) == 4.0, "cfire::math::sqrt check");
        auto r_u64 = cfire::rand::u64();
        auto r_unit = cfire::rand::f64_unit();
        require(r_unit >= 0.0 && r_unit <= 1.0, "cfire::rand::f64_unit check");
        (void)r_u64;

        auto test_tmp_file = std::filesystem::temp_directory_path() / "cfire_io_test.bin";
        std::vector<std::byte> payload = {std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE}, std::byte{0xEF}};
        auto w_res = cfire::fs::write_bytes(test_tmp_file, payload);
        require(w_res.has_value(), "cfire::fs::write_bytes check");
        auto r_res = cfire::fs::read_bytes(test_tmp_file);
        require(r_res.has_value() && *r_res == payload, "cfire::fs::read_bytes check");
        std::filesystem::remove(test_tmp_file);

        auto exec_res = cfire::os::exec_output("echo 'cfire_test'");
        require(exec_res.has_value() && exec_res->contains("cfire_test"), "cfire::os::exec_output check");

        // Verify Guide Build System and Raylib/Raygui reconciliation
        auto guide_path = docs_dir / "GUIDE.md";
        auto guide_txt = cfire::build::read_file_text(guide_path);
        require(guide_txt.has_value() &&
                guide_txt->contains("build.CC(") &&
                guide_txt->contains("KVStore := build.CMake(") &&
                guide_txt->contains("build.Make") &&
                guide_txt->contains("build.Git") &&
                guide_txt->contains("build.Archive") &&
                guide_txt->contains("build.Command") &&
                guide_txt->contains("--target=wasm") &&
                guide_txt->contains("vendor:raylib") &&
                guide_txt->contains("vendor:raygui"),
            "docs/GUIDE.md Build System and Raylib/Raygui reconciliation");

        // DOC-004: Verify chapter-scoped subheading IDs, nested lists, [!CAUTION] callout rendering, and bite-sized topic chunks
        std::string sample_md = R"MD(# Guide Title
## Chapter 1: First Chapter
### C++23 Lowering
- Top item
  - Sub item A
  - Sub item B
> [!CAUTION]
> High risk action.
#### Under the Hood (C++23 Lowering)
```cpp
int x = 42;
```
**Expected Output:**
```text
ok
```
## Chapter 2: Second Chapter
### C++23 Lowering
)MD";
        std::string rendered_html = cfire::doc::render_guide_markdown(sample_md);
        require(rendered_html.contains("id=\"ch1-c23-lowering\"") &&
                rendered_html.contains("id=\"ch2-c23-lowering\""),
            "render_guide_markdown generates chapter-scoped h3 anchor IDs (DOC-001)");
        require(rendered_html.contains("<li>Top item\n    <ul>\n      <li>Sub item A</li>"),
            "render_guide_markdown nests indented sub-bullets inside parent <li> (DOC-002)");
        require(rendered_html.contains("callout-caution") && rendered_html.contains("<div class=\"callout-title\">Caution</div>"),
            "render_guide_markdown renders [!CAUTION] callout blocks (DOC-003)");
        require(rendered_html.contains("topic-pill-bar") &&
                rendered_html.contains("guide-topic") &&
                rendered_html.contains("topic-pager") &&
                rendered_html.contains("lowering-drawer") &&
                rendered_html.contains("terminal-tray"),
            "render_guide_markdown emits bite-sized topic units, topic-pill-bar, lowering-drawer, and terminal-tray");

        std::string ref_html = cfire::doc::generate_reference_html();
        require(ref_html.contains("ref-pane-overview") &&
                ref_html.contains("ref-pane-mod-str") &&
                ref_html.contains("ref-pane-mod-ai") &&
                ref_html.contains("ref-pane-diagnostics"),
            "generate_reference_html emits overview dashboard, per-module panes, and diagnostics pane");

        // Verify CLI --version, --help, and doctor --json
        run_cfire("--version")
            .assert_ok("cfire --version exits 0")
            .assert_contains(std::format("cfire {}", cfire::version::kVersion), "cfire --version reports canonical kVersion");
        run_cfire("--help")
            .assert_ok("cfire --help exits 0")
            .assert_contains("doctor", "cfire --help lists doctor command")
            .assert_contains("setup", "cfire --help lists setup command");
        run_cfire("doctor --json")
            .assert_ok("cfire doctor --json exits 0")
            .assert_contains("\"healthy\": true", "cfire doctor --json reports healthy")
            .assert_contains("\"runtime_matched\": true", "cfire doctor --json reports runtime_matched")
            .assert_contains("\"runtime_dir\":", "cfire doctor --json reports runtime_dir");

        // Verify runtime provenance skew detection (version and commit mismatch)
        {
            auto tmp_rt = std::filesystem::temp_directory_path() / "cfire_test_rt_prov";
            std::error_code rt_ec;
            std::filesystem::remove_all(tmp_rt, rt_ec);
            std::filesystem::create_directories(tmp_rt, rt_ec);
            {
                std::ofstream ofs(tmp_rt / "cfire_prelude.hpp");
                ofs << "#pragma once\n// CFIRE_RUNTIME_VERSION: " << cfire::version::kVersion << "\n";
            }
            auto prov_ok = cfire::version::read_runtime_provenance(tmp_rt);
            require(prov_ok.matched, "read_runtime_provenance matches identical version");

            {
                std::ofstream ofs(tmp_rt / "VERSION");
                ofs << "0.0.1 deadbeef\n";
            }
            auto prov_bad_ver = cfire::version::read_runtime_provenance(tmp_rt);
            require(!prov_bad_ver.matched && prov_bad_ver.mismatch_reason.contains("v0.0.1"),
                "read_runtime_provenance detects version mismatch in runtime/VERSION");

            if (cfire::version::strip_dirty(cfire::version::kGitSha) != "dev") {
                std::ofstream ofs(tmp_rt / "VERSION");
                ofs << cfire::version::kVersion << " 0000000\n";
                ofs.close();
                auto prov_bad_sha = cfire::version::read_runtime_provenance(tmp_rt);
                require(!prov_bad_sha.matched && prov_bad_sha.mismatch_reason.contains("0000000"),
                    "read_runtime_provenance detects commit SHA mismatch in runtime/VERSION");
            }
            std::filesystem::remove_all(tmp_rt, rt_ec);
        }

        // Verify review remediation invariants:
        // 1. No GNU statement expressions `({` or `std::exit(1)` in emitted showcase C++23
        auto idx_html_res = cfire::build::read_file_text(docs_dir / "index.html");
        require(idx_html_res.has_value(), "read docs/index.html");
        if (idx_html_res) {
            const std::string& idx_html = *idx_html_res;
            require(!idx_html.contains("= ({") && !idx_html.contains("std::exit(1)"),
                "docs/index.html contains no GNU statement expressions or std::exit(1)");
            require(idx_html.contains("fh_fnv1a(cfire::c_str(msg), msg.size())"),
                "docs/index.html Tab 5 emits cfire::c_str(msg) coercion via Clang AST bridge");
            require(idx_html.contains("fnv1a hash: 0x268ccafd"),
                "docs/index.html Tab 5 displays verified FNV-1a digest 0x268ccafd");
            require(idx_html.contains("Match [Transpiler]: cfire lowers 1-to-1 to ISO C++23 with zero runtime overhead."),
                "docs/index.html Tab 7 displays matching [Transpiler] title and section text");
            require(!idx_html.contains("4.2k"),
                "docs/index.html does not contain hardcoded 4.2k star count");
        }

        // 2. Verify codegen hygiene for AI + JSON + SQLite snippet (deduplicated defines/links, no unused headers)
        std::string ai_snip_cpp = cfire::doc::compile_snippet_to_cpp("test_ai_hygiene.cf", R"CF(package main
import "ext:ai" as ai
import "ext:db/sqlite" as sqlite
import "core:json" as json
fn main() {
    doc := try ai.parse_document("# Transpiler\ncfire lowers 1-to-1 to ISO C++23 with zero runtime overhead.", "arch.md")
    println("{}", doc.sections[0].title)
}
)CF");
        auto count_substr = [](std::string_view hay, std::string_view needle) {
            std::size_t count = 0, pos = 0;
            while ((pos = hay.find(needle, pos)) != std::string_view::npos) {
                ++count;
                pos += needle.size();
            }
            return count;
        };
        require(count_substr(ai_snip_cpp, "#define CFIRE_USE_SIMDJSON 1") == 1,
            "codegen emits #define CFIRE_USE_SIMDJSON 1 exactly once");
        require(count_substr(ai_snip_cpp, "-lsimdjson") == 1,
            "codegen emits -lsimdjson link tag exactly once");
        require(!ai_snip_cpp.contains("#include \"core/fs.hpp\"") &&
                !ai_snip_cpp.contains("#include \"core/net.hpp\"") &&
                !ai_snip_cpp.contains("#include \"core/os.hpp\"") &&
                !ai_snip_cpp.contains("#include \"core/encoding.hpp\""),
            "codegen does not emit unused transitive core headers when importing core:ai");

        // 3. Execute showcase snippets 2, 3, 5, and 7 end-to-end and verify exact stdout match
        run_cfire_source(R"(package main
type Token enum { Ident(name: str), Number(val: i64), Eof }
fn classify(s: []str) -> str {
    match s {
        []                   => { return "empty" }
        [only]               => { return "single=" + only }
        [first, ..mid, last] => { return first + "..(" + str(mid.len()) + ").." + last }
    }
    return "other"
}
fn main() {
    items: []str = ["alpha", "beta", "gamma"]
    println("result: {}", classify(items))
})", "showcase_tab2")
            .assert_ok("Showcase Tab 2 runs cleanly")
            .assert_contains("result: alpha..(1)..gamma", "Showcase Tab 2 output matches telemetry");

        run_cfire_source(R"(package main
import cpp "fast_hash.h"
fn main() {
    msg := "cfire systems programming"
    hash := cpp::fh_fnv1a(msg, msg.len())
    println("fnv1a hash: 0x{:x}", hash)
})", "showcase_tab5")
            .assert_ok("Showcase Tab 5 runs cleanly")
            .assert_contains("fnv1a hash: 0x268ccafd", "Showcase Tab 5 output matches telemetry");

        run_cfire_source(R"CF(package main
import "ext:ai" as ai
import "ext:db/sqlite" as sqlite
import "core:json" as json
type QueryPlan struct {
    Target: str `json:"target" desc:"Search target concept"`
    TopK: i64   `json:"top_k" desc:"Number of top results"`
}
type DocEntry struct {
    Id: i64      `sql:"id"`
    Title: str   `sql:"title"`
    Snippet: str `sql:"snippet"`
}
fn main() {
    schema := json.schema[QueryPlan]()
    doc := try ai.parse_document("# Transpiler\ncfire lowers 1-to-1 to ISO C++23 with zero runtime overhead.", "arch.md")
    chunks := ai.chunk_document(doc, 512, 64)
    client := ai.local()
    mut db := try sqlite.open(":memory:")
    try db.exec("CREATE TABLE docs (id INTEGER, title TEXT, snippet TEXT, embedding BLOB)")
    for ch in chunks {
        vec := try client.embed(ch.text)
        try db.exec("INSERT INTO docs VALUES (?1, ?2, ?3, ?4)", ch.index, ch.title, ch.text, vec)
    }
    qvec := try client.embed("modern C++23 compiler")
    hits := try db.query[DocEntry]("SELECT id, title, snippet FROM docs ORDER BY vec_cosine_similarity(embedding, ?1) DESC LIMIT 1", qvec)
    if hits.len() > 0 {
        println("Match [{}]: {}", hits[0].Title, hits[0].Snippet)
    }
}
)CF", "showcase_tab7")
            .assert_ok("Showcase Tab 7 runs cleanly")
            .assert_contains("Match [Transpiler]: cfire lowers 1-to-1 to ISO C++23 with zero runtime overhead.",
                "Showcase Tab 7 output matches telemetry");
    }

    // 7. Test DOC-SYNC, LSP-SYNC & MCP-SYNC for ERG-01..ERG-05
    {
        auto llm_ref = cfire::doc::generate_llm_reference();
        require(llm_ref.contains("Same-Directory Package Merging") &&
                llm_ref.contains("for i in 0..n") &&
                llm_ref.contains("for i in 1..=n") &&
                llm_ref.contains("assert_eq") &&
                llm_ref.contains("cfire init") &&
                llm_ref.contains("pkg:tako"),
            "DOC-01a: generate_llm_reference() documents ERG-01..ERG-05 and pkg:tako");

        auto agent_rules = cfire::lsp::cfire_agent_rules_markdown("cfire");
        require(agent_rules.contains("Same-Directory Package Merging") &&
                agent_rules.contains("0..n") &&
                agent_rules.contains("assert_eq") &&
                agent_rules.contains("pkg:tako"),
            "DOC-01b: cfire_agent_rules_markdown() documents ERG-01..ERG-05 and pkg:tako");

        ScopedTempDir sync_tmp("cfire_lsp_mcp_sync");
        auto types_cf = sync_tmp.file("types.cf", R"CF(package main

type Counter struct {
    Value: i64
}

type Runner interface {
    fn step(delta: i64) -> i64
}

fn (mut self: Counter) add_delta(amount: i64) -> i64 {
    self.Value += amount
    return self.Value
}
)CF");

        auto app_cf = sync_tmp.file("app.cf", R"CF(package main

fn run_loop() {
    mut c := Counter{Value: 10}
    defer {
        _ = c.add_delta(5)
    }
    for idx in 0..4 {
        _ = c.add_delta(idx)
    }
    assert_eq(c.Value, 16)
}

test "counter sibling test" {
    mut c := Counter{Value: 1}
    assert_eq(c.add_delta(2), 3)
}
)CF");
        sync_tmp.file(".tako/ignored.cf", "package ignored\nfn should_not_appear() {}\n");

        cfire::lsp::Server lsp_sync;
        std::string app_uri = cfire::lsp::detail::path_to_uri(app_cf);
        std::string types_uri = cfire::lsp::detail::path_to_uri(types_cf);

        // LSP-01a: Hover on range-loop variable `idx` (0-based line 8, col 25)
        std::string hover_idx = lsp_sync.compute_hover(app_uri, 8, 25);
        require(hover_idx.contains("idx: i64"),
            std::format("LSP-01a: expected range loop variable 'idx' hover to report 'idx: i64', got: {}", hover_idx));

        // LSP-01b & LSP-01c: Inlay hints inside `defer { _ = c.add_delta(5) }` resolving sibling receiver method `add_delta(amount: i64)`
        std::string hints = lsp_sync.compute_inlay_hints(app_uri);
        require(hints.contains("amount:"),
            std::format("LSP-01b/c: expected inlay hints to include 'amount:' for sibling method call inside defer block, got: {}", hints));

        // LSP-01c: Go-to-definition on `c.add_delta` (0-based line 5, col 16) resolves to `types.cf`
        std::string def_loc = lsp_sync.compute_definition(app_uri, 5, 16);
        require(def_loc.contains(types_uri),
            std::format("LSP-01c: expected go-to-definition on sibling method 'add_delta' to resolve to '{}', got: {}", types_uri, def_loc));

        // LSP-01d: Hover on `assert_eq` (0-based line 10, col 6)
        std::string hover_assert_eq = lsp_sync.compute_hover(app_uri, 10, 6);
        require(hover_assert_eq.contains("assert_eq"),
            std::format("LSP-01d: expected hover on assert_eq, got: {}", hover_assert_eq));

        // MCP-01a & MCP-01b: Test `cfire_check` with workspace `path` and `cfire_outline` filtering `.tako` & showing `interface`/`test`
        auto mcp_srv = cfire::mcp::build_compiler_mcp_server(rt_dir);
        std::string check_req = std::format(
            R"({{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{{"name":"cfire_check","arguments":{{"path":"{}"}}}}}})",
            cfire::lsp::detail::json_escape(sync_tmp.path.string()));
        std::string check_resp = mcp_srv.handle_jsonrpc(check_req);
        require(check_resp.contains("OK (0 errors)"),
            std::format("MCP-01a: expected cfire_check on multi-file workspace path to return 'OK (0 errors)', got: {}", check_resp));

        std::string outline_req = std::format(
            R"({{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{{"name":"cfire_outline","arguments":{{"path":"{}"}}}}}})",
            cfire::lsp::detail::json_escape(sync_tmp.path.string()));
        std::string outline_resp = mcp_srv.handle_jsonrpc(outline_req);
        require(outline_resp.contains("interface `Runner`") &&
                outline_resp.contains("counter sibling test") &&
                !outline_resp.contains("should_not_appear"),
            std::format("MCP-01b: expected cfire_outline to include interface/test and skip .tako, got: {}", outline_resp));

        // CF-095: MCP security — argv exec without shell injection and isolated eval
        std::string eval_req = R"JSON({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"cfire_eval","arguments":{"code":"println(\"mcp-secure-eval-ok\")"}}})JSON";
        std::string eval_resp = mcp_srv.handle_jsonrpc(eval_req);
        require(eval_resp.contains("mcp-secure-eval-ok"),
            std::format("CF-095: expected cfire_eval to run cleanly and output 'mcp-secure-eval-ok', got: {}", eval_resp));

        std::string test_inject_req = std::format(
            R"JSON({{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{{"name":"cfire_test","arguments":{{"path":"{}","filter":"$(echo injected)"}}}}}})JSON",
            cfire::lsp::detail::json_escape(sync_tmp.path.string()));
        std::string test_inject_resp = mcp_srv.handle_jsonrpc(test_inject_req);
        // The literal filter `$(echo injected)` must NOT be expanded by a shell
        require(!test_inject_resp.contains("injected"),
            std::format("CF-095: expected direct argv execution to prevent shell expansion, got: {}", test_inject_resp));
    }

    std::println("--- PASS: test_formatter_lsp_and_docgen");
}

void test_cfire_refactor_rename([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_cfire_refactor_rename");

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

    // 7. String interpolation format specifiers ("{expr:spec}"), core:log ergonomics, and LSP/MCP symbol intelligence inside "{...}"
    {
        // 7a. End-to-end execution of "{expr:spec}" format specifiers + core:log non-string attributes + log.Level.Debug
        run_cfire_source(R"(package main
import "core:log"

fn scale_val(x: f64, factor: f64) -> f64 {
    return x * factor
}

fn main() {
    pi := 3.14159265
    mask := 255
    css := "{color: red}"
    msg := "pi={scale_val(pi, 2.0):.2f} hex=0x{mask:04x} css={css}"
    println("{}", msg)

    log.set_level(log.Level::Debug)
    log.set_format(log.Format::Text)
    assert_eq(log.get_level(), log.Level::Debug, "log.Level::Debug equals log.Level::Debug")
    log.info("request handled {mask:02x}", "port", 8080, "ok", true)
}
)", "interp_fmt_and_log")
            .assert_ok("interp format specifiers and core:log non-string attrs compile and run")
            .assert_contains("pi=6.28 hex=0x00ff css={color: red}", "inline format specifiers .2f and 04x + unsplit CSS literal")
            .assert_contains("port=8080", "core:log formats integer attribute via cfire::str_ctor")
            .assert_contains("ok=true", "core:log formats boolean attribute via cfire::str_ctor");

        // 7b. LSP Hover, Definition, References, Semantic Tokens, Inlay Hints, Call Hierarchy, Completion & MCP inside "{...}"
        ScopedTempDir interp_tmp("cfire_interp_lsp_mcp");
        auto interp_cf = interp_tmp.file("interp.cf", R"CF(package main
import "core:log"

fn compute_metric(base: f64, weight: f64) -> f64 {
    return base * weight
}

fn run_demo() {
    sample_val := 12.3456
    summary := "plain sample_val metric={compute_metric(sample_val, 1.5):.2f}"
    log.set_level(log.Level::Debug)
    log.info("status={sample_val:.1f}", "count", 42)
    println("{}", summary)
}
)CF");

        cfire::lsp::Server srv;
        std::string uri = cfire::lsp::detail::path_to_uri(interp_cf);

        // Line 9 (0-based): `    summary := "plain sample_val metric={compute_metric(sample_val, 1.5):.2f}"`
        // Plain word "sample_val" inside string literal outside `{...}` is at col 22 -> must NOT trigger variable hover
        std::string plain_hover = srv.compute_hover(uri, 9, 24);
        require(!plain_hover.contains("Local Binding"),
            std::format("plain word inside string literal outside {{...}} must not trigger symbol hover, got: {}", plain_hover));

        // Inside `{compute_metric(sample_val, 1.5):.2f}`:
        // `compute_metric` starts at col 41 (0-based), `sample_val` inside call starts at col 56 (0-based)
        std::string fn_hover = srv.compute_hover(uri, 9, 44);
        require(fn_hover.contains("compute_metric"),
            std::format("hover on function call inside '{{...}}' replacement string resolves compute_metric, got: {}", fn_hover));

        std::string var_hover = srv.compute_hover(uri, 9, 58);
        require(var_hover.contains("sample_val") && var_hover.contains("f64"),
            std::format("hover on variable inside '{{...}}' replacement string resolves sample_val: f64, got: {}", var_hover));

        std::string var_def = srv.compute_definition(uri, 9, 58);
        require(var_def.contains("\"line\":8"),
            std::format("go-to-definition on sample_val inside '{{...}}' resolves to line 8 (0-based), got: {}", var_def));

        // References on `sample_val` (declared on line 8, used in `{compute_metric(sample_val, 1.5):.2f}` on line 9 and `{sample_val:.1f}` on line 11)
        // Note: plain `sample_val` outside `{...}` on line 9 is ignored because it's inside the string literal!
        auto occs = srv.find_symbol_occurrences(uri, 9, 58, "sample_val", "");
        require(occs.size() == 3,
            std::format("find_symbol_occurrences for sample_val finds declaration + 2 interpolated uses (ignoring plain string text), got {}", occs.size()));

        // Inlay hints inside `{compute_metric(sample_val, 1.5):.2f}` should include `base:` and `weight:`
        std::string inlay = srv.compute_inlay_hints(uri);
        require(inlay.contains("base:") && inlay.contains("weight:"),
            std::format("inlay hints resolve parameter labels inside '{{...}}' replacement string, got: {}", inlay));

        // Semantic tokens split the interpolated string and highlight `compute_metric` and `sample_val`
        std::string sem_toks = srv.compute_semantic_tokens(uri);
        require(sem_toks.contains(",14,0") && sem_toks.contains(",6,0"),
            std::format("semantic tokens emit operator (14) for braces and property (6) for :.2f format spec, got: {}", sem_toks));

        // Hover & Completion on `log.Level.Debug` (line 10, col 29)
        std::string lvl_hover = srv.compute_hover(uri, 10, 29);
        require(lvl_hover.contains("log.Level.Debug") && lvl_hover.contains("cfire::log::Level::Debug"),
            std::format("hover on log.Level.Debug resolves enum variant documentation, got: {}", lvl_hover));

        std::string lvl_comp = srv.compute_completions(uri, 10, 28);
        require(lvl_comp.contains("\"label\":\"Debug\"") && lvl_comp.contains("\"label\":\"Info\""),
            std::format("completion on log.Level. offers Debug/Info/Warn/Error, got: {}", lvl_comp));

        // MCP `cfire_inspect` on `sample_val` inside `{compute_metric(sample_val, 1.5):.2f}` (1-based line 10, col 59)
        auto mcp_srv = cfire::mcp::build_compiler_mcp_server(rt_dir);
        std::string inspect_req = std::format(
            R"({{"jsonrpc":"2.0","id":10,"method":"tools/call","params":{{"name":"cfire_inspect","arguments":{{"target":"{}:10:59"}}}}}})",
            cfire::lsp::detail::json_escape(interp_cf.string()));
        std::string inspect_resp = mcp_srv.handle_jsonrpc(inspect_req);
        require(inspect_resp.contains("sample_val") &&
                inspect_resp.contains("String Interpolation Context") &&
                inspect_resp.contains(":.2f") &&
                inspect_resp.contains("References (3)"),
            std::format("MCP cfire_inspect reports hover, interpolation context, format spec, and references, got: {}", inspect_resp));

        // MCP `cfire_rename` on `sample_val` inside `{...}` (1-based line 10, col 59) -> `measured_val`
        std::string rename_req = std::format(
            R"({{"jsonrpc":"2.0","id":11,"method":"tools/call","params":{{"name":"cfire_rename","arguments":{{"target":"{}:10:59","new_name":"measured_val"}}}}}})",
            cfire::lsp::detail::json_escape(interp_cf.string()));
        std::string rename_resp = mcp_srv.handle_jsonrpc(rename_req);
        require(rename_resp.contains("Renamed `sample_val` -> `measured_val` (3 occurrence(s)"),
            std::format("MCP cfire_rename renames symbol across declaration and '{{...}}' replacement strings, got: {}", rename_resp));

        auto updated_txt = cfire::build::read_file_text(interp_cf).value_or("");
        require(updated_txt.contains("measured_val := 12.3456") &&
                updated_txt.contains("plain sample_val metric={compute_metric(measured_val, 1.5):.2f}") &&
                updated_txt.contains("status={measured_val:.1f}"),
            std::format("MCP cfire_rename updated declaration and both '{{...}}' interpolations while preserving plain string text, got:\n{}", updated_txt));
    }

    // 8. CF-119: Scope-sound refactor engine, LSP validity/collision checks, atomic rollback, and CRLF preservation
    {
        // 8a. CRLF line ending preservation
        auto crlf_cf = tmp.file("crlf_sample.cf", "package main\r\n\r\nfn calc(n: i32) -> i32 {\r\n    temp_val := n * 2\r\n    return temp_val + 1\r\n}\r\n\r\nfn main() {\r\n    println(\"{}\", calc(10))\r\n}\r\n");
        run_cfire(std::format("refactor rename \"{}:4:5\" my_val --apply", crlf_cf.string()))
            .assert_ok("crlf rename exit 0 (CF-119)");

        auto crlf_raw = cfire::build::read_file_text(crlf_cf).value_or("");
        require(crlf_raw.contains("\r\n"), "crlf_sample.cf preserves \\r\\n line endings (CF-119)");
        // Ensure no bare \n without preceding \r exists in file
        bool has_bare_lf = false;
        for (std::size_t i = 0; i < crlf_raw.size(); ++i) {
            if (crlf_raw[i] == '\n' && (i == 0 || crlf_raw[i - 1] != '\r')) {
                has_bare_lf = true;
                break;
            }
        }
        require(!has_bare_lf, "crlf_sample.cf contains zero bare LF endings (CF-119)");
        require(crlf_raw.contains("my_val := n * 2") && crlf_raw.contains("return my_val + 1"),
            "crlf_sample.cf renamed occurrences correctly (CF-119)");

        // 8b. Scope-sound renaming across shadowed blocks
        auto shadow_cf = tmp.file("shadow_sample.cf", R"(package main

fn shadow_demo() {
    val := 100
    if true {
        val := "inner string"
        println("inner: {}", val)
    }
    println("outer: {}", val)
}

fn main() {
    shadow_demo()
}
)");
        // Rename outer `val` (line 4, col 5) -> `outer_number`
        run_cfire(std::format("refactor rename \"{}:4:5\" outer_number --apply", shadow_cf.string()))
            .assert_ok("rename outer val exit 0 (CF-119)");

        auto shadow_after_outer = cfire::build::read_file_text(shadow_cf).value_or("");
        require(shadow_after_outer.contains("outer_number := 100"), "outer declaration renamed to outer_number (CF-119)");
        require(shadow_after_outer.contains("println(\"outer: {}\", outer_number)"), "outer usage renamed to outer_number (CF-119)");
        require(shadow_after_outer.contains("val := \"inner string\""), "inner declaration preserved as val (not renamed) (CF-119)");
        require(shadow_after_outer.contains("println(\"inner: {}\", val)"), "inner usage preserved as val (not renamed) (CF-119)");

        // Rename inner `val` (line 6, col 9) -> `inner_string`
        run_cfire(std::format("refactor rename \"{}:6:9\" inner_string --apply", shadow_cf.string()))
            .assert_ok("rename inner val exit 0 (CF-119)");

        auto shadow_after_inner = cfire::build::read_file_text(shadow_cf).value_or("");
        require(shadow_after_inner.contains("outer_number := 100"), "outer declaration remains outer_number (CF-119)");
        require(shadow_after_inner.contains("println(\"outer: {}\", outer_number)"), "outer usage remains outer_number (CF-119)");
        require(shadow_after_inner.contains("inner_string := \"inner string\""), "inner declaration renamed to inner_string (CF-119)");
        require(shadow_after_inner.contains("println(\"inner: {}\", inner_string)"), "inner usage renamed to inner_string (CF-119)");

        // Verify refactored code compiles and executes cleanly
        run_cfire(std::format("run \"{}\"", shadow_cf.string()))
            .assert_ok("scope-renamed code runs cleanly (CF-119)")
            .assert_contains("inner: inner string", "inner execution preserved (CF-119)")
            .assert_contains("outer: 100", "outer execution preserved (CF-119)");

        // 8c. LSP validity and collision checks
        cfire::lsp::Server lsp_srv;
        std::string s_uri = cfire::lsp::detail::path_to_uri(shadow_cf.string());

        // Prepare rename: valid identifier returns placeholder
        std::string prep_res = lsp_srv.compute_prepare_rename(s_uri, 3, 4); // 0-indexed line 3 = line 4
        require(prep_res.contains("\"placeholder\":\"outer_number\""),
            std::format("LSP prepareRename returns outer_number placeholder, got: {}", prep_res));

        // Prepare rename on keyword returns null
        std::string prep_kw = lsp_srv.compute_prepare_rename(s_uri, 4, 4); // 0-indexed line 4 = "if true"
        require(prep_kw == "null", std::format("LSP prepareRename on keyword returns null, got: {}", prep_kw));

        // Rename with keyword rejected by LSP
        std::string rename_kw = lsp_srv.compute_rename(s_uri, 3, 4, "match");
        require(rename_kw == "null", std::format("LSP rename to keyword returns null, got: {}", rename_kw));

        // Rename with invalid identifier rejected by LSP
        std::string rename_inv = lsp_srv.compute_rename(s_uri, 3, 4, "99bad");
        require(rename_inv == "null", std::format("LSP rename to invalid identifier returns null, got: {}", rename_inv));

        // Rename with collision (shadow_demo function name) rejected by LSP
        std::string rename_coll = lsp_srv.compute_rename(s_uri, 3, 4, "shadow_demo");
        require(rename_coll == "null", std::format("LSP rename with collision returns null, got: {}", rename_coll));

        // Valid rename returns structured changes
        std::string rename_valid = lsp_srv.compute_rename(s_uri, 3, 4, "clean_val");
        require(rename_valid.contains("\"newText\":\"clean_val\""),
            std::format("LSP rename with valid non-colliding name returns changes, got: {}", rename_valid));

        // 8d. Atomic multi-file apply rollback safety
        cfire::refactor::RenameResult fake_multi_res;
        fake_multi_res.success = true;
        fake_multi_res.new_name = "rollback_test";

        auto good_cf = tmp.file("good_file.cf", "package main\nfn main() { orig_name := 1 }\n");
        cfire::refactor::FileModification good_mod;
        good_mod.file_path = good_cf.string();
        good_mod.original_content = "package main\nfn main() { orig_name := 1 }\n";
        good_mod.modified_content = "package main\nfn main() { rollback_test := 1 }\n";
        fake_multi_res.modifications.push_back(good_mod);

        cfire::refactor::FileModification bad_mod;
        bad_mod.file_path = "/nonexistent_dir_9876543210/bad.cf";
        bad_mod.original_content = "none";
        bad_mod.modified_content = "bad";
        fake_multi_res.modifications.push_back(bad_mod);

        auto apply_err = cfire::refactor::apply_rename(fake_multi_res);
        require(!apply_err.has_value(), "apply_rename on partial failure returns error (CF-119)");

        auto good_check = cfire::build::read_file_text(good_cf).value_or("");
        require(good_check == "package main\nfn main() { orig_name := 1 }\n",
            "good_file.cf remains untouched after multi-file apply failure (CF-119)");
    }

    std::println("--- PASS: test_cfire_refactor_rename");
}

void test_phase4_tooling_depth([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_phase4_tooling_depth");

    // 1. CF-125: LSP JSON-RPC parser compliance
    {
        std::string unescaped = cfire::lsp::detail::json_unescape(R"(\u0041\u0042\u0043)");
        require(unescaped == "ABC", "json_unescape decodes basic \\uXXXX");

        std::string emoji = cfire::lsp::detail::json_unescape(R"(\uD83D\uDE00)");
        require(emoji == "😀", "json_unescape decodes surrogate pairs \\uD83D\\uDE00 to emoji 😀");

        auto overflow_int = cfire::lsp::detail::extract_json_int_after(R"({"num": 99999999999999999999999999})", "num");
        require(!overflow_int.has_value(), "extract_json_int_after does not throw on large integer overflow");

        std::string decoded_path = cfire::lsp::detail::uri_to_path("file:///path/with%20spaces/main.cf");
        require(decoded_path == "/path/with spaces/main.cf", "uri_to_path percent-decodes spaces properly");

        cfire::lsp::Server srv;
        std::ostringstream ss;
        srv.handle_message(R"({"jsonrpc":"2.0","id":42,"method":"custom/unknownMethod","params":{}})", ss);
        std::string resp = ss.str();
        require(resp.contains("\"code\":-32601") && resp.contains("Method not found"),
            std::format("LSP unknown method returns standard -32601 MethodNotFound error, got: {}", resp));
    }

    // 2. CF-126: Document lifecycle (didClose and workspace/didChangeWatchedFiles)
    {
        cfire::lsp::Server srv;
        std::ostringstream ss;
        std::string uri = "file:///tmp/doc_test.cf";
        srv.handle_message(std::format(
            R"({{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{{"textDocument":{{"uri":"{}","text":"package main\nfn main() {{ let x = 1 }}\n"}}}}}})",
            uri), ss);
        require(srv.get_doc_text(uri).contains("let x = 1"), "didOpen sets document memory buffer");

        std::ostringstream ss_close;
        srv.handle_message(std::format(
            R"({{"jsonrpc":"2.0","method":"textDocument/didClose","params":{{"textDocument":{{"uri":"{}"}}}}}})",
            uri), ss_close);
        std::string close_resp = ss_close.str();
        require(close_resp.contains("publishDiagnostics") && close_resp.contains("[]"),
            std::format("didClose clears document and publishes empty diagnostics, got: '{}'", close_resp));
        require(srv.get_doc_text(uri).empty(), "didClose purges document from memory buffer");

        srv.handle_message(R"({"jsonrpc":"2.0","method":"workspace/didChangeWatchedFiles","params":{"changes":[]}})", ss);
        require(srv.get_doc_text(uri).empty(), "workspace/didChangeWatchedFiles clears in-memory caches");
    }

    // 3. CF-127: UTF-16 position translations & real symbol ranges
    {
        std::string mixed_line = "let s = \"😀 hello\"";
        int u16_len = cfire::lsp::detail::utf8_col_to_utf16_col(mixed_line, mixed_line.size());
        int u8_len = cfire::lsp::detail::utf16_col_to_utf8_col(mixed_line, u16_len);
        require(u8_len == static_cast<int>(mixed_line.size()), "UTF-16 <-> UTF-8 column roundtrip matches byte length");

        cfire::lsp::Server srv;
        std::ostringstream ss;
        std::string uri = "file:///tmp/sym_test.cf";
        srv.handle_message(std::format(
            R"({{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{{"textDocument":{{"uri":"{}","text":"package main\n\ntype Point struct {{\n    x: f64\n    y: f64\n}}\n\nfn add_coords(p: Point) -> f64 {{\n    return p.x + p.y\n}}\n"}}}}}})",
            uri), ss);
        std::string syms = srv.compute_document_symbols(uri);
        require(syms.contains("\"name\":\"Point\"") && syms.contains("\"range\""), "documentSymbols contains Point struct");
        require(syms.contains("\"name\":\"add_coords\"") && syms.contains("\"selectionRange\""), "documentSymbols contains function with selectionRange");
        require(!syms.contains("\"character\":20"), "documentSymbols does not contain hardcoded dummy character:20 range");
    }

    // 4. CF-128: Incremental sync and $/cancelRequest
    {
        cfire::lsp::Server srv;
        std::ostringstream ss;
        std::string uri = "file:///tmp/incr_test.cf";
        srv.handle_message(std::format(
            R"({{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{{"textDocument":{{"uri":"{}","text":"package main\nfn run() {{\n    msg := \"hello\"\n}}\n"}}}}}})",
            uri), ss);
        srv.handle_message(std::format(
            R"({{"jsonrpc":"2.0","method":"textDocument/didChange","params":{{"textDocument":{{"uri":"{}"}},"contentChanges":[{{"range":{{"start":{{"line":2,"character":12}},"end":{{"line":2,"character":17}}}},"text":"world"}}]}}}})",
            uri), ss);
        std::string updated = srv.get_doc_text(uri);
        require(updated.contains("msg := \"world\""),
            std::format("incremental didChange successfully patched buffer, got:\n{}", updated));

        srv.handle_message(R"({"jsonrpc":"2.0","method":"$/cancelRequest","params":{"id":99}})", ss);
    }

    // 5. CF-129: Folding ranges, selection ranges, document highlight, code actions context
    {
        cfire::lsp::Server srv;
        std::ostringstream ss;
        std::string uri = "file:///tmp/features_test.cf";
        srv.handle_message(std::format(
            R"({{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{{"textDocument":{{"uri":"{}","text":"package main\n\nimport \"core:time\"\n\nfn compute(val: i32) -> i32 {{\n    mut total = val\n    total += 10\n    return total\n}}\n"}}}}}})",
            uri), ss);

        std::string folds = srv.compute_folding_ranges(uri);
        require(folds.contains("\"startLine\":4") && folds.contains("\"endLine\":8"),
            std::format("compute_folding_ranges identifies function block, got: {}", folds));

        std::string sel = srv.compute_selection_range(uri, 5, 8);
        require(sel.contains("\"range\"") && sel.contains("\"parent\""),
            std::format("compute_selection_range provides hierarchical parent ranges, got: {}", sel));

        std::string hl = srv.compute_document_highlight(uri, 5, 8);
        require(hl.contains("\"kind\":1") && hl.contains("\"line\":5") && hl.contains("\"line\":6") && hl.contains("\"line\":7"),
            std::format("compute_document_highlight highlights all usages of total, got: {}", hl));

        std::string code_actions = srv.compute_code_actions(uri, 5, {"cfire/UNUSED-VARIABLE"});
        require(code_actions == "[]", "compute_code_actions respects requested diagnostic code filter");
    }

    // 6. CF-130: Formatter token spacing and idempotency
    {
        std::string unformatted = R"(package main

fn calc(a:int,b:int) -> int {
    mut x:int=a+b
    return x*2
}
)";
        auto fmt_res = cfire::fmt::format_source(unformatted, "calc.cf");
        require(fmt_res.has_value(), "cfire::fmt::format_source succeeds on unspaced code");
        if (fmt_res) {
            require(fmt_res->contains("fn calc(a: int, b: int) -> int {") &&
                    fmt_res->contains("    mut x: int = a + b") &&
                    fmt_res->contains("    return x * 2"),
                std::format("expected normalized token spacing, got:\n{}", *fmt_res));

            auto roundtrip = cfire::fmt::format_source(*fmt_res, "calc.cf");
            require(roundtrip.has_value() && *roundtrip == *fmt_res, "formatter is strictly idempotent");
        }
    }

    std::println("--- PASS: test_phase4_tooling_depth");
}
