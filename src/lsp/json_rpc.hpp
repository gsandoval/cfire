#pragma once
// src/lsp/json_rpc.hpp — JSON-RPC 2.0 Framing, Structure-Aware Key Extraction & AST Scope Walkers

#include "../clang_bridge.hpp"
#include "../codegen.hpp"
#include "../docgen/catalog_types.hpp"
#include "../formatter.hpp"
#include "../sema.hpp"
#include "type_query.hpp"
#include "layout.hpp"
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
    return cfire::detail::json_escape(sv);
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

inline void walk_stmt_exprs(
    const std::vector<cfire::frontend::StmtPtr>& stmts,
    const std::function<void(const cfire::frontend::ExprPtr&)>& visitor
);

inline void walk_exprs(
    const cfire::frontend::ExprPtr& e,
    const std::function<void(const cfire::frontend::ExprPtr&)>& visitor
) {
    using namespace cfire::frontend;
    if (!e) return;
    visitor(e);
    if (const auto* un = std::get_if<UnaryExpr>(&e->node)) {
        walk_exprs(un->operand, visitor);
    } else if (const auto* bin = std::get_if<BinaryExpr>(&e->node)) {
        walk_exprs(bin->left, visitor);
        walk_exprs(bin->right, visitor);
    } else if (const auto* mem = std::get_if<MemberExpr>(&e->node)) {
        walk_exprs(mem->object, visitor);
    } else if (const auto* idx = std::get_if<IndexExpr>(&e->node)) {
        walk_exprs(idx->object, visitor);
        walk_exprs(idx->index, visitor);
    } else if (const auto* slc = std::get_if<SliceExpr>(&e->node)) {
        walk_exprs(slc->object, visitor);
        if (slc->start) walk_exprs(slc->start, visitor);
        if (slc->end) walk_exprs(slc->end, visitor);
    } else if (const auto* call = std::get_if<CallExpr>(&e->node)) {
        walk_exprs(call->callee, visitor);
        for (const auto& a : call->args) walk_exprs(a.value, visitor);
        if (call->block_arg) walk_stmt_exprs(call->block_arg->body, visitor);
    } else if (const auto* sl = std::get_if<StructLitExpr>(&e->node)) {
        for (const auto& f : sl->fields) walk_exprs(f.value, visitor);
    } else if (const auto* arr = std::get_if<ArrayLitExpr>(&e->node)) {
        for (const auto& elem : arr->elements) walk_exprs(elem, visitor);
    } else if (const auto* cl = std::get_if<ClosureExpr>(&e->node)) {
        walk_stmt_exprs(cl->body, visitor);
    } else if (const auto* me = std::get_if<MatchExpr>(&e->node)) {
        walk_exprs(me->target, visitor);
        for (const auto& arm : me->arms) walk_stmt_exprs(arm.body, visitor);
    } else if (const auto* tr = std::get_if<TryExpr>(&e->node)) {
        walk_exprs(tr->inner, visitor);
    } else if (const auto* orex = std::get_if<OrExpr>(&e->node)) {
        walk_exprs(orex->inner, visitor);
        walk_stmt_exprs(orex->handler_body, visitor);
        if (orex->fallback_val) walk_exprs(orex->fallback_val, visitor);
    } else if (const auto* tup = std::get_if<TupleExpr>(&e->node)) {
        for (const auto& elem : tup->elements) walk_exprs(elem, visitor);
    }
}

inline void walk_stmt_exprs(
    const std::vector<cfire::frontend::StmtPtr>& stmts,
    const std::function<void(const cfire::frontend::ExprPtr&)>& visitor
) {
    using namespace cfire::frontend;
    for (const auto& s : stmts) {
        if (!s) continue;
        if (const auto* vd = std::get_if<VarDeclStmt>(&s->node)) {
            if (vd->init) walk_exprs(vd->init, visitor);
        } else if (const auto* as = std::get_if<AssignStmt>(&s->node)) {
            if (as->target) walk_exprs(as->target, visitor);
            if (as->value) walk_exprs(as->value, visitor);
        } else if (const auto* das = std::get_if<DestructureAssignStmt>(&s->node)) {
            for (const auto& t : das->targets) walk_exprs(t, visitor);
            if (das->value) walk_exprs(das->value, visitor);
        } else if (const auto* dvs = std::get_if<DestructureVarDeclStmt>(&s->node)) {
            if (dvs->init) walk_exprs(dvs->init, visitor);
        } else if (const auto* def = std::get_if<DeferStmt>(&s->node)) {
            if (def->call) walk_exprs(def->call, visitor);
            walk_stmt_exprs(def->body, visitor);
        } else if (const auto* es = std::get_if<ExprStmt>(&s->node)) {
            if (es->expr) walk_exprs(es->expr, visitor);
        } else if (const auto* rs = std::get_if<ReturnStmt>(&s->node)) {
            if (rs->value) walk_exprs(rs->value, visitor);
        } else if (const auto* ifs = std::get_if<IfStmt>(&s->node)) {
            if (ifs->cond) walk_exprs(ifs->cond, visitor);
            walk_stmt_exprs(ifs->then_body, visitor);
            walk_stmt_exprs(ifs->else_body, visitor);
        } else if (const auto* fs = std::get_if<ForStmt>(&s->node)) {
            if (fs->iter_expr) walk_exprs(fs->iter_expr, visitor);
            if (fs->range_end) walk_exprs(fs->range_end, visitor);
            walk_stmt_exprs(fs->body, visitor);
        } else if (const auto* ms = std::get_if<MatchStmt>(&s->node)) {
            if (ms->target) walk_exprs(ms->target, visitor);
            for (const auto& arm : ms->arms) walk_stmt_exprs(arm.body, visitor);
        } else if (const auto* asc = std::get_if<AsyncScopeStmt>(&s->node)) {
            if (asc->workers) walk_exprs(asc->workers, visitor);
            walk_stmt_exprs(asc->body, visitor);
        }
    }
}

inline void collect_all_exprs(
    const cfire::frontend::ExprPtr& e,
    std::vector<cfire::frontend::ExprPtr>& out
) {
    walk_exprs(e, [&](const cfire::frontend::ExprPtr& ex) { out.push_back(ex); });
}

inline void collect_stmt_exprs(
    const std::vector<cfire::frontend::StmtPtr>& stmts,
    std::vector<cfire::frontend::ExprPtr>& out
) {
    walk_stmt_exprs(stmts, [&](const cfire::frontend::ExprPtr& ex) { out.push_back(ex); });
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
            if (max_line <= 0 || fs->line <= max_line) {
                if (!fs->index_var.empty() && fs->index_var != "_") {
                    LocalBindingInfo idx_info;
                    idx_info.name = fs->index_var;
                    idx_info.is_mut = false;
                    idx_info.decl_line = fs->line;
                    idx_info.cf_type = "usize";
                    idx_info.cpp_type = "std::size_t";
                    idx_info.inferred_colon_eq = false;
                    env[idx_info.name] = idx_info;
                }
                if (!fs->item_var.empty() && fs->item_var != "_") {
                    LocalBindingInfo item_info;
                    item_info.name = fs->item_var;
                    item_info.is_mut = false;
                    item_info.decl_line = fs->line;
                    if (fs->range_end) {
                        item_info.cf_type = "i64";
                        item_info.cpp_type = "std::int64_t";
                    } else if (fs->iter_expr) {
                        std::string col_t = infer_expr_cf_type(fs->iter_expr, ast, ws, env);
                        if (col_t.starts_with("&mut ")) col_t = col_t.substr(5);
                        else if (col_t.starts_with("&")) col_t = col_t.substr(1);
                        if (col_t.starts_with("[]")) {
                            item_info.cf_type = col_t.substr(2);
                        } else if (col_t.starts_with("[")) {
                            if (auto rb = col_t.find(']'); rb != std::string::npos && rb + 1 < col_t.size()) {
                                item_info.cf_type = col_t.substr(rb + 1);
                            }
                        } else if (col_t == "str") {
                            item_info.cf_type = "u8";
                        } else {
                            item_info.cf_type = "auto";
                        }
                        item_info.cpp_type = cf_type_string_to_cpp(item_info.cf_type);
                    }
                    item_info.inferred_colon_eq = false;
                    env[item_info.name] = item_info;
                }
            }
            collect_stmt_bindings(fs->body, ast, ws, lines, max_line, env, ordered_out);
        } else if (const auto* def = std::get_if<DeferStmt>(&s->node)) {
            collect_stmt_bindings(def->body, ast, ws, lines, max_line, env, ordered_out);
        } else if (const auto* as = std::get_if<AsyncScopeStmt>(&s->node)) {
            collect_stmt_bindings(as->body, ast, ws, lines, max_line, env, ordered_out);
        } else if (const auto* ms = std::get_if<MatchStmt>(&s->node)) {
            for (const auto& arm : ms->arms) {
                collect_stmt_bindings(arm.body, ast, ws, lines, max_line, env, ordered_out);
            }
        } else if (const auto* es = std::get_if<ExprStmt>(&s->node)) {
            if (es->expr) {
                if (const auto* call = std::get_if<CallExpr>(&es->expr->node)) {
                    if (call->block_arg) {
                        if (max_line <= 0 || es->line <= max_line) {
                            for (std::size_t pi = 0; pi < call->block_arg->params.size(); ++pi) {
                                const auto& bp_name = call->block_arg->params[pi];
                                if (bp_name.empty() || bp_name == "_") continue;
                                LocalBindingInfo pinfo;
                                pinfo.name = bp_name;
                                pinfo.is_mut = false;
                                pinfo.is_param = true;
                                pinfo.intent = "in";
                                pinfo.decl_line = es->line;
                                if (pi < call->type_args.size()) {
                                    pinfo.cf_type = cfire::fmt::format_type_ref(call->type_args[pi]);
                                } else if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node)) {
                                    if (mem->field == "on" || mem->field == "bind_raw") {
                                        pinfo.cf_type = "str";
                                    } else {
                                        pinfo.cf_type = "auto";
                                    }
                                } else {
                                    pinfo.cf_type = "auto";
                                }
                                pinfo.cpp_type = cf_type_string_to_cpp(pinfo.cf_type);
                                pinfo.inferred_colon_eq = false;
                                env[pinfo.name] = pinfo;
                            }
                        }
                        collect_stmt_bindings(call->block_arg->body, ast, ws, lines, max_line, env, ordered_out);
                    }
                }
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

inline void walk_expr_calls(const cfire::frontend::ExprPtr& expr, const std::function<void(const cfire::frontend::CallExpr&)>& visitor) {
    walk_exprs(expr, [&](const cfire::frontend::ExprPtr& ex) {
        if (const auto* c = std::get_if<cfire::frontend::CallExpr>(&ex->node)) {
            visitor(*c);
        }
    });
}

inline void walk_stmts_calls(const std::vector<cfire::frontend::StmtPtr>& stmts, const std::function<void(const cfire::frontend::CallExpr&)>& visitor) {
    walk_stmt_exprs(stmts, [&](const cfire::frontend::ExprPtr& ex) {
        if (const auto* c = std::get_if<cfire::frontend::CallExpr>(&ex->node)) {
            visitor(*c);
        }
    });
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
            for (const auto& p : t.params) {
                LocalBindingInfo pi;
                pi.name = p.name;
                pi.cf_type = cfire::fmt::format_type_ref(p.type);
                pi.cpp_type = lower_type_preview(p.type, true, p.intent);
                pi.is_mut = false;
                pi.is_param = true;
                pi.intent = p.intent;
                pi.decl_line = p.line;
                env[pi.name] = pi;
            }
            for (const auto& bv : t.bench_variations) {
                LocalBindingInfo bi;
                bi.name = bv.name;
                bi.cf_type = cfire::fmt::format_type_ref(bv.inferred_type);
                bi.cpp_type = lower_type_preview(bv.inferred_type, true, "in");
                bi.is_mut = false;
                bi.is_param = true;
                bi.intent = "in";
                bi.decl_line = bv.line;
                env[bi.name] = bi;
            }
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
    int severity = 1;  // 1 = Error, 2 = Warning
};

} // namespace detail
} // namespace cfire::lsp
