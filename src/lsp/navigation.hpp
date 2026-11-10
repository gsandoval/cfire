#pragma once
// src/lsp/navigation.hpp — LSP Definition, TypeDefinition, Implementation, References, Rename & Symbol Providers

#include "server.hpp"

namespace cfire::lsp {

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

inline auto Server::find_symbol_occurrences(const std::string& uri, int line, int character, std::string_view target_name, std::string_view qualifier) -> std::vector<TokenOccurrence> {
    std::vector<TokenOccurrence> refs;
    if (target_name.empty()) return refs;

    std::string text = get_doc_text(uri);
    auto lines = detail::split_lines(text);
    auto [ast_res, ws] = get_or_parse_ast(uri, text, line, character);
    auto env = ast_res ? detail::build_scope_bindings(*ast_res, ws, lines, line) : std::unordered_map<std::string, detail::LocalBindingInfo>{};

    // 1. Check if symbol is a function-local variable, parameter, or receiver
    bool is_local = (qualifier.empty() && env.find(std::string(target_name)) != env.end());
    if (is_local && ast_res) {
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
                        int ln = std::max(0, t.line - 1);
                        int sc = std::max(0, t.col - 1);
                        int ec = sc + static_cast<int>(t.lexeme.size());
                        refs.push_back(TokenOccurrence{uri, ln, sc, ec});
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

    static const std::unordered_set<std::string_view> keywords = {
        "package", "import", "cpp", "as", "type", "struct", "enum", "interface",
        "map", "fn", "async", "in", "mut", "out", "move", "var",
        "if", "else", "for", "match", "defer", "return", "try", "or", "test",
        "true", "false", "none"
    };
    if (keywords.contains(sym.name)) return "null";

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
    std::string text = get_doc_text(uri);
    auto lines = detail::split_lines(text);
    auto sym = detail::symbol_at_position(lines, line, character);
    if (sym.name.empty()) return "null";

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
    auto [ast_res, ws] = get_or_parse_ast(uri, text);
    if (!ast_res) return "[]";

    std::string out = "[";
    bool first = true;
    auto make_sym = [](std::string_view name, std::string_view detail_str, int kind, int one_based_line, std::string_view children_json = "[]") -> std::string {
        int ln = std::max(0, one_based_line - 1);
        return std::format(
            R"({{"name":"{}","detail":"{}","kind":{},"range":{{"start":{{"line":{},"character":0}},"end":{{"line":{},"character":20}}}},"selectionRange":{{"start":{{"line":{},"character":0}},"end":{{"line":{},"character":10}}}},"children":{}}})",
            detail::json_escape(name), detail::json_escape(detail_str), kind, ln, ln, ln, ln, children_json);
    };

    for (const auto& st : ast_res->structs) {
        std::string children = "[";
        for (std::size_t i = 0; i < st.fields.size(); ++i) {
            if (i > 0) children += ",";
            const auto& f = st.fields[i];
            children += make_sym(f.name, cfire::fmt::format_type_ref(f.type), 8, f.line);
        }
        children += "]";
        if (!first) out += ",";
        first = false;
        out += make_sym(st.name, "struct", 23, st.line, children);
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
            children += make_sym(v.name, ptypes, 22, v.line);
        }
        children += "]";
        if (!first) out += ",";
        first = false;
        out += make_sym(en.name, "enum", 10, en.line, children);
    }
    for (const auto& iface : ast_res->interfaces) {
        if (!first) out += ",";
        first = false;
        out += make_sym(iface.name, "interface", 11, iface.line);
    }
    for (const auto& fn : ast_res->functions) {
        if (!first) out += ",";
        first = false;
        int kind = fn.receiver ? 6 : 12; // 6 = Method, 12 = Function
        std::string label = fn.receiver ? std::format("({}).{}", cfire::fmt::format_type_ref(fn.receiver->type), fn.name) : fn.name;
        out += make_sym(label, cfire::fmt::format_fn_signature(fn), kind, fn.line);
    }
    for (const auto& gv : ast_res->global_vars) {
        if (!first) out += ",";
        first = false;
        out += make_sym(gv.name, "var", 13, gv.line);
    }
    for (const auto& t : ast_res->tests) {
        if (!first) out += ",";
        first = false;
        std::string prefix = t.is_fuzz ? "fuzz" : (t.is_bench ? "bench" : "test");
        out += make_sym(prefix + ": " + t.name, prefix, 12, t.line);
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
