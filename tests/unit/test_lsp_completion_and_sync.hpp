#pragma once

#include "../test_harness.hpp"
#include "../../src/lsp/server.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

inline std::string lsp_comp_frame(std::string_view body) {
    return "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + std::string(body);
}

inline std::string read_file_text_local(const std::filesystem::path& p) {
    std::ifstream ifs(p, std::ios::binary);
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

inline std::string make_did_open_rpc(std::string_view uri, std::string_view text) {
    return std::string("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":"
                       "{\"textDocument\":{\"uri\":\"") +
           std::string(uri) + "\",\"languageId\":\"cfire\",\"version\":1,\"text\":\"" +
           cfire::lsp::detail::json_escape(text) + "\"}}}";
}

inline std::vector<std::string> split_json_array_objects(std::string_view arr_json) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < arr_json.size() && arr_json[i] != '[') ++i;
    if (i >= arr_json.size()) return out;
    ++i;
    while (i < arr_json.size()) {
        while (i < arr_json.size() && (arr_json[i] == ' ' || arr_json[i] == '\t' ||
                                       arr_json[i] == '\n' || arr_json[i] == '\r' ||
                                       arr_json[i] == ',')) {
            ++i;
        }
        if (i >= arr_json.size() || arr_json[i] == ']') break;
        if (arr_json[i] != '{') {
            ++i;
            continue;
        }
        std::size_t start = i;
        int depth = 0;
        bool in_str = false;
        while (i < arr_json.size()) {
            char c = arr_json[i];
            if (in_str) {
                if (c == '\\' && i + 1 < arr_json.size()) {
                    i += 2;
                    continue;
                }
                if (c == '"') in_str = false;
            } else {
                if (c == '"') in_str = true;
                else if (c == '{') ++depth;
                else if (c == '}') {
                    --depth;
                    if (depth == 0) {
                        ++i;
                        break;
                    }
                }
            }
            ++i;
        }
        out.emplace_back(arr_json.substr(start, i - start));
    }
    return out;
}

inline std::string find_json_field_raw(std::string_view obj_json, std::string_view key) {
    std::size_t i = 0;
    while (i < obj_json.size() && obj_json[i] != '{') ++i;
    if (i >= obj_json.size()) return {};
    ++i;
    int depth = 1;
    while (i < obj_json.size() && depth > 0) {
        char c = obj_json[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ',') {
            ++i;
            continue;
        }
        if (c == '{' || c == '[') {
            ++depth;
            ++i;
            continue;
        }
        if (c == '}' || c == ']') {
            --depth;
            ++i;
            continue;
        }
        if (c == '"' && depth == 1) {
            std::size_t kstart = i + 1;
            ++i;
            while (i < obj_json.size()) {
                if (obj_json[i] == '\\' && i + 1 < obj_json.size()) {
                    i += 2;
                    continue;
                }
                if (obj_json[i] == '"') break;
                ++i;
            }
            std::string_view found_key = obj_json.substr(kstart, i - kstart);
            if (i < obj_json.size()) ++i;
            while (i < obj_json.size() && (obj_json[i] == ' ' || obj_json[i] == '\t' ||
                                           obj_json[i] == '\n' || obj_json[i] == '\r')) {
                ++i;
            }
            if (i < obj_json.size() && obj_json[i] == ':') {
                ++i;
                while (i < obj_json.size() && (obj_json[i] == ' ' || obj_json[i] == '\t' ||
                                               obj_json[i] == '\n' || obj_json[i] == '\r')) {
                    ++i;
                }
                std::size_t vstart = i;
                if (i < obj_json.size() && obj_json[i] == '"') {
                    ++i;
                    while (i < obj_json.size()) {
                        if (obj_json[i] == '\\' && i + 1 < obj_json.size()) {
                            i += 2;
                            continue;
                        }
                        if (obj_json[i] == '"') {
                            ++i;
                            break;
                        }
                        ++i;
                    }
                } else if (i < obj_json.size() && (obj_json[i] == '{' || obj_json[i] == '[')) {
                    int vdepth = 0;
                    bool vstr = false;
                    while (i < obj_json.size()) {
                        char vc = obj_json[i];
                        if (vstr) {
                            if (vc == '\\' && i + 1 < obj_json.size()) {
                                i += 2;
                                continue;
                            }
                            if (vc == '"') vstr = false;
                        } else {
                            if (vc == '"') vstr = true;
                            else if (vc == '{' || vc == '[') ++vdepth;
                            else if (vc == '}' || vc == ']') {
                                --vdepth;
                                if (vdepth == 0) {
                                    ++i;
                                    break;
                                }
                            }
                        }
                        ++i;
                    }
                } else {
                    while (i < obj_json.size() && obj_json[i] != ',' && obj_json[i] != '}' &&
                           obj_json[i] != ']' && obj_json[i] != ' ' && obj_json[i] != '\n') {
                        ++i;
                    }
                }
                if (found_key == key) {
                    return std::string(obj_json.substr(vstart, i - vstart));
                }
                continue;
            }
        } else if (c == '"') {
            ++i;
            while (i < obj_json.size()) {
                if (obj_json[i] == '\\' && i + 1 < obj_json.size()) {
                    i += 2;
                    continue;
                }
                if (obj_json[i] == '"') {
                    ++i;
                    break;
                }
                ++i;
            }
            continue;
        }
        ++i;
    }
    return {};
}

inline std::string find_json_str(std::string_view obj_json, std::string_view key) {
    std::string raw = find_json_field_raw(obj_json, key);
    if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"') {
        return cfire::lsp::detail::json_unescape(raw.substr(1, raw.size() - 2));
    }
    return {};
}

inline int find_json_int(std::string_view obj_json, std::string_view key, int fallback = -1) {
    std::string raw = find_json_field_raw(obj_json, key);
    if (raw.empty()) return fallback;
    try {
        return std::stoi(raw);
    } catch (...) {
        return fallback;
    }
}

inline std::string find_item_by_label(const std::vector<std::string>& items, std::string_view label,
                                      std::string_view detail_substr = "") {
    for (const auto& item : items) {
        if (find_json_str(item, "label") != label) continue;
        if (!detail_substr.empty()) {
            std::string det = find_json_str(item, "detail");
            std::string ld = find_json_field_raw(item, "labelDetails");
            if (!det.contains(detail_substr) && !ld.contains(detail_substr)) continue;
        }
        return item;
    }
    return {};
}

inline std::size_t utf16_pos_to_utf8_offset(std::string_view text, int target_line, int target_u16_col) {
    int line = 0;
    std::size_t i = 0;
    while (i < text.size() && line < target_line) {
        if (text[i] == '\n') ++line;
        ++i;
    }
    if (line != target_line) return text.size();
    int u16_col = 0;
    while (i < text.size() && u16_col < target_u16_col) {
        if (text[i] == '\n' || text[i] == '\r') break;
        unsigned char b = static_cast<unsigned char>(text[i]);
        std::size_t char_bytes = 1;
        int char_u16 = 1;
        if ((b & 0x80u) == 0) {
            char_bytes = 1;
            char_u16 = 1;
        } else if ((b & 0xE0u) == 0xC0u) {
            char_bytes = 2;
            char_u16 = 1;
        } else if ((b & 0xF0u) == 0xE0u) {
            char_bytes = 3;
            char_u16 = 1;
        } else if ((b & 0xF8u) == 0xF0u) {
            char_bytes = 4;
            char_u16 = 2;
        }
        if (u16_col + char_u16 > target_u16_col) break;
        i = std::min(text.size(), i + char_bytes);
        u16_col += char_u16;
    }
    return i;
}

inline std::string apply_completion_edits(std::string doc, std::string_view item_json) {
    struct EditSpan {
        std::size_t start;
        std::size_t end;
        std::string new_text;
    };
    std::vector<EditSpan> edits;
    auto push_edit = [&](std::string_view te_json) {
        if (te_json.empty()) return;
        std::string range = find_json_field_raw(te_json, "range");
        std::string start_obj = find_json_field_raw(range, "start");
        std::string end_obj = find_json_field_raw(range, "end");
        int sl = find_json_int(start_obj, "line");
        int sc = find_json_int(start_obj, "character");
        int el = find_json_int(end_obj, "line");
        int ec = find_json_int(end_obj, "character");
        require(sl >= 0 && sc >= 0 && el >= 0 && ec >= 0,
            "completion textEdit missing valid start/end position: " + std::string(te_json));
        edits.push_back({
            utf16_pos_to_utf8_offset(doc, sl, sc),
            utf16_pos_to_utf8_offset(doc, el, ec),
            find_json_str(te_json, "newText"),
        });
    };

    std::string te = find_json_field_raw(item_json, "textEdit");
    require(!te.empty(), "completion item missing textEdit: " + std::string(item_json));
    push_edit(te);

    std::string add_raw = find_json_field_raw(item_json, "additionalTextEdits");
    if (!add_raw.empty()) {
        for (const auto& ae : split_json_array_objects(add_raw)) {
            push_edit(ae);
        }
    }

    std::sort(edits.begin(), edits.end(), [](const EditSpan& a, const EditSpan& b) {
        if (a.start != b.start) return a.start > b.start;
        return a.end > b.end;
    });
    for (const auto& e : edits) {
        require(e.start <= e.end && e.end <= doc.size(), "completion textEdit out of bounds");
        doc.replace(e.start, e.end - e.start, e.new_text);
    }
    return doc;
}

inline std::pair<int, int> strip_cursor_marker(std::string& src, std::string_view marker = "@CURSOR@") {
    std::size_t pos = src.find(marker);
    require(pos != std::string::npos, "test helper requires @CURSOR@ marker");
    int line = 0;
    std::size_t line_start = 0;
    for (std::size_t i = 0; i < pos; ++i) {
        if (src[i] == '\n') {
            ++line;
            line_start = i + 1;
        }
    }
    int u16_col = 0;
    for (std::size_t i = line_start; i < pos;) {
        unsigned char b = static_cast<unsigned char>(src[i]);
        if ((b & 0x80u) == 0) {
            i += 1;
            u16_col += 1;
        } else if ((b & 0xE0u) == 0xC0u) {
            i += 2;
            u16_col += 1;
        } else if ((b & 0xF0u) == 0xE0u) {
            i += 3;
            u16_col += 1;
        } else {
            i += 4;
            u16_col += 2;
        }
    }
    src.erase(pos, marker.size());
    return {line, u16_col};
}

struct CompletionRun {
    std::string doc;
    int line = 0;
    int character = 0;
    cfire::lsp::Server::CompletionListResult list;
    std::vector<std::string> items;
};

inline CompletionRun complete_at_cursor(std::string src_with_cursor,
                                        std::string uri = "file:///tmp/lsp_comp_test.cf") {
    auto [line, col] = strip_cursor_marker(src_with_cursor);
    std::ostringstream dummy_out;
    cfire::lsp::Server srv;
    srv.handle_message(
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"processId":1,"rootUri":"file:///tmp","capabilities":{"textDocument":{"completion":{"completionItem":{"snippetSupport":true}}}}}})",
        dummy_out);
    srv.handle_message(make_did_open_rpc(uri, src_with_cursor), dummy_out);
    auto list = srv.compute_completions_list(uri, line, col);
    auto items = split_json_array_objects(list.items_json);
    srv.stop_worker();
    return {std::move(src_with_cursor), line, col, std::move(list), std::move(items)};
}

inline std::string signature_at_cursor(std::string src_with_cursor,
                                       std::string uri = "file:///tmp/lsp_sig_test.cf") {
    auto [line, col] = strip_cursor_marker(src_with_cursor);
    std::ostringstream dummy_out;
    cfire::lsp::Server srv;
    srv.handle_message(
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"processId":1,"rootUri":"file:///tmp","capabilities":{}}})",
        dummy_out);
    srv.handle_message(make_did_open_rpc(uri, src_with_cursor), dummy_out);
    std::string sig = srv.compute_signature_help(uri, line, col);
    srv.stop_worker();
    return sig;
}

inline void test_sync_and_brace_config(const std::filesystem::path& rt_dir) {
    // 1. Multi-edit didChange (auto-brace + Enter + auto-import), reversed key ordering
    {
        std::ostringstream out;
        cfire::lsp::Server srv;
        std::string uri = "file:///tmp/sync_multi.cf";
        srv.handle_message(
            R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"processId":1,"rootUri":"file:///tmp","capabilities":{}}})",
            out);
        srv.handle_message(
            R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/sync_multi.cf","languageId":"cfire","version":1,"text":"package main\n\nfn main() {\n}\n"}}})",
            out);
        // Reversed keys inside each contentChange ("text" before "range", "end" before "start", "character" before "line")
        srv.handle_message(
            R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":"file:///tmp/sync_multi.cf","version":2},"contentChanges":[{"text":"\n    println(\"hi\")\n","range":{"end":{"character":11,"line":2},"start":{"character":11,"line":2}}},{"text":"import \"core:fmt\"\n","range":{"end":{"character":0,"line":2},"start":{"character":0,"line":2}}}]}})",
            out);
        std::string expected =
            "package main\n\nimport \"core:fmt\"\nfn main() {\n    println(\"hi\")\n\n}\n";
        require(srv.get_doc_text(uri) == expected,
            "multi-edit didChange with reversed keys must apply edits cleanly: got\n" +
                srv.get_doc_text(uri));
        srv.stop_worker();
    }

    // 2. CRLF incremental edits must preserve surrounding \r\n offsets
    {
        std::ostringstream out;
        cfire::lsp::Server srv;
        std::string uri = "file:///tmp/sync_crlf.cf";
        srv.handle_message(
            R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"processId":1,"rootUri":"file:///tmp","capabilities":{}}})",
            out);
        srv.handle_message(
            R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/sync_crlf.cf","languageId":"cfire","version":1,"text":"package main\r\nfn main() {\r\n    x := 1\r\n}\r\n"}}})",
            out);
        srv.handle_message(
            R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":"file:///tmp/sync_crlf.cf","version":2},"contentChanges":[{"range":{"start":{"line":2,"character":9},"end":{"line":2,"character":10}},"text":"42"}]}})",
            out);
        std::string expected = "package main\r\nfn main() {\r\n    x := 42\r\n}\r\n";
        require(srv.get_doc_text(uri) == expected,
            "CRLF didChange edit must stay line/col accurate: got\n" + srv.get_doc_text(uri));
        srv.stop_worker();
    }

    // 3. UTF-16 surrogate pair emoji column offset for didChange & completion textEdit
    {
        std::ostringstream out;
        cfire::lsp::Server srv;
        std::string uri = "file:///tmp/sync_utf16.cf";
        std::string init_doc = "package main\nfn main() {\n    s := \"😀\"; s.le\n}\n";
        srv.handle_message(
            R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"processId":1,"rootUri":"file:///tmp","capabilities":{"textDocument":{"completion":{"completionItem":{"snippetSupport":true}}}}}})",
            out);
        srv.handle_message(make_did_open_rpc(uri, init_doc), out);
        // "    s := \"😀\"; s.le" -> 4 + 5 + 1 + 2 (emoji) + 1 + 4 = 17 UTF-16 code units before "le" -> end is col 19
        auto list = srv.compute_completions_list(uri, 2, 19);
        auto items = split_json_array_objects(list.items_json);
        std::string len_item = find_item_by_label(items, "len");
        require(!len_item.empty(), "UTF-16 emoji before s.le must still resolve str.len()");
        std::string applied = apply_completion_edits(init_doc, len_item);
        require(applied.contains("s := \"😀\"; s.len()$0"),
            "UTF-16 completion textEdit must preserve surrogate pair and replace 'le': got\n" + applied);

        // Apply incremental edit right after the emoji in didChange (col 12 is closing quote)
        srv.handle_message(
            R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":"file:///tmp/sync_utf16.cf","version":2},"contentChanges":[{"range":{"start":{"line":2,"character":12},"end":{"line":2,"character":12}},"text":"!"}]}})",
            out);
        require(srv.get_doc_text(uri).contains("\"😀!\""),
            "didChange UTF-16 offset after 4-byte emoji must insert after closing surrogate: got\n" +
                srv.get_doc_text(uri));
        srv.stop_worker();
    }

    // 4. initialize capabilities, empty onTypeFormatting, and VS Code brace/Enter config
    {
        std::stringstream in_ss;
        std::ostringstream out_ss;
        cfire::lsp::Server srv;
        in_ss << lsp_comp_frame(
            R"({"jsonrpc":"2.0","id":10,"method":"initialize","params":{"processId":1,"rootUri":"file:///tmp","capabilities":{}}})");
        in_ss << lsp_comp_frame(
            R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/ot.cf","languageId":"cfire","version":1,"text":"package main\nfn main() {\n\n}\n"}}})");
        in_ss << lsp_comp_frame(
            R"({"jsonrpc":"2.0","id":11,"method":"textDocument/onTypeFormatting","params":{"textDocument":{"uri":"file:///tmp/ot.cf"},"position":{"line":2,"character":0},"ch":"\n"}})");
        in_ss << lsp_comp_frame(R"({"jsonrpc":"2.0","id":12,"method":"shutdown","params":null})");
        in_ss << lsp_comp_frame(R"({"jsonrpc":"2.0","method":"exit","params":null})");
        srv.handle_stream(in_ss, out_ss);
        srv.stop_worker();
        std::string resp = out_ss.str();
        require(!resp.contains("\"documentOnTypeFormattingProvider\""),
            "initialize must omit documentOnTypeFormattingProvider (Q82)");
        require(!resp.contains("\"@\""),
            "completionProvider triggerCharacters must not include '@'");
        require(resp.contains(R"("id":11,"result":[])"),
            "onTypeFormatting handler must return empty result array");

        ScopedTempDir tmp("cfire_vscode_cfg");
        cfire::lsp::write_cfire_vscode_extension_bundle(tmp.path);
        std::string lang_cfg = read_file_text_local(tmp.path / "language-configuration.json");
        require(lang_cfg.contains("\"onEnterRules\"") && lang_cfg.contains("\"decreaseIndentPattern\""),
            "generated VS Code language-configuration.json must define onEnterRules and decreaseIndentPattern");
        std::string pkg_json = read_file_text_local(tmp.path / "package.json");
        require(pkg_json.contains("\"main\": \"./extension.js\"") &&
                    pkg_json.contains("\"onLanguage:cfire\""),
            "generated VS Code package.json must register extension.js entrypoint and onLanguage:cfire activation");
        require(std::filesystem::exists(tmp.path / "extension.js"),
            "write_cfire_vscode_extension_bundle must generate extension.js LSP client");
        std::string ext_js = read_file_text_local(tmp.path / "extension.js");
        require(ext_js.contains("registerCompletionItemProvider") &&
                    ext_js.contains("textDocument/completion"),
            "generated extension.js must wire VS Code CompletionItemProvider to textDocument/completion");

        ScopedTempDir ext_root("cfire_vs_manifest");
        {
            std::ofstream mofs(ext_root.path / "extensions.json", std::ios::binary | std::ios::trunc);
            mofs << R"([{"identifier":{"id":"mock.other-ext"},"version":"1.2.3","location":{"$mid":1,"fsPath":"/tmp/mock","scheme":"file"},"relativeLocation":"mock.other-ext-1.2.3"},{"identifier":{"id":"cfire.cfire-lang"},"version":"0.1.0","location":{"$mid":1,"fsPath":"/tmp/old","scheme":"file"},"relativeLocation":"cfire.cfire-lang-0.1.0"}])";
        }
        cfire::lsp::register_vscode_extension_manifest(ext_root.path, "cfire.cfire-lang-0.12.1", "0.12.1");
        cfire::lsp::register_vscode_extension_manifest(ext_root.path, "cfire.cfire-lang-0.12.1", "0.12.1");
        std::string reg_json = read_file_text_local(ext_root.path / "extensions.json");
        auto reg_entries = split_json_array_objects(reg_json);
        require(reg_entries.size() == 2,
            "register_vscode_extension_manifest must keep mock entry and replace stale cfire entry without duplicates: got " +
                std::to_string(reg_entries.size()) + " entries: " + reg_json);
        require(reg_json.contains(R"("id":"mock.other-ext")") &&
                    !reg_json.contains(R"("version":"0.1.0")") &&
                    reg_json.contains(R"("version":"0.12.1")") &&
                    reg_json.contains(R"("relativeLocation":"cfire.cfire-lang-0.12.1")"),
            "extensions.json must preserve other extensions and update cfire.cfire-lang to 0.12.1: " + reg_json);

        std::filesystem::path repo_root = rt_dir.parent_path();
        if (std::filesystem::exists(repo_root / ".vscode/settings.json")) {
            std::string settings = read_file_text_local(repo_root / ".vscode/settings.json");
            require(!settings.contains("\"editor.formatOnType\": true"),
                "repo .vscode/settings.json must not enable editor.formatOnType");
        }
        if (std::filesystem::exists(repo_root / ".vscode/extensions/cfire.cfire-lang/language-configuration.json")) {
            std::string repo_lc = read_file_text_local(
                repo_root / ".vscode/extensions/cfire.cfire-lang/language-configuration.json");
            require(repo_lc.contains("\"onEnterRules\""),
                "repo VS Code language-configuration.json must contain onEnterRules");
        }
    }
}

inline void test_method_discoverability() {
    // 1. str receivers across forms + match / if expr
    {
        std::vector<std::pair<std::string, std::string>> str_cases = {
            {"local_var", "package main\nfn main() {\n    s := \"abc\"\n    s.@CURSOR@\n}\n"},
            {"fn_param", "package main\nfn f(s: str) {\n    s.@CURSOR@\n}\n"},
            {"literal", "package main\nfn main() {\n    \"hello\".@CURSOR@\n}\n"},
            {"chained_trim", "package main\nfn main() {\n    s := \" a \"\n    s.trim().@CURSOR@\n}\n"},
            {"slice_expr", "package main\nfn main() {\n    s := \"abcd\"\n    s[0..2].@CURSOR@\n}\n"},
            {"if_expr", "package main\nfn main() {\n    x := if true { \"a\" } else { \"b\" }\n    x.@CURSOR@\n}\n"},
            {"match_expr", "package main\nfn main() {\n    n := 1\n    x := match n { 1 => \"a\", _ => \"b\" }\n    x.@CURSOR@\n}\n"},
        };
        for (const auto& [name, src] : str_cases) {
            auto run = complete_at_cursor(src);
            std::string len_item = find_item_by_label(run.items, "len");
            std::string split_item = find_item_by_label(run.items, "split");
            require(!len_item.empty(), "str case '" + name + "' must include .len()");
            require(!split_item.empty(), "str case '" + name + "' must include .split()");
            require(find_json_str(find_json_field_raw(len_item, "textEdit"), "newText") == "len()$0",
                "str case '" + name + "' len snippet must be len()$0");
            require(find_json_str(find_json_field_raw(split_item, "textEdit"), "newText") == "split($0)",
                "str case '" + name + "' split snippet must be split($0)");
        }
    }

    // 2. []T and [N]T receivers including incomplete AST recovery (range, if, println, field slice)
    {
        std::vector<std::pair<std::string, std::string>> slice_cases = {
            {"slice_literal_var", "package main\nfn main() {\n    xs := [1, 2, 3]\n    xs.@CURSOR@\n}\n"},
            {"make_slice", "package main\nfn main() {\n    xs := make([]i32, 4)\n    xs.@CURSOR@\n}\n"},
            {"fixed_array", "package main\nfn main() {\n    arr: [3]i32 = [1, 2, 3]\n    arr.@CURSOR@\n}\n"},
            {"for_range_incomplete", "package main\nfn main() {\n    xs := [1, 2, 3]\n    for i in 0..xs.@CURSOR@\n}\n"},
            {"if_incomplete", "package main\nfn main() {\n    xs := [1, 2, 3]\n    if xs.@CURSOR@\n}\n"},
            {"println_incomplete", "package main\nimport \"core:fmt\"\nfn main() {\n    xs := [1, 2, 3]\n    println(\"{}\", xs.@CURSOR@\n}\n"},
            {"field_slice", "package main\ntype Doc struct { sections: []str }\nfn f(doc: Doc) {\n    doc.sections.@CURSOR@\n}\n"},
        };
        for (const auto& [name, src] : slice_cases) {
            auto run = complete_at_cursor(src);
            std::string len_item = find_item_by_label(run.items, "len");
            require(!len_item.empty(), "slice/array case '" + name + "' must include .len()");
        }
    }

    // 3. map[K]V + loop binding propagation
    {
        auto run_map = complete_at_cursor(
            "package main\nfn main() {\n    m := make(map[str]i64)\n    m.@CURSOR@\n}\n");
        require(!find_item_by_label(run_map.items, "len").empty(), "map[str]i64 must include .len()");
        require(!find_item_by_label(run_map.items, "contains").empty(), "map[str]i64 must include .contains()");

        auto run_k = complete_at_cursor(
            "package main\nfn main() {\n    m := make(map[str]i64)\n    for k, v in m {\n        k.@CURSOR@\n    }\n}\n");
        require(!find_item_by_label(run_k.items, "split").empty(),
            "map loop key k must resolve as str with .split()");

        auto run_v = complete_at_cursor(
            "package main\nfn main() {\n    m := make(map[str]i64)\n    for k, v in m {\n        v.@CURSOR@\n    }\n}\n");
        require(!find_item_by_label(run_v.items, "wrapping_add").empty() ||
                    !find_item_by_label(run_v.items, "wrapping_as").empty(),
            "map loop value v must resolve as i64 with numeric methods");
    }

    // 4. core:collections (Set, Deque, PriorityQueue, MinQueue)
    {
        auto set_run = complete_at_cursor(
            "package main\nimport \"core:collections\"\nfn f(s: collections.Set[str]) {\n    s.@CURSOR@\n}\n");
        require(!find_item_by_label(set_run.items, "len").empty() &&
                    !find_item_by_label(set_run.items, "contains").empty(),
            "collections.Set must complete len and contains");

        auto dq_run = complete_at_cursor(
            "package main\nimport \"core:collections\"\nfn f(d: collections.Deque[i32]) {\n    d.@CURSOR@\n}\n");
        require(!find_item_by_label(dq_run.items, "len").empty() &&
                    !find_item_by_label(dq_run.items, "push_back").empty(),
            "collections.Deque must complete len and push_back");

        auto pq_run = complete_at_cursor(
            "package main\nimport \"core:collections\"\nfn f(q: collections.PriorityQueue[i32]) {\n    q.@CURSOR@\n}\n");
        require(!find_item_by_label(pq_run.items, "len").empty() &&
                    !find_item_by_label(pq_run.items, "push").empty(),
            "collections.PriorityQueue must complete len and push");

        auto mq_run = complete_at_cursor(
            "package main\nimport \"core:collections\"\nfn f(q: collections.MinQueue[i32]) {\n    q.@CURSOR@\n}\n");
        require(!find_item_by_label(mq_run.items, "len").empty() &&
                    !find_item_by_label(mq_run.items, "pop").empty(),
            "collections.MinQueue must complete len and pop");
    }

    // 5. Numeric/char + static type methods + custom import aliases
    {
        auto byte_run = complete_at_cursor(
            "package main\nfn f(b: byte) {\n    b.@CURSOR@\n}\n");
        require(!find_item_by_label(byte_run.items, "wrapping_add").empty() ||
                    !find_item_by_label(byte_run.items, "is_digit").empty(),
            "byte receiver must offer numeric/byte methods");

        auto int_run = complete_at_cursor(
            "package main\nfn f(n: int) {\n    n.@CURSOR@\n}\n");
        require(!find_item_by_label(int_run.items, "wrapping_add").empty(),
            "int receiver must offer numeric methods");

        auto i32_run = complete_at_cursor(
            "package main\nfn main() {\n    _ = i32.@CURSOR@\n}\n");
        require(!find_item_by_label(i32_run.items, "MAX").empty() &&
                    !find_item_by_label(i32_run.items, "parse").empty(),
            "i32 static receiver must include MAX and parse");

        auto own_run = complete_at_cursor(
            "package main\ntype Node struct { v: int }\nfn main() {\n    _ = own[Node].@CURSOR@\n}\n");
        require(!find_item_by_label(own_run.items, "from_raw").empty(),
            "own[Node]. must include from_raw");

        auto alias_run = complete_at_cursor(
            "package main\nimport \"core:fs\" as myfs\nfn main() {\n    _ = myfs.@CURSOR@\n}\n");
        require(!find_item_by_label(alias_run.items, "read_bytes").empty(),
            "aliased import myfs. must offer core:fs symbols");
    }

    // 6. Disambiguated stdlib handle types (Conn, Channel) & mut/unwrap ranking + unknown fallback
    {
        auto ws_run = complete_at_cursor(
            "package main\nimport \"core:websocket\"\nfn f(c: websocket.Conn) {\n    c.@CURSOR@\n}\n");
        require(!find_item_by_label(ws_run.items, "send_text").empty(),
            "websocket.Conn must offer send_text");
        require(find_item_by_label(ws_run.items, "exec").empty(),
            "websocket.Conn must not bleed sqlite.Conn methods like exec");

        auto ch_run = complete_at_cursor(
            "package main\nimport \"core:sync\"\nfn f(ch: sync.Channel[i32]) {\n    ch.@CURSOR@\n}\n");
        require(!find_item_by_label(ch_run.items, "send").empty(),
            "sync.Channel must offer send");
        require(find_item_by_label(ch_run.items, "make").empty(),
            "sync.Channel instance must not offer Channel.make constructor");

        // mut ranking & annotation on immutable slice
        auto mut_run = complete_at_cursor(
            "package main\nfn main() {\n    xs := [1, 2]\n    xs.@CURSOR@\n}\n");
        std::string len_it = find_item_by_label(mut_run.items, "len");
        std::string push_it = find_item_by_label(mut_run.items, "push");
        require(!len_it.empty() && !push_it.empty(),
            "immutable slice still lists mutating push for discoverability (Q83)");
        require(push_it.contains("requires mut"),
            "mutating method on immutable receiver must note requires mut: " + push_it);
        require(find_json_str(len_it, "sortText") < find_json_str(push_it, "sortText"),
            "non-mutating len must rank ahead of requires-mut push on immutable receiver");

        // Optional & fallible unwrap hints (postfix ? and ! syntax)
        auto opt_run = complete_at_cursor(
            "package main\nfn f(maybe: str?) {\n    maybe.@CURSOR@\n}\n");
        std::string opt_len = find_item_by_label(opt_run.items, "len");
        require(!opt_len.empty() && opt_len.contains("unwrap ? first"),
            "optional str? must list .len() annotated with 'unwrap ? first'");

        auto fall_run = complete_at_cursor(
            "package main\nfn f(res: str!) {\n    res.@CURSOR@\n}\n");
        std::string fall_len = find_item_by_label(fall_run.items, "len");
        require(!fall_len.empty() && fall_len.contains("unwrap ! first"),
            "fallible str! must list .len() annotated with 'unwrap ! first'");

        // Unknown-type fallback
        auto unk_run = complete_at_cursor(
            "package main\nfn main() {\n    x := mystery_fn()\n    x.@CURSOR@\n}\n");
        std::string unk_len = find_item_by_label(unk_run.items, "len");
        require(!unk_len.empty() &&
                    find_json_str(unk_len, "sortText").starts_with("9_") &&
                    unk_len.contains("type unresolved"),
            "unresolved receiver fallback must emit curated builtins ranked 9_ with type unresolved note: " +
                unk_len);
    }
}

inline void test_zero_invalid_tokens_and_context_gating() {
    // 1. Type positions must insert bare types (never `str(val)` or `[][]byte`)
    {
        auto p1 = complete_at_cursor("package main\nfn f(a: st@CURSOR@) {}\n");
        std::string str_item = find_item_by_label(p1.items, "str");
        require(!str_item.empty(), "type position 'a: st' must suggest str");
        std::string applied1 = apply_completion_edits(p1.doc, str_item);
        require(applied1 == "package main\nfn f(a: str) {}\n",
            "type position must replace 'st' with 'str' without '(val)': got\n" + applied1);

        auto p2 = complete_at_cursor("package main\nfn main() {\n    x: []by@CURSOR@\n}\n");
        std::string slice_byte = find_item_by_label(p2.items, "[]byte");
        require(!slice_byte.empty(), "type position '[]by' must suggest []byte");
        std::string slice_ft = find_json_str(slice_byte, "filterText");
        require(slice_ft.empty() || slice_ft == "[]byte",
            "[]by completion filterText must match full '[]byte' span (not 'byte'): got '" + slice_ft + "'");
        std::string applied2 = apply_completion_edits(p2.doc, slice_byte);
        require(applied2 == "package main\nfn main() {\n    x: []byte\n}\n",
            "[]by prefix replacement must produce []byte, not [][]byte: got\n" + applied2);

        auto p3 = complete_at_cursor("package main\nfn f() -> st@CURSOR@ {}\n");
        std::string str_ret = find_item_by_label(p3.items, "str");
        require(!str_ret.empty() && apply_completion_edits(p3.doc, str_ret) == "package main\nfn f() -> str {}\n",
            "return type '-> st' must complete bare 'str'");

        auto p4 = complete_at_cursor("package main\nfn main() {\n    xs := make(@CURSOR@\n}\n");
        std::string str_make = find_item_by_label(p4.items, "str");
        require(!str_make.empty(), "make( type position must offer types");
        std::string te_make = find_json_str(find_json_field_raw(str_make, "textEdit"), "newText");
        require(!te_make.contains('('), "make( type completion must not insert call parens: " + te_make);
    }

    // 2. const & type entries must never insert `()`
    {
        auto c1 = complete_at_cursor(
            "package main\nimport \"core:math\"\nfn main() {\n    _ = math.P@CURSOR@\n}\n");
        std::string pi = find_item_by_label(c1.items, "PI");
        require(!pi.empty(), "math.P must suggest PI");
        require(!find_json_str(find_json_field_raw(pi, "textEdit"), "newText").contains('('),
            "math.PI constant must not insert ()");

        auto c2 = complete_at_cursor(
            "package main\nimport \"core:collections\"\nfn main() {\n    _ = collections.Se@CURSOR@\n}\n");
        std::string set_it = find_item_by_label(c2.items, "Set");
        require(!set_it.empty(), "collections.Se must suggest Set");
        require(!find_json_str(find_json_field_raw(set_it, "textEdit"), "newText").contains('('),
            "collections.Set type must not insert ()");

        auto c3 = complete_at_cursor(
            "package main\nimport \"core:http\"\nfn main() {\n    _ = http.ServerLi@CURSOR@\n}\n");
        std::string lim_it = find_item_by_label(c3.items, "ServerLimits");
        require(!lim_it.empty() &&
                    !find_json_str(find_json_field_raw(lim_it, "textEdit"), "newText").contains('('),
            "http.ServerLimits type must not insert ()");
    }

    // 3. Required explicit generic slots insert `[$1]($0)` / `[$1]()$0`
    {
        auto g1 = complete_at_cursor(
            "package main\nimport \"core:json\"\nfn main() {\n    _ = json.pa@CURSOR@\n}\n");
        std::string parse_it = find_item_by_label(g1.items, "parse");
        require(!parse_it.empty(), "json.pa must suggest parse");
        std::string parse_nt = find_json_str(find_json_field_raw(parse_it, "textEdit"), "newText");
        require(parse_nt == "parse[$1]($0)",
            "json.parse must insert explicit generic snippet parse[$1]($0), got: " + parse_nt);

        std::string schema_it = find_item_by_label(g1.items, "schema");
        require(!schema_it.empty(), "json. must suggest schema");
        std::string schema_nt = find_json_str(find_json_field_raw(schema_it, "textEdit"), "newText");
        require(schema_nt == "schema[$1]()$0",
            "json.schema must insert explicit generic snippet schema[$1]()$0, got: " + schema_nt);

        auto g2 = complete_at_cursor(
            "package main\nfn main() {\n    x: u32 = 1\n    _ = x.wrapping_a@CURSOR@\n}\n");
        std::string wa_it = find_item_by_label(g2.items, "wrapping_as");
        require(!wa_it.empty(), "u32 must suggest wrapping_as");
        std::string wa_nt = find_json_str(find_json_field_raw(wa_it, "textEdit"), "newText");
        require(wa_nt.contains("[$1]"),
            "wrapping_as must insert explicit generic type arg [$1], got: " + wa_nt);
    }

    // 4. Context suppression: plain string, // comment, /* comment */, decl names, mid-line `type Foo st`
    {
        std::vector<std::pair<std::string, std::string>> suppressed = {
            {"plain_str", "package main\nfn main() {\n    s := \"hello @CURSOR@\"\n}\n"},
            {"line_comment", "package main\n// comment @CURSOR@\nfn main() {}\n"},
            {"block_comment", "package main\n/* comment @CURSOR@ */\nfn main() {}\n"},
            {"fn_decl_name", "package main\nfn @CURSOR@\n"},
            {"type_decl_name", "package main\ntype @CURSOR@\n"},
        };
        for (const auto& [name, src] : suppressed) {
            auto run = complete_at_cursor(src);
            require(run.items.empty(), "suppressed context '" + name + "' must return empty completions");
        }

        // Single colon in non-type expression position (map key / label) suppressed
        auto colon_run = complete_at_cursor(
            "package main\nfn main() {\n    m := map{\"k\":@CURSOR@}\n}\n");
        require(colon_run.items.empty(),
            "single ':' trigger inside map literal expression value must not pollute with unrelated completions");

        // `type Foo ` offers struct/enum/interface; mid-line `type Foo st` omits `type struct` snippet
        auto td1 = complete_at_cursor("package main\ntype Foo @CURSOR@\n");
        require(!find_item_by_label(td1.items, "struct").empty() &&
                    !find_item_by_label(td1.items, "enum").empty(),
            "'type Foo ' must suggest struct and enum");

        auto td2 = complete_at_cursor("package main\ntype Foo st@CURSOR@\n");
        require(find_item_by_label(td2.items, "type struct").empty(),
            "mid-line 'type Foo st' must not emit full 'type struct' template snippet");
    }

    // 5. import "...", struct literal fields, Enum:: and Enum. rewrite
    {
        auto imp_run = complete_at_cursor("package main\nimport \"co@CURSOR@\"\n");
        require(!find_item_by_label(imp_run.items, "core:fs").empty(),
            "import string must suggest core:fs");

        auto sl_run = complete_at_cursor(
            "package main\ntype Metric struct { service: str, value: f64, count: int }\nfn main() {\n    m := Metric{ service: \"a\", @CURSOR@ }\n}\n");
        require(find_item_by_label(sl_run.items, "service").empty(),
            "struct literal completion must omit already-assigned field 'service'");
        std::string val_it = find_item_by_label(sl_run.items, "value");
        require(!val_it.empty() &&
                    find_json_str(find_json_field_raw(val_it, "textEdit"), "newText") == "value: $0",
            "struct literal unassigned field must insert 'value: $0'");

        auto en_run = complete_at_cursor(
            "package main\ntype Shape enum { Circle(f64), Rect(f64, f64) }\nfn main() {\n    _ = Shape::@CURSOR@\n}\n");
        std::string cir = find_item_by_label(en_run.items, "Circle");
        require(!cir.empty() &&
                    !find_json_str(find_json_field_raw(cir, "textEdit"), "newText").contains('('),
            "Shape:: enum variant must suggest Circle without forcing ()");

        auto dot_en = complete_at_cursor(
            "package main\ntype Shape enum { Circle(f64), Rect(f64, f64) }\nfn main() {\n    _ = Shape.@CURSOR@\n}\n");
        std::string dot_cir = find_item_by_label(dot_en.items, "Circle");
        require(!dot_cir.empty(), "Shape. must offer enum variants with dot->:: rewrite");
        std::string applied_en = apply_completion_edits(dot_en.doc, dot_cir);
        require(applied_en.contains("Shape::Circle"),
            "Shape. variant acceptance must rewrite '.' to '::': got\n" + applied_en);
    }

    // 6. Receiver method omission, core:build gating, sortText tiers, snippetSupport:false, isIncomplete
    {
        auto rec_run = complete_at_cursor(
            "package main\ntype Foo struct { v: int }\nfn (self: Foo) secret_method() int { return self.v }\nfn main() {\n    secr@CURSOR@\n}\n");
        require(find_item_by_label(rec_run.items, "secret_method").empty(),
            "receiver methods must be omitted from unqualified top-level completions");

        auto normal_file = complete_at_cursor(
            "package main\nfn main() {\n    bui@CURSOR@\n}\n", "file:///tmp/main.cf");
        require(find_item_by_label(normal_file.items, "add_executable", "core:build").empty() &&
                    !normal_file.list.items_json.contains("core:build"),
            "core:build auto-import must be gated out of non-build.cf files");

        auto build_file = complete_at_cursor(
            "package main\nfn main() {\n    @CURSOR@\n}\n", "file:///tmp/build.cf");
        require(build_file.list.items_json.contains("core:build"),
            "build.cf must offer core:build symbols");

        auto tier_run = complete_at_cursor(
            "package main\nfn main() {\n    err := \"oops\"\n    e@CURSOR@\n}\n");
        std::string local_err = find_item_by_label(tier_run.items, "err");
        std::string log_err = find_item_by_label(tier_run.items, "error", "core:log");
        require(!local_err.empty() && !log_err.empty(),
            "tier test expects both local err and auto-import log.error");
        require(find_json_str(local_err, "sortText").starts_with("0_") &&
                    find_json_str(log_err, "sortText").starts_with("7_") &&
                    find_json_str(local_err, "sortText") < find_json_str(log_err, "sortText"),
            "local 'err' (0_) must strictly outrank unimported 'log.error' (7_)");

        // snippetSupport: false
        std::ostringstream dummy;
        cfire::lsp::Server srv_nosnip;
        std::string uri_ns = "file:///tmp/no_snip.cf";
        std::string ns_doc = "package main\nfn main() {\n    s := \"hi\"\n    s.sp\n}\n";
        srv_nosnip.handle_message(
            R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"processId":1,"rootUri":"file:///tmp","capabilities":{"textDocument":{"completion":{"completionItem":{"snippetSupport":false}}}}}})",
            dummy);
        srv_nosnip.handle_message(make_did_open_rpc(uri_ns, ns_doc), dummy);
        auto ns_list = srv_nosnip.compute_completions_list(uri_ns, 3, 8);
        srv_nosnip.stop_worker();
        auto ns_items = split_json_array_objects(ns_list.items_json);
        std::string ns_split = find_item_by_label(ns_items, "split");
        require(!ns_split.empty(), "snippetSupport:false must still include split");
        std::string ns_nt = find_json_str(find_json_field_raw(ns_split, "textEdit"), "newText");
        require(ns_nt == "split" && !ns_nt.contains("$0") && !ns_nt.contains("$1"),
            "snippetSupport:false must omit snippet tabstops: got " + ns_nt);

        // isIncomplete true for unfiltered broad statement/expr completion, false for targeted member
        auto broad = complete_at_cursor("package main\nfn main() {\n    @CURSOR@\n}\n");
        require(broad.list.is_incomplete, "broad unqualified completion should report isIncomplete=true");
        auto member = complete_at_cursor("package main\nfn main() {\n    s := \"a\"\n    s.@CURSOR@\n}\n");
        require(!member.list.is_incomplete, "resolved str member list should report isIncomplete=false");
    }

    // 7. Wrapper bridge & T.parse completions (Q92/Q93)
    {
        auto opt_comp = complete_at_cursor(
            "package main\nfn maybe() -> i32? { return 1 }\nfn main() {\n    opt := maybe()\n    opt.@CURSOR@\n}\n");
        std::string or_err = find_item_by_label(opt_comp.items, "or_error");
        require(!or_err.empty(), "T? completion must suggest 'or_error'");
        require(find_json_str(or_err, "sortText") == "0_or_error" &&
                    !find_json_str(or_err, "detail").contains("unwrap ? first"),
            "T? 'or_error' completion must rank top (0_) without unwrap note, got: " + or_err);

        auto fall_comp = complete_at_cursor(
            "package main\nfn parse_it() -> i32! { return 1 }\nfn main() {\n    res: i32! = parse_it()\n    res.@CURSOR@\n}\n");
        std::string ign_err = find_item_by_label(fall_comp.items, "ignore_error");
        require(!ign_err.empty(), "non-void T! completion must suggest 'ignore_error'");
        require(find_json_str(ign_err, "sortText") == "0_ignore_error" &&
                    !find_json_str(ign_err, "detail").contains("unwrap ! first"),
            "T! 'ignore_error' completion must rank top (0_) without unwrap note, got: " + ign_err);

        auto void_comp = complete_at_cursor(
            "package main\nfn do_void() -> void! { return }\nfn main() {\n    v: void! = do_void()\n    v.@CURSOR@\n}\n");
        require(find_item_by_label(void_comp.items, "ignore_error").empty(),
            "void! completion must NOT suggest 'ignore_error'");

        auto parse_comp = complete_at_cursor("package main\nfn main() {\n    _ = i32.pa@CURSOR@\n}\n");
        std::string p_item = find_item_by_label(parse_comp.items, "parse");
        require(!p_item.empty() && find_json_str(p_item, "detail").contains("-> i32!"),
            "i32.parse completion detail must show '-> i32!', got: " + p_item);
    }
}

inline void test_builtin_signature_help() {
    auto sig_split = signature_at_cursor(
        "package main\nfn main() {\n    s := \"a,b\"\n    _ = s.split(@CURSOR@)\n}\n");
    require(sig_split.contains("fn split(sep: str) -> []str"),
        "builtin signatureHelp on s.split( must show 'fn split(sep: str) -> []str', got: " + sig_split);

    auto sig_insert = signature_at_cursor(
        "package main\nfn main() {\n    mut xs := [1, 2]\n    xs.insert(0, @CURSOR@)\n}\n");
    require(sig_insert.contains("fn insert(idx: usize, val: T)") &&
                sig_insert.contains(R"("activeParameter":1)"),
        "builtin signatureHelp on xs.insert(0, ) must show semantic params & activeParameter 1, got: " +
            sig_insert);
}

inline void test_typed_ast_hover_and_inlay_hints() {
    // 1. Single-file Typed-AST hover & inlay hints: generics, typed ranges, optionals, destructuring, builtin chains
    {
        cfire::lsp::Server srv;
        std::ostringstream dummy;
        const std::string uri = "file:///tmp/wave2_typed_lsp.cf";
        const std::string doc =
            "package main\n"                               // 0
            "type Box[T] struct {\n"                       // 1
            "    val: T\n"                                 // 2
            "}\n"                                          // 3
            "fn identity[T](x: T) -> T {\n"                // 4
            "    return x\n"                               // 5
            "}\n"                                          // 6
            "fn make_pair() -> (i32, str) {\n"             // 7
            "    return (42, \"ok\")\n"                    // 8
            "}\n"                                          // 9
            "fn main() {\n"                                // 10
            "    x := identity(42)\n"                      // 11
            "    parsed := i32.parse(\"42\")\n"            // 12
            "    if v := parsed {\n"                       // 13
            "        _ = v\n"                              // 14
            "    }\n"                                      // 15
            "    n: usize = 5\n"                           // 16
            "    for idx_u in 0..n {\n"                    // 17
            "        _ = idx_u\n"                          // 18
            "    }\n"                                      // 19
            "    a32: i32 = 0\n"                           // 20
            "    b32: i32 = 3\n"                           // 21
            "    for idx_32 in a32..b32 {\n"               // 22
            "        _ = idx_32\n"                         // 23
            "    }\n"                                      // 24
            "    (pa, pb) := make_pair()\n"                // 25
            "    b := Box[i64]{val: 1}\n"                  // 26
            "    parts := \"a,b\".split(\",\")\n"          // 27
            "    b2 := wrap(1)\n"                          // 28
            "    _ = x\n"                                  // 29
            "    _ = pa\n"                                 // 30
            "    _ = pb\n"                                 // 31
            "    _ = b\n"                                  // 32
            "    _ = parts\n"                              // 33
            "    _ = b2\n"                                 // 34
            "}\n"                                          // 35
            "fn wrap[T](v: T) -> Box[T] {\n"               // 36
            "    return Box[T]{val: v}\n"                  // 37
            "}\n";                                         // 38
        srv.handle_message(
            R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"processId":1,"rootUri":"file:///tmp","capabilities":{}}})",
            dummy);
        srv.handle_message(make_did_open_rpc(uri, doc), dummy);

        const std::string hints_json = srv.compute_inlay_hints(uri);
        auto hint_items = split_json_array_objects(hints_json);
        auto has_type_hint_on_line = [&](int line_no, std::string_view expected_label) {
            for (const auto& item : hint_items) {
                std::string pos = find_json_field_raw(item, "position");
                if (pos.contains(std::format("\"line\":{}", line_no)) &&
                    find_json_str(item, "label") == expected_label) {
                    return true;
                }
            }
            return false;
        };

        require(has_type_hint_on_line(11, ": i64"),
            "Typed-AST inlayHint on x := identity(42) must show ': i64', got: " + hints_json);
        require(has_type_hint_on_line(12, ": i32!"),
            "Typed-AST inlayHint on parsed := i32.parse(...) must show ': i32!', got: " + hints_json);
        require(has_type_hint_on_line(25, ": i32") && has_type_hint_on_line(25, ": str"),
            "Typed-AST inlayHints on (pa, pb) := make_pair() must show ': i32' and ': str', got: " + hints_json);
        require(has_type_hint_on_line(26, ": Box[i64]"),
            "Typed-AST inlayHint on b := Box[i64]{val: 1} must show ': Box[i64]', got: " + hints_json);
        require(has_type_hint_on_line(27, ": []str"),
            "Typed-AST inlayHint on parts := \"a,b\".split(\",\") must show ': []str', got: " + hints_json);
        require(has_type_hint_on_line(28, ": Box[i64]"),
            "Typed-AST inlayHint on b2 := wrap(1) must show ': Box[i64]', got: " + hints_json);

        const std::string hover_x = srv.compute_hover(uri, 11, 4);
        require(hover_x.contains("x: i64") && hover_x.contains("std::int64_t"),
            "Typed-AST hover on generic result x must show i64 / std::int64_t, got: " + hover_x);

        const std::string hover_v = srv.compute_hover(uri, 14, 12);
        require(hover_v.contains("v: i32") && hover_v.contains("std::int32_t"),
            "Typed-AST hover on unwrap binding v must show i32 / std::int32_t, got: " + hover_v);

        const std::string hover_idx_u = srv.compute_hover(uri, 18, 12);
        require(hover_idx_u.contains("idx_u: usize") && hover_idx_u.contains("std::size_t"),
            "Typed-AST hover on usize range variable idx_u must show usize / std::size_t, got: " + hover_idx_u);

        const std::string hover_idx_32 = srv.compute_hover(uri, 23, 12);
        require(hover_idx_32.contains("idx_32: i32") && hover_idx_32.contains("std::int32_t"),
            "Typed-AST hover on i32 range variable idx_32 must show i32 / std::int32_t, got: " + hover_idx_32);

        // 2. Resilient mid-edit fallback: partial syntax error on later line preserves earlier bindings
        const std::string broken_doc =
            "package main\n"
            "fn identity[T](x: T) -> T {\n"
            "    return x\n"
            "}\n"
            "fn main() {\n"
            "    x := identity(42)\n"
            "    broken := x.\n"
            "}\n";
        srv.handle_message(
            std::format(
                R"({{"jsonrpc":"2.0","method":"textDocument/didChange","params":{{"textDocument":{{"uri":"{}","version":2}},"contentChanges":[{{"text":"{}"}}]}}}})",
                uri, cfire::lsp::detail::json_escape(broken_doc)),
            dummy);
        const std::string mid_hints = srv.compute_inlay_hints(uri);
        require(mid_hints.contains(R"("label":": i64")"),
            "Resilient mid-edit inlayHint keeps ': i64' for valid earlier bindings, got: " + mid_hints);
        const std::string mid_hover = srv.compute_hover(uri, 5, 4);
        require(mid_hover.contains("x: i64") && mid_hover.contains("std::int64_t"),
            "Resilient mid-edit hover keeps 'x: i64' for valid earlier bindings, got: " + mid_hover);

        srv.stop_worker();
    }

    // 3. Multi-file local package import: inlay hints must NOT leak from imported package lines
    {
        ScopedTempDir tmp("cfire_lsp_typed_pkg_iso");
        (void)tmp.file("helper/helper.cf",
            "package helper\n"
            "pub fn double_it(v: i32) -> i32 {\n"
            "    res := v * 2\n"
            "    return res\n"
            "}\n");
        auto main_path = tmp.file("main.cf",
            "package main\n"
            "import \"./helper\"\n"
            "fn main() {\n"
            "    y := helper.double_it(21)\n"
            "    _ = y\n"
            "}\n");

        cfire::lsp::Server srv;
        std::ostringstream dummy;
        const std::string main_uri = cfire::lsp::detail::path_to_uri(main_path);
        const std::string main_doc = read_file_text_local(main_path);
        srv.set_workspace_root(tmp.path.string());
        srv.handle_message(
            std::format(
                R"({{"jsonrpc":"2.0","id":1,"method":"initialize","params":{{"processId":1,"rootUri":"{}","capabilities":{{}}}}}})",
                cfire::lsp::detail::path_to_uri(tmp.path)),
            dummy);
        srv.handle_message(make_did_open_rpc(main_uri, main_doc), dummy);

        const std::string hints_json = srv.compute_inlay_hints(main_uri);
        auto hint_items = split_json_array_objects(hints_json);
        bool saw_y_hint = false;
        bool leaked_line2_hint = false;
        for (const auto& item : hint_items) {
            std::string pos = find_json_field_raw(item, "position");
            std::string lbl = find_json_str(item, "label");
            if (pos.contains("\"line\":3") && lbl == ": i32") saw_y_hint = true;
            if (pos.contains("\"line\":2") && lbl.starts_with(": ")) leaked_line2_hint = true;
        }
        require(saw_y_hint, "Multi-file Typed-AST inlayHint resolves imported call return type ': i32' on line 3, got: " + hints_json);
        require(!leaked_line2_hint, "Multi-file Typed-AST inlayHint must NOT leak helper.cf line 2 hints into main.cf, got: " + hints_json);

        const std::string hover_y = srv.compute_hover(main_uri, 4, 8);
        require(hover_y.contains("y: i32") && hover_y.contains("std::int32_t"),
            "Multi-file Typed-AST hover on y resolves imported return type i32 / std::int32_t, got: " + hover_y);

        srv.stop_worker();
    }
}

}  // namespace

void test_lsp_completion_and_sync(const std::filesystem::path& rt_dir) {
    test_sync_and_brace_config(rt_dir);
    test_method_discoverability();
    test_zero_invalid_tokens_and_context_gating();
    test_builtin_signature_help();
    test_typed_ast_hover_and_inlay_hints();
}
