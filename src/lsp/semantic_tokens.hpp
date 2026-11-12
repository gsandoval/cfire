#pragma once
// src/lsp/semantic_tokens.hpp — LSP `textDocument/semanticTokens/full` Provider

#include "server.hpp"

namespace cfire::lsp {

inline auto Server::compute_semantic_tokens(const std::string& uri) -> std::string {
    std::string text = get_doc_text(uri);
    auto toks = cfire::frontend::tokenize(detail::uri_to_path(uri), text);
    if (!toks || toks->empty()) return "[]";

    auto [ast_res, ws] = get_or_parse_ast(uri, text);

    // 1. Symbol classification maps
    std::unordered_map<std::string, int> type_kinds = {
        {"void", 0}, {"bool", 0}, {"str", 0}, {"string", 0},
        {"i8", 0}, {"i16", 0}, {"i32", 0}, {"i64", 0},
        {"u8", 0}, {"u16", 0}, {"u32", 0}, {"u64", 0},
        {"f32", 0}, {"f64", 0}, {"usize", 0}, {"isize", 0},
        {"any", 0}
    };

    std::unordered_map<std::string, int> func_names;
    for (const auto& mod : cfire::doc::stdlib_catalog()) {
        for (const auto& fn : mod.functions) {
            if (auto dot = fn.name.find('.'); dot != std::string::npos) {
                func_names[fn.name.substr(dot + 1)] = 9; // method
            } else {
                func_names[fn.name] = 8; // function
            }
        }
    }

    std::unordered_set<std::string> builtins = {"println", "print", "eprintln", "eprint", "panic", "assert", "assert_eq", "assert_ne", "black_box", "make"};
    std::unordered_set<std::string> namespaces = {"cpp"};
    for (const auto& mod : cfire::doc::stdlib_catalog()) {
        namespaces.insert(mod.ns_alias);
    }

    std::unordered_set<int> func_decl_lines;
    std::map<std::pair<int, int>, int> param_mods;
    std::unordered_map<std::string, int> var_mut_state;
    std::set<std::pair<int, std::string>> var_decl_sites;
    std::set<std::pair<int, std::string>> type_decl_sites;

    if (ast_res) {
        for (const auto& st : ast_res->structs) {
            type_kinds[st.name] = 1; // struct
            type_decl_sites.insert({st.line, st.name});
        }
        for (const auto& en : ast_res->enums) {
            type_kinds[en.name] = 2; // enum
            type_decl_sites.insert({en.line, en.name});
            for (const auto& v : en.variants) {
                type_kinds[v.name] = 7; // enumMember
            }
        }
        for (const auto& ifc : ast_res->interfaces) {
            type_kinds[ifc.name] = 3; // interface
            type_decl_sites.insert({ifc.line, ifc.name});
        }

        for (const auto& fn : ast_res->functions) {
            func_decl_lines.insert(fn.line);
            func_names[fn.name] = fn.receiver ? 9 : 8;
            if (fn.receiver) {
                int mod = (fn.receiver->intent == "mut" ? 8 : 4);
                var_mut_state[fn.receiver->name] = mod;
            }
            for (const auto& p : fn.params) {
                int mod = (p.intent == "mut" ? 8 : 4);
                param_mods[{p.line, p.col}] = mod | 1;
                var_mut_state[p.name] = mod;
            }
        }

        for (const auto& gv : ast_res->global_vars) {
            int mod = (gv.is_mut ? 8 : 4);
            var_mut_state[gv.name] = mod;
            var_decl_sites.insert({gv.line, gv.name});
        }

        auto lines = detail::split_lines(text);
        std::unordered_map<std::string, detail::LocalBindingInfo> env;
        std::vector<detail::LocalBindingInfo> ordered;
        for (const auto& fn : ast_res->functions) {
            detail::collect_stmt_bindings(fn.body, *ast_res, ws, lines, 0, env, ordered);
        }
        for (const auto& [vname, b] : env) {
            int mod = (b.is_mut ? 8 : 4);
            var_mut_state[vname] = mod;
            var_decl_sites.insert({b.decl_line, vname});
        }
    }

    for (const auto& pkg : ws.local_packages) {
        namespaces.insert(pkg.alias);
        for (const auto& st : pkg.ast.structs) {
            type_kinds[st.name] = 1;
            type_kinds[pkg.alias + "." + st.name] = 1;
        }
        for (const auto& en : pkg.ast.enums) {
            type_kinds[en.name] = 2;
            type_kinds[pkg.alias + "." + en.name] = 2;
        }
        for (const auto& fn : pkg.ast.functions) {
            func_names[fn.name] = fn.receiver ? 9 : 8;
        }
    }

    struct RawToken {
        int line_0;
        int col_0;
        int length;
        int type;
        int modifiers;
    };

    std::vector<RawToken> raw_tokens;
    raw_tokens.reserve(toks->size());

    using namespace cfire::frontend;
    auto classify_and_push_non_string = [&](const Token& t) {
        if (t.kind == TokenKind::Eof) return;
        int line_0 = std::max(0, t.line - 1);
        int col_0 = std::max(0, t.col - 1);

        if (t.kind == TokenKind::CppBlock) {
            raw_tokens.push_back(RawToken{line_0, col_0, 9, 10 /* keyword */, 0});
            return;
        }
        if (t.kind == TokenKind::RawString) {
            int len = static_cast<int>(t.lexeme.size() + 2);
            int type = (t.lexeme.contains(':') ? 6 /* property */ : 12 /* string */);
            raw_tokens.push_back(RawToken{line_0, col_0, len, type, 0});
            return;
        }
        if (t.kind == TokenKind::Int || t.kind == TokenKind::Float) {
            raw_tokens.push_back(RawToken{line_0, col_0, static_cast<int>(t.lexeme.size()), 13 /* number */, 0});
            return;
        }
        if (t.kind == TokenKind::KwMut || t.kind == TokenKind::KwMove ||
            t.kind == TokenKind::KwIn ||
            t.kind == TokenKind::KwAsync) {
            raw_tokens.push_back(RawToken{line_0, col_0, static_cast<int>(t.lexeme.size()), 11 /* modifier */, 0});
            return;
        }
        if (t.kind == TokenKind::KwPackage || t.kind == TokenKind::KwImport ||
            t.kind == TokenKind::KwType || t.kind == TokenKind::KwStruct ||
            t.kind == TokenKind::KwEnum || t.kind == TokenKind::KwInterface ||
            t.kind == TokenKind::KwFn ||
            t.kind == TokenKind::KwIf || t.kind == TokenKind::KwElse ||
            t.kind == TokenKind::KwFor || t.kind == TokenKind::KwMatch ||
            t.kind == TokenKind::KwReturn || t.kind == TokenKind::KwDefer ||
            t.kind == TokenKind::KwBreak || t.kind == TokenKind::KwContinue ||
            t.kind == TokenKind::KwTry || t.kind == TokenKind::KwOr ||
            t.kind == TokenKind::KwTrue || t.kind == TokenKind::KwFalse ||
            t.kind == TokenKind::KwNone || t.kind == TokenKind::KwMap ||
            t.kind == TokenKind::KwAs) {
            raw_tokens.push_back(RawToken{line_0, col_0, static_cast<int>(t.lexeme.size()), 10 /* keyword */, 0});
            return;
        }
        if (t.kind == TokenKind::ColonEq || t.kind == TokenKind::PlusEq ||
            t.kind == TokenKind::MinusEq || t.kind == TokenKind::StarEq ||
            t.kind == TokenKind::SlashEq || t.kind == TokenKind::PercentEq ||
            t.kind == TokenKind::ShlEq || t.kind == TokenKind::ShrEq ||
            t.kind == TokenKind::AmpEq || t.kind == TokenKind::PipeEq ||
            t.kind == TokenKind::CaretEq || t.kind == TokenKind::Shl ||
            t.kind == TokenKind::Shr || t.kind == TokenKind::Assign ||
            t.kind == TokenKind::Eq || t.kind == TokenKind::Neq ||
            t.kind == TokenKind::Lt || t.kind == TokenKind::Gt ||
            t.kind == TokenKind::Le || t.kind == TokenKind::Ge ||
            t.kind == TokenKind::DoublePipe || t.kind == TokenKind::Arrow ||
            t.kind == TokenKind::FatArrow || t.kind == TokenKind::DoubleColon ||
            t.kind == TokenKind::DotDot || t.kind == TokenKind::DotDotEq) {
            raw_tokens.push_back(RawToken{line_0, col_0, static_cast<int>(t.lexeme.size()), 14 /* operator */, 0});
            return;
        }
        if (t.kind == TokenKind::Ident || t.kind == TokenKind::KwCpp) {
            int len = static_cast<int>(t.lexeme.size());
            if (t.col == 1 && (t.lexeme == "fuzz" || t.lexeme == "bench")) {
                raw_tokens.push_back(RawToken{line_0, col_0, len, 10 /* keyword */, 0});
            } else if (builtins.contains(t.lexeme)) {
                raw_tokens.push_back(RawToken{line_0, col_0, len, 8 /* function */, 16 /* defaultLibrary */});
            } else if (namespaces.contains(t.lexeme)) {
                raw_tokens.push_back(RawToken{line_0, col_0, len, 15 /* namespace */, (t.lexeme == "cpp" ? 16 : 0)});
            } else if (param_mods.contains({t.line, t.col})) {
                raw_tokens.push_back(RawToken{line_0, col_0, len, 4 /* parameter */, param_mods[{t.line, t.col}]});
            } else if (type_decl_sites.contains({t.line, t.lexeme})) {
                int tk = type_kinds.contains(t.lexeme) ? type_kinds[t.lexeme] : 1;
                raw_tokens.push_back(RawToken{line_0, col_0, len, tk, 1 /* declaration */});
            } else if (type_kinds.contains(t.lexeme)) {
                raw_tokens.push_back(RawToken{line_0, col_0, len, type_kinds[t.lexeme], 0});
            } else if (func_decl_lines.contains(t.line) && func_names.contains(t.lexeme)) {
                raw_tokens.push_back(RawToken{line_0, col_0, len, func_names[t.lexeme], 1 /* declaration */});
            } else if (func_names.contains(t.lexeme)) {
                raw_tokens.push_back(RawToken{line_0, col_0, len, func_names[t.lexeme], 0});
            } else if (var_decl_sites.contains({t.line, t.lexeme})) {
                int mod = var_mut_state.contains(t.lexeme) ? var_mut_state[t.lexeme] : 4;
                raw_tokens.push_back(RawToken{line_0, col_0, len, 5 /* variable */, mod | 1 /* declaration */});
            } else if (var_mut_state.contains(t.lexeme)) {
                raw_tokens.push_back(RawToken{line_0, col_0, len, 5 /* variable */, var_mut_state[t.lexeme]});
            } else if (t.lexeme[0] >= 'A' && t.lexeme[0] <= 'Z') {
                raw_tokens.push_back(RawToken{line_0, col_0, len, 1 /* struct */, 0});
            } else {
                raw_tokens.push_back(RawToken{line_0, col_0, len, 5 /* variable */, 4 /* readonly */});
            }
            return;
        }
    };

    for (const auto& t : *toks) {
        if (t.kind == TokenKind::Eof) continue;

        int line_0 = std::max(0, t.line - 1);
        int col_0 = std::max(0, t.col - 1);

        if (t.kind == TokenKind::String) {
            auto placeholders = scan_interpolation_placeholders(t.lexeme);
            if (placeholders.empty()) {
                int len = static_cast<int>(t.lexeme.size() + 2);
                raw_tokens.push_back(RawToken{line_0, col_0, len, 12 /* string */, 0});
            } else {
                // Split string into literal segments, '{' / '}' operators, and inner expression tokens
                int seg_start_col_0 = col_0; // includes opening '"'
                std::size_t prev_lex_idx = 0;
                for (const auto& ph : placeholders) {
                    int open_brace_col_0 = col_0 + 1 + static_cast<int>(ph.start_pos);
                    int seg_len = open_brace_col_0 - seg_start_col_0;
                    if (seg_len > 0) {
                        raw_tokens.push_back(RawToken{line_0, seg_start_col_0, seg_len, 12 /* string */, 0});
                    }
                    // '{'
                    raw_tokens.push_back(RawToken{line_0, open_brace_col_0, 1, 14 /* operator */, 0});
                    // Inner expression tokens
                    if (auto sub_toks = tokenize(detail::uri_to_path(uri), ph.expr_text)) {
                        int base_col_1 = t.col + 1 + static_cast<int>(ph.expr_offset);
                        for (auto sub_t : *sub_toks) {
                            if (sub_t.kind == TokenKind::Eof) continue;
                            if (sub_t.line == 1) {
                                sub_t.col = base_col_1 + (sub_t.col - 1);
                            }
                            sub_t.line = t.line + (sub_t.line - 1);
                            if (sub_t.kind == TokenKind::String) {
                                int slen = static_cast<int>(sub_t.lexeme.size() + 2);
                                raw_tokens.push_back(RawToken{std::max(0, sub_t.line - 1), std::max(0, sub_t.col - 1), slen, 12 /* string */, 0});
                            } else {
                                classify_and_push_non_string(sub_t);
                            }
                        }
                    }
                    // Optional ':fmt_spec'
                    int close_brace_col_0 = col_0 + 1 + static_cast<int>(ph.end_pos);
                    if (!ph.fmt_spec.empty()) {
                        int spec_len = static_cast<int>(ph.fmt_spec.size() + 1); // includes ':'
                        int spec_col_0 = close_brace_col_0 - spec_len;
                        if (spec_col_0 > open_brace_col_0) {
                            raw_tokens.push_back(RawToken{line_0, spec_col_0, spec_len, 6 /* property */, 0});
                        }
                    }
                    // '}'
                    raw_tokens.push_back(RawToken{line_0, close_brace_col_0, 1, 14 /* operator */, 0});
                    seg_start_col_0 = close_brace_col_0 + 1;
                    prev_lex_idx = ph.end_pos + 1;
                }
                int closing_quote_end_col_0 = col_0 + static_cast<int>(t.lexeme.size() + 2);
                int tail_len = closing_quote_end_col_0 - seg_start_col_0;
                if (tail_len > 0) {
                    raw_tokens.push_back(RawToken{line_0, seg_start_col_0, tail_len, 12 /* string */, 0});
                }
                (void)prev_lex_idx;
            }
            continue;
        }

        classify_and_push_non_string(t);
    }

    // Relative delta encoding per LSP 3.17 specification
    std::string out = "[";
    int prev_line = 0;
    int prev_col = 0;
    bool first = true;
    for (const auto& tok : raw_tokens) {
        int delta_line = tok.line_0 - prev_line;
        int delta_col = (delta_line == 0) ? (tok.col_0 - prev_col) : tok.col_0;

        if (!first) out += ",";
        first = false;
        out += std::format("{},{},{},{},{}", delta_line, delta_col, tok.length, tok.type, tok.modifiers);

        prev_line = tok.line_0;
        prev_col = tok.col_0;
    }
    out += "]";
    return out;
}

} // namespace cfire::lsp
