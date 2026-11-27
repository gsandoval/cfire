#pragma once
// src/formatter.hpp — Canonical Code Formatter (`cfire fmt`) & AST Pretty-Printer

#include "ast.hpp"
#if !defined(CFIRE_SPLIT_TUS) || defined(CFIRE_FMT_IMPL)
#include "parser.hpp"
#endif
#include "../runtime/cfire_prelude.hpp"
#include <algorithm>
#include <cctype>
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

#if !defined(CFIRE_SPLIT_TUS) || defined(CFIRE_FMT_IMPL)
#include "formatter_printer.hpp"
#endif

#ifdef CFIRE_SPLIT_TUS
#define CFIRE_FMT_INLINE
#else
#define CFIRE_FMT_INLINE inline
#endif

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
        std::string raw_name = !t.raw_name.empty() ? t.raw_name : (t.name.empty() ? "void" : t.name);
        if (raw_name.starts_with("cpp::")) {
            raw_name = "cpp." + raw_name.substr(5);
        }
        if (!raw_name.starts_with("cpp.") && raw_name.contains("::")) {
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

[[nodiscard]] inline auto format_type_params_list(const std::vector<TypeParam>& tps) -> std::string {
    if (tps.empty()) return "";
    std::string out = "[";
    for (std::size_t i = 0; i < tps.size(); ++i) {
        if (i > 0) out += ", ";
        out += tps[i].name;
        if (!tps[i].constraints.empty()) {
            out += ": ";
            for (std::size_t c = 0; c < tps[i].constraints.size(); ++c) {
                if (c > 0) out += " + ";
                out += tps[i].constraints[c];
            }
        }
    }
    out += "]";
    return out;
}

[[nodiscard]] inline auto format_interface_method_sig(const InterfaceMethodSig& m) -> std::string {
    std::string out = "fn ";
    if (m.is_mut_receiver) out += "mut ";
    out += m.name + "(";
    for (std::size_t p = 0; p < m.param_types.size(); ++p) {
        if (p > 0) out += ", ";
        if (p < m.param_intents.size() && !m.param_intents[p].empty() && m.param_intents[p] != "in") {
            out += m.param_intents[p] + " ";
        }
        if (p < m.param_names.size() && !m.param_names[p].empty()) {
            out += m.param_names[p] + ": ";
        }
        out += format_type_ref(m.param_types[p]);
    }
    out += ")";
    if (m.return_type.name != "void" || m.return_type.is_fallible || m.return_type.is_optional) {
        out += " -> " + format_type_ref(m.return_type);
    }
    return out;
}

[[nodiscard]] inline auto format_interface_signature(const InterfaceDecl& ifc) -> std::string {
    std::string out = std::format("{}type {} interface {{\n", ifc.is_pub ? "pub " : "", ifc.name);
    for (const auto& m : ifc.methods) {
        out += "    " + format_interface_method_sig(m) + "\n";
    }
    out += "}";
    return out;
}

[[nodiscard]] inline auto format_fn_signature(const FnDecl& fn) -> std::string {
    std::string out = fn.is_pub ? "pub fn " : "fn ";
    if (fn.receiver) {
        if (fn.receiver->intent == "mut") {
            out += std::format("(mut {}: {}) ", fn.receiver->name, format_type_ref(fn.receiver->type));
        } else {
            out += std::format("({}: {}) ", fn.receiver->name, format_type_ref(fn.receiver->type));
        }
    }
    out += fn.name;
    out += format_type_params_list(fn.type_params);
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
    std::string out = std::format("{}type {}{} struct {{\n", st.is_pub ? "pub " : "", st.name, format_type_params_list(st.type_params));
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
    std::string out = std::format("{}type {}{} enum {{\n", en.is_pub ? "pub " : "", en.name, format_type_params_list(en.type_params));
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
    while (b < sv.size() && std::isspace(static_cast<unsigned char>(sv[b])) != 0) ++b;
    std::size_t e = sv.size();
    while (e > b && std::isspace(static_cast<unsigned char>(sv[e - 1])) != 0) --e;
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
    std::vector<cfire::frontend::CommentSpan> f_comments;
    auto f_toks = cfire::frontend::tokenize("<field>", trimmed, &f_comments);
    if (!f_toks) return false;
    for (const auto& c : f_comments) {
        if (c.block) return false;
    }
    if (f_comments.size() > 1) return false;

    const auto& toks = *f_toks;
    // tokenize() always appends a final TokenKind::Eof.
    if (toks.size() == 1) {
        if (f_comments.size() == 1 && !f_comments[0].block) {
            out = StructFieldLine{true, "", "", "", trim_ws(trimmed)};
            return true;
        }
        return false;
    }
    if (toks.size() < 4) return false;
    if (toks[0].kind != TokenKind::Ident || toks[1].kind != TokenKind::Colon) return false;
    const std::size_t last_tok_idx = toks.size() - 2;
    if (toks[last_tok_idx].kind == TokenKind::Comma || toks[last_tok_idx].kind == TokenKind::Semicolon) {
        return false;
    }

    const bool has_tag = (last_tok_idx >= 3 &&
                          toks[last_tok_idx].kind == TokenKind::RawString &&
                          toks[last_tok_idx - 1].kind != TokenKind::Assign);
    if (toks[2].line != 1 || toks[last_tok_idx].line != 1 || toks[2].col < 1 || toks[last_tok_idx].col < 1) {
        return false;
    }
    const std::size_t type_start = static_cast<std::size_t>(toks[2].col - 1);
    const std::size_t tag_start = static_cast<std::size_t>(toks[last_tok_idx].col - 1);
    if (type_start >= trimmed.size() || tag_start >= trimmed.size()) return false;
    if (!f_comments.empty() && f_comments[0].begin > trimmed.size()) return false;
    if (has_tag && !f_comments.empty() && f_comments[0].begin < tag_start) return false;

    const std::size_t comment_pos = !f_comments.empty() ? f_comments[0].begin : trimmed.size();
    const std::size_t type_end = has_tag ? tag_start : comment_pos;
    if (type_end < type_start) return false;

    std::string type_and_default = trim_ws(std::string_view(trimmed).substr(type_start, type_end - type_start));
    std::string tag;
    if (has_tag) {
        if (comment_pos < tag_start) return false;
        tag = trim_ws(std::string_view(trimmed).substr(tag_start, comment_pos - tag_start));
    }
    std::string comment;
    if (!f_comments.empty()) {
        if (f_comments[0].end > trimmed.size() || f_comments[0].end < f_comments[0].begin) return false;
        comment = trim_ws(std::string_view(trimmed).substr(f_comments[0].begin, f_comments[0].end - f_comments[0].begin));
    }
    if (type_and_default.empty()) return false;

    out = StructFieldLine{false, toks[0].lexeme, std::move(type_and_default), std::move(tag), std::move(comment)};
    return true;
}

// Net `{([` minus `})]` on a line, skipping strings, char literals, comments and raw strings (LSP on-type indent).
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

// Column-aligns the fields of every `type X struct { ... }` block whose body is only fields and comments.
inline void align_struct_fields(std::vector<std::string>& lines, const std::vector<bool>& unsafe_line = {}) {
    for (std::size_t i = 0; i < lines.size(); ++i) {
        std::string head = trim_ws(lines[i]);
        if (!(head.starts_with("type ") && head.ends_with("struct {"))) continue;
        const std::size_t indent = lines[i].find_first_not_of(' ');
        std::vector<StructFieldLine> fields;
        std::size_t j = i + 1;
        bool pure = i >= unsafe_line.size() || !unsafe_line[i];
        std::size_t real = 0;
        for (; pure && j < lines.size(); ++j) {
            if (j < unsafe_line.size() && unsafe_line[j]) { pure = false; break; }
            std::string ft = trim_ws(lines[j]);
            if (ft == "}") break;
            if (ft.empty()) continue;
            StructFieldLine sfl;
            if (!try_parse_struct_field_line(ft, sfl)) { pure = false; break; }
            if (!sfl.is_comment_only) ++real;
            fields.push_back(std::move(sfl));
        }
        if (!pure || j >= lines.size() || real == 0) continue;
        std::size_t max_name = 0;
        std::size_t max_type = 0;
        bool tags_or_comments = false;
        for (const auto& f : fields) {
            if (f.is_comment_only) continue;
            max_name = std::max(max_name, f.name.size());
            max_type = std::max(max_type, f.type_and_default.size());
            if (!f.tag.empty() || !f.comment.empty()) tags_or_comments = true;
        }
        std::size_t fi = 0;
        for (std::size_t row = i + 1; row < j; ++row) {
            if (trim_ws(lines[row]).empty()) { lines[row].clear(); continue; }
            const auto& f = fields[fi++];
            std::string l(indent + 4, ' ');
            if (f.is_comment_only) {
                l += f.comment;
            } else {
                l += f.name + ": ";
                if (real > 1 && tags_or_comments) l += std::string(max_name - f.name.size(), ' ');
                l += f.type_and_default;
                if (!f.tag.empty()) l += std::string(max_type - f.type_and_default.size() + 1, ' ') + f.tag;
                if (!f.comment.empty()) l += " " + f.comment;
            }
            lines[row] = std::move(l);
        }
        i = j;
    }
}

} // namespace detail

[[nodiscard]] CFIRE_FMT_INLINE auto format_source(std::string_view src, std::string_view filename = "<input>")
    -> std::expected<std::string, std::string>;

[[nodiscard]] CFIRE_FMT_INLINE auto format_range(std::string_view src, int start_line, int end_line, std::string_view filename = "<input>")
    -> std::expected<std::string, std::string>;

[[nodiscard]] CFIRE_FMT_INLINE auto format_path(const std::filesystem::path& target, bool check_only, bool to_stdout)
    -> std::expected<int, std::string>;

#if !defined(CFIRE_SPLIT_TUS) || defined(CFIRE_FMT_IMPL)

// Canonically formats a `.cf` source string:
// 1. Validates syntax via Lexer + Parser.
// 2. Preserves all comments, raw strings, and inline `cpp { ... }` blocks.
// 3. Normalizes 4-space block indentation, struct field + tag column alignment, and blank lines.
// 4. Verifies post-format AST roundtrip invariant so formatting can never corrupt source code.
[[nodiscard]] CFIRE_FMT_INLINE auto format_source(std::string_view src, std::string_view filename)
    -> std::expected<std::string, std::string>
{
    auto ast_res = parse_source(filename, src);
    if (!ast_res) {
        return std::unexpected(ast_res.error());
    }

    std::vector<cfire::frontend::CommentSpan> comments;
    auto src_toks = cfire::frontend::tokenize(filename, src, &comments);
    if (!src_toks) return std::unexpected(src_toks.error().text());
    std::string printed = printer::format(src, *src_toks, comments);

    // Struct field columns are aligned as a final pass over the printed lines.
    std::vector<std::string> lines;
    std::vector<std::size_t> line_starts;
    {
        std::size_t pos = 0;
        while (pos <= printed.size()) {
            auto nl = printed.find('\n', pos);
            if (nl == std::string::npos) {
                if (pos < printed.size()) {
                    line_starts.push_back(pos);
                    lines.push_back(printed.substr(pos));
                }
                break;
            }
            line_starts.push_back(pos);
            lines.push_back(printed.substr(pos, nl - pos));
            pos = nl + 1;
        }
    }
    std::vector<bool> line_has_multiline_or_block_comment(lines.size(), false);
    if (!lines.empty()) {
        auto line_of_offset = [&](std::size_t off) -> std::size_t {
            auto it = std::upper_bound(line_starts.begin(), line_starts.end(), off);
            if (it == line_starts.begin()) return 0;
            return static_cast<std::size_t>((it - line_starts.begin()) - 1);
        };
        auto mark_span = [&](std::size_t start_line, std::size_t end_line) {
            start_line = std::min(start_line, lines.size() - 1);
            end_line = std::min(end_line, lines.size() - 1);
            for (std::size_t r = start_line; r <= end_line; ++r) {
                line_has_multiline_or_block_comment[r] = true;
            }
        };
        std::vector<cfire::frontend::CommentSpan> printed_comments;
        auto printed_toks = cfire::frontend::tokenize(filename, printed, &printed_comments);
        if (!printed_toks) {
            std::fill(line_has_multiline_or_block_comment.begin(), line_has_multiline_or_block_comment.end(), true);
        } else {
            for (const auto& c : printed_comments) {
                if (c.block) {
                    const std::size_t sl = line_of_offset(c.begin);
                    const std::size_t el = line_of_offset(c.end > c.begin ? c.end - 1 : c.begin);
                    mark_span(sl, el);
                }
            }
            for (const auto& t : *printed_toks) {
                if (t.kind == TokenKind::Eof) continue;
                if (t.kind == TokenKind::RawString || t.kind == TokenKind::String || t.kind == TokenKind::CppBlock) {
                    const std::size_t nl_count = static_cast<std::size_t>(std::count(t.lexeme.begin(), t.lexeme.end(), '\n'));
                    if (nl_count > 0 && t.line >= 1) {
                        const std::size_t sl = static_cast<std::size_t>(t.line - 1);
                        mark_span(sl, sl + nl_count);
                    }
                }
            }
        }
    }
    detail::align_struct_fields(lines, line_has_multiline_or_block_comment);
    std::string out;
    for (const auto& l : lines) out += l + "\n";

    // Token-stream invariance check: verify non-whitespace tokens match identically
    // Statement `;` separators are the one token the printer may drop (it puts the statement on its own line).
    auto drop_semicolons = [](std::expected<std::vector<cfire::frontend::Token>, cfire::frontend::SourceError> t) {
        if (t) std::erase_if(*t, [](const cfire::frontend::Token& x) { return x.kind == cfire::frontend::TokenKind::Semicolon; });
        return t;
    };
    auto orig_toks = drop_semicolons(cfire::frontend::tokenize(filename, src));
    auto fmt_toks = drop_semicolons(cfire::frontend::tokenize(filename, out));
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
[[nodiscard]] CFIRE_FMT_INLINE auto format_range(std::string_view src, int start_line, int end_line, std::string_view filename)
    -> std::expected<std::string, std::string>
{
    if (src.empty()) return "";
    auto split = [](std::string_view text) {
        std::vector<std::string> lines;
        std::size_t pos = 0;
        while (pos < text.size()) {
            auto nl = text.find('\n', pos);
            std::string l(text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos));
            if (!l.empty() && l.back() == '\r') l.pop_back();
            lines.push_back(std::move(l));
            if (nl == std::string_view::npos) break;
            pos = nl + 1;
        }
        return lines;
    };
    auto formatted = format_source(src, filename);
    if (!formatted) return std::unexpected(std::format("cannot format range on unparseable source: {}", formatted.error()));
    auto orig_lines = split(src);
    auto fmt_lines = split(*formatted);
    if (orig_lines.empty()) return "";
    start_line = std::clamp(start_line, 0, static_cast<int>(orig_lines.size()) - 1);
    end_line = std::clamp(end_line, start_line, static_cast<int>(orig_lines.size()) - 1);
    // The printer keeps line structure except where it splits `;`-joined statements or collapses blank lines;
    // when the file's line count changed, the range is returned unchanged rather than mapped approximately.
    const auto& use = fmt_lines.size() == orig_lines.size() ? fmt_lines : orig_lines;
    std::string out;
    for (int i = start_line; i <= end_line; ++i) {
        out += use[static_cast<std::size_t>(i)];
        if (i < end_line) out += "\n";
    }
    return out;
}

[[nodiscard]] CFIRE_FMT_INLINE auto format_path(const std::filesystem::path& target, bool check_only, bool to_stdout)
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

#endif // !defined(CFIRE_SPLIT_TUS) || defined(CFIRE_FMT_IMPL)
#undef CFIRE_FMT_INLINE

} // namespace cfire::fmt
