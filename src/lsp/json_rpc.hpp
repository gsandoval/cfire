#pragma once
// src/lsp/json_rpc.hpp — JSON-RPC 2.0 Framing, Structure-Aware Key Extraction, Type Inference & Layout Helpers

#include "../clang_bridge.hpp"
#include "../codegen.hpp"
#include "../docgen.hpp"
#include "../formatter.hpp"
#include "../sema.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <format>
#include <functional>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cfire::lsp {
namespace detail {

[[nodiscard]] inline auto json_escape(std::string_view sv) -> std::string {
    std::string out;
    out.reserve(sv.size() + 16);
    for (char c : sv) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

[[nodiscard]] inline auto json_unescape(std::string_view sv) -> std::string {
    std::string out;
    out.reserve(sv.size());
    for (std::size_t i = 0; i < sv.size(); ++i) {
        if (sv[i] == '\\' && i + 1 < sv.size()) {
            char next = sv[++i];
            switch (next) {
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                default: out.push_back(next); break;
            }
        } else {
            out.push_back(sv[i]);
        }
    }
    return out;
}

// Structure-aware JSON key finder: scans JSON string literals from index 0 (so string literal
// boundaries are always respected) and returns the index immediately following the ':' of the
// first object key matching `key` at or after `start_pos`, or `std::string_view::npos`.
[[nodiscard]] inline auto find_json_key(std::string_view json, std::string_view key, std::size_t start_pos = 0)
    -> std::size_t
{
    std::size_t i = 0;
    const std::size_t n = json.size();
    while (i < n) {
        if (json[i] == '"') {
            std::size_t str_start = i++;
            while (i < n) {
                if (json[i] == '\\') {
                    i += 2;
                    continue;
                }
                if (json[i] == '"') break;
                ++i;
            }
            if (i >= n) return std::string_view::npos;
            std::size_t str_end = i++;
            std::size_t after = i;
            while (after < n && (json[after] == ' ' || json[after] == '\t' || json[after] == '\n' || json[after] == '\r')) {
                ++after;
            }
            if (after < n && json[after] == ':') {
                if (str_start >= start_pos && json.substr(str_start + 1, str_end - str_start - 1) == key) {
                    return after + 1;
                }
                i = after + 1;
            }
        } else {
            ++i;
        }
    }
    return std::string_view::npos;
}

[[nodiscard]] inline auto extract_json_string(std::string_view json, std::string_view key, std::size_t start_pos = 0)
    -> std::optional<std::string>
{
    std::size_t pos = find_json_key(json, key, start_pos);
    if (pos == std::string_view::npos) return std::nullopt;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) ++pos;
    if (pos >= json.size() || json[pos] != '"') return std::nullopt;
    ++pos;
    std::size_t end = pos;
    while (end < json.size()) {
        if (json[end] == '\\') {
            end += 2;
            continue;
        }
        if (json[end] == '"') break;
        ++end;
    }
    if (end >= json.size()) return std::nullopt;
    return json_unescape(json.substr(pos, end - pos));
}

[[nodiscard]] inline auto extract_json_id(std::string_view json) -> std::optional<std::string> {
    std::size_t pos = find_json_key(json, "id", 0);
    if (pos == std::string_view::npos) return std::nullopt;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) ++pos;
    if (pos >= json.size()) return std::nullopt;
    if (json[pos] == '"') {
        std::size_t end = pos + 1;
        while (end < json.size() && json[end] != '"') {
            if (json[end] == '\\') ++end;
            ++end;
        }
        if (end < json.size()) return std::string(json.substr(pos, end - pos + 1));
    } else {
        std::size_t end = pos;
        while (end < json.size() && (std::isdigit(static_cast<unsigned char>(json[end])) || json[end] == '-')) ++end;
        if (end > pos) return std::string(json.substr(pos, end - pos));
    }
    return std::nullopt;
}

[[nodiscard]] inline auto extract_json_int_after(std::string_view json, std::string_view key, std::size_t start_pos = 0)
    -> std::optional<int>
{
    std::size_t pos = find_json_key(json, key, start_pos);
    if (pos == std::string_view::npos) return std::nullopt;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) ++pos;
    std::size_t end = pos;
    while (end < json.size() && (std::isdigit(static_cast<unsigned char>(json[end])) || json[end] == '-')) ++end;
    if (end == pos) return std::nullopt;
    return std::stoi(std::string(json.substr(pos, end - pos)));
}

[[nodiscard]] inline auto extract_json_bool(std::string_view json, std::string_view key, std::size_t start_pos = 0)
    -> std::optional<bool>
{
    std::size_t pos = find_json_key(json, key, start_pos);
    if (pos == std::string_view::npos) return std::nullopt;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) ++pos;
    if (json.substr(pos).starts_with("true")) return true;
    if (json.substr(pos).starts_with("false")) return false;
    return std::nullopt;
}

[[nodiscard]] inline auto uri_to_path(std::string_view uri) -> std::string {
    if (uri.starts_with("file://")) {
        return std::string(uri.substr(7));
    }
    return std::string(uri);
}

[[nodiscard]] inline auto path_to_uri(const std::filesystem::path& p) -> std::string {
    std::error_code ec;
    auto canon = std::filesystem::weakly_canonical(p, ec);
    return "file://" + (ec ? p.string() : canon.string());
}

[[nodiscard]] inline auto split_lines(std::string_view text) -> std::vector<std::string> {
    std::vector<std::string> lines;
    std::istringstream iss{std::string(text)};
    std::string l;
    while (std::getline(iss, l)) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        lines.push_back(l);
    }
    return lines;
}

struct CursorSymbol {
    std::string qualifier; // e.g. "fs", "m_out", "checksum", "cpp", "order.customer"
    std::string name;      // e.g. "read_all", "Service", "compute_tag"
    int start_col = 0;     // 0-based start column of `name`
    int end_col = 0;       // 0-based end column of `name`
};

[[nodiscard]] inline auto extract_leading_doc_comments(const std::vector<std::string>& lines, int one_based_line) -> std::string {
    if (one_based_line <= 1 || lines.empty()) return "";
    int cur_idx = one_based_line - 2;
    std::vector<std::string> comment_lines;
    while (cur_idx >= 0 && static_cast<std::size_t>(cur_idx) < lines.size()) {
        const std::string& raw = lines[static_cast<std::size_t>(cur_idx)];
        std::size_t b = 0;
        while (b < raw.size() && (raw[b] == ' ' || raw[b] == '\t')) ++b;
        if (raw.substr(b).starts_with("//")) {
            std::string ctext = raw.substr(b + 2);
            if (!ctext.empty() && ctext.front() == ' ') ctext = ctext.substr(1);
            comment_lines.push_back(std::move(ctext));
            --cur_idx;
        } else {
            break;
        }
    }
    if (comment_lines.empty()) return "";
    std::reverse(comment_lines.begin(), comment_lines.end());
    std::string out;
    for (const auto& cl : comment_lines) {
        out += cl + "\n";
    }
    return out;
}

[[nodiscard]] inline auto symbol_at_position(const std::vector<std::string>& lines, int line, int character) -> CursorSymbol {
    if (line < 0 || static_cast<std::size_t>(line) >= lines.size()) return {};
    const std::string& ln = lines[static_cast<std::size_t>(line)];
    if (ln.empty()) return {};
    int idx = std::clamp(character, 0, static_cast<int>(ln.size()) - 1);
    auto is_ident = [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '@';
    };

    auto extract_qualifier_before_sep = [&](int sep_idx) -> std::string {
        int qend = sep_idx;
        if (qend > 0 && ln[static_cast<std::size_t>(qend - 1)] == ')') {
            // Handle `(*owned_box).Value` -> extract `owned_box`
            int p = qend - 2;
            while (p >= 0 && is_ident(ln[static_cast<std::size_t>(p)])) --p;
            int id_start = p + 1;
            if (id_start < qend - 1) {
                return ln.substr(static_cast<std::size_t>(id_start), static_cast<std::size_t>(qend - 1 - id_start));
            }
        }
        int qstart = qend - 1;
        while (qstart >= 0 && (is_ident(ln[static_cast<std::size_t>(qstart)]) || ln[static_cast<std::size_t>(qstart)] == '.')) {
            --qstart;
        }
        ++qstart;
        if (qend > qstart) {
            return ln.substr(static_cast<std::size_t>(qstart), static_cast<std::size_t>(qend - qstart));
        }
        return "";
    };

    // Check if cursor is immediately after `.` (e.g. `m_out.|`) or `::` (e.g. `cpp::|`) with no following ident
    if (character > 0 && character <= static_cast<int>(ln.size()) && ln[static_cast<std::size_t>(character - 1)] == '.' &&
        (character == static_cast<int>(ln.size()) || !is_ident(ln[static_cast<std::size_t>(character)]))) {
        return CursorSymbol{extract_qualifier_before_sep(character - 1), "", character, character};
    }
    if (character >= 2 && character <= static_cast<int>(ln.size()) &&
        ln[static_cast<std::size_t>(character - 1)] == ':' && ln[static_cast<std::size_t>(character - 2)] == ':' &&
        (character == static_cast<int>(ln.size()) || !is_ident(ln[static_cast<std::size_t>(character)]))) {
        return CursorSymbol{extract_qualifier_before_sep(character - 2), "", character, character};
    }
    if (!is_ident(ln[static_cast<std::size_t>(idx)]) && idx > 0 && is_ident(ln[static_cast<std::size_t>(idx - 1)])) {
        --idx;
    }
    if (!is_ident(ln[static_cast<std::size_t>(idx)])) {
        if (ln[static_cast<std::size_t>(idx)] == '.' && idx > 0) {
            return CursorSymbol{extract_qualifier_before_sep(idx), "", idx + 1, idx + 1};
        }
        if (ln[static_cast<std::size_t>(idx)] == ':' && idx > 0 && ln[static_cast<std::size_t>(idx - 1)] == ':') {
            return CursorSymbol{extract_qualifier_before_sep(idx - 1), "", idx + 1, idx + 1};
        }
        return {};
    }
    int start = idx;
    while (start > 0 && is_ident(ln[static_cast<std::size_t>(start - 1)])) --start;
    int end = idx;
    while (end < static_cast<int>(ln.size()) && is_ident(ln[static_cast<std::size_t>(end)])) ++end;
    std::string word = ln.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(end - start));

    std::string qual;
    if (start > 0 && ln[static_cast<std::size_t>(start - 1)] == '.') {
        qual = extract_qualifier_before_sep(start - 1);
    } else if (start > 1 && ln[static_cast<std::size_t>(start - 1)] == ':' && ln[static_cast<std::size_t>(start - 2)] == ':') {
        qual = extract_qualifier_before_sep(start - 2);
    }
    return CursorSymbol{qual, word, start, end};
}

// Identifies the enclosing function call `callee(arg0, arg1, ...)` around `(line, character)` for signatureHelp
struct CallContext {
    std::string qualifier;
    std::string callee;
    int active_param = 0;
};

[[nodiscard]] inline auto find_call_context(const std::vector<std::string>& lines, int line, int character) -> std::optional<CallContext> {
    if (line < 0 || static_cast<std::size_t>(line) >= lines.size()) return std::nullopt;
    const std::string& ln = lines[static_cast<std::size_t>(line)];
    int pos = std::clamp(character - 1, 0, static_cast<int>(ln.size()) - 1);
    int depth = 0;
    int commas = 0;
    bool in_str = false;
    for (int i = pos; i >= 0; --i) {
        char c = ln[static_cast<std::size_t>(i)];
        if (c == '"' && (i == 0 || ln[static_cast<std::size_t>(i - 1)] != '\\')) {
            in_str = !in_str;
            continue;
        }
        if (in_str) continue;
        if (c == ')' || c == ']') {
            ++depth;
        } else if (c == ',' && depth == 0) {
            ++commas;
        } else if (c == '(') {
            if (depth > 0) {
                --depth;
            } else {
                // Found opening '(' of the active call! Extract callee before '(' (skipping optional `[TypeArg]`)
                int cend = i;
                if (cend > 0 && ln[static_cast<std::size_t>(cend - 1)] == ']') {
                    int bdepth = 1;
                    cend -= 2;
                    while (cend >= 0 && bdepth > 0) {
                        if (ln[static_cast<std::size_t>(cend)] == ']') ++bdepth;
                        else if (ln[static_cast<std::size_t>(cend)] == '[') --bdepth;
                        --cend;
                    }
                    ++cend;
                }
                auto sym = symbol_at_position(lines, line, cend);
                if (!sym.name.empty()) {
                    return CallContext{sym.qualifier, sym.name, commas};
                }
                return std::nullopt;
            }
        }
    }
    return std::nullopt;
}

// Lowers a `.cf` TypeRef to its human-readable ISO C++23 representation for hover/completion cards
[[nodiscard]] inline auto lower_type_preview(const cfire::frontend::TypeRef& t, bool is_param = false, std::string_view intent = "in") -> std::string {
    auto prim = [](std::string_view n, bool param_borrow) -> std::string {
        if (n == "i8") return "std::int8_t";
        if (n == "i16") return "std::int16_t";
        if (n == "i32" || n == "int") return "std::int32_t";
        if (n == "i64") return "std::int64_t";
        if (n == "u8" || n == "byte") return "std::byte";
        if (n == "u16") return "std::uint16_t";
        if (n == "u32") return "std::uint32_t";
        if (n == "u64") return "std::uint64_t";
        if (n == "usize") return "std::size_t";
        if (n == "f32") return "float";
        if (n == "f64") return "double";
        if (n == "bool") return "bool";
        if (n == "void") return "void";
        if (n == "str") return param_borrow ? "std::string_view" : "std::string";
        if (n == "path") return param_borrow ? "const std::filesystem::path&" : "std::filesystem::path";
        if (n == "bytes") return param_borrow ? "std::span<const std::byte>" : "std::vector<std::byte>";
        return std::string(n);
    };
    bool borrow = is_param && (intent == "in" || intent.empty());
    std::string base;
    if (t.smart_wrap == "own" && t.elem_type) {
        base = std::format("std::unique_ptr<{}>", lower_type_preview(*t.elem_type));
    } else if (t.smart_wrap == "shared" && t.elem_type) {
        base = std::format("std::shared_ptr<{}>", lower_type_preview(*t.elem_type));
    } else if (t.is_slice && t.elem_type) {
        std::string el = lower_type_preview(*t.elem_type);
        base = borrow ? std::format("std::span<const {}>", el) : std::format("std::vector<{}>", el);
    } else if (t.is_array && t.elem_type) {
        base = std::format("std::array<{}, {}>", lower_type_preview(*t.elem_type), t.array_len);
    } else if (t.is_map && t.key_type && t.elem_type) {
        base = std::format("cfire::Map<{}, {}>", lower_type_preview(*t.key_type), lower_type_preview(*t.elem_type));
    } else if (t.is_tuple) {
        std::string inner;
        for (std::size_t i = 0; i < t.tuple_types.size(); ++i) {
            if (i > 0) inner += ", ";
            inner += lower_type_preview(t.tuple_types[i]);
        }
        base = std::format("std::tuple<{}>", inner);
    } else {
        base = prim(t.name, borrow);
    }
    if (t.is_optional) base = std::format("std::optional<{}>", base);
    if (t.is_fallible) base = std::format("std::expected<{}, cfire::Error>", base);
    if (is_param) {
        if (intent == "mut" || intent == "out") return base + "&";
        if (intent == "move") return base + "&&";
        if (!base.starts_with("std::string_view") && !base.starts_with("std::span") &&
            (t.is_array || t.is_map || (!t.smart_wrap.empty()) || base == t.name)) {
            return "const " + base + "&";
        }
    }
    return base;
}

[[nodiscard]] inline auto lower_fn_preview(const cfire::frontend::FnDecl& fn) -> std::string {
    std::string params;
    if (fn.receiver) {
        params += std::format("{} {}", lower_type_preview(fn.receiver->type, true, fn.receiver->intent), fn.receiver->name);
    }
    for (const auto& p : fn.params) {
        if (!params.empty()) params += ", ";
        params += std::format("{} {}", lower_type_preview(p.type, true, p.intent), p.name);
    }
    std::string ret = lower_type_preview(fn.return_type);
    std::string nodiscard = (ret == "void") ? "" : "[[nodiscard]] ";
    return std::format("{}auto {}({}) -> {}", nodiscard, fn.name, params, ret);
}

[[nodiscard]] inline auto cpp_return_to_cf(std::string_view cpp_t) -> std::string {
    std::string s(cfire::bridge::trim_qual(cpp_t));
    if (s == "void") return "void";
    if (s == "bool" || s == "_Bool") return "bool";
    if (s == "int" || s == "int32_t" || s == "std::int32_t") return "i32";
    if (s == "unsigned int" || s == "uint32_t" || s == "std::uint32_t") return "u32";
    if (s == "long" || s == "long long" || s == "int64_t" || s == "std::int64_t") return "i64";
    if (s == "unsigned long" || s == "unsigned long long" || s == "size_t" || s == "std::size_t" || s == "uint64_t") return "u64";
    if (s == "float") return "f32";
    if (s == "double") return "f64";
    if (s == "const char *") return "str";
    return s;
}

// Cross-package & Clang AST Bridge context for a document
struct ImportedPkgFile {
    std::string alias{};
    std::string import_path{};
    std::string file_path{};
    std::string uri{};
    cfire::frontend::FileAST ast{};
};

struct WorkspaceContext {
    std::vector<ImportedPkgFile> local_packages{};
    std::unordered_map<std::string, cfire::bridge::CppSymbol> cpp_symbols{};
    std::unordered_map<std::string, std::string> cpp_symbol_header{};
};

// Information about a local variable or parameter inferred inside a function/test
struct LocalBindingInfo {
    std::string name;
    std::string cf_type;
    std::string cpp_type;
    bool is_mut = false;
    bool is_param = false;
    std::string intent = "in";
    int decl_line = 1;
    int name_end_col = 1;
    bool inferred_colon_eq = false;
};

[[nodiscard]] inline auto strip_wrappers(std::string t) -> std::string {
    while (t.ends_with("!") || t.ends_with("?")) t.pop_back();
    if (t.starts_with("own[") && t.ends_with("]")) return t.substr(4, t.size() - 5);
    if (t.starts_with("shared[") && t.ends_with("]")) return t.substr(7, t.size() - 8);
    if (t.starts_with("*mut ")) return t.substr(5);
    if (t.starts_with("*")) return t.substr(1);
    return t;
}

// Resolves a potentially chained qualifier (e.g. `m_out` or `order.customer` or `(*owned_box)`) to its `.cf` struct/type name
[[nodiscard]] inline auto resolve_qualifier_type(
    std::string_view qualifier,
    const cfire::frontend::FileAST& ast,
    const WorkspaceContext& ws,
    const std::unordered_map<std::string, LocalBindingInfo>& env
) -> std::string {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start < qualifier.size()) {
        auto dot = qualifier.find('.', start);
        if (dot == std::string_view::npos) {
            parts.emplace_back(qualifier.substr(start));
            break;
        }
        parts.emplace_back(qualifier.substr(start, dot - start));
        start = dot + 1;
    }
    if (parts.empty()) return "";

    std::string cur_type = parts[0];
    if (auto it = env.find(parts[0]); it != env.end()) {
        cur_type = strip_wrappers(it->second.cf_type);
    }
    for (std::size_t i = 1; i < parts.size(); ++i) {
        const std::string& fld = parts[i];
        bool found = false;
        auto find_in_structs = [&](const std::vector<cfire::frontend::StructDecl>& structs) {
            for (const auto& st : structs) {
                if (st.name == cur_type) {
                    for (const auto& f : st.fields) {
                        if (f.name == fld) {
                            cur_type = strip_wrappers(cfire::fmt::format_type_ref(f.type));
                            found = true;
                            return;
                        }
                    }
                }
            }
        };
        find_in_structs(ast.structs);
        for (const auto& pkg : ws.local_packages) {
            if (found) break;
            find_in_structs(pkg.ast.structs);
        }
        if (!found) break;
    }
    return cur_type;
}

[[nodiscard]] inline auto infer_expr_cf_type(
    const cfire::frontend::ExprPtr& e,
    const cfire::frontend::FileAST& ast,
    const WorkspaceContext& ws,
    const std::unordered_map<std::string, LocalBindingInfo>& env
) -> std::string {
    using namespace cfire::frontend;
    if (!e) return "auto";
    if (const auto* lit = std::get_if<LiteralExpr>(&e->node)) {
        if (lit->kind == TokenKind::String || lit->kind == TokenKind::RawString) return "str";
        if (lit->kind == TokenKind::Float) return "f64";
        if (lit->kind == TokenKind::KwTrue || lit->kind == TokenKind::KwFalse) return "bool";
        return "i64";
    }
    if (const auto* sl = std::get_if<StructLitExpr>(&e->node)) {
        return sl->type_name;
    }
    if (const auto* arr = std::get_if<ArrayLitExpr>(&e->node)) {
        if (!arr->elements.empty()) {
            return std::format("[{}]{}", arr->elements.size(), infer_expr_cf_type(arr->elements[0], ast, ws, env));
        }
        return "[]auto";
    }
    if (const auto* tup = std::get_if<TupleExpr>(&e->node)) {
        std::string res = "(";
        for (std::size_t i = 0; i < tup->elements.size(); ++i) {
            if (i > 0) res += ", ";
            res += infer_expr_cf_type(tup->elements[i], ast, ws, env);
        }
        res += ")";
        return res;
    }
    if (const auto* un = std::get_if<UnaryExpr>(&e->node)) {
        if (un->op == "!") return "bool";
        std::string inner = infer_expr_cf_type(un->operand, ast, ws, env);
        if (un->op == "*") return strip_wrappers(inner);
        if (un->op == "&mut") return "*mut " + inner;
        if (un->op == "&") return "*" + inner;
        return inner;
    }
    if (const auto* bin = std::get_if<BinaryExpr>(&e->node)) {
        if (bin->op == "==" || bin->op == "!=" || bin->op == "<" || bin->op == "<=" ||
            bin->op == ">" || bin->op == ">=" || bin->op == "&&" || bin->op == "||") {
            return "bool";
        }
        return infer_expr_cf_type(bin->left, ast, ws, env);
    }
    if (const auto* idx = std::get_if<IndexExpr>(&e->node)) {
        std::string obj_t = strip_wrappers(infer_expr_cf_type(idx->object, ast, ws, env));
        if (obj_t.starts_with("[]")) return obj_t.substr(2);
        if (obj_t.starts_with("[")) {
            if (auto rb = obj_t.find(']'); rb != std::string::npos) return obj_t.substr(rb + 1);
        }
        if (obj_t.starts_with("map[")) {
            if (auto rb = obj_t.find(']'); rb != std::string::npos) return obj_t.substr(rb + 1);
        }
        return "auto";
    }
    if (const auto* slc = std::get_if<SliceExpr>(&e->node)) {
        std::string obj_t = strip_wrappers(infer_expr_cf_type(slc->object, ast, ws, env));
        if (obj_t == "str") return "str";
        if (obj_t.starts_with("[")) {
            if (auto rb = obj_t.find(']'); rb != std::string::npos) return "[]" + obj_t.substr(rb + 1);
        }
        return obj_t;
    }
    if (const auto* tr = std::get_if<TryExpr>(&e->node)) {
        std::string inner = infer_expr_cf_type(tr->inner, ast, ws, env);
        if (inner.ends_with("!")) inner.pop_back();
        return inner;
    }
    if (const auto* orex = std::get_if<OrExpr>(&e->node)) {
        std::string inner = infer_expr_cf_type(orex->inner, ast, ws, env);
        if (inner.ends_with("!")) inner.pop_back();
        return inner;
    }
    if (const auto* id = std::get_if<IdentExpr>(&e->node)) {
        if (auto it = env.find(id->name); it != env.end()) return it->second.cf_type;
    }
    if (const auto* call = std::get_if<CallExpr>(&e->node)) {
        if (const auto* cid = std::get_if<IdentExpr>(&call->callee->node)) {
            if (cid->name == "make" && !call->type_args.empty()) {
                return cfire::fmt::format_type_ref(call->type_args[0]);
            }
            for (const auto& fn : ast.functions) {
                if (fn.name == cid->name) {
                    return cfire::fmt::format_type_ref(fn.return_type);
                }
            }
            for (const auto& pkg : ws.local_packages) {
                for (const auto& fn : pkg.ast.functions) {
                    if (fn.name == cid->name) {
                        return cfire::fmt::format_type_ref(fn.return_type);
                    }
                }
            }
        }
        if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node)) {
            if (const auto* mid = std::get_if<IdentExpr>(&mem->object->node)) {
                // 1. Check `core:*` module functions
                for (const auto& mod : cfire::doc::stdlib_catalog()) {
                    if (mod.ns_alias == mid->name) {
                        for (const auto& sfn : mod.functions) {
                            if (sfn.name == mem->field) {
                                if (auto arrow = sfn.cf_sig.find("-> "); arrow != std::string::npos) {
                                    std::string ret = sfn.cf_sig.substr(arrow + 3);
                                    if (ret == "T!" && !call->type_args.empty()) {
                                        return cfire::fmt::format_type_ref(call->type_args[0]) + "!";
                                    }
                                    if (ret == "Channel[T]" && !call->type_args.empty()) {
                                        return std::format("sync.Channel[{}]", cfire::fmt::format_type_ref(call->type_args[0]));
                                    }
                                    if (ret == "TcpListener!") return "net.TcpListener!";
                                    if (ret == "TcpConn!") return "net.TcpConn!";
                                    if (ret == "Conn!") return "sqlite.Conn!";
                                    return ret;
                                }
                                return "void";
                            }
                        }
                    }
                }
                // 2. Check imported local `.cf` packages (`checksum.compute_tag(...)`)
                for (const auto& pkg : ws.local_packages) {
                    if (pkg.alias == mid->name) {
                        for (const auto& fn : pkg.ast.functions) {
                            if (fn.name == mem->field) {
                                return cfire::fmt::format_type_ref(fn.return_type);
                            }
                        }
                    }
                }
                // 3. Check `cpp::fn(...)` Clang AST Bridge functions
                if (mid->name == "cpp" || mem->is_namespace) {
                    if (auto cit = ws.cpp_symbols.find(mem->field); cit != ws.cpp_symbols.end()) {
                        return cpp_return_to_cf(cit->second.return_type);
                    }
                }
                // 4. Check Enum variant constructor (`Shape.Circle(10.0)`)
                for (const auto& en : ast.enums) {
                    if (en.name == mid->name) return en.name;
                }
                // 5. Check receiver methods (`listener.accept()`, `server_conn.read_str()`, `ch.recv()`, user methods)
                if (auto vit = env.find(mid->name); vit != env.end()) {
                    std::string rtype = strip_wrappers(vit->second.cf_type);
                    if (rtype == "net.TcpListener" || rtype == "TcpListener") {
                        if (mem->field == "port") return "u16";
                        if (mem->field == "accept") return "net.TcpConn!";
                    }
                    if (rtype == "net.TcpConn" || rtype == "TcpConn") {
                        if (mem->field == "write") return "usize!";
                        if (mem->field == "read_str") return "str!";
                    }
                    if (rtype.starts_with("sync.Channel[") || rtype.starts_with("Channel[")) {
                        auto b1 = rtype.find('[');
                        auto b2 = rtype.rfind(']');
                        std::string elem = (b1 != std::string::npos && b2 != std::string::npos)
                            ? rtype.substr(b1 + 1, b2 - b1 - 1) : "auto";
                        if (mem->field == "recv") return elem + "!";
                        return "void";
                    }
                    if (rtype == "sqlite.Conn" || rtype == "Conn") {
                        if (mem->field == "query" && !call->type_args.empty()) {
                            return "[]" + cfire::fmt::format_type_ref(call->type_args[0]) + "!";
                        }
                        return "void!";
                    }
                    if (mem->field == "size") return "usize";
                    if (mem->field == "empty") return "bool";
                    for (const auto& fn : ast.functions) {
                        if (fn.receiver && fn.receiver->type.name == rtype && fn.name == mem->field) {
                            return cfire::fmt::format_type_ref(fn.return_type);
                        }
                    }
                    for (const auto& pkg : ws.local_packages) {
                        for (const auto& fn : pkg.ast.functions) {
                            if (fn.receiver && fn.receiver->type.name == rtype && fn.name == mem->field) {
                                return cfire::fmt::format_type_ref(fn.return_type);
                            }
                        }
                    }
                }
            }
        }
    }
    if (const auto* mem = std::get_if<MemberExpr>(&e->node)) {
        if (mem->field == "len") return "usize";
        std::string obj_t = strip_wrappers(infer_expr_cf_type(mem->object, ast, ws, env));
        for (const auto& st : ast.structs) {
            if (st.name == obj_t) {
                for (const auto& f : st.fields) {
                    if (f.name == mem->field) return cfire::fmt::format_type_ref(f.type);
                }
            }
        }
        for (const auto& pkg : ws.local_packages) {
            for (const auto& st : pkg.ast.structs) {
                if (st.name == obj_t) {
                    for (const auto& f : st.fields) {
                        if (f.name == mem->field) return cfire::fmt::format_type_ref(f.type);
                    }
                }
            }
        }
        for (const auto& en : ast.enums) {
            if (en.name == obj_t) return en.name;
        }
    }
    return "auto";
}

[[nodiscard]] inline auto cf_type_string_to_cpp(std::string_view cf_t) -> std::string {
    std::string s(cf_t);
    bool fallible = false;
    if (s.ends_with("!")) {
        fallible = true;
        s.pop_back();
    }
    auto map_inner = [](std::string_view v) -> std::string {
        if (v == "str") return "std::string";
        if (v == "i32" || v == "int") return "std::int32_t";
        if (v == "i64") return "std::int64_t";
        if (v == "u16") return "std::uint16_t";
        if (v == "u32") return "std::uint32_t";
        if (v == "u64") return "std::uint64_t";
        if (v == "usize") return "std::size_t";
        if (v == "f32") return "float";
        if (v == "f64") return "double";
        if (v == "bool") return "bool";
        if (v == "[]byte" || v == "bytes") return "std::vector<std::byte>";
        if (v == "[]str") return "std::vector<std::string>";
        if (v.starts_with("own[") && v.ends_with("]")) {
            return std::format("std::unique_ptr<{}>", v.substr(4, v.size() - 5));
        }
        if (v.starts_with("shared[") && v.ends_with("]")) {
            return std::format("std::shared_ptr<{}>", v.substr(7, v.size() - 8));
        }
        if (v == "net.TcpListener") return "cfire::net::TcpListener";
        if (v == "net.TcpConn") return "cfire::net::TcpConn";
        if (v == "sqlite.Conn") return "cfire::sqlite::Conn";
        if (v == "context.Context" || v == "Context") return "cfire::sync::context::Context";
        if (v == "websocket.Conn") return "cfire::websocket::Conn";
        if (v.starts_with("sync.Channel[") && v.ends_with("]")) {
            return std::format("cfire::sync::Channel<{}>", v.substr(13, v.size() - 14));
        }
        return std::string(v);
    };
    std::string cpp;
    if (s.starts_with("(") && s.ends_with(")")) {
        std::string inner = s.substr(1, s.size() - 2);
        std::string tuple_cpp = "std::tuple<";
        std::size_t start = 0;
        bool first = true;
        while (start < inner.size()) {
            auto comma = inner.find(',', start);
            std::string part = (comma == std::string::npos) ? inner.substr(start) : inner.substr(start, comma - start);
            start = (comma == std::string::npos) ? inner.size() : comma + 1;
            while (!part.empty() && part.front() == ' ') part.erase(part.begin());
            while (!part.empty() && part.back() == ' ') part.pop_back();
            if (!first) tuple_cpp += ", ";
            first = false;
            tuple_cpp += map_inner(part);
        }
        tuple_cpp += ">";
        cpp = tuple_cpp;
    } else {
        cpp = map_inner(s);
    }
    if (fallible) {
        return std::format("std::expected<{}, cfire::Error>", cpp);
    }
    return cpp;
}

struct TypeLayout {
    int size = 0;
    int align = 1;
};

struct FieldLayout {
    std::string name;
    int offset = 0;
    int size = 0;
    int align = 1;
};

struct StructLayout {
    int total_size = 0;
    int align = 1;
    std::vector<FieldLayout> fields;
};

inline auto compute_type_layout(
    const cfire::frontend::TypeRef& tr,
    const cfire::frontend::FileAST& ast,
    const WorkspaceContext& ws,
    std::unordered_set<std::string>& visited
) -> TypeLayout;

inline auto compute_struct_layout(
    const cfire::frontend::StructDecl& st,
    const cfire::frontend::FileAST& ast,
    const WorkspaceContext& ws,
    std::unordered_set<std::string>& visited
) -> StructLayout {
    StructLayout layout;
    if (st.fields.empty()) {
        layout.total_size = 1;
        layout.align = 1;
        return layout;
    }
    if (visited.contains(st.name)) {
        layout.total_size = 8;
        layout.align = 8;
        return layout;
    }
    visited.insert(st.name);

    int cur_offset = 0;
    int max_align = 1;

    for (const auto& f : st.fields) {
        TypeLayout tl = compute_type_layout(f.type, ast, ws, visited);
        int field_align = std::max(1, tl.align);
        max_align = std::max(max_align, field_align);

        cur_offset = (cur_offset + field_align - 1) & ~(field_align - 1);

        FieldLayout fl;
        fl.name = f.name;
        fl.offset = cur_offset;
        fl.size = tl.size;
        fl.align = field_align;
        layout.fields.push_back(fl);

        cur_offset += tl.size;
    }

    layout.align = max_align;
    layout.total_size = (cur_offset + max_align - 1) & ~(max_align - 1);
    if (layout.total_size == 0) layout.total_size = max_align;
    visited.erase(st.name);
    return layout;
}

inline auto compute_type_layout(
    const cfire::frontend::TypeRef& tr,
    const cfire::frontend::FileAST& ast,
    const WorkspaceContext& ws,
    std::unordered_set<std::string>& visited
) -> TypeLayout {
    if (tr.is_slice) return TypeLayout{24, 8}; // std::vector: 24 bytes, 8 align
    if (tr.is_map) return TypeLayout{40, 8};   // std::unordered_map
    if (tr.is_ptr || tr.is_mut_ptr) return TypeLayout{8, 8};
    if (!tr.smart_wrap.empty()) {
        if (tr.smart_wrap == "own") return TypeLayout{8, 8}; // std::unique_ptr
        if (tr.smart_wrap == "shared") return TypeLayout{16, 8}; // std::shared_ptr
        return TypeLayout{8, 8};
    }
    if (tr.is_array) {
        TypeLayout elem = tr.elem_type ? compute_type_layout(*tr.elem_type, ast, ws, visited) : TypeLayout{1, 1};
        int n = 1;
        try {
            if (!tr.array_len.empty()) n = std::stoi(tr.array_len);
        } catch (...) {}
        return TypeLayout{n * elem.size, elem.align};
    }

    std::string_view name = tr.name;
    if (name == "bool" || name == "char" || name == "i8" || name == "u8" || name == "byte") return TypeLayout{1, 1};
    if (name == "i16" || name == "u16") return TypeLayout{2, 2};
    if (name == "i32" || name == "u32" || name == "f32" || name == "int") return TypeLayout{4, 4};
    if (name == "i64" || name == "u64" || name == "f64" || name == "usize" || name == "isize" || name == "rawptr") return TypeLayout{8, 8};
    if (name == "str") return TypeLayout{24, 8};
    if (name == "net.TcpListener" || name == "net.TcpConn" || name == "sqlite.Conn") return TypeLayout{8, 8};
    if (name.starts_with("sync.Channel")) return TypeLayout{16, 8};

    // Check user structs in ast
    for (const auto& s : ast.structs) {
        if (s.name == name) {
            auto sl = compute_struct_layout(s, ast, ws, visited);
            return TypeLayout{sl.total_size, sl.align};
        }
    }
    // Check local package structs
    for (const auto& pkg : ws.local_packages) {
        for (const auto& s : pkg.ast.structs) {
            if (s.name == name || (pkg.alias + "." + s.name) == name) {
                auto sl = compute_struct_layout(s, pkg.ast, ws, visited);
                return TypeLayout{sl.total_size, sl.align};
            }
        }
    }

    return TypeLayout{8, 8}; // Default fallback
}

inline auto expr_to_string(const cfire::frontend::ExprPtr& e) -> std::string {
    using namespace cfire::frontend;
    if (!e) return "";
    if (const auto* id = std::get_if<IdentExpr>(&e->node)) return id->name;
    if (const auto* lit = std::get_if<LiteralExpr>(&e->node)) {
        if (lit->kind == TokenKind::String) return "\"" + lit->value + "\"";
        if (lit->kind == TokenKind::RawString) return "`" + lit->value + "`";
        return lit->value;
    }
    if (const auto* un = std::get_if<UnaryExpr>(&e->node)) {
        return un->op + expr_to_string(un->operand);
    }
    if (const auto* bin = std::get_if<BinaryExpr>(&e->node)) {
        return expr_to_string(bin->left) + " " + bin->op + " " + expr_to_string(bin->right);
    }
    if (const auto* mem = std::get_if<MemberExpr>(&e->node)) {
        return expr_to_string(mem->object) + (mem->is_namespace ? "::" : ".") + mem->field;
    }
    if (const auto* idx = std::get_if<IndexExpr>(&e->node)) {
        return expr_to_string(idx->object) + "[" + expr_to_string(idx->index) + "]";
    }
    if (const auto* slc = std::get_if<SliceExpr>(&e->node)) {
        std::string s = expr_to_string(slc->object) + "[";
        if (slc->start) s += expr_to_string(slc->start);
        s += "..";
        if (slc->end) s += expr_to_string(slc->end);
        s += "]";
        return s;
    }
    if (const auto* call = std::get_if<CallExpr>(&e->node)) {
        std::string s = expr_to_string(call->callee);
        if (!call->type_args.empty()) {
            s += "[";
            for (std::size_t i = 0; i < call->type_args.size(); ++i) {
                if (i > 0) s += ", ";
                s += cfire::fmt::format_type_ref(call->type_args[i]);
            }
            s += "]";
        }
        s += "(";
        for (std::size_t i = 0; i < call->args.size(); ++i) {
            if (i > 0) s += ", ";
            if (!call->args[i].intent.empty()) s += call->args[i].intent + " ";
            s += expr_to_string(call->args[i].value);
        }
        s += ")";
        return s;
    }
    if (const auto* sl = std::get_if<StructLitExpr>(&e->node)) {
        return sl->type_name + "{...}";
    }
    if (std::holds_alternative<ArrayLitExpr>(e->node)) {
        return "[...]";
    }
    if (const auto* tr = std::get_if<TryExpr>(&e->node)) {
        return expr_to_string(tr->inner) + "?";
    }
    if (const auto* orex = std::get_if<OrExpr>(&e->node)) {
        return expr_to_string(orex->inner) + " or ...";
    }
    return "";
}

inline void collect_all_exprs(
    const cfire::frontend::ExprPtr& e,
    std::vector<cfire::frontend::ExprPtr>& out
) {
    using namespace cfire::frontend;
    if (!e) return;
    out.push_back(e);
    if (const auto* un = std::get_if<UnaryExpr>(&e->node)) {
        collect_all_exprs(un->operand, out);
    } else if (const auto* bin = std::get_if<BinaryExpr>(&e->node)) {
        collect_all_exprs(bin->left, out);
        collect_all_exprs(bin->right, out);
    } else if (const auto* mem = std::get_if<MemberExpr>(&e->node)) {
        collect_all_exprs(mem->object, out);
    } else if (const auto* idx = std::get_if<IndexExpr>(&e->node)) {
        collect_all_exprs(idx->object, out);
        collect_all_exprs(idx->index, out);
    } else if (const auto* slc = std::get_if<SliceExpr>(&e->node)) {
        collect_all_exprs(slc->object, out);
        if (slc->start) collect_all_exprs(slc->start, out);
        if (slc->end) collect_all_exprs(slc->end, out);
    } else if (const auto* call = std::get_if<CallExpr>(&e->node)) {
        collect_all_exprs(call->callee, out);
        for (const auto& a : call->args) collect_all_exprs(a.value, out);
    } else if (const auto* sl = std::get_if<StructLitExpr>(&e->node)) {
        for (const auto& f : sl->fields) collect_all_exprs(f.value, out);
    } else if (const auto* arr = std::get_if<ArrayLitExpr>(&e->node)) {
        for (const auto& elem : arr->elements) collect_all_exprs(elem, out);
    } else if (const auto* tr = std::get_if<TryExpr>(&e->node)) {
        collect_all_exprs(tr->inner, out);
    } else if (const auto* orex = std::get_if<OrExpr>(&e->node)) {
        collect_all_exprs(orex->inner, out);
        if (orex->fallback_val) collect_all_exprs(orex->fallback_val, out);
    } else if (const auto* tup = std::get_if<TupleExpr>(&e->node)) {
        for (const auto& elem : tup->elements) collect_all_exprs(elem, out);
    }
}

inline void collect_stmt_exprs(
    const std::vector<cfire::frontend::StmtPtr>& stmts,
    std::vector<cfire::frontend::ExprPtr>& out
) {
    using namespace cfire::frontend;
    for (const auto& s : stmts) {
        if (!s) continue;
        if (const auto* vd = std::get_if<VarDeclStmt>(&s->node)) {
            if (vd->init) collect_all_exprs(vd->init, out);
        } else if (const auto* as = std::get_if<AssignStmt>(&s->node)) {
            if (as->target) collect_all_exprs(as->target, out);
            if (as->value) collect_all_exprs(as->value, out);
        } else if (const auto* das = std::get_if<DestructureAssignStmt>(&s->node)) {
            for (const auto& t : das->targets) collect_all_exprs(t, out);
            if (das->value) collect_all_exprs(das->value, out);
        } else if (const auto* dvs = std::get_if<DestructureVarDeclStmt>(&s->node)) {
            if (dvs->init) collect_all_exprs(dvs->init, out);
        } else if (const auto* def = std::get_if<DeferStmt>(&s->node)) {
            if (def->call) collect_all_exprs(def->call, out);
        } else if (const auto* es = std::get_if<ExprStmt>(&s->node)) {
            if (es->expr) collect_all_exprs(es->expr, out);
        } else if (const auto* rs = std::get_if<ReturnStmt>(&s->node)) {
            if (rs->value) collect_all_exprs(rs->value, out);
        } else if (const auto* ifs = std::get_if<IfStmt>(&s->node)) {
            if (ifs->cond) collect_all_exprs(ifs->cond, out);
            collect_stmt_exprs(ifs->then_body, out);
            collect_stmt_exprs(ifs->else_body, out);
        } else if (const auto* fs = std::get_if<ForStmt>(&s->node)) {
            if (fs->iter_expr) collect_all_exprs(fs->iter_expr, out);
            collect_stmt_exprs(fs->body, out);
        } else if (const auto* ms = std::get_if<MatchStmt>(&s->node)) {
            if (ms->target) collect_all_exprs(ms->target, out);
            for (const auto& arm : ms->arms) collect_stmt_exprs(arm.body, out);
        } else if (const auto* asc = std::get_if<AsyncScopeStmt>(&s->node)) {
            if (asc->workers) collect_all_exprs(asc->workers, out);
            collect_stmt_exprs(asc->body, out);
        }
    }
}

inline void collect_stmt_bindings(
    const std::vector<cfire::frontend::StmtPtr>& stmts,
    const cfire::frontend::FileAST& ast,
    const WorkspaceContext& ws,
    const std::vector<std::string>& lines,
    int max_line,
    std::unordered_map<std::string, LocalBindingInfo>& env,
    std::vector<LocalBindingInfo>& ordered_out
) {
    using namespace cfire::frontend;
    for (const auto& s : stmts) {
        if (!s) continue;
        if (const auto* vd = std::get_if<VarDeclStmt>(&s->node)) {
            if (vd->name != "_" && (max_line <= 0 || vd->line <= max_line)) {
                LocalBindingInfo info;
                info.name = vd->name;
                info.is_mut = vd->is_mut;
                info.decl_line = vd->line;
                if (vd->type) {
                    info.cf_type = cfire::fmt::format_type_ref(*vd->type);
                    info.cpp_type = lower_type_preview(*vd->type);
                    info.inferred_colon_eq = false;
                } else {
                    info.cf_type = infer_expr_cf_type(vd->init, ast, ws, env);
                    info.cpp_type = cf_type_string_to_cpp(info.cf_type);
                    info.inferred_colon_eq = true;
                }
                if (vd->line >= 1 && static_cast<std::size_t>(vd->line) <= lines.size()) {
                    const std::string& ln = lines[static_cast<std::size_t>(vd->line - 1)];
                    if (auto p = ln.find(vd->name); p != std::string::npos) {
                        info.name_end_col = static_cast<int>(p + vd->name.size());
                    }
                }
                env[info.name] = info;
                ordered_out.push_back(info);
            }
        } else if (const auto* dvs = std::get_if<DestructureVarDeclStmt>(&s->node)) {
            std::vector<std::string> elem_types;
            if (dvs->init) {
                std::string init_type = infer_expr_cf_type(dvs->init, ast, ws, env);
                while (init_type.ends_with("!") || init_type.ends_with("?")) init_type.pop_back();
                if (init_type.starts_with("(") && init_type.ends_with(")")) {
                    std::string inner = init_type.substr(1, init_type.size() - 2);
                    std::size_t start = 0;
                    while (start < inner.size()) {
                        auto comma = inner.find(',', start);
                        std::string part = (comma == std::string::npos) ? inner.substr(start) : inner.substr(start, comma - start);
                        start = (comma == std::string::npos) ? inner.size() : comma + 1;
                        while (!part.empty() && part.front() == ' ') part.erase(part.begin());
                        while (!part.empty() && part.back() == ' ') part.pop_back();
                        elem_types.push_back(part);
                    }
                }
            }
            for (std::size_t i = 0; i < dvs->bindings.size(); ++i) {
                const auto& [name, is_mut] = dvs->bindings[i];
                if (name != "_" && (max_line <= 0 || dvs->line <= max_line)) {
                    LocalBindingInfo info;
                    info.name = name;
                    info.is_mut = is_mut;
                    info.decl_line = dvs->line;
                    info.cf_type = (i < elem_types.size()) ? elem_types[i] : "auto";
                    info.cpp_type = cf_type_string_to_cpp(info.cf_type);
                    info.inferred_colon_eq = true;
                    if (dvs->line >= 1 && static_cast<std::size_t>(dvs->line) <= lines.size()) {
                        const std::string& ln = lines[static_cast<std::size_t>(dvs->line - 1)];
                        if (auto p = ln.find(name); p != std::string::npos) {
                            info.name_end_col = static_cast<int>(p + name.size());
                        }
                    }
                    env[info.name] = info;
                    ordered_out.push_back(info);
                }
            }
        } else if (const auto* ifs = std::get_if<IfStmt>(&s->node)) {
            collect_stmt_bindings(ifs->then_body, ast, ws, lines, max_line, env, ordered_out);
            collect_stmt_bindings(ifs->else_body, ast, ws, lines, max_line, env, ordered_out);
        } else if (const auto* fs = std::get_if<ForStmt>(&s->node)) {
            collect_stmt_bindings(fs->body, ast, ws, lines, max_line, env, ordered_out);
        } else if (const auto* as = std::get_if<AsyncScopeStmt>(&s->node)) {
            collect_stmt_bindings(as->body, ast, ws, lines, max_line, env, ordered_out);
        } else if (const auto* ms = std::get_if<MatchStmt>(&s->node)) {
            for (const auto& arm : ms->arms) {
                collect_stmt_bindings(arm.body, ast, ws, lines, max_line, env, ordered_out);
            }
        }
    }
}

inline void collect_stmt_bindings(
    const std::vector<cfire::frontend::StmtPtr>& stmts,
    const cfire::frontend::FileAST& ast,
    const WorkspaceContext& ws,
    const std::vector<std::string>& lines,
    std::unordered_map<std::string, LocalBindingInfo>& env,
    std::vector<LocalBindingInfo>& ordered_out
) {
    collect_stmt_bindings(stmts, ast, ws, lines, 0, env, ordered_out);
}

inline void walk_stmts_calls(const std::vector<cfire::frontend::StmtPtr>& stmts, const std::function<void(const cfire::frontend::CallExpr&)>& visitor);

inline void walk_expr_calls(const cfire::frontend::ExprPtr& expr, const std::function<void(const cfire::frontend::CallExpr&)>& visitor) {
    if (!expr) return;
    using namespace cfire::frontend;
    if (const auto* c = std::get_if<CallExpr>(&expr->node)) {
        visitor(*c);
        walk_expr_calls(c->callee, visitor);
        for (const auto& a : c->args) {
            walk_expr_calls(a.value, visitor);
        }
        if (c->block_arg) {
            walk_stmts_calls(c->block_arg->body, visitor);
        }
    } else if (const auto* u = std::get_if<UnaryExpr>(&expr->node)) {
        walk_expr_calls(u->operand, visitor);
    } else if (const auto* b = std::get_if<BinaryExpr>(&expr->node)) {
        walk_expr_calls(b->left, visitor);
        walk_expr_calls(b->right, visitor);
    } else if (const auto* m = std::get_if<MemberExpr>(&expr->node)) {
        walk_expr_calls(m->object, visitor);
    } else if (const auto* idx = std::get_if<IndexExpr>(&expr->node)) {
        walk_expr_calls(idx->object, visitor);
        walk_expr_calls(idx->index, visitor);
    } else if (const auto* sl = std::get_if<SliceExpr>(&expr->node)) {
        walk_expr_calls(sl->object, visitor);
        walk_expr_calls(sl->start, visitor);
        walk_expr_calls(sl->end, visitor);
    } else if (const auto* st = std::get_if<StructLitExpr>(&expr->node)) {
        for (const auto& fld : st->fields) {
            walk_expr_calls(fld.value, visitor);
        }
    } else if (const auto* arr = std::get_if<ArrayLitExpr>(&expr->node)) {
        for (const auto& el : arr->elements) {
            walk_expr_calls(el, visitor);
        }
    } else if (const auto* cl = std::get_if<ClosureExpr>(&expr->node)) {
        walk_stmts_calls(cl->body, visitor);
    } else if (const auto* tr = std::get_if<TryExpr>(&expr->node)) {
        walk_expr_calls(tr->inner, visitor);
    } else if (const auto* oe = std::get_if<OrExpr>(&expr->node)) {
        walk_expr_calls(oe->inner, visitor);
        walk_stmts_calls(oe->handler_body, visitor);
        walk_expr_calls(oe->fallback_val, visitor);
    } else if (const auto* tup = std::get_if<TupleExpr>(&expr->node)) {
        for (const auto& el : tup->elements) {
            walk_expr_calls(el, visitor);
        }
    }
}

inline void walk_stmts_calls(const std::vector<cfire::frontend::StmtPtr>& stmts, const std::function<void(const cfire::frontend::CallExpr&)>& visitor) {
    using namespace cfire::frontend;
    for (const auto& s : stmts) {
        if (!s) continue;
        if (const auto* vd = std::get_if<VarDeclStmt>(&s->node)) {
            walk_expr_calls(vd->init, visitor);
        } else if (const auto* dvs = std::get_if<DestructureVarDeclStmt>(&s->node)) {
            walk_expr_calls(dvs->init, visitor);
        } else if (const auto* es = std::get_if<ExprStmt>(&s->node)) {
            walk_expr_calls(es->expr, visitor);
        } else if (const auto* as = std::get_if<AssignStmt>(&s->node)) {
            walk_expr_calls(as->target, visitor);
            walk_expr_calls(as->value, visitor);
        } else if (const auto* ifs = std::get_if<IfStmt>(&s->node)) {
            walk_expr_calls(ifs->cond, visitor);
            walk_stmts_calls(ifs->then_body, visitor);
            walk_stmts_calls(ifs->else_body, visitor);
        } else if (const auto* fs = std::get_if<ForStmt>(&s->node)) {
            walk_expr_calls(fs->iter_expr, visitor);
            walk_stmts_calls(fs->body, visitor);
        } else if (const auto* ms = std::get_if<MatchStmt>(&s->node)) {
            walk_expr_calls(ms->target, visitor);
            for (const auto& arm : ms->arms) {
                walk_stmts_calls(arm.body, visitor);
            }
        } else if (const auto* rs = std::get_if<ReturnStmt>(&s->node)) {
            walk_expr_calls(rs->value, visitor);
        } else if (const auto* ds = std::get_if<DeferStmt>(&s->node)) {
            walk_expr_calls(ds->call, visitor);
        } else if (const auto* asc = std::get_if<AsyncScopeStmt>(&s->node)) {
            walk_stmts_calls(asc->body, visitor);
        }
    }
}

[[nodiscard]] inline auto build_scope_bindings(
    const cfire::frontend::FileAST& ast,
    const WorkspaceContext& ws,
    const std::vector<std::string>& lines,
    int cursor_zero_based_line
) -> std::unordered_map<std::string, LocalBindingInfo> {
    std::unordered_map<std::string, LocalBindingInfo> env;
    std::vector<LocalBindingInfo> dummy;
    int one_based = cursor_zero_based_line + 1;

    for (const auto& fn : ast.functions) {
        if (one_based >= fn.line) {
            env.clear();
            if (fn.receiver) {
                LocalBindingInfo r;
                r.name = fn.receiver->name;
                r.cf_type = cfire::fmt::format_type_ref(fn.receiver->type);
                r.cpp_type = lower_type_preview(fn.receiver->type, true, fn.receiver->intent);
                r.is_mut = (fn.receiver->intent == "mut");
                r.is_param = true;
                r.intent = fn.receiver->intent;
                r.decl_line = fn.line;
                env[r.name] = r;
            }
            for (const auto& p : fn.params) {
                LocalBindingInfo pi;
                pi.name = p.name;
                pi.cf_type = cfire::fmt::format_type_ref(p.type);
                pi.cpp_type = lower_type_preview(p.type, true, p.intent);
                pi.is_mut = (p.intent == "mut" || p.intent == "out");
                pi.is_param = true;
                pi.intent = p.intent;
                pi.decl_line = p.line;
                env[pi.name] = pi;
            }
            collect_stmt_bindings(fn.body, ast, ws, lines, one_based, env, dummy);
        }
    }
    for (const auto& t : ast.tests) {
        if (one_based >= t.line) {
            env.clear();
            collect_stmt_bindings(t.body, ast, ws, lines, one_based, env, dummy);
        }
    }
    return env;
}

inline void write_rpc_message(std::ostream& out, std::string_view body) {
    out << "Content-Length: " << body.size() << "\r\n\r\n" << body;
    out.flush();
}

struct DiagnosticEntry {
    int line = 0;      // 0-based
    int col = 0;       // 0-based
    std::string code;  // e.g. "cfire/MUT-CALLSITE"
    std::string message;
};

} // namespace detail
} // namespace cfire::lsp
