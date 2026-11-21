#pragma once
// src/lsp/completion.hpp — LSP `textDocument/completion`, Context Gating, UTF-16 `textEdit` & Auto-Import Edit Synthesis

#include "server.hpp"

namespace cfire::lsp {

#ifndef CFIRE_LSP_FEAT_INLINE
#ifdef CFIRE_SPLIT_TUS
#define CFIRE_LSP_FEAT_INLINE
#else
#define CFIRE_LSP_FEAT_INLINE inline
#endif
#endif

namespace detail {

[[nodiscard]] inline auto is_lsp_ident_char(char c) -> bool {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

[[nodiscard]] inline auto contains_ident_word(std::string_view hay, std::string_view word) -> bool {
    if (word.empty() || hay.size() < word.size()) return false;
    std::size_t pos = 0;
    while ((pos = hay.find(word, pos)) != std::string_view::npos) {
        bool left_ok = (pos == 0) || !is_lsp_ident_char(hay[pos - 1]);
        bool right_ok = (pos + word.size() >= hay.size()) || !is_lsp_ident_char(hay[pos + word.size()]);
        if (left_ok && right_ok) return true;
        pos += word.size();
    }
    return false;
}

[[nodiscard]] inline auto type_ref_mentions_name(const cfire::frontend::TypeRef& t, std::string_view name) -> bool {
    if (t.name == name || t.raw_name == name) return true;
    if (t.elem_type && type_ref_mentions_name(*t.elem_type, name)) return true;
    if (t.key_type && type_ref_mentions_name(*t.key_type, name)) return true;
    if (t.fn_ret && type_ref_mentions_name(*t.fn_ret, name)) return true;
    for (const auto& ta : t.type_args) {
        if (type_ref_mentions_name(ta, name)) return true;
    }
    for (const auto& tt : t.tuple_types) {
        if (type_ref_mentions_name(tt, name)) return true;
    }
    for (const auto& fp : t.fn_params) {
        if (type_ref_mentions_name(fp, name)) return true;
    }
    return false;
}

[[nodiscard]] inline auto is_cursor_in_comment_or_raw_string(
    const std::vector<std::string>& lines,
    int line,
    int u8_col
) -> bool {
    if (line < 0 || static_cast<std::size_t>(line) >= lines.size()) return false;
    bool in_block_comment = false;
    bool in_raw_string = false;

    for (int li = 0; li <= line; ++li) {
        const std::string& ln = lines[static_cast<std::size_t>(li)];
        int limit = (li == line) ? std::clamp(u8_col, 0, static_cast<int>(ln.size())) : static_cast<int>(ln.size());
        bool in_str = false;
        for (int i = 0; i < limit; ++i) {
            char c = ln[static_cast<std::size_t>(i)];
            if (in_block_comment) {
                if (c == '*' && i + 1 < limit && ln[static_cast<std::size_t>(i + 1)] == '/') {
                    in_block_comment = false;
                    ++i;
                }
                continue;
            }
            if (in_raw_string) {
                if (c == '`') in_raw_string = false;
                continue;
            }
            if (in_str) {
                if (c == '\\' && i + 1 < limit) {
                    ++i;
                    continue;
                }
                if (c == '"') in_str = false;
                continue;
            }
            if (c == '/' && i + 1 < limit && ln[static_cast<std::size_t>(i + 1)] == '/') {
                if (li == line) return true;
                break;
            }
            if (c == '/' && i + 1 < limit && ln[static_cast<std::size_t>(i + 1)] == '*') {
                in_block_comment = true;
                ++i;
                continue;
            }
            if (c == '"') {
                in_str = true;
                continue;
            }
            if (c == '`') {
                in_raw_string = true;
                continue;
            }
        }
        if (li == line) {
            return in_block_comment || in_raw_string;
        }
    }
    return false;
}

// Finds nearest enclosing unmatched '(' or '{' across lines up to (line, u8_col), skipping strings/comments.
struct EnclosingDelim {
    char ch = '\0';
    int line = -1;
    int col = -1;
};

[[nodiscard]] inline auto find_enclosing_delim(
    const std::vector<std::string>& lines,
    int line,
    int u8_col
) -> EnclosingDelim {
    if (line < 0 || static_cast<std::size_t>(line) >= lines.size()) return {};
    int min_line = std::max(0, line - 40);
    std::vector<EnclosingDelim> stack;
    bool in_block_comment = false;
    bool in_raw_string = false;
    for (int li = min_line; li <= line; ++li) {
        const std::string& ln = lines[static_cast<std::size_t>(li)];
        int limit = (li == line) ? std::clamp(u8_col, 0, static_cast<int>(ln.size())) : static_cast<int>(ln.size());
        bool in_str = false;
        for (int i = 0; i < limit; ++i) {
            char c = ln[static_cast<std::size_t>(i)];
            if (in_block_comment) {
                if (c == '*' && i + 1 < limit && ln[static_cast<std::size_t>(i + 1)] == '/') {
                    in_block_comment = false;
                    ++i;
                }
                continue;
            }
            if (in_raw_string) {
                if (c == '`') in_raw_string = false;
                continue;
            }
            if (in_str) {
                if (c == '\\' && i + 1 < limit) {
                    ++i;
                    continue;
                }
                if (c == '"') in_str = false;
                continue;
            }
            if (c == '/' && i + 1 < limit && ln[static_cast<std::size_t>(i + 1)] == '/') break;
            if (c == '/' && i + 1 < limit && ln[static_cast<std::size_t>(i + 1)] == '*') {
                in_block_comment = true;
                ++i;
                continue;
            }
            if (c == '"') { in_str = true; continue; }
            if (c == '`') { in_raw_string = true; continue; }
            if (c == '(' || c == '{') {
                stack.push_back(EnclosingDelim{c, li, i});
            } else if (c == ')' && !stack.empty()) {
                for (int si = static_cast<int>(stack.size()) - 1; si >= 0; --si) {
                    if (stack[static_cast<std::size_t>(si)].ch == '(') {
                        stack.resize(static_cast<std::size_t>(si));
                        break;
                    }
                }
            } else if (c == '}' && !stack.empty()) {
                for (int si = static_cast<int>(stack.size()) - 1; si >= 0; --si) {
                    if (stack[static_cast<std::size_t>(si)].ch == '{') {
                        stack.resize(static_cast<std::size_t>(si));
                        break;
                    }
                }
            }
        }
    }
    if (stack.empty()) return {};
    return stack.back();
}

[[nodiscard]] inline auto is_type_slot_before(
    const std::vector<std::string>& lines,
    int line,
    int slot_col,
    const cfire::frontend::FileAST* ast
) -> bool {
    if (line < 0 || static_cast<std::size_t>(line) >= lines.size()) return false;
    const std::string& cur_ln = lines[static_cast<std::size_t>(line)];
    int pos = std::clamp(slot_col, 0, static_cast<int>(cur_ln.size()));

    auto trim_back_ws = [&](int p) -> int {
        while (p > 0 && (cur_ln[static_cast<std::size_t>(p - 1)] == ' ' || cur_ln[static_cast<std::size_t>(p - 1)] == '\t')) {
            --p;
        }
        return p;
    };

    // Strip leading type modifiers (`[]`, `[N]`, `*mut `, `*`, `&mut `, `&`, `mut `)
    for (int iter = 0; iter < 6; ++iter) {
        pos = trim_back_ws(pos);
        if (pos >= 2 && cur_ln[static_cast<std::size_t>(pos - 1)] == ']') {
            int j = pos - 2;
            while (j >= 0 && cur_ln[static_cast<std::size_t>(j)] != '[') {
                char ch = cur_ln[static_cast<std::size_t>(j)];
                if (!std::isdigit(static_cast<unsigned char>(ch))) {
                    j = -1;
                    break;
                }
                --j;
            }
            if (j >= 0 && cur_ln[static_cast<std::size_t>(j)] == '[') {
                pos = j;
                continue;
            }
        }
        if (pos >= 1 && (cur_ln[static_cast<std::size_t>(pos - 1)] == '*' || cur_ln[static_cast<std::size_t>(pos - 1)] == '&')) {
            --pos;
            continue;
        }
        std::string_view sub = std::string_view(cur_ln).substr(0, static_cast<std::size_t>(pos));
        if (sub.ends_with("*mut")) { pos -= 4; continue; }
        if (sub.ends_with("&mut")) { pos -= 4; continue; }
        break;
    }
    pos = trim_back_ws(pos);
    std::string_view pre = std::string_view(cur_ln).substr(0, static_cast<std::size_t>(pos));

    // 1. Return type `-> ...`
    if (pre.ends_with("->")) return true;

    // 2. Inside `make(|)` first argument (no comma before cursor inside `make(`)
    {
        int pdepth = 0;
        bool seen_comma = false;
        for (int i = pos - 1; i >= 0; --i) {
            char c = cur_ln[static_cast<std::size_t>(i)];
            if (c == ')' || c == ']') ++pdepth;
            else if (c == ',' && pdepth == 0) seen_comma = true;
            else if (c == '[' && pdepth > 0) --pdepth;
            else if (c == '(') {
                if (pdepth > 0) {
                    --pdepth;
                } else {
                    if (!seen_comma) {
                        std::string before_lp = cfire::fmt::detail::trim_ws(std::string_view(cur_ln).substr(0, static_cast<std::size_t>(i)));
                        if (before_lp.ends_with("make") &&
                            (before_lp.size() == 4 || !is_lsp_ident_char(before_lp[before_lp.size() - 5]))) {
                            return true;
                        }
                    }
                    break;
                }
            }
        }
    }

    // 3. Inside `own[...]`, `shared[...]`, `map[...]`, or generic type argument `[...]`
    {
        int bdepth = 0;
        for (int i = pos - 1; i >= 0; --i) {
            char c = cur_ln[static_cast<std::size_t>(i)];
            if (c == ']') ++bdepth;
            else if (c == '[') {
                if (bdepth > 0) {
                    --bdepth;
                } else {
                    int end_id = i;
                    while (end_id > 0 && (cur_ln[static_cast<std::size_t>(end_id - 1)] == ' ' || cur_ln[static_cast<std::size_t>(end_id - 1)] == '\t')) --end_id;
                    int start_id = end_id;
                    while (start_id > 0 && (is_lsp_ident_char(cur_ln[static_cast<std::size_t>(start_id - 1)]) || cur_ln[static_cast<std::size_t>(start_id - 1)] == '.')) {
                        --start_id;
                    }
                    std::string_view base_id = std::string_view(cur_ln).substr(static_cast<std::size_t>(start_id), static_cast<std::size_t>(end_id - start_id));
                    if (base_id == "own" || base_id == "shared" || base_id == "map") return true;
                    if (ast) {
                        for (const auto& st : ast->structs) {
                            if (st.name == base_id && !st.type_params.empty()) return true;
                        }
                    }
                    if (base_id.contains('.')) {
                        auto dot = base_id.rfind('.');
                        std::string_view tail = base_id.substr(dot + 1);
                        if (!tail.empty() && std::isupper(static_cast<unsigned char>(tail[0])) != 0) return true;
                    }
                    break;
                }
            }
        }
    }

    // 4. After `map[K]` value slot
    if (pos > 0 && cur_ln[static_cast<std::size_t>(pos - 1)] == ']') {
        int j = pos - 2;
        int bd = 1;
        while (j >= 0 && bd > 0) {
            if (cur_ln[static_cast<std::size_t>(j)] == ']') ++bd;
            else if (cur_ln[static_cast<std::size_t>(j)] == '[') --bd;
            --j;
        }
        if (bd == 0 && j >= 2) {
            std::string_view maybe_map = std::string_view(cur_ln).substr(0, static_cast<std::size_t>(j + 1));
            if (maybe_map.ends_with("map") &&
                (maybe_map.size() == 3 || !is_lsp_ident_char(maybe_map[maybe_map.size() - 4]))) {
                return true;
            }
        }
    }

    // 5. After single `:` (`x: `, `fn f(a: `, `type S struct { f: `) — distinguish from `Point{ x: ` and `call(arg: `
    if (!pre.empty() && pre.back() == ':' && (pre.size() < 2 || pre[pre.size() - 2] != ':')) {
        EnclosingDelim enc = find_enclosing_delim(lines, line, pos);
        if (enc.ch == '\0') return true;
        if (enc.ch == '(') {
            std::string_view head = std::string_view(lines[static_cast<std::size_t>(enc.line)]).substr(0, static_cast<std::size_t>(enc.col));
            std::string trimmed_head = cfire::fmt::detail::trim_ws(head);
            if (trimmed_head.starts_with("fn ") || trimmed_head.starts_with("fn(") ||
                trimmed_head.contains(" fn ") || trimmed_head.contains(" fn(")) {
                return true;
            }
            return false;
        }
        if (enc.ch == '{') {
            std::string_view head = std::string_view(lines[static_cast<std::size_t>(enc.line)]).substr(0, static_cast<std::size_t>(enc.col));
            std::string trimmed_head = cfire::fmt::detail::trim_ws(head);
            if (trimmed_head.ends_with(" struct") || trimmed_head.ends_with(" interface") ||
                trimmed_head.starts_with("type ")) {
                return true;
            }
            // Check if the current line before `:` is a local var annotation (`[mut] ident :`), not inside a struct literal `{`
            std::string before_colon = cfire::fmt::detail::trim_ws(pre.substr(0, pre.size() - 1));
            if (before_colon.starts_with("mut ")) {
                before_colon = cfire::fmt::detail::trim_ws(std::string_view(before_colon).substr(4));
            }
            bool is_plain_ident = !before_colon.empty();
            for (char ch : before_colon) {
                if (!is_lsp_ident_char(ch)) { is_plain_ident = false; break; }
            }
            if (is_plain_ident) {
                // Verify the enclosing `{` is a control/fn block rather than a struct literal `Type{`
                std::string_view last_word = trimmed_head;
                while (!last_word.empty() && (last_word.back() == ' ' || last_word.back() == '\t')) last_word.remove_suffix(1);
                if (last_word.empty() || last_word.ends_with(")") || last_word.ends_with("else") ||
                    last_word.ends_with("defer") || last_word.ends_with("cpp") || last_word.ends_with("\"") ||
                    last_word.ends_with("|") || last_word.starts_with("if ") || last_word.starts_with("for ") ||
                    last_word.starts_with("match ")) {
                    return true;
                }
            }
        }
    }

    return false;
}

struct StructLitFieldContext {
    bool active = false;
    std::string type_name;
    std::unordered_set<std::string> used_fields;
};

[[nodiscard]] inline auto detect_struct_lit_field_context(
    const std::vector<std::string>& lines,
    int line,
    int slot_col
) -> StructLitFieldContext {
    StructLitFieldContext res;
    if (line < 0 || static_cast<std::size_t>(line) >= lines.size()) return res;

    EnclosingDelim enc = find_enclosing_delim(lines, line, slot_col);
    if (enc.ch != '{' || enc.line < 0) return res;

    // Extract identifier before `{` (skipping optional `[...]`)
    const std::string& open_ln = lines[static_cast<std::size_t>(enc.line)];
    int p = enc.col;
    while (p > 0 && (open_ln[static_cast<std::size_t>(p - 1)] == ' ' || open_ln[static_cast<std::size_t>(p - 1)] == '\t')) --p;
    if (p > 0 && open_ln[static_cast<std::size_t>(p - 1)] == ']') {
        int bd = 1;
        p -= 2;
        while (p >= 0 && bd > 0) {
            if (open_ln[static_cast<std::size_t>(p)] == ']') ++bd;
            else if (open_ln[static_cast<std::size_t>(p)] == '[') --bd;
            --p;
        }
        ++p;
    }
    int tend = p;
    while (p > 0 && (is_lsp_ident_char(open_ln[static_cast<std::size_t>(p - 1)]) || open_ln[static_cast<std::size_t>(p - 1)] == '.')) {
        --p;
    }
    if (tend <= p) return res;
    std::string tname = open_ln.substr(static_cast<std::size_t>(p), static_cast<std::size_t>(tend - p));
    std::string_view leaf = tname;
    if (auto dot = leaf.rfind('.'); dot != std::string_view::npos) leaf = leaf.substr(dot + 1);
    if (leaf.empty() || std::isupper(static_cast<unsigned char>(leaf[0])) == 0) return res;

    // Make sure `{` isn't a block header like `struct {` or `enum {`
    std::string pre_t = cfire::fmt::detail::trim_ws(std::string_view(open_ln).substr(0, static_cast<std::size_t>(p)));
    if (pre_t == "type" || pre_t.ends_with(" struct") || pre_t.ends_with(" enum") || pre_t.ends_with(" interface")) {
        return res;
    }

    // Collect used fields & ensure cursor isn't after `:` for the current field value
    bool expecting_key = true;
    std::string cur_ident;
    int depth = 0;
    bool in_str = false;
    for (int li = enc.line; li <= line; ++li) {
        const std::string& ln = lines[static_cast<std::size_t>(li)];
        int start_c = (li == enc.line) ? (enc.col + 1) : 0;
        int end_c = (li == line) ? std::clamp(slot_col, 0, static_cast<int>(ln.size())) : static_cast<int>(ln.size());
        for (int i = start_c; i < end_c; ++i) {
            char c = ln[static_cast<std::size_t>(i)];
            if (in_str) {
                if (c == '\\' && i + 1 < end_c) { ++i; continue; }
                if (c == '"') in_str = false;
                continue;
            }
            if (c == '/' && i + 1 < end_c && ln[static_cast<std::size_t>(i + 1)] == '/') break;
            if (c == '"') { in_str = true; continue; }
            if (c == '{' || c == '(' || c == '[') { ++depth; continue; }
            if (c == '}' || c == ')' || c == ']') { if (depth > 0) --depth; continue; }
            if (depth > 0) continue;

            if (is_lsp_ident_char(c)) {
                cur_ident += c;
            } else if (c == ':') {
                if (i + 1 < end_c && ln[static_cast<std::size_t>(i + 1)] == '=') {
                    return res; // `:=` inside `{` means a statement block, not a struct literal!
                }
                if (!cur_ident.empty()) {
                    res.used_fields.insert(cur_ident);
                    cur_ident.clear();
                }
                expecting_key = false;
            } else if (c == ',') {
                cur_ident.clear();
                expecting_key = true;
            } else if (c != ' ' && c != '\t') {
                cur_ident.clear();
            }
        }
    }
    if (!expecting_key) return res;
    res.active = true;
    res.type_name = std::move(tname);
    return res;
}

} // namespace detail

CFIRE_LSP_FEAT_INLINE auto Server::compute_import_edit(const std::vector<std::string>& lines, std::string_view import_path) -> std::string {
    int last_import_line = -1;
    int package_line = -1;
    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        std::string_view l = lines[static_cast<std::size_t>(i)];
        while (!l.empty() && (l.front() == ' ' || l.front() == '\t')) l.remove_prefix(1);
        if (l.starts_with("package ")) {
            package_line = i;
        } else if (l.starts_with("import ")) {
            last_import_line = i;
        }
    }

    int insert_line = 0;
    std::string new_text;
    if (last_import_line >= 0) {
        insert_line = last_import_line + 1;
        new_text = std::format("import \"{}\"\n", import_path);
    } else if (package_line >= 0) {
        if (package_line + 1 < static_cast<int>(lines.size()) && lines[static_cast<std::size_t>(package_line + 1)].empty()) {
            insert_line = package_line + 2;
            new_text = std::format("import \"{}\"\n\n", import_path);
        } else {
            insert_line = package_line + 1;
            new_text = std::format("\nimport \"{}\"\n", import_path);
        }
    } else {
        insert_line = 0;
        new_text = std::format("import \"{}\"\n\n", import_path);
    }

    return std::format(
        R"([{{"range":{{"start":{{"line":{},"character":0}},"end":{{"line":{},"character":0}}}},"newText":"{}"}}])",
        insert_line, insert_line, detail::json_escape(new_text));
}

CFIRE_LSP_FEAT_INLINE auto Server::compute_completions_list(const std::string& uri, int line, int character) -> Server::CompletionListResult {
    std::string text = get_doc_text(uri);
    auto lines = detail::split_lines(text);
    const std::string cur_ln = (line >= 0 && static_cast<std::size_t>(line) < lines.size())
        ? lines[static_cast<std::size_t>(line)]
        : std::string{};
    int u8_char = std::clamp(detail::utf16_col_to_utf8_col(cur_ln, character), 0, static_cast<int>(cur_ln.size()));

    // 1. Suppress completions inside comments (`//`, `/* ... */`) and raw backtick strings
    if (detail::is_cursor_in_comment_or_raw_string(lines, line, u8_char)) {
        return {"[]", false};
    }

    auto sym = detail::symbol_at_position(lines, line, u8_char);
    if (!sym.name.empty() && sym.end_col < u8_char) {
        // Cursor moved past a delimiter (`fn `, `make(`, `type Foo `) — do not overwrite the prior token.
        sym = {};
    }
    int default_rep_start_u8 = !sym.name.empty() ? sym.start_col : u8_char;
    int default_rep_end_u8 = std::max(default_rep_start_u8, u8_char);

    std::string out = "[";
    bool first = true;
    int emitted_count = 0;
    std::unordered_set<std::string> emitted_keys;
    auto append_item = [&](std::string_view label, int kind, std::string_view detail_str, std::string_view doc_str,
                           std::string_view new_text = "", int insert_text_format = 1,
                           std::string_view sort_text = "", std::string_view filter_text = "",
                           std::string_view label_desc = "", std::string_view additional_edits = "",
                           int rep_start_u8 = -1, int rep_end_u8 = -1) {
        std::string_view eff_text = new_text.empty() ? label : new_text;
        std::string dedup = std::format("{}\x1f{}\x1f{}", kind, label, eff_text);
        if (!emitted_keys.insert(dedup).second) return;

        int rs_u8 = (rep_start_u8 >= 0) ? rep_start_u8 : default_rep_start_u8;
        int re_u8 = (rep_end_u8 >= 0) ? rep_end_u8 : default_rep_end_u8;
        if (re_u8 < rs_u8) re_u8 = rs_u8;
        int rs_u16 = detail::utf8_col_to_utf16_col(cur_ln, rs_u8);
        int re_u16 = detail::utf8_col_to_utf16_col(cur_ln, re_u8);
        int safe_line = std::max(0, line);

        if (!first) out += ",";
        first = false;
        ++emitted_count;
        out += std::format(
            R"({{"label":"{}","kind":{},"detail":"{}","documentation":"{}")",
            detail::json_escape(label), kind, detail::json_escape(detail_str), detail::json_escape(doc_str));

        if (!label_desc.empty()) {
            out += std::format(R"(,"labelDetails":{{"description":"{}"}})", detail::json_escape(label_desc));
        }
        if (!sort_text.empty()) {
            out += std::format(R"(,"sortText":"{}")", detail::json_escape(sort_text));
        }
        if (!filter_text.empty() && filter_text != label) {
            out += std::format(R"(,"filterText":"{}")", detail::json_escape(filter_text));
        }
        if (insert_text_format > 1 && client_snippet_support_) {
            out += std::format(R"(,"insertTextFormat":{})", insert_text_format);
        }
        out += std::format(
            R"(,"textEdit":{{"range":{{"start":{{"line":{},"character":{}}},"end":{{"line":{},"character":{}}}}},"newText":"{}"}})",
            safe_line, rs_u16, safe_line, re_u16, detail::json_escape(eff_text));
        if (!additional_edits.empty()) {
            out += std::format(R"(,"additionalTextEdits":{})", additional_edits);
        }
        out += "}";
    };

    const std::string filename = std::filesystem::path(detail::uri_to_path(uri)).filename().string();
    const bool is_build_cf = (filename == "build.cf");

    // 2. String literal & `import "..."` path handling
    auto interp_span = detail::find_string_interpolation_span_at(cur_ln, u8_char);
    if (interp_span.in_string && !interp_span.in_interp_expr) {
        if (interp_span.min_col > 0) {
            std::string pre_quote = cfire::fmt::detail::trim_ws(
                std::string_view(cur_ln).substr(0, static_cast<std::size_t>(interp_span.min_col - 1)));
            if (pre_quote == "import") {
                for (const auto& mod : cfire::doc::stdlib_catalog()) {
                    if (mod.import_path == "core:build" && !is_build_cf) continue;
                    append_item(mod.import_path, 9 /* Module */, mod.title, mod.overview,
                                mod.import_path, 1, std::format("0_{}", mod.import_path),
                                mod.import_path, mod.ns_alias, "", interp_span.min_col, u8_char);
                }
                out += "]";
                return {out, false};
            }
        }
        return {"[]", false};
    }

    // 3. Single colon suppression (never hijack `x:`, `x :=`, `Point{ x:`, `"{v:`)
    if (sym.name.empty() && sym.qualifier.empty() && !sym.is_double_colon && u8_char > 0 &&
        cur_ln[static_cast<std::size_t>(u8_char - 1)] == ':' &&
        (u8_char < 2 || cur_ln[static_cast<std::size_t>(u8_char - 2)] != ':')) {
        return {"[]", true};
    }

    bool has_following_paren = false;
    bool has_following_colon = false;
    if (!cur_ln.empty()) {
        int check_col = std::max(u8_char, sym.end_col);
        if (check_col >= 0 && static_cast<std::size_t>(check_col) < cur_ln.size()) {
            char next_ch = cur_ln[static_cast<std::size_t>(check_col)];
            if (next_ch == '(' || next_ch == '[') has_following_paren = true;
            if (next_ch == ':') has_following_colon = true;
        }
    }

    auto make_call_snippet = [&](std::string_view base_name, bool has_params, bool needs_type_args) -> std::pair<std::string, int> {
        if (!client_snippet_support_ || has_following_paren) {
            return {std::string(base_name), 1};
        }
        if (needs_type_args) {
            return {has_params ? std::format("{}[$1]($0)", base_name) : std::format("{}[$1]()$0", base_name), 2};
        }
        return {has_params ? std::format("{}($0)", base_name) : std::format("{}()$0", base_name), 2};
    };

    auto make_fn_decl_snippet = [&](std::string_view base_name, const cfire::frontend::FnDecl& fn) -> std::pair<std::string, int> {
        bool needs_targs = false;
        for (const auto& tp : fn.type_params) {
            bool mentioned = false;
            for (const auto& p : fn.params) {
                if (detail::type_ref_mentions_name(p.type, tp.name)) {
                    mentioned = true;
                    break;
                }
            }
            if (!mentioned) {
                needs_targs = true;
                break;
            }
        }
        return make_call_snippet(base_name, !fn.params.empty(), needs_targs);
    };

    auto make_sig_snippet = [&](std::string_view base_name, std::string_view cf_sig) -> std::pair<std::string, int> {
        std::string_view s = cf_sig;
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
        if (s.starts_with("fn ")) {
            s.remove_prefix(3);
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
        }
        // Skip optional receiver `(mut f: File)` before method name
        if (!s.empty() && s.front() == '(') {
            int depth = 1;
            s.remove_prefix(1);
            while (!s.empty() && depth > 0) {
                if (s.front() == '(') ++depth;
                else if (s.front() == ')') --depth;
                s.remove_prefix(1);
            }
        }
        auto lp = s.find('(');
        if (lp == std::string_view::npos) {
            return make_call_snippet(base_name, false, cfire::frontend::builtin_method_requires_type_arg(base_name));
        }
        std::string_view pre_lp = s.substr(0, lp);
        std::vector<std::string_view> tparams;
        if (auto lb = pre_lp.find('['); lb != std::string_view::npos) {
            if (auto rb = pre_lp.find(']', lb + 1); rb != std::string_view::npos && rb > lb + 1) {
                std::string_view tlist = pre_lp.substr(lb + 1, rb - lb - 1);
                std::size_t p = 0;
                while (p < tlist.size()) {
                    auto cm = tlist.find(',', p);
                    std::string_view item = tlist.substr(p, cm == std::string_view::npos ? std::string_view::npos : cm - p);
                    while (!item.empty() && item.front() == ' ') item.remove_prefix(1);
                    while (!item.empty() && item.back() == ' ') item.remove_suffix(1);
                    if (auto col = item.find(':'); col != std::string_view::npos) item = item.substr(0, col);
                    while (!item.empty() && item.back() == ' ') item.remove_suffix(1);
                    if (!item.empty()) tparams.push_back(item);
                    if (cm == std::string_view::npos) break;
                    p = cm + 1;
                }
            }
        }
        std::size_t rp = lp + 1;
        int pdepth = 1;
        while (rp < s.size() && pdepth > 0) {
            if (s[rp] == '(') ++pdepth;
            else if (s[rp] == ')') --pdepth;
            if (pdepth > 0) ++rp;
        }
        std::string_view plist = (rp > lp + 1) ? s.substr(lp + 1, rp - lp - 1) : std::string_view{};
        std::string trimmed_plist = cfire::fmt::detail::trim_ws(plist);
        bool has_params = !trimmed_plist.empty();
        bool needs_targs = cfire::frontend::builtin_method_requires_type_arg(base_name);
        if (!needs_targs && !tparams.empty()) {
            for (auto tp : tparams) {
                if (!detail::contains_ident_word(trimmed_plist, tp)) {
                    needs_targs = true;
                    break;
                }
            }
        }
        return make_call_snippet(base_name, has_params, needs_targs);
    };

    auto [ast_res, ws] = get_or_parse_ast(uri, text, line, u8_char);
    const cfire::frontend::FileAST* ast_ptr = ast_res ? &(*ast_res) : nullptr;
    auto env = ast_res ? detail::build_scope_bindings(*ast_res, ws, lines, line)
                       : std::unordered_map<std::string, detail::LocalBindingInfo>{};

    auto is_module_imported = [&](std::string_view import_path, std::string_view alias) -> bool {
        if (ast_res) {
            for (const auto& imp : ast_res->imports) {
                if (imp.path == import_path || imp.path == alias) return true;
            }
        }
        for (const auto& l : lines) {
            if (l.find(import_path) != std::string::npos && l.find("import ") != std::string::npos) return true;
        }
        return false;
    };

    auto strip_generic_suffix = [](std::string_view s) -> std::string_view {
        if (auto br = s.find('['); br != std::string_view::npos) return s.substr(0, br);
        return s;
    };

    // Helper to emit enum variants for a resolved enum qualifier (works for `::` and `.`->`::` fixup)
    auto emit_enum_variants = [&](std::string_view qual, bool fix_dot_to_colons) -> bool {
        bool any = false;
        auto emit_var = [&](std::string_view vname, std::string_view detail_s, std::string_view doc_s) {
            any = true;
            if (fix_dot_to_colons && sym.sep_col >= 0) {
                append_item(vname, 20 /* EnumMember */, detail_s, doc_s,
                            std::format("::{}", vname), 1, std::format("0_{}", vname),
                            std::format(".{}", vname), "uses ::", "", sym.sep_col, default_rep_end_u8);
            } else {
                append_item(vname, 20 /* EnumMember */, detail_s, doc_s,
                            vname, 1, std::format("0_{}", vname));
            }
        };
        if (ast_res) {
            for (const auto& en : ast_res->enums) {
                if (en.name == qual) {
                    for (const auto& v : en.variants) {
                        emit_var(v.name, en.name + "::" + v.name, "Algebraic enum variant");
                    }
                    return any;
                }
            }
        }
        for (const auto& pkg : ws.local_packages) {
            for (const auto& en : pkg.ast.enums) {
                if (en.name == qual || pkg.alias + "." + en.name == qual) {
                    for (const auto& v : en.variants) {
                        emit_var(v.name, en.name + "::" + v.name, "Algebraic enum variant");
                    }
                    return any;
                }
            }
        }
        std::string norm_qual(qual);
        if (auto dc = norm_qual.find("::"); dc != std::string::npos) norm_qual.replace(dc, 2, ".");
        if (auto dot = norm_qual.find('.'); dot != std::string::npos) {
            std::string_view al = std::string_view(norm_qual).substr(0, dot);
            std::string_view ename = std::string_view(norm_qual).substr(dot + 1);
            if (const auto* mod = detail::resolve_stdlib_module_by_alias(al, ast_ptr)) {
                if (auto [m, td] = detail::find_stdlib_type_doc(mod->ns_alias, ename); td != nullptr && td->kind == "enum") {
                    for (const auto& v : td->variants) {
                        emit_var(v, std::format("{}.{}::{}", al, td->bare_name(), v), td->summary);
                    }
                    return any;
                }
            }
        } else {
            for (const auto& mod : cfire::doc::stdlib_catalog()) {
                for (const auto& td : mod.type_docs) {
                    if (td.kind == "enum" && td.bare_name() == norm_qual && is_module_imported(mod.import_path, mod.ns_alias)) {
                        for (const auto& v : td.variants) {
                            emit_var(v, std::format("{}.{}::{}", mod.ns_alias, td.bare_name(), v), td.summary);
                        }
                        return any;
                    }
                }
            }
        }
        return any;
    };

    // 4. Double-colon `::` context (`Shape::`, `log.Level::`, `cpp::`)
    if (sym.is_double_colon) {
        if (sym.qualifier == "cpp") {
            for (const auto& [cname, csym] : ws.cpp_symbols) {
                std::string params_str;
                for (std::size_t i = 0; i < csym.param_types.size(); ++i) {
                    if (i > 0) params_str += ", ";
                    params_str += csym.param_types[i];
                }
                auto [ins, fmt] = make_call_snippet(cname, !csym.param_types.empty(), false);
                append_item(cname, 3, std::format("{}({}) -> {}", cname, params_str, detail::cpp_return_to_cf(csym.return_type)),
                    std::format("Native C/C++ symbol from <{}>", ws.cpp_symbol_header[cname]), ins, fmt, std::format("0_{}", cname));
            }
        } else if (!sym.qualifier.empty()) {
            (void)emit_enum_variants(sym.qualifier, false);
        }
        out += "]";
        return {out, false};
    }

    // 5. Declaration-name suppression (`fn |`, `type |`, `package |`, statement-start `mut |` / `for |`, and `type Foo <kind>`)
    if (sym.qualifier.empty() && !interp_span.in_interp_expr) {
        std::string pre_tok = cfire::fmt::detail::trim_ws(
            std::string_view(cur_ln).substr(0, static_cast<std::size_t>(default_rep_start_u8)));
        auto last_word_of = [](std::string_view sv) -> std::string_view {
            auto pos = sv.size();
            while (pos > 0 && detail::is_lsp_ident_char(sv[pos - 1])) --pos;
            return sv.substr(pos);
        };
        if (pre_tok == "package" || pre_tok == "type" || pre_tok == "fn" || pre_tok == "mut" || pre_tok == "for") {
            return {"[]", false};
        }
        if (pre_tok.starts_with("fn(") || pre_tok.starts_with("fn (")) {
            if (pre_tok.ends_with(')')) return {"[]", false};
        }
        if (pre_tok.starts_with("type ")) {
            std::string after_type = cfire::fmt::detail::trim_ws(std::string_view(pre_tok).substr(5));
            std::string_view lw = last_word_of(after_type);
            if (!after_type.empty() && lw == after_type) {
                for (const char* kw : {"struct", "enum", "interface"}) {
                    append_item(kw, 14 /* Keyword */, std::format("type {} {}", after_type, kw),
                        std::format("Declare a {} type", kw), kw, 1, std::format("0_{}", kw));
                }
                out += "]";
                return {out, false};
            }
        }
    }

    // 6. Type position (`x: |`, `fn f(a: []by|)`, `-> |`, `own[|]`, `make(|)`, `type S struct { f: | }`)
    if (!interp_span.in_interp_expr) {
        int slot_col = default_rep_start_u8;
        if (!sym.qualifier.empty() && sym.sep_col > 0) {
            slot_col = sym.sep_col - static_cast<int>(sym.qualifier.size());
            if (slot_col < 0) slot_col = 0;
        }
        if (detail::is_type_slot_before(lines, line, slot_col, ast_ptr)) {
            if (!sym.qualifier.empty()) {
                if (!env.contains(sym.qualifier)) {
                    if (const auto* mod = detail::resolve_stdlib_module_by_alias(sym.qualifier, ast_ptr)) {
                        if (!(mod->import_path == "core:build" && !is_build_cf && !is_module_imported(mod->import_path, mod->ns_alias))) {
                            bool imported = is_module_imported(mod->import_path, mod->ns_alias);
                            std::string add_edit = imported ? "" : compute_import_edit(lines, mod->import_path);
                            for (const auto& td : mod->type_docs) {
                                int tk = (td.kind == "enum") ? 13 : 22;
                                std::string_view bname = td.bare_name();
                                append_item(bname, tk, std::format("{}.{}", mod->ns_alias, td.name), td.summary,
                                    bname, 1, std::format("0_{}", bname), bname,
                                    imported ? mod->import_path : (mod->import_path + " (auto-import)"), add_edit);
                            }
                        }
                    }
                    for (const auto& pkg : ws.local_packages) {
                        if (pkg.alias == sym.qualifier) {
                            for (const auto& st : pkg.ast.structs) {
                                append_item(st.name, 22, "type " + st.name + " struct", cfire::fmt::format_struct_signature(st),
                                    st.name, 1, std::format("0_{}", st.name));
                            }
                            for (const auto& en : pkg.ast.enums) {
                                append_item(en.name, 13, "type " + en.name + " enum", cfire::fmt::format_enum_signature(en),
                                    en.name, 1, std::format("0_{}", en.name));
                            }
                            for (const auto& ifc : pkg.ast.interfaces) {
                                append_item(ifc.name, 8, "type " + ifc.name + " interface", cfire::fmt::format_interface_signature(ifc),
                                    ifc.name, 1, std::format("0_{}", ifc.name));
                            }
                        }
                    }
                }
                out += "]";
                return {out, true};
            }

            const bool has_bracket_prefix = default_rep_start_u8 >= 2 &&
                cur_ln.substr(static_cast<std::size_t>(default_rep_start_u8 - 2), 2) == "[]";
            const int slice_rep_start = has_bracket_prefix ? (default_rep_start_u8 - 2) : default_rep_start_u8;

            if (ast_res) {
                for (const auto& st : ast_res->structs) {
                    append_item(st.name, 22, "type " + st.name + " struct", cfire::fmt::format_struct_signature(st),
                        st.name, 1, std::format("0_{}", st.name));
                }
                for (const auto& en : ast_res->enums) {
                    append_item(en.name, 13, "type " + en.name + " enum", cfire::fmt::format_enum_signature(en),
                        en.name, 1, std::format("0_{}", en.name));
                }
                for (const auto& ifc : ast_res->interfaces) {
                    append_item(ifc.name, 8, "type " + ifc.name + " interface", cfire::fmt::format_interface_signature(ifc),
                        ifc.name, 1, std::format("0_{}", ifc.name));
                }
                for (const auto& pkg : ws.local_packages) {
                    if (pkg.ast.package_name == ast_res->package_name && !ast_res->package_name.empty()) {
                        for (const auto& st : pkg.ast.structs) {
                            append_item(st.name, 22, "type " + st.name + " struct", cfire::fmt::format_struct_signature(st),
                                st.name, 1, std::format("0_{}", st.name));
                        }
                        for (const auto& en : pkg.ast.enums) {
                            append_item(en.name, 13, "type " + en.name + " enum", cfire::fmt::format_enum_signature(en),
                                en.name, 1, std::format("0_{}", en.name));
                        }
                        for (const auto& ifc : pkg.ast.interfaces) {
                            append_item(ifc.name, 8, "type " + ifc.name + " interface", cfire::fmt::format_interface_signature(ifc),
                                ifc.name, 1, std::format("0_{}", ifc.name));
                        }
                    }
                }
            }
            for (const char* ptype : {"str", "i8", "i16", "i32", "i64", "isize", "int",
                                      "u8", "u16", "u32", "u64", "usize", "uint", "byte",
                                      "f32", "f64", "bool", "void", "any", "own", "shared", "map"}) {
                append_item(ptype, 25 /* TypeParameter */, "builtin type", std::format("Built-in type `{}`", ptype),
                    ptype, 1, std::format("1_{}", ptype));
            }
            append_item("[]byte", 25, "builtin byte slice", "Owning byte slice type `[]byte`",
                "[]byte", 1, "1_[]byte", "[]byte", "", "", slice_rep_start, default_rep_end_u8);
            append_item("[]u8", 25, "builtin u8 slice", "Owning u8 slice type `[]u8`",
                "[]u8", 1, "1_[]u8", "[]u8", "", "", slice_rep_start, default_rep_end_u8);

            for (const auto& mod : cfire::doc::stdlib_catalog()) {
                if (mod.import_path == "core:build" && !is_build_cf && !is_module_imported(mod.import_path, mod.ns_alias)) continue;
                bool imported = is_module_imported(mod.import_path, mod.ns_alias);
                if (!imported) continue;
                std::string eff_alias = mod.ns_alias;
                if (ast_res) {
                    for (const auto& imp : ast_res->imports) {
                        if (imp.path == mod.import_path && !imp.alias.empty()) {
                            eff_alias = imp.alias;
                            break;
                        }
                    }
                }
                append_item(eff_alias, 9, mod.import_path, mod.title, eff_alias, 1, std::format("2_{}", eff_alias));
                for (const auto& td : mod.type_docs) {
                    std::string qname = std::format("{}.{}", eff_alias, td.bare_name());
                    int tk = (td.kind == "enum") ? 13 : 22;
                    append_item(qname, tk, std::format("{}: {}", mod.import_path, td.name), td.summary,
                        qname, 1, std::format("2_{}", qname), std::format("{} {}", td.bare_name(), qname), mod.import_path);
                }
            }
            out += "]";
            return {out, true};
        }
    }

    // 7. Struct literal field key context (`Metric{ | }`)
    if (sym.qualifier.empty() && !interp_span.in_interp_expr) {
        auto sl_ctx = detail::detect_struct_lit_field_context(lines, line, default_rep_start_u8);
        if (sl_ctx.active) {
            bool found_struct = false;
            auto emit_field = [&](std::string_view fname, std::string_view ftype, std::string_view fdoc) {
                found_struct = true;
                if (sl_ctx.used_fields.contains(std::string(fname))) return;
                std::string ins = has_following_colon
                    ? std::string(fname)
                    : (client_snippet_support_ ? std::format("{}: $0", fname) : std::format("{}: ", fname));
                int fmt = (!has_following_colon && client_snippet_support_) ? 2 : 1;
                append_item(fname, 5 /* Field */, ftype, fdoc, ins, fmt, std::format("0_{}", fname));
            };
            if (ast_res) {
                for (const auto& st : ast_res->structs) {
                    if (st.name == sl_ctx.type_name) {
                        for (const auto& f : st.fields) {
                            emit_field(f.name, cfire::fmt::format_type_ref(f.type),
                                std::format("Field of `{}`", st.name));
                        }
                    }
                }
            }
            for (const auto& pkg : ws.local_packages) {
                for (const auto& st : pkg.ast.structs) {
                    if (st.name == sl_ctx.type_name || pkg.alias + "." + st.name == sl_ctx.type_name) {
                        for (const auto& f : st.fields) {
                            emit_field(f.name, cfire::fmt::format_type_ref(f.type),
                                std::format("Field of `{}`", st.name));
                        }
                    }
                }
            }
            if (!found_struct) {
                std::string_view ns;
                std::string_view tn = sl_ctx.type_name;
                if (auto dot = tn.find('.'); dot != std::string_view::npos) {
                    ns = tn.substr(0, dot);
                    tn = tn.substr(dot + 1);
                }
                if (!ns.empty()) {
                    if (const auto* mod = detail::resolve_stdlib_module_by_alias(ns, ast_ptr)) {
                        ns = mod->ns_alias;
                    }
                    if (auto [m, td] = detail::find_stdlib_type_doc(ns, tn); m != nullptr && td != nullptr) {
                        for (const auto& f : td->fields) {
                            emit_field(f.name, f.type, f.summary.empty() ? std::format("Field of {}.{}", m->ns_alias, td->bare_name()) : f.summary);
                        }
                    }
                    if (!found_struct) {
                        if (const auto* sst = cfire::frontend::find_stdlib_struct(ns, tn)) {
                            for (const auto& sf : cfire::frontend::stdlib_struct_fields(*sst)) {
                                emit_field(sf.name, sf.type, sst->summary);
                            }
                        }
                    }
                }
            }
            if (found_struct) {
                out += "]";
                return {out, true};
            }
        }
    }

    // 8. Member / Qualifier completions (`expr.`, `mod.`, `pkg.`, `i32.`, `Shape.`)
    if (!sym.qualifier.empty()) {
        const bool qual_is_local = env.contains(sym.qualifier);
        bool known_non_fallback_target = false;

        // 8a. `core:*` / `ext:*` module functions, constants & types (supports custom import aliases)
        if (!qual_is_local) {
            if (const auto* mod = detail::resolve_stdlib_module_by_alias(sym.qualifier, ast_ptr)) {
                bool imported = is_module_imported(mod->import_path, mod->ns_alias);
                if (!(mod->import_path == "core:build" && !is_build_cf && !imported)) {
                    known_non_fallback_target = true;
                    std::string add_edit = imported ? "" : compute_import_edit(lines, mod->import_path);
                    std::string label_desc = imported ? mod->import_path : (mod->import_path + " (auto-import)");
                    for (const auto& fn : mod->functions) {
                        if (fn.name.find('.') != std::string::npos) continue;
                        if (fn.cf_sig.starts_with("const ")) {
                            std::string c_detail = imported ? fn.cf_sig : std::format("{} (auto-import)", fn.cf_sig);
                            std::string c_doc = std::format("{} (C++23: {})", fn.summary, fn.cpp_lowering);
                            if (!imported) {
                                c_doc += std::format("\n\n*Selecting this will automatically insert `import \"{}\"`.*", mod->import_path);
                            }
                            append_item(fn.name, 21 /* Constant */, c_detail, c_doc, fn.name, 1,
                                std::format("0_{}", fn.name), fn.name, label_desc, add_edit);
                            continue;
                        }
                        if (fn.cf_sig.starts_with("type ")) {
                            std::string t_doc = std::format("{} (C++23: {})", fn.summary, fn.cpp_lowering);
                            if (!imported) {
                                t_doc += std::format("\n\n*Selecting this will automatically insert `import \"{}\"`.*", mod->import_path);
                            }
                            append_item(fn.name, 22 /* Struct */, fn.cf_sig, t_doc, fn.name, 1,
                                std::format("1_{}", fn.name), fn.name, label_desc, add_edit);
                            continue;
                        }
                        std::string fn_detail = imported ? fn.cf_sig : std::format("{} (auto-import)", fn.cf_sig);
                        std::string fn_doc = std::format("{} (C++23: {})", fn.summary, fn.cpp_lowering);
                        if (!imported) {
                            fn_doc += std::format("\n\n*Selecting this will automatically insert `import \"{}\"`.*", mod->import_path);
                        }
                        auto [ins, fmt] = make_sig_snippet(fn.name, fn.cf_sig);
                        append_item(fn.name, 3, fn_detail, fn_doc, ins, fmt,
                            std::format("0_{}", fn.name), fn.name, label_desc, add_edit);
                    }
                    for (const auto& td : mod->type_docs) {
                        std::string_view bname = td.bare_name();
                        int tk = (td.kind == "enum") ? 13 : 22;
                        std::string t_doc = td.summary;
                        if (!imported) {
                            t_doc += std::format("\n\n*Selecting this will automatically insert `import \"{}\"`.*", mod->import_path);
                        }
                        append_item(bname, tk, std::format("type {}.{}", mod->ns_alias, td.name), t_doc,
                            bname, 1, std::format("1_{}", bname), bname, label_desc, add_edit);
                    }
                }
            }
        }

        // 8b. Primitive associated items (`i32.MAX`, `i32.parse`, `own[T].from_raw`)
        if (!qual_is_local) {
            if (sym.qualifier.starts_with("own[") && sym.qualifier.ends_with("]")) {
                known_non_fallback_target = true;
                auto [ins, fmt] = make_sig_snippet("from_raw", "fn from_raw(p: *mut T) -> own[T]");
                append_item("from_raw", 3, std::format("fn {}.from_raw(p: *mut T) -> {}", sym.qualifier, sym.qualifier),
                    "Adopts ownership of a raw pointer returned by `release()`.", ins, fmt, "0_from_raw");
            } else if (std::uint16_t pbits = cfire::frontend::builtin_recv_bits_for_type_name(sym.qualifier);
                       (pbits & (cfire::frontend::BmInt | cfire::frontend::BmFloat | cfire::frontend::BmBool)) != 0) {
                known_non_fallback_target = true;
                for (const auto& c : cfire::frontend::kBuiltinAssocConsts) {
                    if ((c.recv & pbits) == 0) continue;
                    std::string ty = (c.name == "BITS") ? "u32" : sym.qualifier;
                    append_item(c.name, 21 /* Constant */, std::format("const {}.{}: {}", sym.qualifier, c.name, ty),
                        c.summary, c.name, 1, std::format("0_{}", c.name));
                }
                std::string psig = std::format("fn parse(s: str) -> {}!", sym.qualifier);
                auto [ins, fmt] = make_sig_snippet("parse", psig);
                append_item("parse", 3, std::format("fn {}.parse(s: str) -> {}!", sym.qualifier, sym.qualifier),
                    "Parses a complete string literal into this type, returning an `Error` if invalid or out of range.",
                    ins, fmt, "0_parse");
            }
        }

        // 8c. Imported local `.cf` package symbols (`checksum.`)
        if (!qual_is_local) {
            for (const auto& pkg : ws.local_packages) {
                if (pkg.alias == sym.qualifier) {
                    known_non_fallback_target = true;
                    auto pkg_lines = detail::split_lines(get_doc_text(pkg.uri));
                    for (const auto& fn : pkg.ast.functions) {
                        if (fn.receiver) continue;
                        std::string doc = detail::extract_leading_doc_comments(pkg_lines, fn.line);
                        if (doc.empty()) doc = detail::lower_fn_preview(fn);
                        auto [ins, fmt] = make_fn_decl_snippet(fn.name, fn);
                        append_item(fn.name, 3, cfire::fmt::format_fn_signature(fn), doc, ins, fmt, std::format("0_{}", fn.name));
                    }
                    for (const auto& st : pkg.ast.structs) {
                        std::string doc = detail::extract_leading_doc_comments(pkg_lines, st.line);
                        if (doc.empty()) doc = cfire::fmt::format_struct_signature(st);
                        append_item(st.name, 22, "type " + st.name + " struct", doc, st.name, 1, std::format("1_{}", st.name));
                    }
                    for (const auto& en : pkg.ast.enums) {
                        std::string doc = detail::extract_leading_doc_comments(pkg_lines, en.line);
                        if (doc.empty()) doc = cfire::fmt::format_enum_signature(en);
                        append_item(en.name, 13, "type " + en.name + " enum", doc, en.name, 1, std::format("1_{}", en.name));
                    }
                    for (const auto& ifc : pkg.ast.interfaces) {
                        std::string doc = detail::extract_leading_doc_comments(pkg_lines, ifc.line);
                        if (doc.empty()) doc = cfire::fmt::format_interface_signature(ifc);
                        append_item(ifc.name, 8, "type " + ifc.name + " interface", doc, ifc.name, 1, std::format("1_{}", ifc.name));
                    }
                }
            }
        }

        // 8d. `cpp.` qualifier
        if (sym.qualifier == "cpp" && !qual_is_local) {
            known_non_fallback_target = true;
            for (const auto& [cname, csym] : ws.cpp_symbols) {
                std::string params_str;
                for (std::size_t i = 0; i < csym.param_types.size(); ++i) {
                    if (i > 0) params_str += ", ";
                    params_str += csym.param_types[i];
                }
                auto [ins, fmt] = make_call_snippet(cname, !csym.param_types.empty(), false);
                append_item(cname, 3, std::format("{}({}) -> {}", cname, params_str, detail::cpp_return_to_cf(csym.return_type)),
                    std::format("Native C/C++ symbol from <{}>", ws.cpp_symbol_header[cname]), ins, fmt, std::format("0_{}", cname));
            }
        }

        // 8e. Enum type accessed via `.` -> rewrite `.` to `::Variant`
        if (!qual_is_local && emit_enum_variants(sym.qualifier, true)) {
            known_non_fallback_target = true;
        }

        // 8f. Static stdlib type constructors (`http.Response.`, `Response.`) when qualifier is a type name
        if (!qual_is_local) {
            for (const auto& mod : cfire::doc::stdlib_catalog()) {
                for (const auto& fn : mod.functions) {
                    if (auto dot = fn.name.find('.'); dot != std::string::npos) {
                        std::string htype = fn.name.substr(0, dot);
                        std::string mname = fn.name.substr(dot + 1);
                        if (mname == "make") continue;
                        if (fn.cf_sig.starts_with("fn (")) continue; // instance method, not static
                        if (sym.qualifier == mod.ns_alias + "." + htype ||
                            (sym.qualifier == htype && is_module_imported(mod.import_path, mod.ns_alias))) {
                            known_non_fallback_target = true;
                            auto [ins, fmt] = make_sig_snippet(mname, fn.cf_sig);
                            append_item(mname, 3, fn.cf_sig, fn.summary, ins, fmt, std::format("0_{}", mname));
                        }
                    }
                }
            }
        }

        // 8g. Value / Chained expression receiver (`s.`, `xs.`, `0..xs.`, `m.`, `doc.sections.`, etc.)
        std::string rtype;
        if (!known_non_fallback_target && ast_res) {
            std::string raw_type;
            bool is_mut_binding = true;
            if (auto it = env.find(sym.qualifier); it != env.end()) {
                raw_type = it->second.cf_type;
                is_mut_binding = it->second.is_mut;
            } else {
                raw_type = detail::resolve_qualifier_type(sym.qualifier, *ast_res, ws, env, /*preserve_wrappers=*/true);
                if (auto dot = sym.qualifier.find('.'); dot != std::string::npos) {
                    if (auto root_it = env.find(sym.qualifier.substr(0, dot)); root_it != env.end()) {
                        is_mut_binding = root_it->second.is_mut;
                    }
                }
            }
            auto tinfo = detail::strip_wrappers_with_info(raw_type);
            rtype = tinfo.base;
            std::string_view base_rtype = strip_generic_suffix(rtype);
            std::string unwrap_note = tinfo.was_optional ? "unwrap ? first"
                                    : (tinfo.was_fallible ? "unwrap ! first" : "");

            if (std::uint16_t raw_bits = cfire::frontend::builtin_recv_bits_for_type_name(raw_type);
                (raw_bits & (cfire::frontend::BmOptional | cfire::frontend::BmFallible)) != 0) {
                known_non_fallback_target = true;
                std::string raw_trim = raw_type;
                while (!raw_trim.empty() && raw_trim.back() == ' ') raw_trim.pop_back();
                const bool is_void_fallible = (raw_bits & cfire::frontend::BmFallible) != 0 &&
                    raw_trim.size() > 1 && raw_trim.substr(0, raw_trim.size() - 1) == "void";
                for (const auto& bm : cfire::frontend::kBuiltinMethods) {
                    if ((bm.recv & raw_bits) == 0) continue;
                    if (bm.name == "ignore_error" && is_void_fallible) continue;
                    std::string sig = cfire::frontend::builtin_method_signature(bm);
                    auto [ins, fmt] = make_sig_snippet(bm.name, sig);
                    append_item(bm.name, 2, sig, bm.summary, ins, fmt, std::format("0_{}", bm.name));
                }
            }

            auto rank_and_note = [&](bool mutates) -> std::pair<std::string, std::string> {
                bool needs_mut = mutates && !is_mut_binding;
                std::string note;
                if (needs_mut) note = "requires mut";
                if (!unwrap_note.empty()) {
                    if (!note.empty()) note += ", ";
                    note += unwrap_note;
                }
                return {note.empty() ? "0_" : "2_", note};
            };

            if (rtype == "Error") {
                known_non_fallback_target = true;
                auto [ic, fc] = make_sig_snippet("code", "fn code() -> i32");
                append_item("code", 2, "fn (e: Error) code() -> i32",
                    "Machine-readable error code (e.g. fs.ERR_EXISTS, or 0 if unspecified).", ic, fc, "0_code");
                auto [im, fm] = make_sig_snippet("message", "fn message() -> str");
                append_item("message", 2, "fn (e: Error) message() -> str",
                    "Human-readable error message carried by this Error.", im, fm, "0_message");
            }
            if (auto dot = rtype.find('.'); dot != std::string::npos) {
                std::string_view rns = std::string_view(base_rtype).substr(0, dot);
                std::string_view rtn = std::string_view(base_rtype).substr(dot + 1);
                if (auto [m, td] = detail::find_stdlib_type_doc(rns, rtn); td != nullptr) {
                    known_non_fallback_target = true;
                    auto [sp, note] = rank_and_note(false);
                    for (const auto& f : td->fields) {
                        append_item(f.name, 5, note.empty() ? f.type : std::format("{} ({})", f.type, note),
                            f.summary.empty() ? std::format("Field of {}.{}", m->ns_alias, td->bare_name()) : f.summary,
                            f.name, 1, sp + f.name, f.name, note);
                    }
                } else if (const auto* sst = cfire::frontend::find_stdlib_struct(rns, rtn)) {
                    known_non_fallback_target = true;
                    auto [sp, note] = rank_and_note(false);
                    for (const auto& sf : cfire::frontend::stdlib_struct_fields(*sst)) {
                        append_item(sf.name, 5, std::string(sf.type), sst->summary,
                            sf.name, 1, sp + std::string(sf.name), sf.name, note);
                    }
                }
            }
            if (std::uint16_t bits = cfire::frontend::builtin_recv_bits_for_type_name(rtype); bits != 0) {
                known_non_fallback_target = true;
                std::unordered_set<std::string_view> seen;
                for (const auto& bm : cfire::frontend::kBuiltinMethods) {
                    if ((bm.recv & bits) == 0 || !seen.insert(bm.name).second) continue;
                    std::string sig = cfire::frontend::builtin_method_signature(bm);
                    auto [ins, fmt] = make_sig_snippet(bm.name, sig);
                    auto [sp, note] = rank_and_note(bm.mutates);
                    std::string det = note.empty() ? sig : std::format("{} ({})", sig, note);
                    append_item(bm.name, 2, det, bm.summary, ins, fmt, sp + std::string(bm.name), bm.name, note);
                }
            }
            auto add_struct_fields = [&](const std::vector<cfire::frontend::StructDecl>& structs) {
                for (const auto& st : structs) {
                    if (st.name == rtype || st.name == base_rtype) {
                        known_non_fallback_target = true;
                        auto [sp, note] = rank_and_note(false);
                        for (const auto& f : st.fields) {
                            std::string f_sig = cfire::fmt::format_type_ref(f.type);
                            std::string det = note.empty() ? f_sig : std::format("{} ({})", f_sig, note);
                            std::string f_doc = std::format("Field of {} (C++23: {}) {}",
                                st.name, detail::lower_type_preview(f.type), f.raw_tag);
                            append_item(f.name, 5, det, f_doc, f.name, 1, sp + f.name, f.name, note);
                        }
                    }
                }
            };
            add_struct_fields(ast_res->structs);
            for (const auto& pkg : ws.local_packages) add_struct_fields(pkg.ast.structs);

            for (const auto& fn : ast_res->functions) {
                if (fn.receiver && (fn.receiver->type.name == rtype || fn.receiver->type.name == base_rtype)) {
                    known_non_fallback_target = true;
                    auto [ins, fmt] = make_fn_decl_snippet(fn.name, fn);
                    auto [sp, note] = rank_and_note(fn.receiver->intent == "mut");
                    std::string sig = cfire::fmt::format_fn_signature(fn);
                    std::string det = note.empty() ? sig : std::format("{} ({})", sig, note);
                    append_item(fn.name, 2, det, detail::lower_fn_preview(fn), ins, fmt, sp + fn.name, fn.name, note);
                }
            }
            for (const auto& pkg : ws.local_packages) {
                for (const auto& fn : pkg.ast.functions) {
                    if (fn.receiver && (fn.receiver->type.name == rtype || fn.receiver->type.name == base_rtype ||
                                        pkg.alias + "." + fn.receiver->type.name == base_rtype)) {
                        known_non_fallback_target = true;
                        auto [ins, fmt] = make_fn_decl_snippet(fn.name, fn);
                        auto [sp, note] = rank_and_note(fn.receiver->intent == "mut");
                        std::string sig = cfire::fmt::format_fn_signature(fn);
                        std::string det = note.empty() ? sig : std::format("{} ({})", sig, note);
                        append_item(fn.name, 2, det, detail::lower_fn_preview(fn), ins, fmt, sp + fn.name, fn.name, note);
                    }
                }
            }
            for (const auto& mod : cfire::doc::stdlib_catalog()) {
                for (const auto& fn : mod.functions) {
                    if (auto dot = fn.name.find('.'); dot != std::string::npos) {
                        std::string htype = fn.name.substr(0, dot);
                        std::string mname = fn.name.substr(dot + 1);
                        if (mname == "make") continue;
                        if (!fn.cf_sig.starts_with("fn (")) continue; // static Type.fn does not belong on instance values
                        if (detail::match_stdlib_handle_type(rtype, mod, htype)) {
                            known_non_fallback_target = true;
                            auto [ins, fmt] = make_sig_snippet(mname, fn.cf_sig);
                            bool mutates = fn.cf_sig.starts_with("fn (mut ");
                            auto [sp, note] = rank_and_note(mutates);
                            std::string det = note.empty() ? fn.cf_sig : std::format("{} ({})", fn.cf_sig, note);
                            append_item(mname, 2, det, fn.summary, ins, fmt, sp + mname, mname, note);
                        }
                    }
                }
            }
        }

        // 8h. Low-ranked fallback for genuinely unresolved receiver types (`auto`, `any`, generic `T`, or partial parse)
        if (emitted_count == 0 && !known_non_fallback_target &&
            (rtype.empty() || rtype == "auto" || rtype == "any" || rtype == sym.qualifier)) {
            static constexpr std::array<std::string_view, 16> kFallbackMethods = {
                "len", "is_empty", "contains", "push", "pop", "trim", "split", "starts_with",
                "ends_with", "replace", "get", "insert", "remove", "keys", "values", "clear"
            };
            for (std::string_view mname : kFallbackMethods) {
                for (const auto& bm : cfire::frontend::kBuiltinMethods) {
                    if (bm.name != mname) continue;
                    std::string sig = cfire::frontend::builtin_method_signature(bm);
                    auto [ins, fmt] = make_sig_snippet(bm.name, sig);
                    append_item(bm.name, 2, std::format("{} (type unresolved)", sig), bm.summary,
                        ins, fmt, "9_" + std::string(bm.name), bm.name, "type unresolved");
                    break;
                }
            }
        }

        out += "]";
        return {out, false};
    }

    // 9. Unqualified completions: strict `sortText` tiers `0_`..`7_` + statement-start snippet gating
    const bool is_stmt_start = !interp_span.in_interp_expr && [&]() {
        for (int i = 0; i < default_rep_start_u8 && i < static_cast<int>(cur_ln.size()); ++i) {
            char c = cur_ln[static_cast<std::size_t>(i)];
            if (c != ' ' && c != '\t') return false;
        }
        return true;
    }();

    std::unordered_set<std::string> local_names;
    if (ast_res) {
        for (const auto& [vname, b] : env) {
            local_names.insert(vname);
            append_item(vname, 6 /* Variable */, b.cf_type, std::format("C++23: {}", b.cpp_type),
                vname, 1, std::format("0_{}", vname));
        }
        for (const auto& pkg : ws.local_packages) {
            append_item(pkg.alias, 9 /* Module */, pkg.import_path, "Imported local .cf package",
                pkg.alias, 1, std::format("1_{}", pkg.alias));
        }
        if (!ws.cpp_symbols.empty()) {
            append_item("cpp", 9, "Clang AST Header Bridge", "Native C/C++ imported symbols",
                "cpp", 1, "1_cpp");
        }
        for (const auto& fn : ast_res->functions) {
            if (fn.receiver) continue;
            std::string doc = detail::extract_leading_doc_comments(lines, fn.line);
            if (doc.empty()) doc = detail::lower_fn_preview(fn);
            auto [ins, fmt] = make_fn_decl_snippet(fn.name, fn);
            append_item(fn.name, 3, cfire::fmt::format_fn_signature(fn), doc,
                ins, fmt, std::format("1_{}", fn.name));
        }
        for (const auto& st : ast_res->structs) {
            std::string doc = detail::extract_leading_doc_comments(lines, st.line);
            if (doc.empty()) doc = cfire::fmt::format_struct_signature(st);
            append_item(st.name, 22, "type " + st.name + " struct", doc,
                st.name, 1, std::format("1_{}", st.name));
        }
        for (const auto& en : ast_res->enums) {
            std::string doc = detail::extract_leading_doc_comments(lines, en.line);
            if (doc.empty()) doc = cfire::fmt::format_enum_signature(en);
            append_item(en.name, 13, "type " + en.name + " enum", doc,
                en.name, 1, std::format("1_{}", en.name));
        }
        for (const auto& ifc : ast_res->interfaces) {
            std::string doc = detail::extract_leading_doc_comments(lines, ifc.line);
            if (doc.empty()) doc = cfire::fmt::format_interface_signature(ifc);
            append_item(ifc.name, 8, "type " + ifc.name + " interface", doc,
                ifc.name, 1, std::format("1_{}", ifc.name));
        }
        for (const auto& pkg : ws.local_packages) {
            if (pkg.ast.package_name == ast_res->package_name && !ast_res->package_name.empty()) {
                auto pkg_lines = detail::split_lines(get_doc_text(pkg.uri));
                for (const auto& fn : pkg.ast.functions) {
                    if (fn.receiver) continue;
                    std::string doc = detail::extract_leading_doc_comments(pkg_lines, fn.line);
                    if (doc.empty()) doc = detail::lower_fn_preview(fn);
                    auto [ins, fmt] = make_fn_decl_snippet(fn.name, fn);
                    append_item(fn.name, 3, cfire::fmt::format_fn_signature(fn), doc,
                        ins, fmt, std::format("1_{}", fn.name));
                }
                for (const auto& st : pkg.ast.structs) {
                    std::string doc = detail::extract_leading_doc_comments(pkg_lines, st.line);
                    if (doc.empty()) doc = cfire::fmt::format_struct_signature(st);
                    append_item(st.name, 22, "type " + st.name + " struct", doc,
                        st.name, 1, std::format("1_{}", st.name));
                }
                for (const auto& en : pkg.ast.enums) {
                    std::string doc = detail::extract_leading_doc_comments(pkg_lines, en.line);
                    if (doc.empty()) doc = cfire::fmt::format_enum_signature(en);
                    append_item(en.name, 13, "type " + en.name + " enum", doc,
                        en.name, 1, std::format("1_{}", en.name));
                }
                for (const auto& ifc : pkg.ast.interfaces) {
                    std::string doc = detail::extract_leading_doc_comments(pkg_lines, ifc.line);
                    if (doc.empty()) doc = cfire::fmt::format_interface_signature(ifc);
                    append_item(ifc.name, 8, "type " + ifc.name + " interface", doc,
                        ifc.name, 1, std::format("1_{}", ifc.name));
                }
            }
        }
    }

    // Tier 2: Built-in functions, expression conversions, slot-valid keywords, and statement-start snippets
    {
        auto add_builtin_fn = [&](std::string_view name, std::string_view sig, std::string_view detail_s, std::string_view doc_s) {
            auto [ins, fmt] = make_sig_snippet(name, sig);
            append_item(name, 3, detail_s, doc_s, ins, fmt, std::format("2_{}", name));
        };
        add_builtin_fn("println", "fn println(fmt: str)", "println(args...)", "Prints formatted arguments followed by a newline to standard output.");
        add_builtin_fn("print", "fn print(fmt: str)", "print(args...)", "Prints formatted arguments to standard output without trailing newline.");
        add_builtin_fn("eprintln", "fn eprintln(fmt: str)", "eprintln(args...)", "Prints formatted arguments followed by a newline to standard error.");
        add_builtin_fn("eprint", "fn eprint(fmt: str)", "eprint(args...)", "Prints formatted arguments to standard error without trailing newline.");
        add_builtin_fn("panic", "fn panic(message: str)", "panic(message: str)", "Terminates the program immediately with an error message.");
        add_builtin_fn("assert", "fn assert(cond: bool, msg: str = \"\")", "assert(cond: bool, msg: str = \"\")", "Aborts execution if condition evaluates to false (formats [left, right] value diffs).");
        add_builtin_fn("assert_eq", "fn assert_eq(left: T, right: T, msg: str = \"\")", "assert_eq(left: T, right: T, msg: str = \"\")", "Verifies left == right with single evaluation and [left, right] value diff reporting.");
        add_builtin_fn("assert_ne", "fn assert_ne(left: T, right: T, msg: str = \"\")", "assert_ne(left: T, right: T, msg: str = \"\")", "Verifies left != right with single evaluation and [left, right] value diff reporting.");
        add_builtin_fn("black_box", "fn black_box(val: T)", "black_box(val: T) -> T", "Compiler optimization barrier for benchmarks inside bench \"...\" blocks.");
        add_builtin_fn("make", "fn make(type_or_args: T)", "make([]T, len, cap) / make(map[K]V) / make(T, args...)", "Allocates and initializes slices, maps, handles, or values.");

        if (!is_stmt_start) {
            const bool has_bracket_prefix = default_rep_start_u8 >= 2 &&
                cur_ln.substr(static_cast<std::size_t>(default_rep_start_u8 - 2), 2) == "[]";
            const int slice_rep_start = has_bracket_prefix ? (default_rep_start_u8 - 2) : default_rep_start_u8;
            auto [s_bvec, f_bvec] = make_sig_snippet("[]byte", "fn []byte(val: str)");
            append_item("[]byte", 3, "[]byte(val: str | []u8) -> []byte", "Constructs an owning byte slice ([]byte) from a string or slice.",
                s_bvec, f_bvec, "2_[]byte", "[]byte", "", "", slice_rep_start, default_rep_end_u8);
            auto [s_uvec, f_uvec] = make_sig_snippet("[]u8", "fn []u8(val: str)");
            append_item("[]u8", 3, "[]u8(val: str | []byte) -> []u8", "Constructs an owning u8 slice ([]u8) from a string or byte slice.",
                s_uvec, f_uvec, "2_[]u8", "[]u8", "", "", slice_rep_start, default_rep_end_u8);
            auto [s_str, f_str] = make_sig_snippet("str", "fn str(val: Any)");
            append_item("str", 3, "str(val: []byte | []u8 | scalar) -> str", "Constructs a UTF-8 string from a byte/u8 slice or scalar value.",
                s_str, f_str, "2_str");
            auto [s_byte, f_byte] = make_sig_snippet("byte", "fn byte(val: i64)");
            append_item("byte", 3, "byte(val: integer) -> byte", "Converts an integer to a byte (`u8`), panicking when it is out of range.",
                s_byte, f_byte, "2_byte");
        }

        for (const char* kw : {"return", "mut", "move", "try", "or", "true", "false", "none", "break", "continue", "else", "async"}) {
            append_item(kw, 14 /* Keyword */, std::format("{} keyword", kw), std::format("Language keyword `{}`", kw),
                kw, 1, std::format("2_{}", kw));
        }

        if (is_stmt_start) {
            if (client_snippet_support_) {
                append_item("fn", 15, "fn name(params) -> void { ... }", "Function declaration snippet",
                    "fn ${1:name}(${2:params}) -> ${3:void} {\n\t${0}\n}", 2, "2_fn");
                append_item("type struct", 15, "type Name struct { ... }", "Struct declaration snippet",
                    "type ${1:Name} struct {\n\t${0}\n}", 2, "2_type struct");
                append_item("type enum", 15, "type Name enum { ... }", "Algebraic enum declaration snippet",
                    "type ${1:Name} enum {\n\t${0}\n}", 2, "2_type enum");
                append_item("match", 15, "match expr { ... }", "Pattern matching expression snippet",
                    "match ${1:expr} {\n\t${2:case} => ${0}\n}", 2, "2_match");
                append_item("for", 15, "for item in iter { ... }", "For-in iteration loop snippet",
                    "for ${1:item} in ${2:iter} {\n\t${0}\n}", 2, "2_for");
                append_item("for ..", 15, "for i in 0..n { ... }", "Numeric range loop snippet",
                    "for ${1:i} in ${2:0}..${3:n} {\n\t${0}\n}", 2, "2_for ..");
                append_item("if", 15, "if condition { ... }", "Conditional statement snippet",
                    "if ${1:condition} {\n\t${0}\n}", 2, "2_if");
                append_item("defer", 15, "defer <expr>", "Schedules statement to execute when enclosing function returns.",
                    "defer ${0}", 2, "2_defer");
                append_item("defer {}", 15, "defer { ... }", "Schedules a multi-statement cleanup block on scope exit.",
                    "defer {\n\t${0}\n}", 2, "2_defer {}");
                append_item("test", 15, "test \"name\" { ... }", "Unit test block snippet",
                    "test \"${1:name}\" {\n\t${0}\n}", 2, "2_test");
                append_item("fuzz", 15, "fuzz \"name\" |x: i64| { ... }", "Property-based fuzz test with counterexample shrinking",
                    "fuzz \"${1:name}\" |${2:x: i64}| {\n\t${0}\n}", 2, "2_fuzz");
                append_item("bench", 15, "bench \"name\" |n in [64, 1024]| { ... }", "Statistical micro-benchmark with parameter variations",
                    "bench \"${1:name}\" |${2:n in [64, 1024]}| {\n\tblack_box(${0})\n}", 2, "2_bench");
                append_item("cpp", 15, "cpp { ... }", "Direct inline C++23 emission block.",
                    "cpp {\n\t${0}\n}", 2, "2_cpp");
            } else {
                for (const char* kw : {"fn", "type struct", "type enum", "match", "for", "if", "defer", "test", "fuzz", "bench", "cpp"}) {
                    append_item(kw, 14, std::format("{} keyword", kw), std::format("Language construct `{}`", kw),
                        kw, 1, std::format("2_{}", kw));
                }
            }
        } else {
            append_item("if", 14, "if condition { a } else { b }", "Conditional expression/statement", "if", 1, "2_if");
            append_item("match", 14, "match expr { ... }", "Pattern match expression", "match", 1, "2_match");
        }
    }

    // Tiers 3-7: Stdlib module aliases (3_ imported, 5_ unimported) & functions/consts (4_ imported, 7_ auto-import)
    for (const auto& mod : cfire::doc::stdlib_catalog()) {
        bool imported = is_module_imported(mod.import_path, mod.ns_alias);
        if (mod.import_path == "core:build" && !is_build_cf && !imported) continue;

        std::string active_alias = mod.ns_alias;
        if (imported && ast_res) {
            for (const auto& imp : ast_res->imports) {
                if (!imp.is_cpp && (imp.path == mod.import_path ||
                    std::ranges::find(mod.aliases, imp.path) != mod.aliases.end())) {
                    if (!imp.alias.empty()) active_alias = imp.alias;
                    break;
                }
            }
        }

        std::string add_edit = imported ? "" : compute_import_edit(lines, mod.import_path);
        std::string label_desc = imported ? mod.import_path : (mod.import_path + " (auto-import)");
        std::string mod_detail = imported ? mod.import_path : (mod.import_path + " (auto-import)");

        if (imported || !local_names.contains(active_alias)) {
            append_item(active_alias, 9, mod_detail, mod.title, active_alias, 1,
                std::format("{}_{}", imported ? "3" : "5", active_alias), active_alias, label_desc, add_edit);
        }

        for (const auto& fn : mod.functions) {
            if (fn.name.find('.') != std::string::npos) continue;
            if (fn.cf_sig.starts_with("type ")) continue;
            std::string call_name = active_alias + "." + fn.name;
            bool is_const = fn.cf_sig.starts_with("const ");
            std::string fn_detail = imported
                ? std::format("{}: {}", active_alias, fn.cf_sig)
                : std::format("{} (auto-import): {}", mod.import_path, fn.cf_sig);
            std::string fn_doc = std::format("{}\n\nSignature: `{}`\nC++23: `{}`\nExample: `{}`",
                fn.summary, fn.cf_sig, fn.cpp_lowering, fn.example);
            if (!imported) {
                fn_doc += std::format("\n\n*Selecting this will automatically insert `import \"{}\"`.*", mod.import_path);
            }
            std::string filter = fn.name + " " + call_name;
            auto [ins, fmt] = is_const
                ? std::pair<std::string, int>{call_name, 1}
                : make_sig_snippet(call_name, fn.cf_sig);
            append_item(fn.name, is_const ? 21 : 3, fn_detail, fn_doc, ins, fmt,
                std::format("{}_{}", imported ? "4" : "7", fn.name), filter, label_desc, add_edit);
        }
    }

    out += "]";
    return {out, true};
}

CFIRE_LSP_FEAT_INLINE auto Server::compute_completions(const std::string& uri, int line, int character) -> std::string {
    return compute_completions_list(uri, line, character).items_json;
}

#undef CFIRE_LSP_FEAT_INLINE

} // namespace cfire::lsp
