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
        auto w_res = cfire::fs::write_all(test_tmp_file, payload);
        require(w_res.has_value(), "cfire::fs::write_all check");
        auto r_res = cfire::fs::read_all(test_tmp_file);
        require(r_res.has_value() && *r_res == payload, "cfire::fs::read_all check");
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
            .assert_contains("cfire 0.9.5", "cfire --version reports version");
        run_cfire("--help")
            .assert_ok("cfire --help exits 0")
            .assert_contains("doctor", "cfire --help lists doctor command")
            .assert_contains("setup", "cfire --help lists setup command");
        run_cfire("doctor --json")
            .assert_ok("cfire doctor --json exits 0")
            .assert_contains("\"healthy\": true", "cfire doctor --json reports healthy")
            .assert_contains("\"runtime_dir\":", "cfire doctor --json reports runtime_dir");

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

    std::println("--- PASS: test_cfire_refactor_rename");
}
