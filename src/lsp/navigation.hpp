#pragma once
// src/lsp/navigation.hpp — LSP Definition, TypeDefinition, Implementation, References, Rename & Symbol Providers

#include "server.hpp"

namespace cfire::lsp {

namespace detail {

[[nodiscard]] inline auto is_valid_identifier(std::string_view name) -> bool {
    if (name.empty()) return false;
    if (!std::isalpha(static_cast<unsigned char>(name[0])) && name[0] != '_') return false;
    for (char c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') return false;
    }
    if (name == "_" || name == "true" || name == "false" || name == "none" || name == "nil" || name == "null") {
        return false;
    }
    if (cfire::frontend::find_keyword(name).has_value()) {
        return false;
    }
    return true;
}

[[nodiscard]] inline auto check_rename_collision(
    const cfire::frontend::FileAST& ast,
    const WorkspaceContext& ws,
    const std::vector<std::string>& lines,
    int line_0,
    std::string_view new_name
) -> std::optional<std::string> {
    if (!is_valid_identifier(new_name)) {
        return std::format("cfire/REFACTOR-INVALID-NAME: '{}' is not a valid non-keyword cfire identifier", new_name);
    }
    auto env = build_scope_bindings(ast, ws, lines, line_0);
    if (env.contains(std::string(new_name))) {
        const auto& existing = env.at(std::string(new_name));
        return std::format("cfire/REFACTOR-COLLISION: symbol '{}' already exists in scope (declared at line {})",
            new_name, existing.decl_line);
    }
    for (const auto& st : ast.structs) {
        if (st.name == new_name) {
            return std::format("cfire/REFACTOR-COLLISION: struct '{}' already exists in package at line {}",
                new_name, st.line);
        }
    }
    for (const auto& fn : ast.functions) {
        if (fn.name == new_name && !fn.receiver) {
            return std::format("cfire/REFACTOR-COLLISION: function '{}' already exists in package at line {}",
                new_name, fn.line);
        }
    }
    for (const auto& en : ast.enums) {
        if (en.name == new_name) {
            return std::format("cfire/REFACTOR-COLLISION: enum '{}' already exists in package at line {}",
                new_name, en.line);
        }
    }
    for (const auto& gv : ast.global_vars) {
        if (gv.name == new_name) {
            return std::format("cfire/REFACTOR-COLLISION: global variable '{}' already exists in package at line {}",
                new_name, gv.line);
        }
    }
    for (const auto& imp : ast.imports) {
        if (imp.alias == new_name) {
            return std::format("cfire/REFACTOR-COLLISION: import alias '{}' already exists in package at line {}",
                new_name, imp.line);
        }
    }
    return std::nullopt;
}

} // namespace detail

inline auto Server::compute_definition(const std::string& uri, int line, int character) -> std::string {
    std::string text = get_doc_text(uri);
    auto lines = detail::split_lines(text);
    auto [ast_res, ws] = get_or_parse_ast(uri, text, line, character);
    if (!ast_res) return "null";

    // Check if cursor is on an `import "./..."` declaration line
    for (const auto& imp : ast_res->imports) {
        if (imp.line - 1 == line) {
            for (const auto& pkg : ws.local_packages) {
                if (pkg.import_path == imp.path) {
                    return std::format(
                        R"({{"uri":"{}","range":{{"start":{{"line":0,"character":0}},"end":{{"line":0,"character":12}}}}}})",
                        detail::json_escape(pkg.uri));
                }
            }
        }
    }

    auto sym = detail::symbol_at_position(lines, line, character);
    if (sym.name.empty()) return "null";

    std::string target_uri = uri;
    int target_line = -1;
    auto env = detail::build_scope_bindings(*ast_res, ws, lines, line);

    // 1. Check cross-package references (`checksum` or `checksum.compute_tag`)
    for (const auto& pkg : ws.local_packages) {
        if (sym.qualifier.empty() && sym.name == pkg.alias) {
            return std::format(
                R"({{"uri":"{}","range":{{"start":{{"line":0,"character":0}},"end":{{"line":0,"character":12}}}}}})",
                detail::json_escape(pkg.uri));
        }
        if (sym.qualifier == pkg.alias) {
            for (const auto& fn : pkg.ast.functions) {
                if (fn.name == sym.name) {
                    target_uri = pkg.uri;
                    target_line = fn.line - 1;
                    break;
                }
            }
            for (const auto& st : pkg.ast.structs) {
                if (st.name == sym.name) {
                    target_uri = pkg.uri;
                    target_line = st.line - 1;
                    break;
                }
            }
            for (const auto& en : pkg.ast.enums) {
                if (en.name == sym.name) {
                    target_uri = pkg.uri;
                    target_line = en.line - 1;
                    break;
                }
            }
        }
    }

    // 2. Check struct fields, methods, or enum variants (`obj.field`, `obj.method`, `Enum.Variant`)
    if (target_line < 0 && !sym.qualifier.empty()) {
        std::string rtype = detail::resolve_qualifier_type(sym.qualifier, *ast_res, ws, env);
        for (const auto& st : ast_res->structs) {
            if (st.name == rtype) {
                for (const auto& f : st.fields) {
                    if (f.name == sym.name) { target_line = f.line - 1; break; }
                }
            }
        }
        for (const auto& fn : ast_res->functions) {
            if (fn.receiver && fn.receiver->type.name == rtype && fn.name == sym.name) {
                target_line = fn.line - 1;
                break;
            }
        }
        for (const auto& en : ast_res->enums) {
            if (en.name == rtype) {
                for (const auto& v : en.variants) {
                    if (v.name == sym.name) { target_line = v.line - 1; break; }
                }
            }
        }
        if (target_line < 0) {
            for (const auto& pkg : ws.local_packages) {
                for (const auto& st : pkg.ast.structs) {
                    if (st.name == rtype || pkg.alias + "." + st.name == rtype) {
                        for (const auto& f : st.fields) {
                            if (f.name == sym.name) { target_uri = pkg.uri; target_line = f.line - 1; break; }
                        }
                    }
                    if (target_line >= 0) break;
                }
                if (target_line >= 0) break;
                for (const auto& fn : pkg.ast.functions) {
                    if (fn.receiver && (fn.receiver->type.name == rtype || pkg.alias + "." + fn.receiver->type.name == rtype) && fn.name == sym.name) {
                        target_uri = pkg.uri;
                        target_line = fn.line - 1;
                        break;
                    }
                }
                if (target_line >= 0) break;
                for (const auto& en : pkg.ast.enums) {
                    if (en.name == rtype || pkg.alias + "." + en.name == rtype) {
                        for (const auto& v : en.variants) {
                            if (v.name == sym.name) { target_uri = pkg.uri; target_line = v.line - 1; break; }
                        }
                    }
                    if (target_line >= 0) break;
                }
                if (target_line >= 0) break;
            }
        }
    }
    if (target_line < 0) {
        if (auto it = env.find(sym.name); it != env.end()) {
            target_line = it->second.decl_line - 1;
        }
    }
    if (target_line < 0) {
        for (const auto& fn : ast_res->functions) {
            if (fn.name == sym.name) { target_line = fn.line - 1; break; }
        }
    }
    if (target_line < 0) {
        for (const auto& st : ast_res->structs) {
            if (st.name == sym.name) { target_line = st.line - 1; break; }
        }
    }
    if (target_line < 0) {
        for (const auto& en : ast_res->enums) {
            if (en.name == sym.name) { target_line = en.line - 1; break; }
            for (const auto& v : en.variants) {
                if (v.name == sym.name) { target_line = v.line - 1; break; }
            }
        }
    }
    if (target_line < 0) {
        for (const auto& iface : ast_res->interfaces) {
            if (iface.name == sym.name) { target_line = iface.line - 1; break; }
        }
    }
    if (target_line < 0) {
        for (const auto& pkg : ws.local_packages) {
            for (const auto& fn : pkg.ast.functions) {
                if (fn.name == sym.name) { target_uri = pkg.uri; target_line = fn.line - 1; break; }
            }
            if (target_line >= 0) break;
            for (const auto& st : pkg.ast.structs) {
                if (st.name == sym.name) { target_uri = pkg.uri; target_line = st.line - 1; break; }
            }
            if (target_line >= 0) break;
            for (const auto& en : pkg.ast.enums) {
                if (en.name == sym.name) { target_uri = pkg.uri; target_line = en.line - 1; break; }
                for (const auto& v : en.variants) {
                    if (v.name == sym.name) { target_uri = pkg.uri; target_line = v.line - 1; break; }
                }
            }
            if (target_line >= 0) break;
        }
    }
    if (target_line < 0) return "null";

    return std::format(
        R"({{"uri":"{}","range":{{"start":{{"line":{},"character":0}},"end":{{"line":{},"character":12}}}}}})",
        detail::json_escape(target_uri), target_line, target_line);
}

inline auto Server::compute_type_definition(const std::string& uri, int line, int character) -> std::string {
    std::string text = get_doc_text(uri);
    auto lines = detail::split_lines(text);
    auto [ast_res, ws] = get_or_parse_ast(uri, text, line, character);
    if (!ast_res) return "null";

    auto sym = detail::symbol_at_position(lines, line, character);
    if (sym.name.empty()) return "null";

    auto env = detail::build_scope_bindings(*ast_res, ws, lines, line);

    std::string raw_type;
    // 1. If symbol is qualified (`obj.field`), get the type of field
    if (!sym.qualifier.empty()) {
        std::string rtype = detail::resolve_qualifier_type(sym.qualifier, *ast_res, ws, env);
        for (const auto& st : ast_res->structs) {
            if (st.name == rtype) {
                for (const auto& f : st.fields) {
                    if (f.name == sym.name) {
                        raw_type = cfire::fmt::format_type_ref(f.type);
                        break;
                    }
                }
            }
        }
        for (const auto& pkg : ws.local_packages) {
            for (const auto& st : pkg.ast.structs) {
                if (st.name == rtype) {
                    for (const auto& f : st.fields) {
                        if (f.name == sym.name) {
                            raw_type = cfire::fmt::format_type_ref(f.type);
                            break;
                        }
                    }
                }
            }
        }
    } else if (auto it = env.find(sym.name); it != env.end()) {
        // 2. If symbol is a variable in scope
        raw_type = it->second.cf_type;
    } else {
        // 3. Fallback: symbol itself might already be a type identifier (e.g. `AuditLog`)
        raw_type = sym.name;
    }

    // Unwrap type wrappers, collections, pointers, and intents:
    // []T -> T, own[T] -> T, shared[T] -> T, map[K]V -> V, *T -> T, mut T -> T, T? -> T, T! -> T
    auto unwrap_type = [](std::string t) -> std::string {
        bool changed = true;
        while (changed) {
            changed = false;
            while (!t.empty() && (t.front() == ' ' || t.front() == '\t')) t.erase(0, 1);
            while (!t.empty() && (t.back() == ' ' || t.back() == '\t')) t.pop_back();

            if (t.starts_with("mut ")) { t = t.substr(4); changed = true; continue; }
            if (t.starts_with("in ")) { t = t.substr(3); changed = true; continue; }
            if (t.starts_with("move ")) { t = t.substr(5); changed = true; continue; }
            if (t.starts_with("out ")) { t = t.substr(4); changed = true; continue; }
            if (t.starts_with("&") || t.starts_with("*")) { t = t.substr(1); changed = true; continue; }
            if (t.starts_with("[]")) { t = t.substr(2); changed = true; continue; }
            if (t.starts_with("own[") && t.ends_with("]")) {
                t = t.substr(4, t.size() - 5);
                changed = true;
                continue;
            }
            if (t.starts_with("shared[") && t.ends_with("]")) {
                t = t.substr(7, t.size() - 8);
                changed = true;
                continue;
            }
            if (t.starts_with("map[") && t.ends_with("]")) {
                if (auto close_br = t.find(']'); close_br != std::string::npos && close_br + 1 < t.size()) {
                    t = t.substr(close_br + 1);
                    changed = true;
                    continue;
                }
            }
            if (t.ends_with("?") || t.ends_with("!")) {
                t.pop_back();
                changed = true;
                continue;
            }
        }
        return t;
    };

    std::string type_name = unwrap_type(raw_type);
    if (type_name.empty()) return "null";

    // Filter out primitive types (per user clarification decision: return null)
    static const std::unordered_set<std::string_view> primitives = {
        "void", "bool", "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64",
        "f32", "f64", "str", "string", "usize", "isize", "any"
    };
    if (primitives.contains(type_name)) return "null";

    // Find declaration in current file
    for (const auto& st : ast_res->structs) {
        if (st.name == type_name) {
            return std::format(
                R"({{"uri":"{}","range":{{"start":{{"line":{},"character":0}},"end":{{"line":{},"character":12}}}}}})",
                detail::json_escape(uri), st.line - 1, st.line - 1);
        }
    }
    for (const auto& en : ast_res->enums) {
        if (en.name == type_name) {
            return std::format(
                R"({{"uri":"{}","range":{{"start":{{"line":{},"character":0}},"end":{{"line":{},"character":12}}}}}})",
                detail::json_escape(uri), en.line - 1, en.line - 1);
        }
    }
    for (const auto& iface : ast_res->interfaces) {
        if (iface.name == type_name) {
            return std::format(
                R"({{"uri":"{}","range":{{"start":{{"line":{},"character":0}},"end":{{"line":{},"character":12}}}}}})",
                detail::json_escape(uri), iface.line - 1, iface.line - 1);
        }
    }

    // Find declaration in local packages
    for (const auto& pkg : ws.local_packages) {
        std::string prefix = pkg.alias + ".";
        std::string stripped_type = type_name.starts_with(prefix) ? type_name.substr(prefix.size()) : type_name;
        for (const auto& st : pkg.ast.structs) {
            if (st.name == stripped_type) {
                return std::format(
                    R"({{"uri":"{}","range":{{"start":{{"line":{},"character":0}},"end":{{"line":{},"character":12}}}}}})",
                    detail::json_escape(pkg.uri), st.line - 1, st.line - 1);
            }
        }
        for (const auto& en : pkg.ast.enums) {
            if (en.name == stripped_type) {
                return std::format(
                    R"({{"uri":"{}","range":{{"start":{{"line":{},"character":0}},"end":{{"line":{},"character":12}}}}}})",
                    detail::json_escape(pkg.uri), en.line - 1, en.line - 1);
            }
        }
    }

    return "null";
}

inline auto Server::compute_implementation(const std::string& uri, int line, int character) -> std::string {
    std::string text = get_doc_text(uri);
    auto lines = detail::split_lines(text);
    auto [ast_res, ws] = get_or_parse_ast(uri, text, line, character);
    if (!ast_res) return "[]";

    auto sym = detail::symbol_at_position(lines, line, character);
    if (sym.name.empty()) return "[]";

    auto all_cf_files = discover_workspace_cf_files(uri);

    // 1. Check if symbol is an interface name
    const cfire::frontend::InterfaceDecl* target_iface = nullptr;
    for (const auto& iface : ast_res->interfaces) {
        if (iface.name == sym.name) {
            target_iface = &iface;
            break;
        }
    }
    if (!target_iface) {
        for (const auto& pkg : ws.local_packages) {
            for (const auto& iface : pkg.ast.interfaces) {
                if (iface.name == sym.name) {
                    target_iface = &iface;
                    break;
                }
            }
            if (target_iface) break;
        }
    }

    std::string out = "[";
    bool first = true;
    auto append_loc = [&](std::string_view doc_uri, int one_based_line) {
        int ln = std::max(0, one_based_line - 1);
        if (!first) out += ",";
        first = false;
        out += std::format(
            R"({{"uri":"{}","range":{{"start":{{"line":{},"character":0}},"end":{{"line":{},"character":12}}}}}})",
            detail::json_escape(doc_uri), ln, ln);
    };

    if (target_iface && !target_iface->methods.empty()) {
        auto check_ast_impls = [&](std::string_view doc_uri, const cfire::frontend::FileAST& doc_ast) {
            std::unordered_map<std::string, std::unordered_set<std::string>> struct_methods;
            for (const auto& fn : doc_ast.functions) {
                if (fn.receiver) {
                    struct_methods[fn.receiver->type.name].insert(fn.name);
                }
            }
            for (const auto& st : doc_ast.structs) {
                auto it = struct_methods.find(st.name);
                if (it == struct_methods.end()) continue;
                bool all_implemented = true;
                for (const auto& m : target_iface->methods) {
                    if (!it->second.contains(m.name)) {
                        all_implemented = false;
                        break;
                    }
                }
                if (all_implemented) {
                    append_loc(doc_uri, st.line);
                }
            }
        };
        check_ast_impls(uri, *ast_res);
        for (const auto& doc_uri : all_cf_files) {
            if (doc_uri == uri) continue;
            if (auto doc_ast = cfire::frontend::parse_source(detail::uri_to_path(doc_uri), get_doc_text(doc_uri))) {
                check_ast_impls(doc_uri, *doc_ast);
            }
        }
        out += "]";
        return out;
    }

    // 2. Otherwise, find all receiver methods (or functions) matching `sym.name`
    auto collect_methods = [&](std::string_view doc_uri, const cfire::frontend::FileAST& doc_ast, bool receiver_only) {
        for (const auto& fn : doc_ast.functions) {
            if (fn.name == sym.name && (!receiver_only || fn.receiver.has_value())) {
                append_loc(doc_uri, fn.line);
            }
        }
    };
    collect_methods(uri, *ast_res, true);
    for (const auto& doc_uri : all_cf_files) {
        if (doc_uri == uri) continue;
        if (auto doc_ast = cfire::frontend::parse_source(detail::uri_to_path(doc_uri), get_doc_text(doc_uri))) {
            collect_methods(doc_uri, *doc_ast, true);
        }
    }
    if (first) {
        collect_methods(uri, *ast_res, false);
        for (const auto& doc_uri : all_cf_files) {
            if (doc_uri == uri) continue;
            if (auto doc_ast = cfire::frontend::parse_source(detail::uri_to_path(doc_uri), get_doc_text(doc_uri))) {
                collect_methods(doc_uri, *doc_ast, false);
            }
        }
    }
    out += "]";
    return out;
}

namespace detail {
// CF-155: token ranges `[first, last]` where `name` is rebound by a closure parameter (`|name| ...`) or a match-arm
// pattern (`Some(name) => ...`). Inside such a range the name is a different variable from any outer binding.
[[nodiscard]] inline auto rebinding_scopes(const std::vector<cfire::frontend::Token>& toks, std::string_view name)
    -> std::vector<std::pair<std::size_t, std::size_t>> {
    using cfire::frontend::TokenKind;
    std::vector<std::pair<std::size_t, std::size_t>> out;
    auto match_close = [&](std::size_t open) -> std::size_t {
        int depth = 0;
        for (std::size_t k = open; k < toks.size(); ++k) {
            if (toks[k].kind == TokenKind::LBrace) ++depth;
            else if (toks[k].kind == TokenKind::RBrace && --depth == 0) return k;
        }
        return toks.size() - 1;
    };
    // End of an expression body starting at `k`: the matching `}` of a block, else the last token before a
    // depth-0 `,` / `)` / `}` or a line change.
    auto body_end = [&](std::size_t k) -> std::size_t {
        if (k >= toks.size()) return toks.size() - 1;
        if (toks[k].kind == TokenKind::LBrace) return match_close(k);
        int depth = 0;
        const int body_line = toks[k].line;
        std::size_t last = k;
        for (std::size_t j = k; j < toks.size(); ++j) {
            const auto kind = toks[j].kind;
            if (depth == 0 && (kind == TokenKind::Comma || kind == TokenKind::RParen || kind == TokenKind::RBrace || toks[j].line != body_line)) break;
            if (kind == TokenKind::LParen || kind == TokenKind::LBrace || kind == TokenKind::LBracket) ++depth;
            else if (kind == TokenKind::RParen || kind == TokenKind::RBrace || kind == TokenKind::RBracket) --depth;
            last = j;
        }
        return last;
    };
    for (std::size_t i = 0; i < toks.size(); ++i) {
        // Closures: `|a, mut b: T| body` after `(`, `,`, `:=`, `=`, `)` (trailing block), `{`, `=>` or `return`.
        if (toks[i].kind == TokenKind::Pipe) {
            const bool opener = i == 0 || toks[i - 1].kind == TokenKind::LParen || toks[i - 1].kind == TokenKind::Comma ||
                                toks[i - 1].kind == TokenKind::ColonEq || toks[i - 1].kind == TokenKind::Assign ||
                                toks[i - 1].kind == TokenKind::RParen || toks[i - 1].kind == TokenKind::LBrace ||
                                toks[i - 1].kind == TokenKind::FatArrow || toks[i - 1].kind == TokenKind::KwReturn;
            if (!opener) continue;
            std::size_t close = i + 1;
            bool binds = false;
            bool after_colon = false;
            for (; close < toks.size() && toks[close].kind != TokenKind::Pipe; ++close) {
                const auto& t = toks[close];
                if (t.kind == TokenKind::Colon) after_colon = true;
                else if (t.kind == TokenKind::Comma) after_colon = false;
                else if (t.kind == TokenKind::Ident && !after_colon && t.lexeme == name) binds = true;
                if (t.kind == TokenKind::LBrace || t.kind == TokenKind::Semicolon) break;
            }
            if (close >= toks.size() || toks[close].kind != TokenKind::Pipe) continue;
            std::size_t b = close + 1;
            if (b < toks.size() && toks[b].kind == TokenKind::Arrow) {
                while (b < toks.size() && toks[b].kind != TokenKind::LBrace) ++b;
            }
            if (binds) out.emplace_back(i, body_end(b));
            i = close;
            continue;
        }
        // Match arms: `pattern => body` inside `match x { ... }`.
        if (toks[i].kind == TokenKind::KwMatch) {
            std::size_t open = i + 1;
            while (open < toks.size() && toks[open].kind != TokenKind::LBrace) ++open;
            if (open >= toks.size()) break;
            const std::size_t close = match_close(open);
            std::size_t k = open + 1;
            while (k < close) {
                const std::size_t pat_start = k;
                bool binds = false;
                int depth = 0;
                for (; k < close; ++k) {
                    const auto& t = toks[k];
                    if (depth == 0 && t.kind == TokenKind::FatArrow) break;
                    if (t.kind == TokenKind::LParen || t.kind == TokenKind::LBracket || t.kind == TokenKind::LBrace) ++depth;
                    else if (t.kind == TokenKind::RParen || t.kind == TokenKind::RBracket || t.kind == TokenKind::RBrace) --depth;
                    const bool qualified = k > 0 && (toks[k - 1].kind == TokenKind::Dot || toks[k - 1].kind == TokenKind::DoubleColon);
                    const bool callee = k + 1 < close && (toks[k + 1].kind == TokenKind::LParen || toks[k + 1].kind == TokenKind::DoubleColon ||
                                                          toks[k + 1].kind == TokenKind::Dot || toks[k + 1].kind == TokenKind::LBrace);
                    if (t.kind == TokenKind::Ident && t.lexeme == name && !qualified && !callee) binds = true;
                }
                if (k >= close) break;
                const std::size_t end = body_end(k + 1);
                if (binds) out.emplace_back(pat_start, end);
                k = end + 1;
                if (k < close && toks[k].kind == TokenKind::Comma) ++k;
            }
        }
    }
    return out;
}
} // namespace detail

inline auto Server::find_symbol_occurrences(const std::string& uri, int line, int character, std::string_view target_name, std::string_view qualifier) -> std::vector<TokenOccurrence> {
    std::vector<TokenOccurrence> refs;
    if (target_name.empty()) return refs;

    std::string text = get_doc_text(uri);
    auto lines = detail::split_lines(text);

    // CF-155: closure parameters and match bindings are their own scopes.
    if (qualifier.empty()) {
        if (auto raw_toks = cfire::frontend::tokenize(detail::uri_to_path(uri), text)) {
            auto toks = detail::expand_tokens_with_interpolations(detail::uri_to_path(uri), *raw_toks);
            auto scopes = detail::rebinding_scopes(toks, target_name);
            std::size_t cursor = toks.size();
            for (std::size_t i = 0; i < toks.size(); ++i) {
                if (toks[i].line - 1 == line && toks[i].col - 1 <= character &&
                    character < toks[i].col - 1 + static_cast<int>(toks[i].lexeme.size())) {
                    cursor = i;
                    break;
                }
            }
            auto innermost = [&](std::size_t idx) -> std::optional<std::pair<std::size_t, std::size_t>> {
                std::optional<std::pair<std::size_t, std::size_t>> best;
                for (const auto& sc : scopes) {
                    if (idx >= sc.first && idx <= sc.second && (!best || sc.second - sc.first < best->second - best->first)) best = sc;
                }
                return best;
            };
            auto ident_at = [&](std::size_t i) {
                return toks[i].kind == cfire::frontend::TokenKind::Ident && toks[i].lexeme == target_name &&
                       !(i > 0 && toks[i - 1].kind == cfire::frontend::TokenKind::Dot);
            };
            if (cursor < toks.size()) {
                if (auto own = innermost(cursor)) {
                    for (std::size_t i = own->first; i <= own->second && i < toks.size(); ++i) {
                        if (!ident_at(i) || innermost(i) != own) continue;
                        int sc = std::max(0, toks[i].col - 1);
                        refs.push_back(TokenOccurrence{uri, std::max(0, toks[i].line - 1), sc, sc + static_cast<int>(toks[i].lexeme.size())});
                    }
                    return refs;
                }
            }
            if (!scopes.empty()) {
                // The cursor names an outer binding: compute its references as usual, then drop the shadowed ones.
                auto all = find_symbol_occurrences_unscoped(uri, line, character, target_name, qualifier);
                std::vector<TokenOccurrence> kept;
                for (const auto& r : all) {
                    bool shadowed = false;
                    if (r.uri == uri) {
                        for (std::size_t i = 0; i < toks.size(); ++i) {
                            if (toks[i].line - 1 == r.line && toks[i].col - 1 == r.start_col) {
                                shadowed = innermost(i).has_value();
                                break;
                            }
                        }
                    }
                    if (!shadowed) kept.push_back(r);
                }
                return kept;
            }
        }
    }
    return find_symbol_occurrences_unscoped(uri, line, character, target_name, qualifier);
}

inline auto Server::find_symbol_occurrences_unscoped(const std::string& uri, int line, int character, std::string_view target_name, std::string_view qualifier) -> std::vector<TokenOccurrence> {
    std::vector<TokenOccurrence> refs;
    if (target_name.empty()) return refs;

    std::string text = get_doc_text(uri);
    auto lines = detail::split_lines(text);
    auto [ast_res, ws] = get_or_parse_ast(uri, text, line, character);
    auto env = ast_res ? detail::build_scope_bindings(*ast_res, ws, lines, line) : std::unordered_map<std::string, detail::LocalBindingInfo>{};

    // 1. Check if symbol is a function-local variable, parameter, or receiver
    bool is_local = (qualifier.empty() && env.find(std::string(target_name)) != env.end());
    if (is_local && ast_res) {
        int target_decl_line = env.at(std::string(target_name)).decl_line;
        int start_line = 1;
        int end_line = static_cast<int>(lines.size());
        int target_line_1 = line + 1;

        for (const auto& fn : ast_res->functions) {
            if (fn.line <= target_line_1) {
                start_line = fn.line;
            }
        }
        for (const auto& t : ast_res->tests) {
            if (t.line <= target_line_1) {
                start_line = t.line;
            }
        }
        for (const auto& fn : ast_res->functions) {
            if (fn.line > start_line && fn.line < end_line) {
                end_line = fn.line - 1;
            }
        }
        for (const auto& t : ast_res->tests) {
            if (t.line > start_line && t.line < end_line) {
                end_line = t.line - 1;
            }
        }
        for (const auto& st : ast_res->structs) {
            if (st.line > start_line && st.line < end_line) {
                end_line = st.line - 1;
            }
        }

        if (auto raw_toks = cfire::frontend::tokenize(detail::uri_to_path(uri), text)) {
            auto toks = detail::expand_tokens_with_interpolations(detail::uri_to_path(uri), *raw_toks);
            for (std::size_t i = 0; i < toks.size(); ++i) {
                const auto& t = toks[i];
                if (t.line < start_line) continue;
                if (t.line > end_line) break;
                if (t.kind == cfire::frontend::TokenKind::Ident && t.lexeme == target_name) {
                    bool preceded_by_dot = (i > 0 && toks[i - 1].kind == cfire::frontend::TokenKind::Dot);
                    if (!preceded_by_dot) {
                        bool match = false;
                        if (t.line == target_decl_line) {
                            match = true;
                        } else if (t.line > target_decl_line) {
                            auto env_t = detail::build_scope_bindings(*ast_res, ws, lines, t.line - 1);
                            auto it = env_t.find(std::string(target_name));
                            if (it != env_t.end() && it->second.decl_line == target_decl_line) {
                                match = true;
                            }
                        }
                        if (match) {
                            int ln = std::max(0, t.line - 1);
                            int sc = std::max(0, t.col - 1);
                            int ec = sc + static_cast<int>(t.lexeme.size());
                            refs.push_back(TokenOccurrence{uri, ln, sc, ec});
                        }
                    }
                }
            }
        }
        return refs;
    }

    // 2. Cross-workspace / top-level symbol resolution
    std::string defining_pkg_name;
    std::string defining_uri = uri;
    bool is_field = false;

    if (!qualifier.empty()) {
        for (const auto& pkg : ws.local_packages) {
            if (pkg.alias == qualifier) {
                defining_pkg_name = pkg.ast.package_name;
                defining_uri = pkg.uri;
                break;
            }
        }
    } else if (ast_res) {
        defining_pkg_name = ast_res->package_name;
        for (const auto& st : ast_res->structs) {
            for (const auto& f : st.fields) {
                if (f.name == target_name) {
                    is_field = true;
                    break;
                }
            }
        }
    }

    auto all_cf_files = discover_workspace_cf_files(uri);

    for (const auto& doc_uri : all_cf_files) {
        std::string doc_text = get_doc_text(doc_uri);
        auto doc_ast_res = cfire::frontend::parse_source(detail::uri_to_path(doc_uri), doc_text);
        auto raw_doc_toks = cfire::frontend::tokenize(detail::uri_to_path(doc_uri), doc_text);
        if (!raw_doc_toks) continue;
        auto doc_toks = detail::expand_tokens_with_interpolations(detail::uri_to_path(doc_uri), *raw_doc_toks);

        bool same_pkg = (doc_uri == defining_uri) || (doc_ast_res && !defining_pkg_name.empty() && doc_ast_res->package_name == defining_pkg_name);
        std::optional<std::string> import_alias;

        if (doc_ast_res) {
            auto doc_ws = resolve_workspace(doc_uri, *doc_ast_res);
            for (const auto& pkg : doc_ws.local_packages) {
                if ((!defining_pkg_name.empty() && pkg.ast.package_name == defining_pkg_name) || pkg.uri == defining_uri) {
                    import_alias = pkg.alias;
                    break;
                }
            }
        }

        if (!same_pkg && !import_alias.has_value() && !is_field && !defining_pkg_name.empty()) {
            continue;
        }

        for (std::size_t i = 0; i < doc_toks.size(); ++i) {
            const auto& t = doc_toks[i];
            if (t.kind != cfire::frontend::TokenKind::Ident || t.lexeme != target_name) {
                continue;
            }

            bool preceded_by_dot = (i > 0 && doc_toks[i - 1].kind == cfire::frontend::TokenKind::Dot);

            if (same_pkg) {
                if (!preceded_by_dot || is_field) {
                    int ln = std::max(0, t.line - 1);
                    int sc = std::max(0, t.col - 1);
                    int ec = sc + static_cast<int>(t.lexeme.size());
                    refs.push_back(TokenOccurrence{doc_uri, ln, sc, ec});
                }
            } else if (import_alias.has_value()) {
                if (i >= 2 && doc_toks[i - 1].kind == cfire::frontend::TokenKind::Dot &&
                    doc_toks[i - 2].lexeme == *import_alias) {
                    int ln = std::max(0, t.line - 1);
                    int sc = std::max(0, t.col - 1);
                    int ec = sc + static_cast<int>(t.lexeme.size());
                    refs.push_back(TokenOccurrence{doc_uri, ln, sc, ec});
                } else if (!preceded_by_dot) {
                    int ln = std::max(0, t.line - 1);
                    int sc = std::max(0, t.col - 1);
                    int ec = sc + static_cast<int>(t.lexeme.size());
                    refs.push_back(TokenOccurrence{doc_uri, ln, sc, ec});
                }
            } else if (is_field && preceded_by_dot) {
                int ln = std::max(0, t.line - 1);
                int sc = std::max(0, t.col - 1);
                int ec = sc + static_cast<int>(t.lexeme.size());
                refs.push_back(TokenOccurrence{doc_uri, ln, sc, ec});
            }
        }
    }

    return refs;
}

inline auto Server::compute_references(const std::string& uri, int line, int character) -> std::string {
    std::string text = get_doc_text(uri);
    auto lines = detail::split_lines(text);
    auto sym = detail::symbol_at_position(lines, line, character);
    if (sym.name.empty()) return "[]";

    auto refs = find_symbol_occurrences(uri, line, character, sym.name, sym.qualifier);
    std::string out = "[";
    for (std::size_t i = 0; i < refs.size(); ++i) {
        if (i > 0) out += ",";
        const auto& r = refs[i];
        out += std::format(
            R"({{"uri":"{}","range":{{"start":{{"line":{},"character":{}}},"end":{{"line":{},"character":{}}}}}}})",
            detail::json_escape(r.uri), r.line, r.start_col, r.line, r.end_col);
    }
    out += "]";
    return out;
}

inline auto Server::compute_prepare_rename(const std::string& uri, int line, int character) -> std::string {
    std::string text = get_doc_text(uri);
    auto lines = detail::split_lines(text);
    auto sym = detail::symbol_at_position(lines, line, character);
    if (sym.name.empty()) return "null";
    if (!detail::is_valid_identifier(sym.name)) return "null";

    static const std::unordered_set<std::string_view> primitives = {
        "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64", "f32", "f64",
        "bool", "char", "str", "void", "usize", "isize", "rawptr", "any"
    };
    if (primitives.contains(sym.name)) return "null";

    for (const auto& mod : cfire::doc::stdlib_catalog()) {
        if (sym.name == mod.ns_alias) return "null";
    }
    return std::format(
        R"({{"range":{{"start":{{"line":{},"character":{}}},"end":{{"line":{},"character":{}}}}},"placeholder":"{}"}})",
        line, sym.start_col, line, sym.end_col, detail::json_escape(sym.name));
}

inline auto Server::compute_rename(const std::string& uri, int line, int character, std::string_view new_name) -> std::string {
    if (new_name.empty()) return "null";
    if (!detail::is_valid_identifier(new_name)) return "null";
    std::string text = get_doc_text(uri);
    auto lines = detail::split_lines(text);
    auto sym = detail::symbol_at_position(lines, line, character);
    if (sym.name.empty() || sym.name == new_name) return "null";

    auto [ast_res, ws] = get_or_parse_ast(uri, text, line, character);
    if (ast_res) {
        auto coll = detail::check_rename_collision(*ast_res, ws, lines, line, new_name);
        if (coll.has_value()) {
            return "null";
        }
    }

    auto refs = find_symbol_occurrences(uri, line, character, sym.name, sym.qualifier);
    if (refs.empty()) return "null";

    std::unordered_map<std::string, std::vector<TokenOccurrence>> by_uri;
    for (const auto& r : refs) {
        by_uri[r.uri].push_back(r);
    }

    std::string changes = "{";
    bool first_uri = true;
    for (const auto& [doc_uri, doc_refs] : by_uri) {
        if (!first_uri) changes += ",";
        first_uri = false;
        changes += std::format("\"{}\":[", detail::json_escape(doc_uri));
        for (std::size_t i = 0; i < doc_refs.size(); ++i) {
            if (i > 0) changes += ",";
            const auto& r = doc_refs[i];
            changes += std::format(
                R"({{"range":{{"start":{{"line":{},"character":{}}},"end":{{"line":{},"character":{}}}}},"newText":"{}"}})",
                r.line, r.start_col, r.line, r.end_col, detail::json_escape(new_name));
        }
        changes += "]";
    }
    changes += "}";
    return std::format(R"({{"changes":{}}})", changes);
}

inline auto Server::compute_document_symbols(const std::string& uri) -> std::string {
    std::string text = get_doc_text(uri);
    auto lines = detail::split_lines(text);
    auto [ast_res, ws] = get_or_parse_ast(uri, text);
    if (!ast_res) return "[]";

    auto find_block_end_line = [&](int start_line) -> int {
        int idx = std::max(0, start_line - 1);
        int depth = 0;
        bool found_open = false;
        for (int i = idx; i < static_cast<int>(lines.size()); ++i) {
            bool in_str = false;
            for (std::size_t k = 0; k < lines[i].size(); ++k) {
                char c = lines[i][k];
                if (c == '"' && (k == 0 || lines[i][k - 1] != '\\')) {
                    in_str = !in_str;
                    continue;
                }
                if (in_str) continue;
                if (c == '/' && k + 1 < lines[i].size() && lines[i][k + 1] == '/') break;
                if (c == '{') {
                    ++depth;
                    found_open = true;
                } else if (c == '}') {
                    --depth;
                    if (found_open && depth <= 0) {
                        return i;
                    }
                }
            }
        }
        return idx;
    };

    std::string out = "[";
    bool first = true;
    auto make_sym = [&](std::string_view name, std::string_view detail_str, int kind, int one_based_line, int end_line_0 = -1, std::string_view children_json = "[]") -> std::string {
        int start_ln = std::max(0, one_based_line - 1);
        int end_ln = end_line_0 >= 0 ? end_line_0 : start_ln;
        if (end_ln < start_ln) end_ln = start_ln;

        int line_len = start_ln < static_cast<int>(lines.size()) ? static_cast<int>(detail::utf8_col_to_utf16_col(lines[start_ln], lines[start_ln].size())) : 0;
        int end_line_len = end_ln < static_cast<int>(lines.size()) ? static_cast<int>(detail::utf8_col_to_utf16_col(lines[end_ln], lines[end_ln].size())) : 0;

        int sel_start = 0;
        int sel_end = 0;
        if (start_ln < static_cast<int>(lines.size())) {
            std::size_t found_pos = lines[start_ln].find(name);
            if (found_pos != std::string::npos) {
                sel_start = static_cast<int>(detail::utf8_col_to_utf16_col(lines[start_ln], found_pos));
                sel_end = static_cast<int>(detail::utf8_col_to_utf16_col(lines[start_ln], found_pos + name.size()));
            } else {
                sel_start = 0;
                sel_end = line_len;
            }
        }

        return std::format(
            R"({{"name":"{}","detail":"{}","kind":{},"range":{{"start":{{"line":{},"character":0}},"end":{{"line":{},"character":{}}}}},"selectionRange":{{"start":{{"line":{},"character":{}}},"end":{{"line":{},"character":{}}}}},"children":{}}})",
            detail::json_escape(name), detail::json_escape(detail_str), kind,
            start_ln, end_ln, end_line_len,
            start_ln, sel_start, start_ln, sel_end,
            children_json);
    };

    for (const auto& st : ast_res->structs) {
        std::string children = "[";
        for (std::size_t i = 0; i < st.fields.size(); ++i) {
            if (i > 0) children += ",";
            const auto& f = st.fields[i];
            children += make_sym(f.name, cfire::fmt::format_type_ref(f.type), 8, f.line, f.line - 1);
        }
        children += "]";
        if (!first) out += ",";
        first = false;
        out += make_sym(st.name, "struct", 23, st.line, find_block_end_line(st.line), children);
    }
    for (const auto& en : ast_res->enums) {
        std::string children = "[";
        for (std::size_t i = 0; i < en.variants.size(); ++i) {
            if (i > 0) children += ",";
            const auto& v = en.variants[i];
            std::string ptypes;
            for (std::size_t j = 0; j < v.payload_types.size(); ++j) {
                if (j > 0) ptypes += ", ";
                ptypes += cfire::fmt::format_type_ref(v.payload_types[j]);
            }
            children += make_sym(v.name, ptypes, 22, v.line, v.line - 1);
        }
        children += "]";
        if (!first) out += ",";
        first = false;
        out += make_sym(en.name, "enum", 10, en.line, find_block_end_line(en.line), children);
    }
    for (const auto& iface : ast_res->interfaces) {
        if (!first) out += ",";
        first = false;
        out += make_sym(iface.name, "interface", 11, iface.line, find_block_end_line(iface.line));
    }
    for (const auto& fn : ast_res->functions) {
        if (!first) out += ",";
        first = false;
        int kind = fn.receiver ? 6 : 12; // 6 = Method, 12 = Function
        std::string label = fn.receiver ? std::format("({}).{}", cfire::fmt::format_type_ref(fn.receiver->type), fn.name) : fn.name;
        out += make_sym(label, cfire::fmt::format_fn_signature(fn), kind, fn.line, find_block_end_line(fn.line));
    }
    for (const auto& gv : ast_res->global_vars) {
        if (!first) out += ",";
        first = false;
        out += make_sym(gv.name, "var", 13, gv.line, gv.line - 1);
    }
    for (const auto& t : ast_res->tests) {
        if (!first) out += ",";
        first = false;
        std::string prefix = t.is_fuzz ? "fuzz" : (t.is_bench ? "bench" : "test");
        out += make_sym(prefix + ": " + t.name, prefix, 12, t.line, find_block_end_line(t.line));
    }
    out += "]";
    return out;
}

inline auto Server::compute_folding_ranges(const std::string& uri) -> std::string {
    std::string text = get_doc_text(uri);
    auto lines = detail::split_lines(text);
    if (lines.empty()) return "[]";

    std::string out = "[";
    bool first = true;
    auto add_fold = [&](int start_line, int end_line, std::string_view kind = "") {
        if (start_line >= end_line) return;
        if (!first) out += ",";
        first = false;
        if (kind.empty()) {
            out += std::format(R"({{"startLine":{},"endLine":{}}})", start_line, end_line);
        } else {
            out += std::format(R"({{"startLine":{},"endLine":{},"kind":"{}"}})", start_line, end_line, detail::json_escape(kind));
        }
    };

    int import_start = -1;
    int import_end = -1;
    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        auto trimmed = cfire::fmt::detail::trim_ws(lines[i]);
        if (trimmed.starts_with("import ")) {
            if (import_start < 0) import_start = i;
            import_end = i;
        } else if (!trimmed.empty() && import_start >= 0) {
            if (import_end > import_start) {
                add_fold(import_start, import_end, "imports");
            }
            import_start = -1;
            import_end = -1;
        }
    }
    if (import_start >= 0 && import_end > import_start) {
        add_fold(import_start, import_end, "imports");
    }

    std::vector<int> brace_stack;
    bool in_block_comment = false;
    int block_comment_start = -1;

    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        const auto& ln = lines[i];
        bool in_str = false;
        for (std::size_t k = 0; k < ln.size(); ++k) {
            char c = ln[k];
            if (in_block_comment) {
                if (c == '*' && k + 1 < ln.size() && ln[k + 1] == '/') {
                    in_block_comment = false;
                    add_fold(block_comment_start, i, "comment");
                    ++k;
                }
                continue;
            }
            if (!in_str && c == '/' && k + 1 < ln.size() && ln[k + 1] == '*') {
                in_block_comment = true;
                block_comment_start = i;
                ++k;
                continue;
            }
            if (!in_str && c == '/' && k + 1 < ln.size() && ln[k + 1] == '/') {
                break;
            }
            if (c == '"' && (k == 0 || ln[k - 1] != '\\')) {
                in_str = !in_str;
                continue;
            }
            if (in_str) continue;

            if (c == '{') {
                brace_stack.push_back(i);
            } else if (c == '}') {
                if (!brace_stack.empty()) {
                    int open_line = brace_stack.back();
                    brace_stack.pop_back();
                    if (i > open_line) {
                        add_fold(open_line, i);
                    }
                }
            }
        }
    }

    out += "]";
    return out;
}

inline auto Server::compute_selection_range(const std::string& uri, int line, int character) -> std::string {
    std::string text = get_doc_text(uri);
    auto lines = detail::split_lines(text);
    if (lines.empty() || line < 0 || line >= static_cast<int>(lines.size())) {
        return "[]";
    }

    const auto& cur_line = lines[line];
    int line_len = static_cast<int>(detail::utf8_col_to_utf16_col(cur_line, cur_line.size()));
    auto sym = detail::symbol_at_position(lines, line, character);

    int sym_start = sym.name.empty() ? 0 : sym.start_col;
    int sym_end = sym.name.empty() ? line_len : sym.end_col;

    int block_start = 0;
    int block_end = static_cast<int>(lines.size()) - 1;
    bool has_block = false;
    int depth = 0;
    for (int i = line; i >= 0; --i) {
        for (int k = static_cast<int>(lines[i].size()) - 1; k >= 0; --k) {
            if (lines[i][k] == '}') ++depth;
            else if (lines[i][k] == '{') {
                if (depth == 0) {
                    block_start = i;
                    has_block = true;
                    break;
                } else {
                    --depth;
                }
            }
        }
        if (has_block) break;
    }
    if (has_block) {
        depth = 0;
        for (int i = block_start; i < static_cast<int>(lines.size()); ++i) {
            for (char c : lines[i]) {
                if (c == '{') ++depth;
                else if (c == '}') {
                    --depth;
                    if (depth <= 0) {
                        block_end = i;
                        break;
                    }
                }
            }
            if (depth <= 0) break;
        }
    }

    int block_end_len = block_end < static_cast<int>(lines.size()) ? static_cast<int>(detail::utf8_col_to_utf16_col(lines[block_end], lines[block_end].size())) : 0;
    int doc_end_line = static_cast<int>(lines.size()) - 1;
    int doc_end_len = static_cast<int>(detail::utf8_col_to_utf16_col(lines[doc_end_line], lines[doc_end_line].size()));

    std::string doc_range = std::format(
        R"({{"range":{{"start":{{"line":0,"character":0}},"end":{{"line":{},"character":{}}}}}}})",
        doc_end_line, doc_end_len);

    std::string block_range = has_block ? std::format(
        R"({{"range":{{"start":{{"line":{},"character":0}},"end":{{"line":{},"character":{}}}}},"parent":{}}})",
        block_start, block_end, block_end_len, doc_range) : doc_range;

    std::string line_range = std::format(
        R"({{"range":{{"start":{{"line":{},"character":0}},"end":{{"line":{},"character":{}}}}},"parent":{}}})",
        line, line, line_len, block_range);

    std::string token_range;
    if (!sym.name.empty()) {
        token_range = std::format(
            R"([ {{"range":{{"start":{{"line":{},"character":{}}},"end":{{"line":{},"character":{}}}}},"parent":{}}} ])",
            line, sym_start, line, sym_end, line_range);
    } else {
        token_range = std::format("[ {} ]", line_range);
    }

    return token_range;
}

inline auto Server::compute_document_highlight(const std::string& uri, int line, int character) -> std::string {
    std::string text = get_doc_text(uri);
    auto lines = detail::split_lines(text);
    auto sym = detail::symbol_at_position(lines, line, character);
    if (sym.name.empty()) return "[]";

    auto occurrences = find_symbol_occurrences(uri, line, character, sym.name, sym.qualifier);
    if (occurrences.empty()) return "[]";

    std::string out = "[";
    bool first = true;
    for (const auto& occ : occurrences) {
        if (!first) out += ",";
        first = false;
        out += std::format(
            R"({{"range":{{"start":{{"line":{},"character":{}}},"end":{{"line":{},"character":{}}}}},"kind":1}})",
            occ.line, occ.start_col, occ.line, occ.end_col);
    }
    out += "]";
    return out;
}

inline auto Server::compute_workspace_symbols(std::string_view query) -> std::string {
    auto matches_query = [&](std::string_view name) -> bool {
        if (query.empty()) return true;
        std::string ln(name);
        std::string lq(query);
        std::transform(ln.begin(), ln.end(), ln.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::transform(lq.begin(), lq.end(), lq.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return ln.find(lq) != std::string::npos;
    };

    std::string out = "[";
    bool first = true;
    auto add_ws_sym = [&](std::string_view name, int kind, std::string_view container, const std::string& uri, int one_based_line) {
        if (!matches_query(name)) return;
        if (!first) out += ",";
        first = false;
        int ln = std::max(0, one_based_line - 1);
        out += std::format(
            R"({{"name":"{}","kind":{},"containerName":"{}","location":{{"uri":"{}","range":{{"start":{{"line":{},"character":0}},"end":{{"line":{},"character":12}}}}}}}})",
            detail::json_escape(name), kind, detail::json_escape(container), detail::json_escape(uri), ln, ln);
    };

    // 1. Scan all open documents
    std::unordered_set<std::string> scanned_uris;
    for (const auto& [doc_uri, doc_text] : docs_) {
        scanned_uris.insert(doc_uri);
        if (auto ast = cfire::frontend::parse_source(detail::uri_to_path(doc_uri), doc_text)) {
            for (const auto& st : ast->structs) add_ws_sym(st.name, 23, ast->package_name, doc_uri, st.line);
            for (const auto& en : ast->enums) add_ws_sym(en.name, 10, ast->package_name, doc_uri, en.line);
            for (const auto& fn : ast->functions) add_ws_sym(fn.name, 12, ast->package_name, doc_uri, fn.line);
        }
    }
    // 2. Scan `core:*` stdlib catalog
    std::string fallback_uri = docs_.empty() ? "file:///stdlib.cf" : docs_.begin()->first;
    for (const auto& mod : cfire::doc::stdlib_catalog()) {
        for (const auto& fn : mod.functions) {
            add_ws_sym(mod.ns_alias + "." + fn.name, 12, mod.import_path, fallback_uri, 1);
        }
    }
    out += "]";
    return out;
}

} // namespace cfire::lsp
