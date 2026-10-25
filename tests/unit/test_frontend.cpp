#include "../test_harness.hpp"

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

        // Verify Guide Chapter 9 Build System and Chapter 11 Raylib/Raygui reconciliation
        auto guide_path = docs_dir / "GUIDE.md";
        auto guide_txt = cfire::build::read_file_text(guide_path);
        require(guide_txt.has_value() &&
                guide_txt->contains("Chapter 9: The Cfire Build System: Native, Cross-Target & WebAssembly") &&
                guide_txt->contains("build.CC(") &&
                guide_txt->contains("KVStore := build.CMake(") &&
                guide_txt->contains("build.Make") &&
                guide_txt->contains("build.Git") &&
                guide_txt->contains("build.Archive") &&
                guide_txt->contains("build.Command") &&
                guide_txt->contains("--target=wasm") &&
                guide_txt->contains("Chapter 11: 2D/3D Graphics & GUI Applications with Raylib and Raygui") &&
                guide_txt->contains("vendor:raylib") &&
                guide_txt->contains("vendor:raygui"),
            "docs/GUIDE.md Chapter 9 Build System and Chapter 11 Raylib/Raygui reconciliation");
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

    std::println("--- PASS: test_section2_language_ergonomics");
}



