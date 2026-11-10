#pragma once
// src/formatter.hpp — Canonical Code Formatter (`cfire fmt`) & AST Pretty-Printer

#include "parser.hpp"
#include "../runtime/cfire_prelude.hpp"
#include <algorithm>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#if __has_include(<print>)
#include <print>
#endif
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace cfire::fmt {

using namespace cfire::frontend;

[[nodiscard]] inline auto format_type_ref(const TypeRef& t) -> std::string {
    std::string prefix;
    if (t.is_mut_borrow) {
        prefix = "&mut ";
    } else if (t.is_borrow) {
        prefix = "&";
    }

    std::string base;
    if (t.is_tuple) {
        base = "(";
        for (std::size_t i = 0; i < t.tuple_types.size(); ++i) {
            if (i > 0) base += ", ";
            base += format_type_ref(t.tuple_types[i]);
        }
        base += ")";
    } else if (t.is_fn) {
        base = "fn(";
        for (std::size_t i = 0; i < t.fn_params.size(); ++i) {
            if (i > 0) base += ", ";
            base += format_type_ref(t.fn_params[i]);
        }
        base += ")";
        if (t.fn_ret && (t.fn_ret->name != "void" || t.fn_ret->is_fallible || t.fn_ret->is_optional)) {
            base += " -> " + format_type_ref(*t.fn_ret);
        }
    } else if (!t.smart_wrap.empty() && t.elem_type) {
        base = std::format("{}[{}]", t.smart_wrap, format_type_ref(*t.elem_type));
    } else if (t.is_slice && t.elem_type) {
        base = std::format("[]{}", format_type_ref(*t.elem_type));
    } else if (t.is_array && t.elem_type) {
        base = std::format("[{}]{}", t.array_len, format_type_ref(*t.elem_type));
    } else if (t.is_map && t.key_type && t.elem_type) {
        base = std::format("map[{}]{}", format_type_ref(*t.key_type), format_type_ref(*t.elem_type));
    } else if (t.is_mut_ptr && t.elem_type) {
        base = std::format("*mut {}", format_type_ref(*t.elem_type));
    } else if (t.is_ptr && t.elem_type) {
        base = std::format("*{}", format_type_ref(*t.elem_type));
    } else {
        std::string raw_name = t.name.empty() ? "void" : t.name;
        if (!raw_name.starts_with("cpp::") && raw_name.contains("::")) {
            std::string normalized;
            for (std::size_t i = 0; i < raw_name.size(); ++i) {
                if (i + 1 < raw_name.size() && raw_name[i] == ':' && raw_name[i + 1] == ':') {
                    normalized += '.';
                    ++i;
                } else {
                    normalized += raw_name[i];
                }
            }
            raw_name = std::move(normalized);
        }
        if (!t.is_borrow && !t.is_mut_borrow && !t.type_args.empty()) {
            std::string targs;
            for (std::size_t i = 0; i < t.type_args.size(); ++i) {
                if (i > 0) targs += ", ";
                targs += format_type_ref(t.type_args[i]);
            }
            base = std::format("{}[{}]", raw_name, targs);
        } else if (!t.is_borrow && !t.is_mut_borrow && t.elem_type) {
            base = std::format("{}[{}]", raw_name, format_type_ref(*t.elem_type));
        } else {
            base = std::move(raw_name);
        }
    }
    if (t.is_optional) base += "?";
    if (t.is_fallible) base += "!";
    return prefix + base;
}

[[nodiscard]] inline auto format_fn_signature(const FnDecl& fn) -> std::string {
    std::string out = "fn ";
    if (fn.receiver) {
        if (fn.receiver->intent == "mut") {
            out += std::format("(mut {}: {}) ", fn.receiver->name, format_type_ref(fn.receiver->type));
        } else {
            out += std::format("({}: {}) ", fn.receiver->name, format_type_ref(fn.receiver->type));
        }
    }
    out += fn.name;
    if (!fn.type_params.empty()) {
        out += "[";
        for (std::size_t i = 0; i < fn.type_params.size(); ++i) {
            if (i > 0) out += ", ";
            out += fn.type_params[i].name;
            if (!fn.type_params[i].constraint.empty()) {
                out += ": " + fn.type_params[i].constraint;
            }
        }
        out += "]";
    }
    out += "(";
    for (std::size_t i = 0; i < fn.params.size(); ++i) {
        if (i > 0) out += ", ";
        const auto& p = fn.params[i];
        if (p.intent != "in" && !p.intent.empty()) {
            out += p.intent + " ";
        }
        out += std::format("{}: {}", p.name, format_type_ref(p.type));
    }
    out += ")";
    if (fn.return_type.name != "void" || fn.return_type.is_fallible || fn.return_type.is_optional) {
        out += " -> " + format_type_ref(fn.return_type);
    }
    return out;
}

[[nodiscard]] inline auto format_struct_signature(const StructDecl& st) -> std::string {
    std::string out = std::format("type {} struct {{\n", st.name);
    std::size_t max_name = 0;
    std::size_t max_type = 0;
    for (const auto& f : st.fields) {
        max_name = std::max(max_name, f.name.size());
        max_type = std::max(max_type, format_type_ref(f.type).size());
    }
    for (const auto& f : st.fields) {
        std::string t_str = format_type_ref(f.type);
        out += std::format("    {}: {}", f.name, std::string(max_name - f.name.size(), ' '));
        if (!f.raw_tag.empty()) {
            out += std::format("{}{} `{}`\n", t_str, std::string(max_type - t_str.size(), ' '), f.raw_tag);
        } else {
            out += t_str + "\n";
        }
    }
    out += "}";
    return out;
}

[[nodiscard]] inline auto format_enum_signature(const EnumDecl& en) -> std::string {
    std::string out = std::format("type {} enum {{\n", en.name);
    for (const auto& v : en.variants) {
        out += "    " + v.name;
        if (!v.payload_types.empty()) {
            out += "(";
            for (std::size_t i = 0; i < v.payload_types.size(); ++i) {
                if (i > 0) out += ", ";
                out += format_type_ref(v.payload_types[i]);
            }
            out += ")";
        }
        out += ",\n";
    }
    out += "}";
    return out;
}

namespace detail {

[[nodiscard]] inline auto trim_ws(std::string_view sv) -> std::string {
    std::size_t b = 0;
    while (b < sv.size() && (sv[b] == ' ' || sv[b] == '\t' || sv[b] == '\r')) ++b;
    std::size_t e = sv.size();
    while (e > b && (sv[e - 1] == ' ' || sv[e - 1] == '\t' || sv[e - 1] == '\r')) --e;
    return std::string(sv.substr(b, e - b));
}

struct StructFieldLine {
    bool is_comment_only = false;
    std::string name;
    std::string type_and_default;
    std::string tag;
    std::string comment;
};

[[nodiscard]] inline auto find_comment_start(std::string_view s) -> std::string_view::size_type {
    bool in_str = false;
    bool in_tag = false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (in_str) {
            if (c == '\\' && i + 1 < s.size()) {
                ++i;
            } else if (c == '"') {
                in_str = false;
            }
            continue;
        }
        if (in_tag) {
            if (c == '`') in_tag = false;
            continue;
        }
        if (c == '"') {
            in_str = true;
            continue;
        }
        if (c == '`') {
            in_tag = true;
            continue;
        }
        if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') {
            return i;
        }
    }
    return std::string_view::npos;
}

[[nodiscard]] inline auto try_parse_struct_field_line(const std::string& trimmed, StructFieldLine& out) -> bool {
    if (trimmed.empty() || trimmed == "}") return false;
    if (trimmed.starts_with("//")) {
        out = StructFieldLine{true, "", "", "", trimmed};
        return true;
    }
    auto colon = trimmed.find(':');
    if (colon == std::string::npos || colon == 0) return false;
    std::string fname = trim_ws(std::string_view(trimmed).substr(0, colon));
    for (char c : fname) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
    }
    std::string rest = trim_ws(std::string_view(trimmed).substr(colon + 1));
    std::string tag;
    std::string comment;
    // Check if a comment starts before any backtick tag (string-aware)
    auto cpos = find_comment_start(rest);
    auto b1 = rest.find('`');
    if (b1 != std::string::npos && (cpos == std::string::npos || b1 < cpos)) {
        auto b2 = rest.find('`', b1 + 1);
        if (b2 != std::string::npos) {
            tag = rest.substr(b1, b2 - b1 + 1);
            std::string after_tag = trim_ws(std::string_view(rest).substr(b2 + 1));
            if (after_tag.starts_with("//")) {
                comment = after_tag;
            }
            rest = trim_ws(std::string_view(rest).substr(0, b1));
        }
    } else if (cpos != std::string::npos) {
        comment = trim_ws(std::string_view(rest).substr(cpos));
        rest = trim_ws(std::string_view(rest).substr(0, cpos));
    }
    out = StructFieldLine{false, fname, rest, tag, comment};
    return !out.type_and_default.empty();
}

[[nodiscard]] inline auto scan_line_brace_delta(std::string_view line, std::size_t start_idx, bool& in_raw_string) -> int {
    bool in_str = false;
    int net_delta = 0;
    for (std::size_t k = start_idx; k < line.size(); ++k) {
        char c = line[k];
        if (in_str) {
            if (c == '\\' && k + 1 < line.size()) {
                ++k;
            } else if (c == '"') {
                in_str = false;
            }
            continue;
        }
        if (c == '/' && k + 1 < line.size() && line[k + 1] == '/') {
            break;
        }
        if (c == '"') {
            in_str = true;
            continue;
        }
        if (c == '\'') {
            // C++ char literal inside cpp {} block (e.g. '{', '\'', '\x7b')
            if (k + 2 < line.size() && line[k + 1] != '\\' && line[k + 2] == '\'') {
                k += 2;
                continue;
            }
            if (k + 3 < line.size() && line[k + 1] == '\\' && line[k + 3] == '\'') {
                k += 3;
                continue;
            }
        }
        if (c == '`') {
            auto next_bt = line.find('`', k + 1);
            if (next_bt != std::string_view::npos) {
                k = next_bt;
                continue;
            } else {
                in_raw_string = true;
                break;
            }
        }
        if (c == '{' || c == '(' || c == '[') ++net_delta;
        else if (c == '}' || c == ')' || c == ']') --net_delta;
    }
    return net_delta;
}

[[nodiscard]] inline auto normalize_statement_tokens(std::string_view line) -> std::string {
    if (line.empty()) return "";
    auto cpos = find_comment_start(line);
    std::string_view code = (cpos != std::string_view::npos) ? line.substr(0, cpos) : line;
    std::string_view comment = (cpos != std::string_view::npos) ? line.substr(cpos) : "";

    std::string out;
    out.reserve(code.size() + 16);

    bool in_str = false;
    bool in_char = false;
    bool in_backtick = false;

    auto is_ident_char = [](char c) -> bool {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
    };

    auto ends_with_expr_term = [&](std::string_view sv) -> bool {
        while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t')) sv.remove_suffix(1);
        if (sv.empty()) return false;
        char c = sv.back();
        if (c == ')' || c == ']' || c == '"' || c == '`') return true;
        if (is_ident_char(c)) {
            // Check if last word is a keyword that cannot end an expression (e.g. or, return, case, in)
            std::size_t start = sv.size() - 1;
            while (start > 0 && is_ident_char(sv[start - 1])) --start;
            std::string_view word = sv.substr(start);
            if (word == "or" || word == "return" || word == "case" || word == "in" || word == "throw" || word == "if" || word == "while") {
                return false;
            }
            return true;
        }
        return false;
    };

    for (std::size_t i = 0; i < code.size(); ++i) {
        char c = code[i];

        if (in_str) {
            out.push_back(c);
            if (c == '\\' && i + 1 < code.size()) {
                out.push_back(code[++i]);
            } else if (c == '"') {
                in_str = false;
            }
            continue;
        }
        if (in_char) {
            out.push_back(c);
            if (c == '\\' && i + 1 < code.size()) {
                out.push_back(code[++i]);
            } else if (c == '\'') {
                in_char = false;
            }
            continue;
        }
        if (in_backtick) {
            out.push_back(c);
            if (c == '`') in_backtick = false;
            continue;
        }

        if (c == '"') {
            in_str = true;
            out.push_back(c);
            continue;
        }
        if (c == '\'') {
            in_char = true;
            out.push_back(c);
            continue;
        }
        if (c == '`') {
            in_backtick = true;
            out.push_back(c);
            continue;
        }

        // Two-character operators: ==, !=, <=, >=, +=, -=, *=, /=, %=, &&, ||, =>, ->, :=
        if (i + 1 < code.size()) {
            std::string_view two = code.substr(i, 2);
            if (two == "::") {
                out.append("::");
                ++i;
                continue;
            }
            if (two == ":=" || two == "==" || two == "!=" || two == "<=" || two == ">=" ||
                two == "+=" || two == "-=" || two == "*=" || two == "/=" || two == "%=" ||
                two == "&&" || two == "||" || two == "=>" || two == "->") {
                if (!out.empty() && out.back() != ' ') out.push_back(' ');
                out.append(two);
                if (i + 2 < code.size() && code[i + 2] != ' ' && code[i + 2] != '\n' && code[i + 2] != '\r') {
                    out.push_back(' ');
                }
                ++i;
                continue;
            }
        }

        // Single character punctuation
        if (c == ',') {
            while (!out.empty() && out.back() == ' ') out.pop_back();
            out.push_back(',');
            if (i + 1 < code.size() && code[i + 1] != ' ' && code[i + 1] != '\n' && code[i + 1] != '\r' && code[i + 1] != ')' && code[i + 1] != ']') {
                out.push_back(' ');
            }
            continue;
        }

        if (c == ':') {
            out.push_back(':');
            if (i + 1 < code.size() && code[i + 1] != ' ' && code[i + 1] != ':' && code[i + 1] != '=' && code[i + 1] != '\n' && code[i + 1] != '\r') {
                out.push_back(' ');
            }
            continue;
        }

        if (c == '=') {
            if (!out.empty() && out.back() != ' ') out.push_back(' ');
            out.push_back('=');
            if (i + 1 < code.size() && code[i + 1] != ' ' && code[i + 1] != '\n' && code[i + 1] != '\r') {
                out.push_back(' ');
            }
            continue;
        }

        // Infix binary operators: +, -, *, /, %
        if (c == '+' || c == '-' || c == '*' || c == '/' || c == '%') {
            bool preceded = ends_with_expr_term(out);
            std::size_t next_i = i + 1;
            while (next_i < code.size() && (code[next_i] == ' ' || code[next_i] == '\t')) ++next_i;
            bool followed = next_i < code.size() && (is_ident_char(code[next_i]) || code[next_i] == '(' || code[next_i] == '[');
            if (preceded && followed) {
                if (!out.empty() && out.back() != ' ') out.push_back(' ');
                out.push_back(c);
                if (code[i + 1] != ' ' && code[i + 1] != '\t') {
                    out.push_back(' ');
                }
                continue;
            }
        }

        out.push_back(c);
    }

    while (!out.empty() && (out.back() == ' ' || out.back() == '\t')) {
        out.pop_back();
    }

    if (!comment.empty()) {
        if (!out.empty()) out.push_back(' ');
        out.append(comment);
    }
    return out;
}

inline auto format_lines_internal(const std::vector<std::string>& raw_lines) -> std::vector<std::string> {
    std::vector<std::string> out_lines(raw_lines.size());
    int indent = 0;
    bool in_raw_string = false;
    int in_cpp_depth = 0;

    for (std::size_t i = 0; i < raw_lines.size(); ++i) {
        const std::string& raw = raw_lines[i];
        if (in_raw_string) {
            out_lines[i] = raw;
            auto close_bt = raw.find('`');
            if (close_bt != std::string::npos) {
                in_raw_string = false;
                int net_delta = scan_line_brace_delta(raw, close_bt + 1, in_raw_string);
                indent = std::max(0, indent + net_delta);
            }
            continue;
        }

        std::string trimmed = trim_ws(raw);
        if (trimmed.empty()) {
            out_lines[i] = "";
            continue;
        }

        if (trimmed.starts_with("cpp {") || trimmed.contains(" cpp {")) {
            in_cpp_depth += 1;
        }

        // Check if this starts a `type <Name> struct {` block whose fields we can column-align
        if (trimmed.starts_with("type ") && trimmed.ends_with("struct {")) {
            std::size_t j = i + 1;
            std::vector<StructFieldLine> fields;
            bool pure_fields = true;
            std::size_t real_field_count = 0;
            while (j < raw_lines.size()) {
                std::string ft = trim_ws(raw_lines[j]);
                if (ft == "}") break;
                if (ft.empty()) {
                    ++j;
                    continue;
                }
                StructFieldLine sfl;
                if (try_parse_struct_field_line(ft, sfl)) {
                    if (!sfl.is_comment_only) ++real_field_count;
                    fields.push_back(std::move(sfl));
                } else {
                    pure_fields = false;
                    break;
                }
                ++j;
            }
            if (pure_fields && j < raw_lines.size() && real_field_count > 0) {
                out_lines[i] = std::string(indent * 4, ' ') + trimmed;
                std::size_t max_name = 0;
                std::size_t max_type = 0;
                bool any_tags_or_comments = false;
                for (const auto& f : fields) {
                    if (f.is_comment_only) continue;
                    max_name = std::max(max_name, f.name.size());
                    max_type = std::max(max_type, f.type_and_default.size());
                    if (!f.tag.empty() || !f.comment.empty()) any_tags_or_comments = true;
                }
                std::size_t field_idx = 0;
                for (std::size_t row = i + 1; row < j; ++row) {
                    if (trim_ws(raw_lines[row]).empty()) {
                        out_lines[row] = "";
                        continue;
                    }
                    const auto& f = fields[field_idx++];
                    std::string line_str = std::string((indent + 1) * 4, ' ');
                    if (f.is_comment_only) {
                        line_str += f.comment;
                    } else {
                        line_str += f.name + ": ";
                        if (real_field_count > 1 && any_tags_or_comments) {
                            line_str += std::string(max_name - f.name.size(), ' ');
                        }
                        line_str += f.type_and_default;
                        if (!f.tag.empty()) {
                            line_str += std::string(max_type - f.type_and_default.size() + 1, ' ') + f.tag;
                        }
                        if (!f.comment.empty()) {
                            line_str += " " + f.comment;
                        }
                    }
                    out_lines[row] = line_str;
                }
                out_lines[j] = std::string(indent * 4, ' ') + "}";
                i = j;
                continue;
            }
        }

        if (in_cpp_depth == 0 && !trimmed.starts_with("//") && !trimmed.starts_with("/*")) {
            trimmed = normalize_statement_tokens(trimmed);
        }

        // Count leading closing braces on this line
        int leading_close = 0;
        for (char c : trimmed) {
            if (c == '}' || c == ')' || c == ']') {
                ++leading_close;
            } else {
                break;
            }
        }
        int line_indent = std::max(0, indent - leading_close);
        out_lines[i] = std::string(line_indent * 4, ' ') + trimmed;

        // Update net brace depth ignoring strings, char literals, and comments
        int net_delta = scan_line_brace_delta(trimmed, 0, in_raw_string);
        indent = std::max(0, indent + net_delta);
        if (in_cpp_depth > 0 && net_delta < 0) {
            in_cpp_depth = std::max(0, in_cpp_depth + net_delta);
        }
    }
    return out_lines;
}

} // namespace detail

// Canonically formats a `.cf` source string:
// 1. Validates syntax via Lexer + Parser.
// 2. Preserves all comments, raw strings, and inline `cpp { ... }` blocks.
// 3. Normalizes 4-space block indentation, struct field + tag column alignment, and blank lines.
// 4. Verifies post-format AST roundtrip invariant so formatting can never corrupt source code.
[[nodiscard]] inline auto format_source(std::string_view src, std::string_view filename = "<input>")
    -> std::expected<std::string, std::string>
{
    auto ast_res = parse_source(filename, src);
    if (!ast_res) {
        return std::unexpected(ast_res.error());
    }

    std::vector<std::string> raw_lines;
    {
        std::istringstream iss{std::string(src)};
        std::string l;
        while (std::getline(iss, l)) {
            if (!l.empty() && l.back() == '\r') l.pop_back();
            raw_lines.push_back(l);
        }
    }

    auto formatted_lines = detail::format_lines_internal(raw_lines);
    std::string out;
    bool prev_blank = false;
    for (const auto& line : formatted_lines) {
        if (line.empty()) {
            if (!prev_blank && !out.empty()) {
                out += "\n";
                prev_blank = true;
            }
            continue;
        }
        prev_blank = false;
        out += line + "\n";
    }

    while (out.size() >= 2 && out.ends_with("\n\n")) {
        out.pop_back();
    }
    if (!out.empty() && !out.ends_with("\n")) {
        out += "\n";
    }

    // Token-stream invariance check (CF-096): verify non-whitespace tokens match identically
    auto orig_toks = cfire::frontend::tokenize(filename, src);
    auto fmt_toks = cfire::frontend::tokenize(filename, out);
    if (!orig_toks || !fmt_toks || orig_toks->size() != fmt_toks->size()) {
        return std::unexpected("formatter roundtrip verification failed: token stream count mismatch");
    }
    for (std::size_t ti = 0; ti < orig_toks->size(); ++ti) {
        if ((*orig_toks)[ti].kind != (*fmt_toks)[ti].kind || (*orig_toks)[ti].lexeme != (*fmt_toks)[ti].lexeme) {
            return std::unexpected(std::format("formatter roundtrip verification failed: token mismatch at token index {}", ti));
        }
    }

    // Post-format AST verification invariant: never return formatted code that fails to parse or alters declaration counts
    auto verify_ast = parse_source(filename, out);
    if (!verify_ast ||
        verify_ast->structs.size() != ast_res->structs.size() ||
        verify_ast->enums.size() != ast_res->enums.size() ||
        verify_ast->functions.size() != ast_res->functions.size() ||
        verify_ast->tests.size() != ast_res->tests.size()) {
        return std::unexpected("formatter roundtrip verification failed");
    }
    return out;
}

// Formats a bounded range of lines [start_line, end_line] in a source string:
// Determines indentation from preceding lines and normalizes formatting only within the selected range.
[[nodiscard]] inline auto format_range(std::string_view src, int start_line, int end_line, std::string_view filename = "<input>")
    -> std::expected<std::string, std::string>
{
    if (src.empty()) return "";
    auto ast_check = parse_source(filename, src);
    if (!ast_check) {
        return std::unexpected(std::format("cannot format range on unparseable source: {}", ast_check.error()));
    }
    std::vector<std::string> raw_lines;
    {
        std::istringstream iss{std::string(src)};
        std::string l;
        while (std::getline(iss, l)) {
            if (!l.empty() && l.back() == '\r') l.pop_back();
            raw_lines.push_back(l);
        }
    }
    if (raw_lines.empty()) return "";

    start_line = std::clamp(start_line, 0, static_cast<int>(raw_lines.size()) - 1);
    end_line = std::clamp(end_line, start_line, static_cast<int>(raw_lines.size()) - 1);

    auto formatted_lines = detail::format_lines_internal(raw_lines);
    std::string out;
    for (int i = start_line; i <= end_line; ++i) {
        out += formatted_lines[static_cast<std::size_t>(i)];
        if (i < end_line) {
            out += "\n";
        }
    }
    return out;
}

[[nodiscard]] inline auto format_path(const std::filesystem::path& target, bool check_only, bool to_stdout)
    -> std::expected<int, std::string>
{
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    if (std::filesystem::is_directory(target, ec)) {
        for (auto it = std::filesystem::recursive_directory_iterator(target, ec);
             it != std::filesystem::recursive_directory_iterator();
             it.increment(ec)) {
            if (ec) break;
            if (it->is_directory(ec)) {
                auto dn = it->path().filename().string();
                if (dn == ".cfire" || dn == ".tako" || dn == ".git" ||
                    dn == "dist" || dn == "vendor" || dn == "node_modules" ||
                    dn == "ejected_cpp") {
                    it.disable_recursion_pending();
                }
                continue;
            }
            if (it->is_regular_file(ec) && it->path().extension() == ".cf") {
                files.push_back(it->path());
            }
        }
        std::sort(files.begin(), files.end());
    } else if (std::filesystem::exists(target, ec)) {
        files.push_back(target);
    } else {
        return std::unexpected(std::format("path not found: {}", target.string()));
    }

    int changed_count = 0;
    std::string first_error;
    for (const auto& f : files) {
        std::ifstream ifs(f);
        std::stringstream ss;
        ss << ifs.rdbuf();
        std::string orig = ss.str();

        auto formatted = format_source(orig, f.string());
        if (!formatted) {
            if (first_error.empty()) first_error = std::format("{}: {}", f.string(), formatted.error());
            continue;
        }
        if (to_stdout) {
            std::print("{}", *formatted);
            continue;
        }
        if (*formatted != orig) {
            ++changed_count;
            if (!check_only) {
                auto tmp = f;
                tmp += ".cffmt.tmp";
                {
                    std::ofstream ofs(tmp, std::ios::trunc | std::ios::binary);
                    ofs << *formatted;
                }
                std::error_code rename_ec;
                std::filesystem::rename(tmp, f, rename_ec);
                if (rename_ec) {
                    std::filesystem::remove(tmp, rename_ec);
                    if (first_error.empty()) {
                        first_error = std::format("failed to atomic write {}: {}", f.string(), rename_ec.message());
                    }
                }
            }
        }
    }
    if (!first_error.empty()) {
        return std::unexpected(first_error);
    }
    return changed_count;
}

} // namespace cfire::fmt
