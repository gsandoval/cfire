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
import "core:ai" as ai
import "core:db/sqlite" as sqlite
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
import "core:ai" as ai
import "core:db/sqlite" as sqlite
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
    mut db := try sqlite.open(":memory:")
    try db.exec("CREATE TABLE docs (id INTEGER, title TEXT, snippet TEXT, embedding BLOB)")
    for ch in chunks {
        vec := try ai.embed("local", ch.text)
        try db.exec("INSERT INTO docs VALUES (?1, ?2, ?3, ?4)", ch.index, ch.title, ch.text, vec)
    }
    qvec := try ai.embed("local", "modern C++23 compiler")
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

void test_google_cpp_and_language_design(const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_google_cpp_and_language_design");

    // 1. ARCH-CPP-04: Test zero-allocation <charconv> integer parsing
    {
        auto v1 = cfire::detail::parse_int<int>("12345");
        require(v1.has_value() && *v1 == 12345, "parse_int<int>(12345)");

        auto v2 = cfire::detail::parse_int<std::uint16_t>("8080");
        require(v2.has_value() && *v2 == 8080, "parse_int<uint16>(8080)");

        auto v3 = cfire::detail::parse_int<int>("-42");
        require(v3.has_value() && *v3 == -42, "parse_int<int>(-42)");

        auto v4 = cfire::detail::parse_int<int>("invalid");
        require(!v4.has_value(), "parse_int<int>(invalid) returns nullopt");

        auto v5 = cfire::detail::parse_int<std::uint8_t>("99999");
        require(!v5.has_value(), "parse_int<uint8>(overflow) returns nullopt");

        auto v6 = cfire::detail::parse_int<std::size_t>("");
        require(!v6.has_value(), "parse_int(empty) returns nullopt");
    }

    // 2. ARCH-CPP-05: Test CFIRE_LIFETIMEBOUND and slice / view accessors
    {
        std::string text = "hello google cpp";
        std::string_view sub = cfire::slice(text, 6, 12);
        require(sub == "google", "slice string_view");

        std::vector<int> nums = {10, 20, 30, 40};
        std::span<const int> sp = cfire::slice(nums, 1, 3);
        require(sp.size() == 2 && sp[0] == 20 && sp[1] == 30, "slice vector span");

        cfire::Error err("operation timed out");
        require(err.message() == "operation timed out", "Error::message() lifetimebound view");
    }

    // 3. ARCH-CPP-01: Multi-struct receiver method name collision resolution (LD-NEXT-03)
    {
        ScopedTempDir tmp("cfire_shapes");
        auto cf_path = tmp.file("shapes.cf", R"(package main

type Circle struct {
    r: i32
}

type Rect struct {
    w: i32
    h: i32
}

fn (self: Circle) area() -> i32 {
    return 3 * self.r * self.r
}

fn (self: Rect) area() -> i32 {
    return self.w * self.h
}

fn main() {
    c := Circle{r: 2}
    r := Rect{w: 3, h: 4}
    println("circle_area={}, rect_area={}", c.area(), r.area())
}
)");

        auto out_bin = tmp.path / "shapes_bin";
        auto plan = cfire::build::build_native(cf_path, rt_dir, out_bin);
        require(plan.has_value(), std::format("build_native(shapes.cf) with multi-struct area() methods: {}", plan ? "" : plan.error()));
        if (plan) {
            int rc = 0;
            std::string out = cfire::bridge::run_command_capture(out_bin.string(), rc);
            require(rc == 0, std::format("shapes exited with code {}", rc));
            require(out.contains("circle_area=12, rect_area=12"),
                std::format("expected 'circle_area=12, rect_area=12', got:\n{}", out));
        }
    }

    std::println("--- PASS: test_google_cpp_and_language_design");
}

void test_section2_language_ergonomics(const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_section2_language_ergonomics");

    // 1. AGENT-P1-01: String interpolation & format lowering in println / print
    {
        ScopedTempDir tmp("cfire_p1_01");
        auto cf_path = tmp.file("print_test.cf", R"(package main

fn main() {
    name := "cfire"
    println("hello {name}")
    count := 42
    println("item count={}", count)
    msg := "runtime msg"
    println(msg)
    println(100)
    print("prefix: ")
    println("done")
}
)");

        auto out_bin = tmp.path / "print_bin";
        auto plan = cfire::build::build_native(cf_path, rt_dir, out_bin);
        require(plan.has_value(), std::format("build_native(print_test.cf): {}", plan ? "" : plan.error()));
        if (plan) {
            int rc = 0;
            std::string out = cfire::bridge::run_command_capture(out_bin.string(), rc);
            require(rc == 0, std::format("print_test exited with code {}", rc));
            require(out.contains("hello cfire"), "contains 'hello cfire'");
            require(out.contains("item count=42"), "contains 'item count=42'");
            require(out.contains("runtime msg"), "contains 'runtime msg'");
            require(out.contains("100"), "contains '100'");
            require(out.contains("prefix: done"), "contains 'prefix: done'");
        }
    }

    // 2. AGENT-P1-02: Struct literal disambiguation in control-flow headers
    {
        ScopedTempDir tmp("cfire_p1_02");
        auto cf_path = tmp.file("control_flow_disambig.cf", R"(package main

type Point struct {
    x: i32
    y: i32
}

fn is_origin(p: Point) -> bool {
    return p.x == 0 && p.y == 0
}

fn check_point(p: Point) -> bool {
    return p.x == 1 && p.y == 2
}

fn main() {
    MAX_LIMIT := 10
    count := 5
    if count < MAX_LIMIT {
        println("if_limit_ok")
    }

    mut i := 0
    LIMIT := 3
    for i < LIMIT {
        i += 1
    }
    println("for_limit_ok={}", i)

    STATE := 2
    match STATE {
        1 => println("one"),
        2 => println("match_state_ok"),
        _ => println("other"),
    }

    if is_origin(Point{ x: 0, y: 0 }) {
        println("origin_ok")
    }

    if check_point(Point{ x: 1, y: 2 }) {
        println("check_point_ok")
    }

    if (Point{ x: 1, y: 2 }.x == 1) {
        println("paren_struct_ok")
    }
}
)");

        auto out_bin = tmp.path / "control_flow_bin";
        auto plan = cfire::build::build_native(cf_path, rt_dir, out_bin);
        require(plan.has_value(), std::format("build_native(control_flow_disambig.cf): {}", plan ? "" : plan.error()));
        if (plan) {
            int rc = 0;
            std::string out = cfire::bridge::run_command_capture(out_bin.string(), rc);
            require(rc == 0, std::format("control_flow exited with code {}", rc));
            require(out.contains("if_limit_ok"), "contains 'if_limit_ok'");
            require(out.contains("for_limit_ok=3"), "contains 'for_limit_ok=3'");
            require(out.contains("match_state_ok"), "contains 'match_state_ok'");
            require(out.contains("origin_ok"), "contains 'origin_ok'");
            require(out.contains("check_point_ok"), "contains 'check_point_ok'");
            require(out.contains("paren_struct_ok"), "contains 'paren_struct_ok'");
        }
    }

    // 3. AGENT-P1-03: Expression-Oriented match (MatchExpr)
    {
        ScopedTempDir tmp("cfire_p1_03");
        auto cf_path = tmp.file("match_expr_test.cf", R"(package main

type Shape enum {
    Circle(i32)
    Rect(i32, i32)
}

fn describe(s: Shape) -> str {
    return match s {
        Shape::Circle(r) => "circle",
        Shape::Rect(w, h) => "rect",
    }
}

fn http_code(verb: str) -> i32 {
    code := match verb {
        "GET" => 200,
        "POST" => 201,
        _ => 400,
    }
    return code
}

fn main() {
    c := Shape::Circle(5)
    r := Shape::Rect(10, 20)
    println("c={}, r={}, code_get={}, code_post={}, code_unk={}",
        describe(c), describe(r), http_code("GET"), http_code("POST"), http_code("DELETE"))
}
)");

        auto out_bin = tmp.path / "match_expr_bin";
        auto plan = cfire::build::build_native(cf_path, rt_dir, out_bin);
        require(plan.has_value(), std::format("build_native(match_expr_test.cf): {}", plan ? "" : plan.error()));
        if (plan) {
            int rc = 0;
            std::string out = cfire::bridge::run_command_capture(out_bin.string(), rc);
            require(rc == 0, std::format("match_expr exited with code {}", rc));
            require(out.contains("c=circle, r=rect, code_get=200, code_post=201, code_unk=400"),
                std::format("expected match expressions to evaluate, got: {}", out));
        }

        // Test formatter roundtrip on MatchExpr
        auto fmt_res = cfire::fmt::format_source(R"(package main

fn test_fn(x: str) -> i32 {
    return match x {
        "A" => 1,
        _ => 0,
    }
}
)", "fmt_match.cf");
        require(fmt_res.has_value(), "cfire::fmt::format_source on match expression");

        // Negative sema tests for MatchExpr:
        // A. Enum missing variant
        require_sema_error(R"(package main
type Shape enum { Circle(i32), Rect(i32, i32) }
fn test_fn(s: Shape) -> str {
    return match s {
        Shape::Circle(r) => "circle",
    }
}
)", "cfire/NON-EXHAUSTIVE-MATCH", "missing_rect.cf");

        // B. Primitive match missing wildcard '_' fallback
        require_sema_error(R"(package main
fn test_fn(v: str) -> i32 {
    return match v {
        "GET" => 1,
    }
}
)", "cfire/NON-EXHAUSTIVE-MATCH", "missing_wildcard.cf");
    }

    // 4. AGENT-P1-04: Compound Assignment & Bitwise Operators
    {
        ScopedTempDir tmp("cfire_p1_04");
        auto cf_path = tmp.file("bitwise_and_compound.cf", R"(package main

fn main() {
    mut x := 10
    x *= 3      // 30
    x /= 2      // 15
    x %= 4      // 3
    x <<= 2     // 12
    x >>= 1     // 6
    x |= 1      // 7
    x &= 3      // 3
    x ^= 6      // 5
    println("compound_x={}", x)

    // Bitwise operators
    a := 0xF0
    b := 0x3F
    c := (a & b) | (1 << 4) ^ (~0)
    shift := 16 >> 2
    println("c={}, shift={}", c, shift)
}
)");

        auto out_bin = tmp.path / "bitwise_bin";
        auto plan = cfire::build::build_native(cf_path, rt_dir, out_bin);
        require(plan.has_value(), std::format("build_native(bitwise_and_compound.cf): {}", plan ? "" : plan.error()));
        if (plan) {
            int rc = 0;
            std::string out = cfire::bridge::run_command_capture(out_bin.string(), rc);
            require(rc == 0, std::format("bitwise exited with code {}", rc));
            require(out.contains("compound_x=5"), std::format("expected compound_x=5, got: {}", out));
            require(out.contains("shift=4"), std::format("expected shift=4, got: {}", out));
        }

        // Negative sema test: compound assignment on immutable variable
        require_sema_error(R"(package main
fn test_fn() {
    x := 5
    x *= 2
}
)", "cfire/ASSIGN-IMMUTABLE", "imm_compound.cf");
    }

    // 5. Trailing closures and fn(...) -> R parameter types on user-defined methods
    {
        ScopedTempDir tmp("cfire_trailing_closure");
        auto cf_path = tmp.file("trailing_closure.cf", R"(package main

type Layout struct {
    y: i32
}

fn (mut l: Layout) section(title: str, body: fn()) {
    println("begin:{}", title)
    body()
    l.y += 10
    println("end:{}:{}", title, l.y)
}

fn (mut l: Layout) with_card(h: i32, draw: fn(i32)) {
    draw(l.y)
    l.y += h
}

fn main() {
    mut lay := Layout{y: 5}
    lay.section("Header") || {
        lay.with_card(20) |top_y| {
            println("card_at={}", top_y)
        }
    }
    println("final_y={}", lay.y)
}
)");

        auto out_bin = tmp.path / "trailing_closure_bin";
        auto plan = cfire::build::build_native(cf_path, rt_dir, out_bin);
        require(plan.has_value(), std::format("build_native(trailing_closure.cf): {}", plan ? "" : plan.error()));
        if (plan) {
            int rc = 0;
            std::string out = cfire::bridge::run_command_capture(out_bin.string(), rc);
            require(rc == 0, std::format("trailing_closure exited with code {}", rc));
            require(out.contains("begin:Header"), "contains 'begin:Header'");
            require(out.contains("card_at=5"), "contains 'card_at=5'");
            require(out.contains("end:Header:35"), "contains 'card_at=35'");
            require(out.contains("final_y=35"), "contains 'final_y=35'");
        }
    }

    // 6. Multi-target core:audio synthesis, WAV codec, polyphonic Sound & Stream DSP
    {
        ScopedTempDir tmp("cfire_core_audio");
        auto wav_path = (tmp.path / "test_tone.wav").string();
        auto cf_path = tmp.file("audio_test.cf", std::format(R"CF(package main

import "core:audio"
import "core:os"

fn main() {{
    os.setenv("CFIRE_HEADLESS_TEST", "1")
    try audio.init(48000, 2)
    defer audio.close()

    assert(audio.is_ready(), "audio engine should be ready")
    audio.set_master_volume(0.8)

    tone := audio.gen_tone(audio.Waveform::Sine, 440.0, 0.1, 0.5)
    assert_eq(tone.frame_count(), 4800, "48kHz * 0.1s == 4800 frames")
    assert_eq(tone.channels, 2, "stereo channels")

    try audio.save_wav(tone, "{}")
    loaded := try audio.load_wav("{}")
    assert_eq(loaded.frame_count(), 4800, "decoded WAV frame count matches")
    assert_eq(loaded.sample_rate, 48000, "decoded WAV sample rate matches")

    sfx := audio.load_sound_from_wave(audio.gen_sfx(audio.SfxPreset::Coin))
    sfx.set_volume(0.9)
    sfx.set_pitch(1.2)
    sfx.set_pan(-0.25)
    sfx.play()
    assert(sfx.is_playing(), "sound voice should be active")

    stream := try audio.open_stream(48000, 2)
    assert(stream.needs_data(2048), "fresh stream needs data")
    stream.push_samples(tone.samples)
    assert(audio.peak_level() > 0.05, "mixer peak level should be > 0.05")
    assert(audio.rms_level() > 0.01, "mixer rms level should be > 0.01")

    println("audio_ok:frames={{}}:ready={{}}", loaded.frame_count(), audio.is_ready())
}}
)CF", wav_path, wav_path));

        auto out_bin = tmp.path / "audio_test_bin";
        auto plan = cfire::build::build_native(cf_path, rt_dir, out_bin);
        require(plan.has_value(), std::format("build_native(audio_test.cf): {}", plan ? "" : plan.error()));
        if (plan) {
            int rc = 0;
            std::string out = cfire::bridge::run_command_capture(out_bin.string(), rc);
            require(rc == 0, std::format("audio_test exited with code {}:\n{}", rc, out));
            require(out.contains("audio_ok:frames=4800:ready=true"), "audio_test output verified");
        }
    }

    // 7. AI-First Architecture: json.schema[T](), core:ai embeddings, Vector SQLite, SSE Streaming, Structured Outputs & MCP
    {
        ScopedTempDir tmp("cfire_core_ai");
        auto cf_path = tmp.file("ai_test.cf", R"CF(package main

import "core:ai"
import "core:db/sqlite"
import "core:fs"
import "core:json"
import "core:net/http"
import "core:str"

type ToolArgs struct {
    query: str  `json:"query" desc:"Search query string"`
    limit: i32  `json:"limit" desc:"Maximum results"`
    exact: bool `json:"exact"`
}

type ExtractedBug struct {
    component: str `json:"component" desc:"Subsystem name"`
    severity:  i32 `json:"severity"  desc:"Severity 1..5"`
}

type DocRow struct {
    title: str   `sql:"title"`
    score: f64   `sql:"score"`
    vec:   []f32 `sql:"vec"`
}

fn main() {
    // 1. Compile-time JSON Schema reflection with desc:"..." tags
    schema := json.schema[ToolArgs]()
    assert(str.contains(schema, `"type":"object"`), "schema object type")
    assert(str.contains(schema, `"description":"Search query string"`), "schema field description")
    assert(str.contains(schema, `"required":["query","limit","exact"]`), "schema required fields")

    // 2. RAG text chunking & language detection
    chunks := ai.chunk_text("First paragraph about systems programming.\n\nSecond paragraph about vector search and embeddings in SQLite.", 60, 12)
    assert(chunks.len() >= 2, "chunk_text splits into multiple chunks")
    lang := ai.detect_language("Hello from the cfire systems language")
    assert(!lang.empty(), "detect_language returns non-empty code")

    // 3. Unified local embeddings + SQLite vector similarity search + []f32 column mapping
    v_sys := try ai.embed("local", "C++23 systems programming memory safety and RAII")
    v_sql := try ai.embed("local", "SQLite embedded vector similarity search and cosine distance")
    assert(v_sys.len() >= 128, "embedding dimension >= 128")
    self_sim := ai.cosine_similarity(v_sql, v_sql)
    assert(self_sim > 0.99, "self cosine similarity ~ 1.0")

    mut db := try sqlite.open(":memory:")
    defer db.close()
    try db.exec("CREATE TABLE docs (title TEXT, vec BLOB)")
    try db.exec("INSERT INTO docs VALUES (?1, ?2)", "Systems", v_sys)
    try db.exec("INSERT INTO docs VALUES (?1, ?2)", "VectorDB", v_sql)

    q_vec := try ai.embed("local", "vector search in sqlite cosine distance")
    rows := try db.query[DocRow](
        "SELECT title, vec_cosine_similarity(vec, ?1) AS score, vec FROM docs ORDER BY score DESC",
        q_vec
    )
    assert_eq(rows.len(), 2, "2 rows returned")
    assert_eq(rows[0].title, "VectorDB", "VectorDB ranks #1 for vector search query")
    assert_eq(rows[0].vec.len(), v_sql.len(), "round-tripped []f32 BLOB matches dimension")

    // 4. Model Context Protocol (MCP) server registration & JSON-RPC dispatch
    mut mcp := ai.make_mcp_server("demo-mcp", "1.0.0")
    mcp.register_tool[ToolArgs]("search_docs", "Search indexed docs", |args| {
        return "hit:" + args.query
    })
    init_resp := mcp.handle_jsonrpc(`{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}`)
    assert(str.contains(init_resp, `"name":"demo-mcp"`), "MCP initialize response")
    list_resp := mcp.handle_jsonrpc(`{"jsonrpc":"2.0","id":2,"method":"tools/list"}`)
    assert(str.contains(list_resp, `"name":"search_docs"`), "MCP tools/list includes search_docs")
    call_resp := mcp.handle_jsonrpc(`{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"search_docs","arguments":{"query":"simd","limit":5,"exact":true}}}`)
    assert(str.contains(call_resp, "hit:simd"), "MCP tools/call executes typed handler")

    // 5. Local mock OpenAI-compatible HTTP server for structured outputs & SSE token streaming
    mut srv := try http.make_server(0)
    defer srv.stop()
    mut router := http.make_router()
    router.post("/v1/chat/completions", |req| {
        if str.contains(req.body, `"stream":true`) {
            sse_payload := "data: {\"choices\":[{\"delta\":{\"content\":\"Hello \"}}]}\n\ndata: {\"choices\":[{\"delta\":{\"content\":\"AI\"}}]}\n\ndata: [DONE]\n\n"
            mut r := http.Response.text(200, sse_payload)
            r.headers["Content-Type"] = "text/event-stream"
            return r
        }
        return http.Response.json(200, `{"model":"mock-gpt","choices":[{"message":{"content":"{\"component\":\"audio\",\"severity\":4}"}}],"usage":{"prompt_tokens":12,"completion_tokens":8}}`)
    })
    srv.serve_in_background(router)

    base := "http://127.0.0.1:{srv.port()}"
    client := ai.openai_compatible(base, "test-key")
    bug := try client.generate_struct[ExtractedBug]("mock-gpt", "audio glitch")
    assert_eq(bug.component, "audio", "generate_struct parsed component")
    assert_eq(bug.severity, 4, "generate_struct parsed severity")

    mut streamed_tokens := ""
    full := try client.stream("mock-gpt", [ai.user("Say hi")], |tok| {
        streamed_tokens = streamed_tokens + tok
    })
    assert_eq(full, "Hello AI", "SSE stream accumulated full text")
    assert_eq(streamed_tokens, "Hello AI", "SSE stream callback received tokens")

    // 6. Multimodal & Document Ingestion (.docx, .pptx, .xlsx, .pdf, .html, PNG OCR & Image Embeddings)
    docx_bytes := ai.create_zip([
        ai.ZipEntry{
            name: "word/document.xml",
            content: `<w:document><w:body><w:p><w:pPr><w:pStyle w:val="Heading1"/></w:pPr><w:r><w:t>Architecture Overview</w:t></w:r></w:p><w:p><w:r><w:t>Zero-overhead C++23 lowering and memory safety.</w:t></w:r></w:p></w:body></w:document>`
        }
    ])
    try fs.write_bytes("spec.docx", docx_bytes)
    docx_doc := try ai.load_document("spec.docx")
    try fs.remove("spec.docx")
    assert_eq(docx_doc.format, "docx", "detected docx format")
    assert(str.contains(docx_doc.text, "# Architecture Overview"), "docx heading converted to markdown")
    assert(str.contains(docx_doc.text, "Zero-overhead C++23 lowering"), "docx paragraph extracted")

    pptx_bytes := ai.create_zip([
        ai.ZipEntry{
            name: "ppt/slides/slide1.xml",
            content: `<p:sld><p:cSld><p:spTree><p:sp><p:nvSpPr><p:nvPr><p:ph type="title"/></p:nvPr></p:nvSpPr><p:txBody><a:p><a:r><a:t>Q3 Performance</a:t></a:r></a:p></p:txBody></p:sp><p:sp><p:txBody><a:p><a:r><a:t>Sub-millisecond SIMD vector queries</a:t></a:r></a:p></p:txBody></p:sp></p:spTree></p:cSld></p:sld>`
        },
        ai.ZipEntry{
            name: "ppt/slides/slide2.xml",
            content: `<p:sld><p:cSld><p:spTree><p:sp><p:nvSpPr><p:nvPr><p:ph type="title"/></p:nvPr></p:nvSpPr><p:txBody><a:p><a:r><a:t>Multimodal Roadmap</a:t></a:r></a:p></p:txBody></p:sp><p:sp><p:txBody><a:p><a:r><a:t>Native PDF and slide deck ingestion</a:t></a:r></a:p></p:txBody></p:sp></p:spTree></p:cSld></p:sld>`
        }
    ])
    try fs.write_bytes("deck.pptx", pptx_bytes)
    pptx_doc := try ai.load_document("deck.pptx")
    assert_eq(pptx_doc.format, "pptx", "detected pptx format")
    assert_eq(pptx_doc.sections.len(), 2, "pptx extracted 2 slide sections")
    assert_eq(pptx_doc.sections[0].index, 1, "slide 1 index")
    assert_eq(pptx_doc.sections[0].title, "Q3 Performance", "slide 1 title")
    assert_eq(pptx_doc.sections[1].index, 2, "slide 2 index")
    assert_eq(pptx_doc.sections[1].title, "Multimodal Roadmap", "slide 2 title")

    slide_chunks := ai.chunk_document(pptx_doc, 120, 16)
    assert(slide_chunks.len() >= 2, "chunk_document preserves slide sections")
    assert_eq(slide_chunks[1].index, 2, "chunk_document retains slide 2 provenance index")

    xlsx_bytes := ai.create_zip([
        ai.ZipEntry{
            name: "xl/sharedStrings.xml",
            content: `<sst><si><t>Metric</t></si><si><t>Latency</t></si></sst>`
        },
        ai.ZipEntry{
            name: "xl/worksheets/sheet1.xml",
            content: `<worksheet><sheetData><row><c t="s"><v>0</v></c><c t="s"><v>1</v></c></row><row><c t="inlineStr"><is><t>P99</t></is></c><c><v>1.2</v></c></row></sheetData></worksheet>`
        }
    ])
    try fs.write_bytes("metrics.xlsx", xlsx_bytes)
    xlsx_doc := try ai.load_document("metrics.xlsx")
    try fs.remove("metrics.xlsx")
    assert_eq(xlsx_doc.format, "xlsx", "detected xlsx format")
    assert(str.contains(xlsx_doc.text, "Metric"), "xlsx shared string resolved")
    assert(str.contains(xlsx_doc.text, "1.2"), "xlsx numeric value extracted")

    pdf_raw := "%PDF-1.4\n1 0 obj\n<< /Length 120 >>\nstream\n/CIDInit /ProcSet findresource begin\n2 beginbfchar\n<0001> <004C>\n<0002> <0069>\nendbfchar\n1 beginbfrange\n<0010> <0012> <006E>\nendbfrange\nendstream\nendobj\n2 0 obj\n<< /Length 88 >>\nstream\nBT /F1 12 Tf 72 712 Td [<000100020010> -250 (Quarterly Financial Summary)] TJ ET\nendstream\nendobj\n%%EOF"
    try fs.write_text("report.pdf", pdf_raw)
    pdf_doc := try ai.load_document("report.pdf")
    try fs.remove("report.pdf")
    assert_eq(pdf_doc.format, "pdf", "detected pdf format")
    assert(str.contains(pdf_doc.text, "Lin Quarterly Financial Summary"), "pdf CMap + TJ kerning array extracted")

    odt_bytes := ai.create_zip([
        ai.ZipEntry{name: "mimetype", content: "application/vnd.oasis.opendocument.text"},
        ai.ZipEntry{
            name: "content.xml",
            content: `<office:document-content><office:body><office:text><text:h>Linux Deployment</text:h><text:p>Zero external dev packages required.</text:p></office:text></office:body></office:document-content>`
        }
    ])
    try fs.write_bytes("guide.odt", odt_bytes)
    odt_doc := try ai.load_document("guide.odt")
    try fs.remove("guide.odt")
    assert_eq(odt_doc.format, "odt", "detected LibreOffice odt format")
    assert(str.contains(odt_doc.text, "Zero external dev packages required."), "odt paragraph extracted")

    epub_bytes := ai.create_zip([
        ai.ZipEntry{name: "META-INF/container.xml", content: "<container/>"},
        ai.ZipEntry{name: "OEBPS/ch1.xhtml", content: "<html><body><h1>Chapter One</h1><p>Systems programming on Linux and macOS.</p></body></html>"}
    ])
    try fs.write_bytes("book.epub", epub_bytes)
    epub_doc := try ai.load_document("book.epub")
    try fs.remove("book.epub")
    assert_eq(epub_doc.format, "epub", "detected epub format")
    assert(str.contains(epub_doc.text, "Systems programming on Linux and macOS."), "epub XHTML chapter extracted")

    // Construct a minimal PNG with an embedded tEXt chunk for deterministic cross-platform OCR & image embedding
    mut png_data: []u8 = [
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
        0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x10,
        0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x91, 0x68, 0x36,
        0x00, 0x00, 0x00, 0x15, 0x74, 0x45, 0x58, 0x74,
        0x43, 0x6F, 0x6D, 0x6D, 0x65, 0x6E, 0x74, 0x00,
        0x49, 0x6E, 0x76, 0x6F, 0x69, 0x63, 0x65, 0x20, 0x23, 0x34, 0x32, 0x30, 0x30,
        0x00, 0x00, 0x00, 0x00
    ]
    try fs.write_bytes("invoice.png", png_data)
    ocr_txt := try ai.ocr("invoice.png")
    assert(str.contains(ocr_txt, "Invoice #4200"), "OCR extracted PNG text metadata")
    img_vec := try ai.embed_image("local", "invoice.png")
    try fs.remove("invoice.png")
    assert_eq(img_vec.len(), 384, "embed_image returns 384-dim normalized vector")
    assert(ai.cosine_similarity(img_vec, img_vec) > 0.99, "embed_image self similarity ~ 1.0")

    // Construct two 2x2 24-bit BMP images (red vs blue) to verify pure C++23 pixel decoding & visual embedding discrimination
    mut bmp_red: []u8 = [
        0x42, 0x4D, 0x46, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x36, 0x00, 0x00, 0x00,
        0x28, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x00,
        0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x13, 0x0B, 0x00, 0x00,
        0x13, 0x0B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0xFF, 0x00, 0x00, 0xFF, 0x00, 0x00,
        0x00, 0x00, 0xFF, 0x00, 0x00, 0xFF, 0x00, 0x00
    ]
    mut bmp_blue: []u8 = [
        0x42, 0x4D, 0x46, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x36, 0x00, 0x00, 0x00,
        0x28, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x00,
        0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x13, 0x0B, 0x00, 0x00,
        0x13, 0x0B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xFF, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00,
        0xFF, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00
    ]
    try fs.write_bytes("red.bmp", bmp_red)
    try fs.write_bytes("blue.bmp", bmp_blue)
    red_vec := try ai.embed_image("local", "red.bmp")
    blue_vec := try ai.embed_image("local", "blue.bmp")
    try fs.remove("red.bmp")
    try fs.remove("blue.bmp")
    assert(ai.cosine_similarity(red_vec, blue_vec) < 0.85, "pixel-decoded RGB moments distinguish red vs blue images")

    // Verify P6 PPM pixel decoding & multi-language detection across all platforms
    mut ppm_green: []u8 = [
        0x50, 0x36, 0x0A, 0x32, 0x20, 0x32, 0x0A, 0x32, 0x35, 0x35, 0x0A,
        0x00, 0xFF, 0x00, 0x00, 0xFF, 0x00,
        0x00, 0xFF, 0x00, 0x00, 0xFF, 0x00
    ]
    try fs.write_bytes("green.ppm", ppm_green)
    green_vec := try ai.embed_image("local", "green.ppm")
    try fs.remove("green.ppm")
    assert_eq(green_vec.len(), 384, "PPM image embedded into 384-dim vector")

    assert_eq(ai.detect_language("Hello world, the quick brown fox jumps with the dog"), "en", "detect_language English")
    assert_eq(ai.detect_language("Hola mundo, el perro corre con la niña por el parque"), "es", "detect_language Spanish")
    assert_eq(ai.detect_language("こんにちは世界"), "ja", "detect_language Japanese")

    // Verify multimodal client.stream with file attachment
    att_msg := try ai.user_with_file("Stream summary of slides", "deck.pptx")
    mut stream_att_out := ""
    stream_att_full := try client.stream("mock-gpt", [att_msg], |tok| {
        stream_att_out = stream_att_out + tok
    })
    assert_eq(stream_att_full, "Hello AI", "multimodal stream with attachment succeeded")
    assert_eq(stream_att_out, "Hello AI", "multimodal stream callback accumulated tokens")

    bug_from_file := try client.generate_struct_from_file[ExtractedBug]("mock-gpt", "Inspect this slide deck", "deck.pptx")
    try fs.remove("deck.pptx")
    assert_eq(bug_from_file.component, "audio", "generate_struct_from_file parsed struct")

    println("ai_ok:top={}:bug={}:{}:stream={}:slides={}", rows[0].title, bug.component, bug.severity, full, pptx_doc.sections.len())
}
)CF");

        auto out_bin = tmp.path / "ai_test_bin";
        auto plan = cfire::build::build_native(cf_path, rt_dir, out_bin);
        require(plan.has_value(), std::format("build_native(ai_test.cf): {}", plan ? "" : plan.error()));
        if (plan) {
            int rc = 0;
            std::string out = cfire::bridge::run_command_capture(out_bin.string(), rc);
            require(rc == 0, std::format("ai_test exited with code {}:\n{}", rc, out));
            require(out.contains("ai_ok:top=VectorDB:bug=audio:4:stream=Hello AI:slides=2"),
                    std::format("unexpected ai_test output: {}", out));
        }
    }

    // =========================================================================
    // 2.21 Developer Ergonomics Milestones (ERG-01 through ERG-05)
    // =========================================================================
    {
        // ERG-03a: Numeric range loop immutability check (negative sema test)
        require_sema_error(R"(package main
fn main() {
    for i in 0..5 {
        i += 1
    }
}
)", "cfire/ASSIGN-IMMUTABLE", "imm_range.cf");

        // ERG-03b: Return inside defer block (negative sema test)
        require_sema_error(R"(package main
fn work() -> i64 {
    defer {
        return 1
    }
    return 0
}
)", "cfire/RETURN-IN-DEFER", "ret_defer.cf");

        // ERG-05a: Use-after-move inside arbitrary expression interpolation (negative sema test)
        require_sema_error(R"(package main
fn consume(move b: []u8) {}
fn main() {
    mut buf: []u8 = []
    consume(move buf)
    s := "len={buf.len() + 1}"
    println(s)
}
)", "cfire/USE-AFTER-MOVE", "bad_interp_move.cf");

        // ERG-02, ERG-03, ERG-04, ERG-05a: End-to-end multi-file package merging, range loops,
        // block defer LIFO order, expression interpolation, and single-eval power assertions
        ScopedTempDir tmp("cfire_erg_suite");
        tmp.file("helpers.cf", R"CF(package main

fn compute_range_sum(limit: i64) -> i64 {
    mut sum: i64 = 0
    for i in 0..limit {
        sum += i
    }
    for j in 1..=3 {
        sum += j * 10
    }
    return sum
}

fn format_summary(a: i64, b: i64) -> str {
    mut items: []str = ["alpha", "beta"]
    return "sum={a + b} first={items[0]} literal={{ok}}"
}
)CF");

        auto main_cf = tmp.file("main.cf", R"CF(package main

fn run_defer_demo() {
    println("step:start")
    defer {
        println("step:defer1a")
        println("step:defer1b")
    }
    defer {
        println("step:defer2")
    }
    println("step:body")
}

fn main() {
    total := compute_range_sum(5)
    assert(total == 70)
    assert_eq(total, 70)
    msg := format_summary(12, 30)
    println("{} hex={:02x}", msg, 255)
    run_defer_demo()
}
)CF");

        tmp.file("main_test.cf", R"CF(package main

mut eval_counter: i64 = 0

fn bumped() -> i64 {
    eval_counter += 1
    return eval_counter
}

test "ergonomics suite and single-eval assertions" {
    assert(compute_range_sum(5) == 70)
    assert_eq(bumped(), 1)
    assert(bumped() == 2)
    assert_eq(eval_counter, 2)
}
)CF");

        auto erg_bin = tmp.path / "erg_bin";
        auto plan = cfire::build::build_native(main_cf, rt_dir, erg_bin);
        require(plan.has_value(), std::format("ERG-02/03/04/05 build_native(main.cf): {}", plan ? "" : plan.error()));
        if (plan) {
            int rc = 0;
            std::string out = cfire::bridge::run_command_capture(erg_bin.string(), rc);
            require(rc == 0, std::format("erg_bin exited with code {}:\n{}", rc, out));
            require(out.contains("sum=42 first=alpha literal={ok} hex=ff"),
                    std::format("ERG-05a: unexpected interpolation output: {}", out));
            require(out.contains("step:start\nstep:body\nstep:defer2\nstep:defer1a\nstep:defer1b"),
                    std::format("ERG-03b: unexpected block defer output: {}", out));
        }

        // Verify ERG-01a/b & ERG-02c via CLI commands (`cfire check`, `cfire test`, `cfire new`)
        run_cfire(std::format("check \"{}\"", tmp.path.string()))
            .assert_ok("ERG-01b: cfire check <dir> ok");

        run_cfire(std::format("test \"{}\"", tmp.path.string()))
            .assert_ok("ERG-01a/02b: cfire test <dir> ok")
            .assert_contains("PASS (1 cfire tests succeeded)", "ERG-01a: test suite summary");

        // Verify power-assertion failure formatting (`[left: ..., right: ...]`)
        ScopedTempDir fail_tmp("cfire_erg_fail_assert");
        auto fail_test_cf = fail_tmp.file("fail_test.cf", R"CF(package main
test "power assert diff" {
    lhs := 41
    rhs := 42
    assert(lhs == rhs)
}
)CF");
        run_cfire(std::format("test \"{}\"", fail_test_cf.string()))
            .assert_fail("ERG-04b: failing power assert exits non-zero")
            .assert_contains("[left: 41, right: 42]", "ERG-04b: power assert shows evaluated lhs and rhs");

        // Verify ERG-05b (`cfire new <dir>`)
        ScopedTempDir new_tmp("cfire_erg_new_proj");
        auto proj_dir = new_tmp.path / "demo_app";
        run_cfire(std::format("new \"{}\"", proj_dir.string()))
            .assert_ok("ERG-05b: cfire new <dir> ok");
        require(std::filesystem::exists(proj_dir / "build.cf") &&
                std::filesystem::exists(proj_dir / "main.cf") &&
                std::filesystem::exists(proj_dir / "main_test.cf"),
                "ERG-05b: scaffolded files created");

        run_cfire(std::format("test \"{}\"", proj_dir.string()))
            .assert_ok("ERG-05b: cfire test on scaffolded project ok")
            .assert_contains("PASS (1 cfire tests succeeded)", "ERG-05b: scaffolded project test passed");

        // TEST-01 through TEST-06: Fuzzing, Shrinking, Auto-JSON Round-Trip, FD Leak Canary, Doc-Tests, Benchmarks & LSP
        ScopedTempDir test_suite_tmp("cfire_native_test_fuzz_bench");
        auto suite_cf = test_suite_tmp.file("suite.cf", R"CF(package main

import "ext:json"

type Packet struct {
    Id:   i64  `json:"id"`
    Tag:  str  `json:"tag"`
    Flag: bool `json:"flag"`
}

/// Doubles an integer and verifies via executable doc-test.
/// ```cf
/// assert_eq(double_val(21), 42)
/// assert(double_val(0) == 0)
/// ```
fn double_val(x: i64) -> i64 {
    return x * 2
}

fuzz "double_val algebraic property" |x: i64| {
    if x > -1000000 && x < 1000000 {
        assert_eq(double_val(x), x + x)
    }
}

fuzz "struct arbitrary generation" |p: Packet| {
    s := json.stringify(p)
    assert(s.len() > 0)
}

bench "double_val throughput" |size in [16, 64]| {
    mut acc: i64 = 0
    for i in 0..size {
        acc += double_val(i)
    }
    black_box(acc)
}

fn main() {}
)CF");

        // 1. Run `cfire test` (verifies fuzzing, doc-test extraction, [auto] json round-trip synthesis, and 1-iter bench smoke pass)
        run_cfire(std::format("test \"{}\" --fuzz-runs 32 --seed 42", suite_cf.string()))
            .assert_ok("TEST-01/03b/05a: cfire test with fuzz, doctest, auto json round-trip, and bench smoke")
            .assert_contains("[doctest] double_val", "TEST-05a: executable /// doctest discovered and passed")
            .assert_contains("[auto] json round-trip: Packet", "TEST-03b: automatic JSON round-trip test synthesized and passed")
            .assert_contains("--- PASS: double_val algebraic property", "TEST-01: fuzz block passed");

        // 2. Run `cfire bench` (verifies multi-iteration statistical harness and parameter variations)
        run_cfire(std::format("bench \"{}\" --bench-time 25", suite_cf.string()))
            .assert_ok("TEST-05b/c/d: cfire bench multi-iteration parameterized benchmark")
            .assert_contains("double_val throughput/size=16", "TEST-05c: benchmark variation size=16")
            .assert_contains("double_val throughput/size=64", "TEST-05c: benchmark variation size=64")
            .assert_contains("ops/s", "TEST-05b: benchmark statistical output columns");

        // 3. Verify TEST-02: Automatic Counterexample Shrinking on failing fuzz test
        ScopedTempDir shrink_tmp("cfire_native_fuzz_shrink");
        auto shrink_cf = shrink_tmp.file("shrink_fail.cf", R"CF(package main
fuzz "shrink integer above 100" |x: i64| {
    assert(x < 100)
}
)CF");
        run_cfire(std::format("test \"{}\" --fuzz-runs 64 --seed 12345", shrink_cf.string()))
            .assert_fail("TEST-02: failing fuzz block exits non-zero")
            .assert_contains("(x=100)", "TEST-02: shrinks failing i64 down to exact minimal boundary 100")
            .assert_contains("reproduce: --seed 12345", "TEST-02: prints deterministic reproduction command");

        // 4. Verify TEST-04: Automatic Resource-Leak Canary on unclosed file descriptor
        ScopedTempDir leak_tmp("cfire_native_fd_leak");
        auto leak_cf = leak_tmp.file("leak_fail.cf", R"CF(package main
test "deliberate fd leak" {
    cpp {
        std::FILE* leaked_fp = std::fopen("/dev/null", "r");
        (void)leaked_fp;
    }
}
)CF");
        run_cfire(std::format("test \"{}\"", leak_cf.string()))
            .assert_fail("TEST-04: leaked file descriptor fails test")
            .assert_contains("resource leak canary", "TEST-04: reports open file descriptor leak canary");

        // 5. Verify TEST-06: LSP hover/symbols and `cfire outline --json` for `fuzz`, `bench`, `black_box`
        run_cfire(std::format("outline --json \"{}\"", suite_cf.string()))
            .assert_ok("TEST-06: cfire outline --json ok")
            .assert_contains("\"kind\": \"fuzz\"", "TEST-06: outline --json includes fuzz kind")
            .assert_contains("\"kind\": \"bench\"", "TEST-06: outline --json includes bench kind");

        {
            cfire::lsp::Server lsp_srv;
            std::string uri = cfire::lsp::detail::path_to_uri(suite_cf);
            std::string syms = lsp_srv.compute_document_symbols(uri);
            require(syms.contains("fuzz: double_val algebraic property") &&
                    syms.contains("bench: double_val throughput"),
                    std::format("TEST-06a: LSP documentSymbol includes fuzz and bench blocks: {}", syms));
        }

        // 6. Verify Upstream Feedback Language & Codegen Fixes (CF-013..CF-049) & Language Design Invariants (UPSTREAM-02)
        {
            // CF-039: Multiline raw string closing line with braces does not drift formatter indentation
            std::string raw_src = "package main\n\nfn build_html() -> str {\n    if true {\n        s := `line1\nline2`}\n    return \"ok\"\n}\n";
            std::string formatted = cfire::fmt::format_source(raw_src).value_or("");
            require(formatted.contains("    return \"ok\""),
                std::format("CF-039: formatter keeps 4-space indent after multiline raw string close:\n{}", formatted));

            // UPSTREAM-02 Item 1: `as` cast syntax is rejected with actionable diagnostic pointing to `T(x)` / `[]byte(x)`
            {
                auto as_res = cfire::frontend::parse_source("bad_as.cf", "package main\nfn main() {\n    x := 42 as i64\n}\n");
                require(!as_res.has_value() && as_res.error().contains("'as' cast syntax is not supported in cfire") &&
                        as_res.error().contains("i64(...)"),
                        std::format("UPSTREAM-02: expected actionable 'as' diagnostic, got: {}", as_res ? "ok" : as_res.error()));

                auto as_bytes = cfire::frontend::parse_source("bad_as_bytes.cf", "package main\nfn main() {\n    b := \"hello\" as []byte\n}\n");
                require(!as_bytes.has_value() && as_bytes.error().contains("use constructor conversion '[]byte(...)' instead"),
                        std::format("UPSTREAM-02: expected actionable '[]byte(...)' diagnostic, got: {}", as_bytes ? "ok" : as_bytes.error()));
            }

            // UPSTREAM-02 Item 2: Top-level `cpp { ... }` is rejected with actionable diagnostic
            {
                auto top_cpp = cfire::frontend::parse_source("bad_top_cpp.cf", "package main\ncpp { int x = 1; }\nfn main() {}\n");
                require(!top_cpp.has_value() && top_cpp.error().contains("top-level 'cpp { ... }' blocks are not supported in cfire"),
                        std::format("UPSTREAM-02: expected top-level cpp rejection diagnostic, got: {}", top_cpp ? "ok" : top_cpp.error()));
            }

            // UPSTREAM-02 Item 3: `const` declaration is rejected with actionable diagnostic pointing to `:=`
            {
                auto c_res = cfire::frontend::parse_source("bad_const.cf", "package main\nconst LIMIT = 42\nfn main() {}\n");
                require(!c_res.has_value() && c_res.error().contains("bindings are immutable by default"),
                        std::format("UPSTREAM-02: expected actionable 'const' diagnostic, got: {}", c_res ? "ok" : c_res.error()));
            }

            // UPSTREAM-02 Item 4: `.{ ... }` anonymous struct literal is rejected
            {
                auto dot_res = cfire::frontend::parse_source("bad_dot.cf", "package main\nfn main() {\n    x := .{ a: 1 }\n}\n");
                require(!dot_res.has_value(), "UPSTREAM-02: anonymous '.{...}' struct literal must be rejected");
            }

            // UPSTREAM-02 Item 5: `or err { ... }` on optional `T?` is rejected by `cfire/OPTIONAL-OR-ERR`
            require_sema_error(R"(package main
fn maybe_num() -> i64? {
    return none
}
fn main() {
    x := maybe_num() or err {
        return
    }
    println("{}", x)
}
)", "cfire/OPTIONAL-OR-ERR", "bad_opt_or_err.cf");

            // UPSTREAM-02 Item 6: Unguarded `opt.field` on optional `T?` is rejected by `cfire/NULL-DEREF`
            require_sema_error(R"(package main
type Item struct {
    id: i64
}
fn get_item() -> Item? {
    return none
}
fn main() {
    it := get_item()
    println("{}", it.id)
}
)", "cfire/NULL-DEREF", "bad_opt_deref.cf");

            ScopedTempDir cf_tmp("cfire_upstream_lang_fixes");
            auto sub_dir = cf_tmp.subpath("subpkg");
            std::filesystem::create_directories(sub_dir);
            (void)cfire::fs::write_text(sub_dir / "helper.cf", R"CF(package subpkg

LIMIT := 100

fn helper() -> i64 {
    return LIMIT + 7
}
)CF");

            auto upstream_cf = cf_tmp.file("upstream_suite.cf", R"CF(package main

import "ext:json"
import "ext:db/sqlite"
import "core:str"
import "core:fs"
import "./subpkg" as sp

GLOBAL_LIMIT := 42
TYPED_LIMIT: i64 = 100
LIMIT := 5

// UPSTREAM-02 Item 7: Same-named helper() in main and imported package `sp`
fn helper() -> i64 {
    return LIMIT
}

// User struct with .open() and .read() methods to verify no UFCS collision with `fs.open` / `f.read`
type CustomHandle struct {
    tag: str
}

fn (h: CustomHandle) open() -> str {
    return "custom-open:" + h.tag
}

fn (h: CustomHandle) read() -> str {
    return "custom-read:" + h.tag
}

// CF-033: Out-of-order struct declarations (Parent references Child before Child is declared)
// CF-025: Comma-separated struct fields
// CF-049 & CF-037: Untagged struct fields + optional T? field in JSON
type ParentConfig struct {
    title: str,
    child: ChildItem,
    note: str?
}

type ChildItem struct {
    export: i32,
    stdout: str
}

type PtrNode struct {
    val: i32
    next: *PtrNode = none
}

fn find_node(flag: bool) -> *PtrNode {
    if !flag {
        return none
    }
    return none
}

fn make_rows(ok: bool) -> [][]str! {
    if !ok {
        return error("rows failed")
    }
    return [["a", "b"], ["c"]]
}

fn greet(name: str) -> str {
    return "hi-" + name
}

fn classify_code(code: i32) -> str {
    return match code {
        200 => "ok"
        404 => "missing"
        _ => "other"
    }
}

fn maybe_child(ok: bool) -> ChildItem? {
    if !ok {
        return none
    }
    return ChildItem{export: 55, stdout: "narrowed"}
}

test "upstream language and codegen fixes (CF-013..CF-049)" {
    // CF-013: Canonical constructor conversions `i64(x)` and `bool(x)`
    x: i32 = 15
    y := i64(x) + GLOBAL_LIMIT + TYPED_LIMIT
    assert(y == 157)
    assert(bool(1) == true)
    assert(bool(0) == false)

    // CF-014: local immutable binding (including `input` identifier without prelude collision)
    LOCAL_K := 9
    input := "user_input_var"
    assert(LOCAL_K == 9)
    assert(input == "user_input_var")

    // UPSTREAM-02 Item 2: Canonical `[]byte(...)` and `[]u8(...)` slice type constructors
    mut b1: []byte = []byte("abc")
    b1.push(byte(100))
    assert(b1.len() == 4)
    assert(str(b1) == "abcd")
    u1 := []u8("xyz")
    assert(u1.len() == 3)
    assert(str(u1) == "xyz")

    // UPSTREAM-02 Item 7: Multi-package symbol hygiene & UFCS non-collision
    assert(helper() == 5)
    assert(sp.helper() == 107)
    ch := CustomHandle{tag: "h1"}
    assert(ch.open() == "custom-open:h1")
    assert(ch.read() == "custom-read:h1")

    // CF-019: pointer default none and return none
    n := PtrNode{val: 5}
    assert(n.next == none)
    assert(find_node(false) == none)

    // CF-020 & CF-038: C++ keywords (`export`, `delete`, `class`) and `stdout` macro as identifiers
    delete := 12
    class := "ssh"
    assert(delete == 12)
    assert(class == "ssh")

    // CF-033, CF-037, CF-049: Out-of-order structs + untagged JSON + optional T? field
    p := ParentConfig{
        title: "flint",
        child: ChildItem{export: 7, stdout: "ready"},
        note: none
    }
    raw_json := json.stringify(p)
    parsed := try json.parse[ParentConfig](raw_json)
    assert(parsed.title == "flint")
    assert(parsed.child.export == 7)
    assert(parsed.child.stdout == "ready")
    assert(parsed.note == none)

    // CF-032 & CF-040/CF-047: `[][]str!` and `or` fallback values
    rows := make_rows(true) or []
    assert(rows.len() == 2)
    empty_rows := make_rows(false) or []
    assert(empty_rows.len() == 0)

    num := str.parse_i64("not_a_number") or -1
    assert(num == -1)

    // CF-034: Nested string literals inside string interpolation
    msg := "result={greet("world")}"
    assert(msg == "result=hi-world")

    // CF-035: `match` expression returning string literals
    label := classify_code(200)
    assert(label == "ok")

    // CF-036: Optional `T?` field access after `!= none` and early-return `== none` guard
    mut opt_child: ChildItem? = ChildItem{export: 99, stdout: "opt"}
    if opt_child != none {
        assert(opt_child.export == 99)
        assert(opt_child.stdout == "opt")
    }
    guarded := maybe_child(true)
    if guarded == none {
        return
    }
    assert(guarded.export == 55)

    // CF-044: `assert(slice == [a, b])`
    items := ["alpha", "beta"]
    assert(items == ["alpha", "beta"])

    // CF-048: `or` handler block inside reassignment (`=`) and parameterless `or { ... }`
    mut parsed_val: i64 = 0
    parsed_val = str.parse_i64("1234") or {
        return
    }
    assert(parsed_val == 1234)

    mut r2: [][]str = []
    r2 = make_rows(true) or err {
        return
    }
    assert(r2.len() == 2)

    unwrapped_opt := maybe_child(true) or {
        return
    }
    assert(unwrapped_opt.export == 55)

    // CF-007: sqlite.open_encrypted + []byte BLOB parameter binding
    mut db := try sqlite.open_encrypted(":memory:", "vault-secret-key")
    defer db.close()
    try db.exec("CREATE TABLE secrets (id TEXT, cipher BLOB)")
    blob := []byte("encrypted-payload")
    try db.exec("INSERT INTO secrets VALUES (?1, ?2)", "k1", blob)
}
)CF");
            run_cfire(std::format("test \"{}\"", upstream_cf.string()))
                .assert_ok("CF-013..CF-049: upstream language & codegen suite passes")
                .assert_contains("PASS: upstream language and codegen fixes", "CF-013..CF-049 test block passed");

            // UPSTREAM-03: Comprehensive Orthogonality, build.cf Frameworks/Libraries, Type Inference & Docs/LSP/MCP Parity
            {
                // 1. Reject `: bytes` type annotation with actionable diagnostic suggesting `[]byte`
                auto bad_bytes_type = cfire::frontend::parse_source("bad_bytes_type.cf", "package main\nfn main() {\n    b: bytes = []byte(\"hi\")\n}\n");
                require(!bad_bytes_type.has_value() && bad_bytes_type.error().contains("'bytes' is not a type in cfire; use '[]byte' instead"),
                        std::format("UPSTREAM-03: expected ': bytes' rejection diagnostic, got: {}", bad_bytes_type ? "ok" : bad_bytes_type.error()));

                // 2. Reject top-level `cpp { ... }`
                auto bad_top_cpp = cfire::frontend::parse_source("bad_top_cpp.cf", "package main\ncpp {\n    int x = 1;\n}\nfn main() {}\n");
                require(!bad_top_cpp.has_value(), "UPSTREAM-03: top-level cpp { ... } must be rejected");

                // 3. Declarative `Frameworks` and `Libraries` on `build.Executable` in `build.cf`
                auto bcf_dir = cf_tmp.subpath("bcf_frameworks");
                std::filesystem::create_directories(bcf_dir);
                (void)cfire::fs::write_text(bcf_dir / "build.cf", R"CF(package main
import "core:build"

app := build.Executable{
    Name: "vault_app",
    Entry: "main.cf",
    Frameworks: ["Security", "CoreFoundation"],
    Libraries: ["z", "sqlite3"],
    UrlSchemes: ["ssh", "sftp"],
    SignIdentity: "Developer ID Application: Flint",
}
)CF");
                (void)cfire::fs::write_text(bcf_dir / "main.cf", "package main\nfn main() {}\n");
                auto parsed_bcf = cfire::build::parse_build_cf(bcf_dir / "build.cf");
                require(parsed_bcf.has_value() && parsed_bcf->executables.size() == 1, "UPSTREAM-03: parse_build_cf with Frameworks & Libraries");
                require(parsed_bcf->executables[0].frameworks == std::vector<std::string>{"Security", "CoreFoundation"},
                        "UPSTREAM-03: Executable.Frameworks parsed");
                require(parsed_bcf->executables[0].libraries == std::vector<std::string>{"z", "sqlite3"},
                        "UPSTREAM-03: Executable.Libraries parsed");
                require(parsed_bcf->executables[0].url_schemes == std::vector<std::string>{"ssh", "sftp"},
                        "CF-052: Executable.UrlSchemes parsed");
                require(parsed_bcf->executables[0].sign_identity == "Developer ID Application: Flint",
                        "CF-052: Executable.SignIdentity parsed");

                // CF-005: End-to-end .cf compilation and execution with `import "ext:net/ssh"`
                auto ssh_cf = cf_tmp.file("ssh_e2e/ssh_suite.cf", R"CF(package main

import "ext:net/ssh"

test "ssh e2e session and pty" {
    ssh.enable_mock_server(true)
    mut sess := try ssh.dial("10.0.0.42", 22, "root", 2000)
    defer sess.close()

    fp := sess.host_key_fingerprint()
    assert(fp.starts_with("SHA256:"))
    try sess.auth_publickey_mem("-----BEGIN OPENSSH PRIVATE KEY-----\ntest\n-----END OPENSSH PRIVATE KEY-----", "")

    res := try sess.exec("whoami")
    assert_eq(res.exit_code, 0)

    mut ch := try sess.open_pty("xterm-256color", 120, 36)
    defer ch.close()
    _ = try ch.write("echo pty-ok\n")
    try ch.resize_pty(132, 42)
    chunk := try ch.read(1024)
    assert(chunk.contains("pty-ok"))

    try sess.sftp_write_all("/tmp/hello.txt", "sftp-data")
    got := try sess.sftp_read_all("/tmp/hello.txt")
    assert_eq(got, "sftp-data")
}
)CF");
                run_cfire(std::format("test \"{}\"", ssh_cf.string()))
                    .assert_ok("CF-005: ext:net/ssh .cf compilation and execution passes")
                    .assert_contains("PASS: ssh e2e session and pty", "CF-005 test block passed");

                // 4. LSP & Sema type inference for `[]byte(x)`, `[]u8(x)`, `str(x)`, `readln()`, and `OrExpr` on `T?`
                auto inf_ast = cfire::frontend::parse_source("infer_test.cf", R"CF(package main
fn main() {
    b := []byte("abc")
    u := []u8("abc")
    s := str(b)
    ln := readln("> ") or "fallback"
}
)CF");
                require(inf_ast.has_value(), "UPSTREAM-03: parse infer_test.cf");
                cfire::lsp::detail::WorkspaceContext ws_ctx;
                std::unordered_map<std::string, cfire::lsp::detail::LocalBindingInfo> env_map;
                const auto& body = inf_ast->functions[0].body;
                require(body.size() == 4, "UPSTREAM-03: 4 var decls in infer_test.cf");
                const auto* vd_b = std::get_if<cfire::frontend::VarDeclStmt>(&body[0]->node);
                const auto* vd_u = std::get_if<cfire::frontend::VarDeclStmt>(&body[1]->node);
                const auto* vd_s = std::get_if<cfire::frontend::VarDeclStmt>(&body[2]->node);
                const auto* vd_ln = std::get_if<cfire::frontend::VarDeclStmt>(&body[3]->node);
                require(vd_b && cfire::lsp::detail::infer_expr_cf_type(vd_b->init, *inf_ast, ws_ctx, env_map) == "[]byte",
                        "UPSTREAM-03: infer_expr_cf_type([]byte(\"abc\")) == []byte");
                require(vd_u && cfire::lsp::detail::infer_expr_cf_type(vd_u->init, *inf_ast, ws_ctx, env_map) == "[]u8",
                        "UPSTREAM-03: infer_expr_cf_type([]u8(\"abc\")) == []u8");
                require(vd_s && cfire::lsp::detail::infer_expr_cf_type(vd_s->init, *inf_ast, ws_ctx, env_map) == "str",
                        "UPSTREAM-03: infer_expr_cf_type(str(b)) == str");
                require(vd_ln && cfire::lsp::detail::infer_expr_cf_type(vd_ln->init, *inf_ast, ws_ctx, env_map) == "str",
                        "UPSTREAM-03: infer_expr_cf_type(readln() or \"fallback\") unwraps str? to str");

                // 5. Catalog, MCP, and LLM Reference Completeness Invariant Check
                auto has_cat_fn = [](std::string_view mod_path, std::string_view fn_name) -> bool {
                    for (const auto& mod : cfire::doc::stdlib_catalog()) {
                        if (mod.import_path == mod_path) {
                            for (const auto& fn : mod.functions) {
                                if (fn.name == fn_name) return true;
                            }
                        }
                    }
                    return false;
                };
                require(has_cat_fn("core:cli", "prompt_password") && has_cat_fn("core:cli", "subcommand") && has_cat_fn("core:cli", "positional"),
                        "UPSTREAM-03: core:cli functions in stdlib_catalog");
                require(has_cat_fn("core:crypto/aes", "generate_key") && has_cat_fn("core:crypto/aes", "encrypt") && has_cat_fn("core:crypto/aes", "decrypt"),
                        "UPSTREAM-03: core:crypto/aes functions in stdlib_catalog");
                require(has_cat_fn("core:crypto/hmac", "pbkdf2_sha256"),
                        "UPSTREAM-03: core:crypto/hmac pbkdf2_sha256 in stdlib_catalog");
                require(has_cat_fn("core:os", "args") && has_cat_fn("core:os", "exit") && has_cat_fn("core:os", "getenv_opt") &&
                        has_cat_fn("core:os", "term_size") && has_cat_fn("core:os", "make_raw") && has_cat_fn("core:os", "on_signal") &&
                        !has_cat_fn("core:os", "read_line"),
                        "UPSTREAM-03: core:os functions in stdlib_catalog (and removed read_line absent)");
                require(has_cat_fn("core:str", "parse_i64") && has_cat_fn("core:str", "parse_f64") && has_cat_fn("core:str", "parse_bool") && has_cat_fn("core:str", "find"),
                        "UPSTREAM-03: core:str parse_* and find in stdlib_catalog");
                require(has_cat_fn("core:sync", "Channel.try_recv") && has_cat_fn("core:sync", "Channel.recv_timeout") &&
                        has_cat_fn("core:sync", "Mutex") && has_cat_fn("core:sync", "Mutex.with_lock"),
                        "UPSTREAM-03: core:sync try_recv, recv_timeout, and Mutex in stdlib_catalog");
                require(has_cat_fn("ext:webui", "App.bind_async") && has_cat_fn("ext:webui", "App.emit_bytes") &&
                        has_cat_fn("ext:webui", "App.clipboard_set") && has_cat_fn("ext:webui", "App.clipboard_set_secret") &&
                        has_cat_fn("ext:webui", "App.set_icon") && has_cat_fn("ext:webui", "App.add_menu_item") &&
                        has_cat_fn("ext:webui", "App.monospace_fonts"),
                        "UPSTREAM-03: ext:webui async/binary/clipboard/secret/icon/menu/fonts in stdlib_catalog");
                require(has_cat_fn("ext:net/ssh", "dial") && has_cat_fn("ext:net/ssh", "Session.auth_publickey_mem") &&
                        has_cat_fn("ext:net/ssh", "Session.open_pty") && has_cat_fn("ext:net/ssh", "Channel.resize_pty") &&
                        has_cat_fn("ext:net/ssh", "Session.sftp_write_all") && has_cat_fn("ext:net/ssh", "Session.open_forward"),
                        "CF-005: ext:net/ssh functions in stdlib_catalog");

                std::string llm_ref = cfire::doc::generate_llm_reference();
                require(llm_ref.contains("[]byte(s_or_slice)") && llm_ref.contains("readln(prompt: str = \"\") -> str?") &&
                        llm_ref.contains("cfire/OPTIONAL-OR-ERR") && !llm_ref.contains("`bytes`, `[]byte`"),
                        "UPSTREAM-03: generate_llm_reference includes canonical []byte(x), readln, and OPTIONAL-OR-ERR");
            }
        }
    }

    std::println("--- PASS: test_section2_language_ergonomics");
}



