#include "cfire_prelude.hpp"
#include <lexer_types.hpp>
#include <ast.hpp>

struct SelfhostParser;

struct SelfhostParser {
    std::string file{};
    std::vector<cfire::frontend::Token> tokens{};
    std::size_t pos{};
    std::vector<std::string> errors{};
    bool resilient{};
};

template <>
struct cfire::reflect_struct<SelfhostParser> {
    static constexpr std::string_view name = "SelfhostParser";
    static constexpr std::size_t field_count = 5;
    static constexpr std::array<std::string_view, 5> field_names = {
        "file",
        "tokens",
        "pos",
        "errors",
        "resilient",
    };
    static constexpr std::array<std::string_view, 5> field_tags = {
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
    };
};

[[nodiscard]] auto parser_usize_to_str(std::size_t val) -> std::string;
[[nodiscard]] auto parser_i32_to_str(std::int32_t val) -> std::string;
[[nodiscard]] auto get_binary_precedence(const cfire::frontend::TokenKind& k) -> std::int32_t;
[[nodiscard]] auto make_parser(std::string_view file, const std::vector<cfire::frontend::Token>& tokens, bool resilient) -> SelfhostParser;
[[nodiscard]] auto SelfhostParser__cur(const SelfhostParser& p) -> cfire::frontend::Token;
[[nodiscard]] auto SelfhostParser__peek(const SelfhostParser& p, std::size_t offset) -> cfire::frontend::Token;
[[nodiscard]] auto SelfhostParser__advance(SelfhostParser& p) -> cfire::frontend::Token;
auto SelfhostParser__record_error(SelfhostParser& p, const std::string& err) -> void;
[[nodiscard]] auto SelfhostParser__expect(SelfhostParser& p, const cfire::frontend::TokenKind& kind) -> std::tuple<cfire::frontend::Token, bool>;
auto SelfhostParser__synchronize_decl(SelfhostParser& p) -> void;
auto SelfhostParser__synchronize_stmt(SelfhostParser& p) -> void;
[[nodiscard]] auto SelfhostParser__is_generic_call_lookahead(const SelfhostParser& p) -> bool;
[[nodiscard]] auto SelfhostParser__is_destructure_lookahead(const SelfhostParser& p, std::size_t offset) -> std::tuple<bool, bool>;
[[nodiscard]] auto SelfhostParser__parse_type_ref(SelfhostParser& p) -> std::tuple<cfire::frontend::TypeRef, bool>;
[[nodiscard]] auto SelfhostParser__parse_import(SelfhostParser& p) -> std::tuple<cfire::frontend::ImportDecl, bool>;
[[nodiscard]] auto SelfhostParser__parse_type_decl(SelfhostParser& p, cfire::frontend::FileAST& f) -> bool;
[[nodiscard]] auto SelfhostParser__parse_fn_decl(SelfhostParser& p) -> std::tuple<cfire::frontend::FnDecl, bool>;
[[nodiscard]] auto SelfhostParser__parse_global_var(SelfhostParser& p) -> std::tuple<cfire::frontend::GlobalVarDecl, bool>;
[[nodiscard]] auto SelfhostParser__parse_test_decl(SelfhostParser& p) -> std::tuple<cfire::frontend::TestDecl, bool>;
[[nodiscard]] auto SelfhostParser__parse_block(SelfhostParser& p) -> std::tuple<std::vector<cfire::frontend::StmtPtr>, bool>;
[[nodiscard]] auto SelfhostParser__parse_stmt(SelfhostParser& p) -> std::tuple<cfire::frontend::StmtPtr, bool>;
[[nodiscard]] auto SelfhostParser__parse_if_stmt(SelfhostParser& p) -> std::tuple<cfire::frontend::StmtPtr, bool>;
[[nodiscard]] auto SelfhostParser__parse_for_stmt(SelfhostParser& p) -> std::tuple<cfire::frontend::StmtPtr, bool>;
[[nodiscard]] auto SelfhostParser__parse_match_stmt(SelfhostParser& p) -> std::tuple<cfire::frontend::StmtPtr, bool>;
[[nodiscard]] auto SelfhostParser__parse_expr(SelfhostParser& p) -> std::tuple<cfire::frontend::ExprPtr, bool>;
[[nodiscard]] auto SelfhostParser__parse_or_expr(SelfhostParser& p) -> std::tuple<cfire::frontend::ExprPtr, bool>;
[[nodiscard]] auto SelfhostParser__parse_binary_expr(SelfhostParser& p, std::int32_t min_prec) -> std::tuple<cfire::frontend::ExprPtr, bool>;
[[nodiscard]] auto SelfhostParser__parse_unary_expr(SelfhostParser& p) -> std::tuple<cfire::frontend::ExprPtr, bool>;
[[nodiscard]] auto SelfhostParser__parse_postfix_expr(SelfhostParser& p) -> std::tuple<cfire::frontend::ExprPtr, bool>;
[[nodiscard]] auto SelfhostParser__parse_call_with_callee(SelfhostParser& p, const cfire::frontend::ExprPtr& callee, const std::vector<cfire::frontend::TypeRef>& type_args) -> std::tuple<cfire::frontend::ExprPtr, bool>;
[[nodiscard]] auto SelfhostParser__parse_closure_expr(SelfhostParser& p) -> std::tuple<cfire::frontend::ClosureExpr, bool>;
[[nodiscard]] auto SelfhostParser__parse_primary_expr(SelfhostParser& p) -> std::tuple<cfire::frontend::ExprPtr, bool>;
[[nodiscard]] auto SelfhostParser__parse_file_result(SelfhostParser& p) -> cfire::frontend::ParseResult;
[[nodiscard]] auto selfhost_parse_file_result(std::string_view file, const std::vector<cfire::frontend::Token>& tokens, bool resilient) -> cfire::frontend::ParseResult;

[[nodiscard]] auto parser_usize_to_str(std::size_t val) -> std::string {
    if (val == 0) {
        return "0";
    }
    auto v = val;
    std::string digits = "";
    while (v > 0) {
        auto d = (v % 10);
        if (d == 0) {
            digits = ("0" + digits);
        } else {
            if (d == 1) {
                digits = ("1" + digits);
            } else {
                if (d == 2) {
                    digits = ("2" + digits);
                } else {
                    if (d == 3) {
                        digits = ("3" + digits);
                    } else {
                        if (d == 4) {
                            digits = ("4" + digits);
                        } else {
                            if (d == 5) {
                                digits = ("5" + digits);
                            } else {
                                if (d == 6) {
                                    digits = ("6" + digits);
                                } else {
                                    if (d == 7) {
                                        digits = ("7" + digits);
                                    } else {
                                        if (d == 8) {
                                            digits = ("8" + digits);
                                        } else {
                                            digits = ("9" + digits);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        v = (v / 10);
    }
    return digits;
}

[[nodiscard]] auto parser_i32_to_str(std::int32_t val) -> std::string {
    if (val <= 0) {
        return "0";
    }
    auto v = val;
    std::string digits = "";
    while (v > 0) {
        auto d = (v % 10);
        if (d == 0) {
            digits = ("0" + digits);
        } else {
            if (d == 1) {
                digits = ("1" + digits);
            } else {
                if (d == 2) {
                    digits = ("2" + digits);
                } else {
                    if (d == 3) {
                        digits = ("3" + digits);
                    } else {
                        if (d == 4) {
                            digits = ("4" + digits);
                        } else {
                            if (d == 5) {
                                digits = ("5" + digits);
                            } else {
                                if (d == 6) {
                                    digits = ("6" + digits);
                                } else {
                                    if (d == 7) {
                                        digits = ("7" + digits);
                                    } else {
                                        if (d == 8) {
                                            digits = ("8" + digits);
                                        } else {
                                            digits = ("9" + digits);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        v = (v / 10);
    }
    return digits;
}

[[nodiscard]] auto get_binary_precedence(const cfire::frontend::TokenKind& k) -> std::int32_t {
    if (k == cfire::frontend::TokenKind::DoublePipe) {
        return 1;
    }
    if (k == cfire::frontend::TokenKind::DoubleAmp) {
        return 2;
    }
    if (k == cfire::frontend::TokenKind::Pipe) {
        return 3;
    }
    if (k == cfire::frontend::TokenKind::Amp) {
        return 4;
    }
    if ((k == cfire::frontend::TokenKind::Eq) || (k == cfire::frontend::TokenKind::Neq)) {
        return 5;
    }
    if ((((k == cfire::frontend::TokenKind::Lt) || (k == cfire::frontend::TokenKind::Gt)) || (k == cfire::frontend::TokenKind::Le)) || (k == cfire::frontend::TokenKind::Ge)) {
        return 6;
    }
    if ((k == cfire::frontend::TokenKind::Plus) || (k == cfire::frontend::TokenKind::Minus)) {
        return 7;
    }
    if (((k == cfire::frontend::TokenKind::Star) || (k == cfire::frontend::TokenKind::Slash)) || (k == cfire::frontend::TokenKind::Percent)) {
        return 8;
    }
    return 0;
}

[[nodiscard]] auto make_parser(std::string_view file, const std::vector<cfire::frontend::Token>& tokens, bool resilient) -> SelfhostParser {
    return SelfhostParser{.file = ("" + file), .tokens = tokens, .pos = 0, .errors = {}, .resilient = resilient};
}

[[nodiscard]] auto SelfhostParser__cur(const SelfhostParser& p) -> cfire::frontend::Token {
    if (p.pos < p.tokens.size()) {
        return p.tokens[p.pos];
    }
    return p.tokens[(p.tokens.size() - 1)];
}

[[nodiscard]] auto SelfhostParser__peek(const SelfhostParser& p, std::size_t offset) -> cfire::frontend::Token {
    auto idx = (p.pos + offset);
    if (idx < p.tokens.size()) {
        return p.tokens[idx];
    }
    return p.tokens[(p.tokens.size() - 1)];
}

[[nodiscard]] auto SelfhostParser__advance(SelfhostParser& p) -> cfire::frontend::Token {
    auto t = SelfhostParser__cur(p);
    if ((p.pos + 1) < p.tokens.size()) {
        p.pos = (p.pos + 1);
    }
    return t;
}

auto SelfhostParser__record_error(SelfhostParser& p, const std::string& err) -> void {
    if (p.errors.size() < 20) {
        (void)(p.errors.push_back(err));
    }
}

[[nodiscard]] auto SelfhostParser__expect(SelfhostParser& p, const cfire::frontend::TokenKind& kind) -> std::tuple<cfire::frontend::Token, bool> {
    auto t = SelfhostParser__cur(p);
    if (t.kind != kind) {
        auto err = (((((((((((p.file + ":") + parser_i32_to_str(t.line)) + ":") + parser_i32_to_str(t.col)) + ": expected '") + cfire::frontend::token_kind_name(kind)) + "', got '") + t.lexeme) + "' (") + cfire::frontend::token_kind_name(t.kind)) + ")");
        (void)(SelfhostParser__record_error(p, err));
        return std::make_tuple(t, false);
    }
    return std::make_tuple(SelfhostParser__advance(p), true);
}

auto SelfhostParser__synchronize_decl(SelfhostParser& p) -> void {
    if (SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::Eof) {
        (void)(SelfhostParser__advance(p));
    }
    while (SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::Eof) {
        auto k = SelfhostParser__cur(p).kind;
        if (((((((k == cfire::frontend::TokenKind::KwPackage) || (k == cfire::frontend::TokenKind::KwImport)) || (k == cfire::frontend::TokenKind::KwType)) || (k == cfire::frontend::TokenKind::KwFn)) || (k == cfire::frontend::TokenKind::KwVar)) || (k == cfire::frontend::TokenKind::KwMut)) || (k == cfire::frontend::TokenKind::KwTest)) {
            return;
        }
        if (k == cfire::frontend::TokenKind::Ident) {
            auto p1 = SelfhostParser__peek(p, 1).kind;
            if ((p1 == cfire::frontend::TokenKind::ColonEq) || (p1 == cfire::frontend::TokenKind::Colon)) {
                return;
            }
            (void)(SelfhostParser__advance(p));
        } else {
            (void)(SelfhostParser__advance(p));
        }
    }
}

auto SelfhostParser__synchronize_stmt(SelfhostParser& p) -> void {
    if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Semicolon) {
        (void)(SelfhostParser__advance(p));
        return;
    }
    if ((SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::Eof) && (SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::RBrace)) {
        (void)(SelfhostParser__advance(p));
    }
    while ((SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::Eof) && (SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::RBrace)) {
        if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Semicolon) {
            (void)(SelfhostParser__advance(p));
            return;
        }
        auto k = SelfhostParser__cur(p).kind;
        if (((((((((k == cfire::frontend::TokenKind::KwReturn) || (k == cfire::frontend::TokenKind::KwIf)) || (k == cfire::frontend::TokenKind::KwFor)) || (k == cfire::frontend::TokenKind::KwMatch)) || (k == cfire::frontend::TokenKind::KwVar)) || (k == cfire::frontend::TokenKind::KwMut)) || (k == cfire::frontend::TokenKind::KwDefer)) || (k == cfire::frontend::TokenKind::CppBlock)) || (k == cfire::frontend::TokenKind::KwAsync)) {
            return;
        }
        if (k == cfire::frontend::TokenKind::Ident) {
            auto p1 = SelfhostParser__peek(p, 1).kind;
            if (((((p1 == cfire::frontend::TokenKind::ColonEq) || (p1 == cfire::frontend::TokenKind::Colon)) || (p1 == cfire::frontend::TokenKind::Assign)) || (p1 == cfire::frontend::TokenKind::PlusEq)) || (p1 == cfire::frontend::TokenKind::MinusEq)) {
                return;
            }
        }
        if ((((k == cfire::frontend::TokenKind::KwFn) || (k == cfire::frontend::TokenKind::KwType)) || (k == cfire::frontend::TokenKind::KwTest)) || (k == cfire::frontend::TokenKind::KwImport)) {
            return;
        }
        (void)(SelfhostParser__advance(p));
    }
}

[[nodiscard]] auto SelfhostParser__is_generic_call_lookahead(const SelfhostParser& p) -> bool {
    if (SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::LBracket) {
        return false;
    }
    std::int32_t depth = 0;
    auto i = p.pos;
    auto n = p.tokens.size();
    while (i < n) {
        auto k = p.tokens[i].kind;
        if (k == cfire::frontend::TokenKind::LBracket) {
            depth = (depth + 1);
        } else {
            if (k == cfire::frontend::TokenKind::RBracket) {
                depth = (depth - 1);
                if (depth == 0) {
                    return (((i + 1) < n) && (p.tokens[(i + 1)].kind == cfire::frontend::TokenKind::LParen));
                }
            } else {
                if (((k == cfire::frontend::TokenKind::Semicolon) || (k == cfire::frontend::TokenKind::Eof)) || (k == cfire::frontend::TokenKind::RBrace)) {
                    break;
                }
            }
        }
        i = (i + 1);
    }
    return false;
}

[[nodiscard]] auto SelfhostParser__is_destructure_lookahead(const SelfhostParser& p, std::size_t offset) -> std::tuple<bool, bool> {
    auto start = (p.pos + offset);
    auto n = p.tokens.size();
    if ((start >= n) || (p.tokens[start].kind != cfire::frontend::TokenKind::LParen)) {
        return std::make_tuple(false, false);
    }
    auto i = (start + 1);
    std::int32_t depth = 1;
    while ((i < n) && (depth > 0)) {
        auto k = p.tokens[i].kind;
        if (k == cfire::frontend::TokenKind::LParen) {
            depth = (depth + 1);
        } else {
            if (k == cfire::frontend::TokenKind::RParen) {
                depth = (depth - 1);
            } else {
                if (((k == cfire::frontend::TokenKind::Semicolon) || (k == cfire::frontend::TokenKind::Eof)) || (k == cfire::frontend::TokenKind::RBrace)) {
                    return std::make_tuple(false, false);
                }
            }
        }
        i = (i + 1);
    }
    if ((depth == 0) && (i < n)) {
        if (p.tokens[i].kind == cfire::frontend::TokenKind::ColonEq) {
            return std::make_tuple(true, true);
        }
        if (p.tokens[i].kind == cfire::frontend::TokenKind::Assign) {
            return std::make_tuple(true, false);
        }
    }
    return std::make_tuple(false, false);
}

[[nodiscard]] auto SelfhostParser__parse_type_ref(SelfhostParser& p) -> std::tuple<cfire::frontend::TypeRef, bool> {
    auto tr = cfire::frontend::TypeRef{};
    if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Amp) {
        (void)(SelfhostParser__advance(p));
        if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::KwMut) {
            (void)(SelfhostParser__advance(p));
            tr.is_mut_borrow = true;
        } else {
            tr.is_borrow = true;
        }
        auto [inner, ok_in] = SelfhostParser__parse_type_ref(p);
        if (!ok_in) {
            return std::make_tuple(tr, false);
        }
        tr.name = inner.name;
        tr.is_slice = inner.is_slice;
        tr.is_array = inner.is_array;
        tr.array_len = inner.array_len;
        tr.is_map = inner.is_map;
        tr.key_type = inner.key_type;
        if (inner.elem_type != std::nullopt) {
            tr.elem_type = inner.elem_type;
        } else {
            tr.elem_type = std::make_shared<cfire::frontend::TypeRef>(inner);
        }
    } else {
        if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Star) {
            (void)(SelfhostParser__advance(p));
            if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::KwMut) {
                (void)(SelfhostParser__advance(p));
                tr.is_mut_ptr = true;
            } else {
                tr.is_ptr = true;
            }
            auto [inner, ok_in] = SelfhostParser__parse_type_ref(p);
            if (!ok_in) {
                return std::make_tuple(tr, false);
            }
            tr.name = inner.name;
            tr.elem_type = std::make_shared<cfire::frontend::TypeRef>(inner);
        } else {
            if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::KwMap) {
                (void)(SelfhostParser__advance(p));
                auto [tok_discard_1, ok_lb] = SelfhostParser__expect(p, cfire::frontend::TokenKind::LBracket);
                if (!ok_lb) {
                    return std::make_tuple(tr, false);
                }
                auto [ktype, ok_k] = SelfhostParser__parse_type_ref(p);
                if (!ok_k) {
                    return std::make_tuple(tr, false);
                }
                auto [tok_discard_2, ok_rb] = SelfhostParser__expect(p, cfire::frontend::TokenKind::RBracket);
                if (!ok_rb) {
                    return std::make_tuple(tr, false);
                }
                auto [vtype, ok_v] = SelfhostParser__parse_type_ref(p);
                if (!ok_v) {
                    return std::make_tuple(tr, false);
                }
                tr.is_map = true;
                tr.name = ((("map[" + ktype.name) + "]") + vtype.name);
                tr.key_type = std::make_shared<cfire::frontend::TypeRef>(ktype);
                tr.elem_type = std::make_shared<cfire::frontend::TypeRef>(vtype);
            } else {
                if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::LBracket) {
                    (void)(SelfhostParser__advance(p));
                    if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Int) {
                        auto len_str = SelfhostParser__advance(p).lexeme;
                        auto [tok_discard_3, ok_rb] = SelfhostParser__expect(p, cfire::frontend::TokenKind::RBracket);
                        if (!ok_rb) {
                            return std::make_tuple(tr, false);
                        }
                        auto [elem, ok_el] = SelfhostParser__parse_type_ref(p);
                        if (!ok_el) {
                            return std::make_tuple(tr, false);
                        }
                        tr.is_array = true;
                        tr.array_len = len_str;
                        tr.name = ((("[" + tr.array_len) + "]") + elem.name);
                        tr.elem_type = std::make_shared<cfire::frontend::TypeRef>(elem);
                    } else {
                        auto [tok_discard_4, ok_rb] = SelfhostParser__expect(p, cfire::frontend::TokenKind::RBracket);
                        if (!ok_rb) {
                            return std::make_tuple(tr, false);
                        }
                        auto [elem, ok_el] = SelfhostParser__parse_type_ref(p);
                        if (!ok_el) {
                            return std::make_tuple(tr, false);
                        }
                        tr.is_slice = true;
                        tr.name = ("[]" + elem.name);
                        tr.elem_type = std::make_shared<cfire::frontend::TypeRef>(elem);
                    }
                } else {
                    if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::LParen) {
                        (void)(SelfhostParser__advance(p));
                        std::vector<cfire::frontend::TypeRef> types = {};
                        while ((SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::RParen) && (SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::Eof)) {
                            auto [t, ok_t] = SelfhostParser__parse_type_ref(p);
                            if (!ok_t) {
                                return std::make_tuple(tr, false);
                            }
                            (void)(types.push_back(t));
                            if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Comma) {
                                (void)(SelfhostParser__advance(p));
                            } else {
                                if (SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::RParen) {
                                    auto err = (((((p.file + ":") + parser_i32_to_str(SelfhostParser__cur(p).line)) + ":") + parser_i32_to_str(SelfhostParser__cur(p).col)) + ": expected ',' or ')' in tuple type");
                                    (void)(SelfhostParser__record_error(p, err));
                                    return std::make_tuple(tr, false);
                                }
                            }
                        }
                        auto [tok_discard_5, ok_rp] = SelfhostParser__expect(p, cfire::frontend::TokenKind::RParen);
                        if (!ok_rp) {
                            return std::make_tuple(tr, false);
                        }
                        tr.is_tuple = true;
                        tr.tuple_types = types;
                        tr.name = "(";
                        std::size_t i = 0;
                        auto n = tr.tuple_types.size();
                        while (i < n) {
                            if (i > 0) {
                                tr.name = (tr.name + ", ");
                            }
                            tr.name = (tr.name + tr.tuple_types[i].name);
                            i = (i + 1);
                        }
                        tr.name = (tr.name + ")");
                    } else {
                        if ((SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Ident) || (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::KwCpp)) {
                            tr.name = SelfhostParser__advance(p).lexeme;
                        } else {
                            auto err = (((((((p.file + ":") + parser_i32_to_str(SelfhostParser__cur(p).line)) + ":") + parser_i32_to_str(SelfhostParser__cur(p).col)) + ": expected type name, got '") + SelfhostParser__cur(p).lexeme) + "'");
                            (void)(SelfhostParser__record_error(p, err));
                            return std::make_tuple(tr, false);
                        }
                        while ((SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::DoubleColon) || (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Dot)) {
                            (void)(SelfhostParser__advance(p));
                            auto [next_id, ok_next] = SelfhostParser__expect(p, cfire::frontend::TokenKind::Ident);
                            if (!ok_next) {
                                return std::make_tuple(tr, false);
                            }
                            tr.name = ((tr.name + "::") + next_id.lexeme);
                        }
                        if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::LBracket) {
                            (void)(SelfhostParser__advance(p));
                            auto [inner, ok_in] = SelfhostParser__parse_type_ref(p);
                            if (!ok_in) {
                                return std::make_tuple(tr, false);
                            }
                            auto [tok_discard_6, ok_rb] = SelfhostParser__expect(p, cfire::frontend::TokenKind::RBracket);
                            if (!ok_rb) {
                                return std::make_tuple(tr, false);
                            }
                            if ((tr.name == "own") || (tr.name == "shared")) {
                                tr.smart_wrap = tr.name;
                            }
                            tr.elem_type = std::make_shared<cfire::frontend::TypeRef>(inner);
                        }
                    }
                }
            }
        }
    }
    if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Question) {
        (void)(SelfhostParser__advance(p));
        tr.is_optional = true;
    }
    if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Bang) {
        (void)(SelfhostParser__advance(p));
        tr.is_fallible = true;
    }
    return std::make_tuple(tr, true);
}

[[nodiscard]] auto SelfhostParser__parse_import(SelfhostParser& p) -> std::tuple<cfire::frontend::ImportDecl, bool> {
    auto start = SelfhostParser__advance(p);
    auto imp = cfire::frontend::ImportDecl{};
    imp.line = start.line;
    if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::KwCpp) {
        (void)(SelfhostParser__advance(p));
        imp.is_cpp = true;
    }
    auto [path_tok, ok_path] = SelfhostParser__expect(p, cfire::frontend::TokenKind::String);
    if (!ok_path) {
        return std::make_tuple(imp, false);
    }
    imp.path = path_tok.lexeme;
    if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::KwAs) {
        (void)(SelfhostParser__advance(p));
        auto [alias_tok, ok_alias] = SelfhostParser__expect(p, cfire::frontend::TokenKind::Ident);
        if (!ok_alias) {
            return std::make_tuple(imp, false);
        }
        imp.alias = alias_tok.lexeme;
    }
    while (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::At) {
        (void)(SelfhostParser__advance(p));
        auto [attr_name, ok_name] = SelfhostParser__expect(p, cfire::frontend::TokenKind::Ident);
        if (!ok_name) {
            return std::make_tuple(imp, false);
        }
        auto [tok_discard_7, ok_lp] = SelfhostParser__expect(p, cfire::frontend::TokenKind::LParen);
        if (!ok_lp) {
            return std::make_tuple(imp, false);
        }
        auto attr = cfire::frontend::ImportAttr{};
        attr.name = attr_name.lexeme;
        while ((SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::RParen) && (SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::Eof)) {
            if ((SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::String) || (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Ident)) {
                (void)(attr.args.push_back(SelfhostParser__advance(p).lexeme));
            } else {
                (void)(SelfhostParser__advance(p));
            }
            if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Comma) {
                (void)(SelfhostParser__advance(p));
            }
        }
        auto [tok_discard_8, ok_rp] = SelfhostParser__expect(p, cfire::frontend::TokenKind::RParen);
        if (!ok_rp) {
            return std::make_tuple(imp, false);
        }
        (void)(imp.attrs.push_back(attr));
    }
    return std::make_tuple(imp, true);
}

[[nodiscard]] auto SelfhostParser__parse_type_decl(SelfhostParser& p, cfire::frontend::FileAST& f) -> bool {
    auto start = SelfhostParser__advance(p);
    auto [name_tok, ok_name] = SelfhostParser__expect(p, cfire::frontend::TokenKind::Ident);
    if (!ok_name) {
        return false;
    }
    if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::KwStruct) {
        (void)(SelfhostParser__advance(p));
        auto [tok_discard_9, ok_lb] = SelfhostParser__expect(p, cfire::frontend::TokenKind::LBrace);
        if (!ok_lb) {
            return false;
        }
        auto st = cfire::frontend::StructDecl{};
        st.name = name_tok.lexeme;
        st.line = start.line;
        while ((SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::RBrace) && (SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::Eof)) {
            auto [f_tok, ok_f] = SelfhostParser__expect(p, cfire::frontend::TokenKind::Ident);
            if (!ok_f) {
                return false;
            }
            if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Colon) {
                (void)(SelfhostParser__advance(p));
            }
            auto [t_ref, ok_tr] = SelfhostParser__parse_type_ref(p);
            if (!ok_tr) {
                return false;
            }
            auto field = cfire::frontend::StructField{};
            field.name = f_tok.lexeme;
            field.type = t_ref;
            field.line = f_tok.line;
            if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Assign) {
                (void)(SelfhostParser__advance(p));
                auto [def_val, ok_def] = SelfhostParser__parse_primary_expr(p);
                if (!ok_def) {
                    return false;
                }
                field.default_val = def_val;
            }
            if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::RawString) {
                field.raw_tag = SelfhostParser__advance(p).lexeme;
            }
            (void)(st.fields.push_back(field));
        }
        auto [tok_discard_10, ok_rb] = SelfhostParser__expect(p, cfire::frontend::TokenKind::RBrace);
        if (!ok_rb) {
            return false;
        }
        (void)(f.structs.push_back(st));
        return true;
    }
    if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::KwEnum) {
        (void)(SelfhostParser__advance(p));
        auto [tok_discard_11, ok_lb] = SelfhostParser__expect(p, cfire::frontend::TokenKind::LBrace);
        if (!ok_lb) {
            return false;
        }
        auto en = cfire::frontend::EnumDecl{};
        en.name = name_tok.lexeme;
        en.line = start.line;
        while ((SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::RBrace) && (SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::Eof)) {
            auto [v_tok, ok_v] = SelfhostParser__expect(p, cfire::frontend::TokenKind::Ident);
            if (!ok_v) {
                return false;
            }
            auto v = cfire::frontend::EnumVariant{};
            v.name = v_tok.lexeme;
            v.line = v_tok.line;
            if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::LParen) {
                (void)(SelfhostParser__advance(p));
                while ((SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::RParen) && (SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::Eof)) {
                    if ((SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Ident) && (SelfhostParser__peek(p, 1).kind == cfire::frontend::TokenKind::Colon)) {
                        (void)(SelfhostParser__advance(p));
                        (void)(SelfhostParser__advance(p));
                    }
                    auto [pt, ok_pt] = SelfhostParser__parse_type_ref(p);
                    if (!ok_pt) {
                        return false;
                    }
                    (void)(v.payload_types.push_back(pt));
                    if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Comma) {
                        (void)(SelfhostParser__advance(p));
                    }
                }
                auto [tok_discard_12, ok_rp] = SelfhostParser__expect(p, cfire::frontend::TokenKind::RParen);
                if (!ok_rp) {
                    return false;
                }
            }
            if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Comma) {
                (void)(SelfhostParser__advance(p));
            }
            (void)(en.variants.push_back(v));
        }
        auto [tok_discard_13, ok_rb] = SelfhostParser__expect(p, cfire::frontend::TokenKind::RBrace);
        if (!ok_rb) {
            return false;
        }
        (void)(f.enums.push_back(en));
        return true;
    }
    if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::KwInterface) {
        (void)(SelfhostParser__advance(p));
        auto [tok_discard_14, ok_lb] = SelfhostParser__expect(p, cfire::frontend::TokenKind::LBrace);
        if (!ok_lb) {
            return false;
        }
        auto iface = cfire::frontend::InterfaceDecl{};
        iface.name = name_tok.lexeme;
        iface.line = start.line;
        while ((SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::RBrace) && (SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::Eof)) {
            if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::KwFn) {
                (void)(SelfhostParser__advance(p));
            }
            auto [m_tok, ok_m] = SelfhostParser__expect(p, cfire::frontend::TokenKind::Ident);
            if (!ok_m) {
                return false;
            }
            auto [tok_discard_15, ok_lp] = SelfhostParser__expect(p, cfire::frontend::TokenKind::LParen);
            if (!ok_lp) {
                return false;
            }
            auto sig = cfire::frontend::InterfaceMethodSig{};
            sig.name = m_tok.lexeme;
            while ((SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::RParen) && (SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::Eof)) {
                if ((SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Ident) && (SelfhostParser__peek(p, 1).kind == cfire::frontend::TokenKind::Colon)) {
                    (void)(SelfhostParser__advance(p));
                    (void)(SelfhostParser__advance(p));
                }
                auto [pt, ok_pt] = SelfhostParser__parse_type_ref(p);
                if (!ok_pt) {
                    return false;
                }
                (void)(sig.param_types.push_back(pt));
                if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Comma) {
                    (void)(SelfhostParser__advance(p));
                }
            }
            auto [tok_discard_16, ok_rp] = SelfhostParser__expect(p, cfire::frontend::TokenKind::RParen);
            if (!ok_rp) {
                return false;
            }
            if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Arrow) {
                (void)(SelfhostParser__advance(p));
                auto [rt, ok_rt] = SelfhostParser__parse_type_ref(p);
                if (!ok_rt) {
                    return false;
                }
                sig.return_type = rt;
                if ((sig.return_type.is_slice && (sig.return_type.elem_type != std::nullopt)) && (*sig.return_type.elem_type).is_fallible) {
                    (*sig.return_type.elem_type).is_fallible = false;
                    sig.return_type.is_fallible = true;
                } else {
                    if ((sig.return_type.is_slice && (sig.return_type.elem_type != std::nullopt)) && (*sig.return_type.elem_type).is_optional) {
                        (*sig.return_type.elem_type).is_optional = false;
                        sig.return_type.is_optional = true;
                    }
                }
            }
            (void)(iface.methods.push_back(sig));
        }
        auto [tok_discard_17, ok_rb] = SelfhostParser__expect(p, cfire::frontend::TokenKind::RBrace);
        if (!ok_rb) {
            return false;
        }
        (void)(f.interfaces.push_back(iface));
        return true;
    }
    auto err = (((((p.file + ":") + parser_i32_to_str(SelfhostParser__cur(p).line)) + ":") + parser_i32_to_str(SelfhostParser__cur(p).col)) + ": expected 'struct', 'enum', or 'interface' after type name");
    (void)(SelfhostParser__record_error(p, err));
    return false;
}

[[nodiscard]] auto SelfhostParser__parse_fn_decl(SelfhostParser& p) -> std::tuple<cfire::frontend::FnDecl, bool> {
    auto start = SelfhostParser__advance(p);
    auto fn_decl = cfire::frontend::FnDecl{};
    fn_decl.return_type = cfire::frontend::TypeRef{.name = "void"};
    fn_decl.line = start.line;
    if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::LParen) {
        (void)(SelfhostParser__advance(p));
        auto recv = cfire::frontend::ReceiverDecl{};
        if ((SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::KwMut) || (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::KwIn)) {
            recv.intent = SelfhostParser__advance(p).lexeme;
        }
        auto [r_name, ok_rn] = SelfhostParser__expect(p, cfire::frontend::TokenKind::Ident);
        if (!ok_rn) {
            return std::make_tuple(fn_decl, false);
        }
        recv.name = r_name.lexeme;
        auto [tok_discard_18, ok_col] = SelfhostParser__expect(p, cfire::frontend::TokenKind::Colon);
        if (!ok_col) {
            return std::make_tuple(fn_decl, false);
        }
        if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::KwMut) {
            (void)(SelfhostParser__advance(p));
            recv.intent = "mut";
        }
        auto [r_type, ok_rt] = SelfhostParser__parse_type_ref(p);
        if (!ok_rt) {
            return std::make_tuple(fn_decl, false);
        }
        recv.type = r_type;
        auto [tok_discard_19, ok_rp] = SelfhostParser__expect(p, cfire::frontend::TokenKind::RParen);
        if (!ok_rp) {
            return std::make_tuple(fn_decl, false);
        }
        fn_decl.receiver = recv;
    }
    auto [name_tok, ok_name] = SelfhostParser__expect(p, cfire::frontend::TokenKind::Ident);
    if (!ok_name) {
        return std::make_tuple(fn_decl, false);
    }
    fn_decl.name = name_tok.lexeme;
    if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::LBracket) {
        (void)(SelfhostParser__advance(p));
        while ((SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::RBracket) && (SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::Eof)) {
            auto [tp_name, ok_tp] = SelfhostParser__expect(p, cfire::frontend::TokenKind::Ident);
            if (!ok_tp) {
                return std::make_tuple(fn_decl, false);
            }
            auto tp = cfire::frontend::TypeParam{.name = tp_name.lexeme, .constraint = ""};
            if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Colon) {
                (void)(SelfhostParser__advance(p));
                auto [c_name, ok_c] = SelfhostParser__expect(p, cfire::frontend::TokenKind::Ident);
                if (!ok_c) {
                    return std::make_tuple(fn_decl, false);
                }
                tp.constraint = c_name.lexeme;
            }
            (void)(fn_decl.type_params.push_back(tp));
            if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Comma) {
                (void)(SelfhostParser__advance(p));
            }
        }
        auto [tok_discard_20, ok_rb] = SelfhostParser__expect(p, cfire::frontend::TokenKind::RBracket);
        if (!ok_rb) {
            return std::make_tuple(fn_decl, false);
        }
    }
    auto [tok_discard_21, ok_lp] = SelfhostParser__expect(p, cfire::frontend::TokenKind::LParen);
    if (!ok_lp) {
        return std::make_tuple(fn_decl, false);
    }
    while ((SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::RParen) && (SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::Eof)) {
        std::string intent = "in";
        auto k = SelfhostParser__cur(p).kind;
        if ((((k == cfire::frontend::TokenKind::KwIn) || (k == cfire::frontend::TokenKind::KwMut)) || (k == cfire::frontend::TokenKind::KwOut)) || (k == cfire::frontend::TokenKind::KwMove)) {
            intent = SelfhostParser__advance(p).lexeme;
        }
        auto [p_tok, ok_pt] = SelfhostParser__expect(p, cfire::frontend::TokenKind::Ident);
        if (!ok_pt) {
            return std::make_tuple(fn_decl, false);
        }
        auto [tok_discard_22, ok_col] = SelfhostParser__expect(p, cfire::frontend::TokenKind::Colon);
        if (!ok_col) {
            return std::make_tuple(fn_decl, false);
        }
        if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::KwMut) {
            (void)(SelfhostParser__advance(p));
            intent = "mut";
        }
        auto [t_ref, ok_tr] = SelfhostParser__parse_type_ref(p);
        if (!ok_tr) {
            return std::make_tuple(fn_decl, false);
        }
        (void)(fn_decl.params.push_back(cfire::frontend::ParamDecl{.intent = intent, .name = p_tok.lexeme, .type = t_ref, .line = p_tok.line, .col = p_tok.col}));
        if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Comma) {
            (void)(SelfhostParser__advance(p));
        }
    }
    auto [tok_discard_23, ok_rp] = SelfhostParser__expect(p, cfire::frontend::TokenKind::RParen);
    if (!ok_rp) {
        return std::make_tuple(fn_decl, false);
    }
    if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::KwAsync) {
        (void)(SelfhostParser__advance(p));
        fn_decl.is_async = true;
    }
    if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Arrow) {
        (void)(SelfhostParser__advance(p));
        auto [ret, ok_ret] = SelfhostParser__parse_type_ref(p);
        if (!ok_ret) {
            return std::make_tuple(fn_decl, false);
        }
        fn_decl.return_type = ret;
        if ((fn_decl.return_type.is_slice && (fn_decl.return_type.elem_type != std::nullopt)) && (*fn_decl.return_type.elem_type).is_fallible) {
            (*fn_decl.return_type.elem_type).is_fallible = false;
            fn_decl.return_type.is_fallible = true;
        } else {
            if ((fn_decl.return_type.is_slice && (fn_decl.return_type.elem_type != std::nullopt)) && (*fn_decl.return_type.elem_type).is_optional) {
                (*fn_decl.return_type.elem_type).is_optional = false;
                fn_decl.return_type.is_optional = true;
            }
        }
    }
    auto [body, ok_body] = SelfhostParser__parse_block(p);
    if (!ok_body) {
        return std::make_tuple(fn_decl, false);
    }
    fn_decl.body = body;
    return std::make_tuple(fn_decl, true);
}

[[nodiscard]] auto SelfhostParser__parse_global_var(SelfhostParser& p) -> std::tuple<cfire::frontend::GlobalVarDecl, bool> {
    auto was_var = false;
    auto is_mut = false;
    auto line = SelfhostParser__cur(p).line;
    auto gv = cfire::frontend::GlobalVarDecl{};
    if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::KwVar) {
        (void)(SelfhostParser__advance(p));
        was_var = true;
        if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::KwMut) {
            (void)(SelfhostParser__advance(p));
            is_mut = true;
        }
    } else {
        if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::KwMut) {
            (void)(SelfhostParser__advance(p));
            is_mut = true;
        }
    }
    auto [name_tok, ok_name] = SelfhostParser__expect(p, cfire::frontend::TokenKind::Ident);
    if (!ok_name) {
        return std::make_tuple(gv, false);
    }
    std::optional<cfire::frontend::TypeRef> type_annot = std::nullopt;
    if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Colon) {
        (void)(SelfhostParser__advance(p));
        auto [tr, ok_tr] = SelfhostParser__parse_type_ref(p);
        if (!ok_tr) {
            return std::make_tuple(gv, false);
        }
        type_annot = tr;
    }
    if ((SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::ColonEq) || (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Assign)) {
        (void)(SelfhostParser__advance(p));
    } else {
        auto err = (((((p.file + ":") + parser_i32_to_str(SelfhostParser__cur(p).line)) + ":") + parser_i32_to_str(SelfhostParser__cur(p).col)) + ": expected ':=' or '=' in variable declaration");
        (void)(SelfhostParser__record_error(p, err));
        return std::make_tuple(gv, false);
    }
    auto [val, ok_val] = SelfhostParser__parse_expr(p);
    if (!ok_val) {
        return std::make_tuple(gv, false);
    }
    gv.name = name_tok.lexeme;
    gv.is_mut = is_mut;
    gv.type = type_annot;
    gv.value = val;
    gv.was_var = was_var;
    gv.line = line;
    return std::make_tuple(gv, true);
}

[[nodiscard]] auto SelfhostParser__parse_test_decl(SelfhostParser& p) -> std::tuple<cfire::frontend::TestDecl, bool> {
    auto start = SelfhostParser__advance(p);
    auto td = cfire::frontend::TestDecl{};
    auto [name_tok, ok_name] = SelfhostParser__expect(p, cfire::frontend::TokenKind::String);
    if (!ok_name) {
        return std::make_tuple(td, false);
    }
    auto [body, ok_body] = SelfhostParser__parse_block(p);
    if (!ok_body) {
        return std::make_tuple(td, false);
    }
    td.name = name_tok.lexeme;
    td.body = body;
    td.line = start.line;
    return std::make_tuple(td, true);
}

[[nodiscard]] auto SelfhostParser__parse_block(SelfhostParser& p) -> std::tuple<std::vector<cfire::frontend::StmtPtr>, bool> {
    std::vector<cfire::frontend::StmtPtr> stmts = {};
    auto [tok_discard_24, ok_lb] = SelfhostParser__expect(p, cfire::frontend::TokenKind::LBrace);
    if (!ok_lb) {
        return std::make_tuple(stmts, false);
    }
    while ((SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::RBrace) && (SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::Eof)) {
        if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Semicolon) {
            (void)(SelfhostParser__advance(p));
            continue;
        }
        auto [st, ok_st] = SelfhostParser__parse_stmt(p);
        if (!ok_st) {
            if (!p.resilient) {
                return std::make_tuple(stmts, false);
            }
            (void)(SelfhostParser__synchronize_stmt(p));
        } else {
            (void)(stmts.push_back(st));
        }
        while (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Semicolon) {
            (void)(SelfhostParser__advance(p));
        }
    }
    if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::RBrace) {
        (void)(SelfhostParser__advance(p));
    } else {
        if (!p.resilient) {
            auto [tok_discard_25, ok_rb] = SelfhostParser__expect(p, cfire::frontend::TokenKind::RBrace);
            if (!ok_rb) {
                return std::make_tuple(stmts, false);
            }
        } else {
            auto err = (((((p.file + ":") + parser_i32_to_str(SelfhostParser__cur(p).line)) + ":") + parser_i32_to_str(SelfhostParser__cur(p).col)) + ": expected '}' at end of block");
            (void)(SelfhostParser__record_error(p, err));
        }
    }
    return std::make_tuple(stmts, true);
}

[[nodiscard]] auto SelfhostParser__parse_stmt(SelfhostParser& p) -> std::tuple<cfire::frontend::StmtPtr, bool> {
    auto tok = SelfhostParser__cur(p);
    if (tok.kind == cfire::frontend::TokenKind::CppBlock) {
        (void)(SelfhostParser__advance(p));
        return std::make_tuple(cfire::frontend::make_stmt(cfire::frontend::CppBlockStmt{.raw_code = tok.lexeme, .line = tok.line}, tok.line), true);
    }
    if (tok.kind == cfire::frontend::TokenKind::KwDefer) {
        (void)(SelfhostParser__advance(p));
        auto [call, ok_call] = SelfhostParser__parse_expr(p);
        if (!ok_call) {
            return std::make_tuple(cfire::frontend::StmtPtr{}, false);
        }
        return std::make_tuple(cfire::frontend::make_stmt(cfire::frontend::DeferStmt{.call = call, .line = tok.line}, tok.line), true);
    }
    if (tok.kind == cfire::frontend::TokenKind::KwReturn) {
        (void)(SelfhostParser__advance(p));
        if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::RBrace) {
            return std::make_tuple(cfire::frontend::make_stmt(cfire::frontend::ReturnStmt{.value = cfire::frontend::ExprPtr{}, .line = tok.line}, tok.line), true);
        }
        auto [val, ok_val] = SelfhostParser__parse_expr(p);
        if (!ok_val) {
            return std::make_tuple(cfire::frontend::StmtPtr{}, false);
        }
        return std::make_tuple(cfire::frontend::make_stmt(cfire::frontend::ReturnStmt{.value = val, .line = tok.line}, tok.line), true);
    }
    if (tok.kind == cfire::frontend::TokenKind::KwIf) {
        return SelfhostParser__parse_if_stmt(p);
    }
    if (tok.kind == cfire::frontend::TokenKind::KwFor) {
        return SelfhostParser__parse_for_stmt(p);
    }
    if (tok.kind == cfire::frontend::TokenKind::KwMatch) {
        return SelfhostParser__parse_match_stmt(p);
    }
    if (tok.kind == cfire::frontend::TokenKind::KwVar) {
        (void)(SelfhostParser__advance(p));
        auto is_mut = false;
        if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::KwMut) {
            (void)(SelfhostParser__advance(p));
            is_mut = true;
        }
        auto [id_tok, ok_id] = SelfhostParser__expect(p, cfire::frontend::TokenKind::Ident);
        if (!ok_id) {
            return std::make_tuple(cfire::frontend::StmtPtr{}, false);
        }
        std::optional<cfire::frontend::TypeRef> t_ref = std::nullopt;
        if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Colon) {
            (void)(SelfhostParser__advance(p));
            auto [tr, ok_tr] = SelfhostParser__parse_type_ref(p);
            if (!ok_tr) {
                return std::make_tuple(cfire::frontend::StmtPtr{}, false);
            }
            t_ref = tr;
        }
        auto init = cfire::frontend::ExprPtr{};
        if ((SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Assign) || (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::ColonEq)) {
            (void)(SelfhostParser__advance(p));
            auto [ex, ok_ex] = SelfhostParser__parse_expr(p);
            if (!ok_ex) {
                return std::make_tuple(cfire::frontend::StmtPtr{}, false);
            }
            init = ex;
        }
        return std::make_tuple(cfire::frontend::make_stmt(cfire::frontend::VarDeclStmt{.name = id_tok.lexeme, .is_mut = is_mut, .type = t_ref, .init = init, .was_var = true, .line = id_tok.line, .col = id_tok.col}, id_tok.line), true);
    }
    if (tok.kind == cfire::frontend::TokenKind::KwMut) {
        (void)(SelfhostParser__advance(p));
        auto [id_tok, ok_id] = SelfhostParser__expect(p, cfire::frontend::TokenKind::Ident);
        if (!ok_id) {
            return std::make_tuple(cfire::frontend::StmtPtr{}, false);
        }
        if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Colon) {
            (void)(SelfhostParser__advance(p));
            auto [t_ref, ok_tr] = SelfhostParser__parse_type_ref(p);
            if (!ok_tr) {
                return std::make_tuple(cfire::frontend::StmtPtr{}, false);
            }
            auto init = cfire::frontend::ExprPtr{};
            if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Assign) {
                (void)(SelfhostParser__advance(p));
                auto [ex, ok_ex] = SelfhostParser__parse_expr(p);
                if (!ok_ex) {
                    return std::make_tuple(cfire::frontend::StmtPtr{}, false);
                }
                init = ex;
            }
            return std::make_tuple(cfire::frontend::make_stmt(cfire::frontend::VarDeclStmt{.name = id_tok.lexeme, .is_mut = true, .type = t_ref, .init = init, .was_var = false, .line = id_tok.line, .col = id_tok.col}, id_tok.line), true);
        }
        auto [tok_discard_26, ok_ce] = SelfhostParser__expect(p, cfire::frontend::TokenKind::ColonEq);
        if (!ok_ce) {
            return std::make_tuple(cfire::frontend::StmtPtr{}, false);
        }
        auto [init, ok_in] = SelfhostParser__parse_expr(p);
        if (!ok_in) {
            return std::make_tuple(cfire::frontend::StmtPtr{}, false);
        }
        return std::make_tuple(cfire::frontend::make_stmt(cfire::frontend::VarDeclStmt{.name = id_tok.lexeme, .is_mut = true, .type = std::nullopt, .init = init, .was_var = false, .line = id_tok.line, .col = id_tok.col}, id_tok.line), true);
    }
    if (tok.kind == cfire::frontend::TokenKind::KwAsync) {
        (void)(SelfhostParser__advance(p));
        auto [tok_discard_27, ok_dot] = SelfhostParser__expect(p, cfire::frontend::TokenKind::Dot);
        if (!ok_dot) {
            return std::make_tuple(cfire::frontend::StmtPtr{}, false);
        }
        auto [m, ok_m] = SelfhostParser__expect(p, cfire::frontend::TokenKind::Ident);
        if (!ok_m || (m.lexeme != "scope")) {
            auto err = (((((p.file + ":") + parser_i32_to_str(tok.line)) + ":") + parser_i32_to_str(tok.col)) + ": expected async.scope");
            (void)(SelfhostParser__record_error(p, err));
            return std::make_tuple(cfire::frontend::StmtPtr{}, false);
        }
        auto [tok_discard_28, ok_lp] = SelfhostParser__expect(p, cfire::frontend::TokenKind::LParen);
        if (!ok_lp) {
            return std::make_tuple(cfire::frontend::StmtPtr{}, false);
        }
        if ((SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Ident) && (SelfhostParser__peek(p, 1).kind == cfire::frontend::TokenKind::Colon)) {
            (void)(SelfhostParser__advance(p));
            (void)(SelfhostParser__advance(p));
        }
        auto [workers, ok_w] = SelfhostParser__parse_expr(p);
        if (!ok_w) {
            return std::make_tuple(cfire::frontend::StmtPtr{}, false);
        }
        auto [tok_discard_29, ok_rp] = SelfhostParser__expect(p, cfire::frontend::TokenKind::RParen);
        if (!ok_rp) {
            return std::make_tuple(cfire::frontend::StmtPtr{}, false);
        }
        auto [tok_discard_30, ok_p1] = SelfhostParser__expect(p, cfire::frontend::TokenKind::Pipe);
        if (!ok_p1) {
            return std::make_tuple(cfire::frontend::StmtPtr{}, false);
        }
        auto [scope_var, ok_sv] = SelfhostParser__expect(p, cfire::frontend::TokenKind::Ident);
        if (!ok_sv) {
            return std::make_tuple(cfire::frontend::StmtPtr{}, false);
        }
        auto [tok_discard_31, ok_p2] = SelfhostParser__expect(p, cfire::frontend::TokenKind::Pipe);
        if (!ok_p2) {
            return std::make_tuple(cfire::frontend::StmtPtr{}, false);
        }
        auto [body, ok_b] = SelfhostParser__parse_block(p);
        if (!ok_b) {
            return std::make_tuple(cfire::frontend::StmtPtr{}, false);
        }
        return std::make_tuple(cfire::frontend::make_stmt(cfire::frontend::AsyncScopeStmt{.workers = workers, .scope_var = scope_var.lexeme, .body = body, .line = tok.line}, tok.line), true);
    }
    if ((tok.kind == cfire::frontend::TokenKind::KwMut) && (SelfhostParser__peek(p, 1).kind == cfire::frontend::TokenKind::LParen)) {
        auto [is_destr, is_decl] = SelfhostParser__is_destructure_lookahead(p, 1);
        if (is_destr && is_decl) {
            (void)(SelfhostParser__advance(p));
            (void)(SelfhostParser__advance(p));
            auto decl = cfire::frontend::DestructureVarDeclStmt{};
            decl.line = tok.line;
            decl.col = tok.col;
            while ((SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::RParen) && (SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::Eof)) {
                if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::KwMut) {
                    (void)(SelfhostParser__advance(p));
                }
                auto [id, ok_id] = SelfhostParser__expect(p, cfire::frontend::TokenKind::Ident);
                if (!ok_id) {
                    return std::make_tuple(cfire::frontend::StmtPtr{}, false);
                }
                (void)(decl.bindings.push_back(std::make_pair(id.lexeme, true)));
                if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Comma) {
                    (void)(SelfhostParser__advance(p));
                }
            }
            auto [tok_discard_32, ok_rp] = SelfhostParser__expect(p, cfire::frontend::TokenKind::RParen);
            if (!ok_rp) {
                return std::make_tuple(cfire::frontend::StmtPtr{}, false);
            }
            auto [tok_discard_33, ok_ce] = SelfhostParser__expect(p, cfire::frontend::TokenKind::ColonEq);
            if (!ok_ce) {
                return std::make_tuple(cfire::frontend::StmtPtr{}, false);
            }
            auto [init, ok_in] = SelfhostParser__parse_expr(p);
            if (!ok_in) {
                return std::make_tuple(cfire::frontend::StmtPtr{}, false);
            }
            decl.init = init;
            return std::make_tuple(cfire::frontend::make_stmt(decl, tok.line), true);
        }
    }
    if (tok.kind == cfire::frontend::TokenKind::LParen) {
        auto [is_destr, is_decl] = SelfhostParser__is_destructure_lookahead(p, 0);
        if (is_destr && is_decl) {
            auto lp = SelfhostParser__advance(p);
            auto decl = cfire::frontend::DestructureVarDeclStmt{};
            decl.line = lp.line;
            decl.col = lp.col;
            while ((SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::RParen) && (SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::Eof)) {
                auto m = false;
                if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::KwMut) {
                    (void)(SelfhostParser__advance(p));
                    m = true;
                }
                auto [id, ok_id] = SelfhostParser__expect(p, cfire::frontend::TokenKind::Ident);
                if (!ok_id) {
                    return std::make_tuple(cfire::frontend::StmtPtr{}, false);
                }
                (void)(decl.bindings.push_back(std::make_pair(id.lexeme, m)));
                if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Comma) {
                    (void)(SelfhostParser__advance(p));
                }
            }
            auto [tok_discard_34, ok_rp] = SelfhostParser__expect(p, cfire::frontend::TokenKind::RParen);
            if (!ok_rp) {
                return std::make_tuple(cfire::frontend::StmtPtr{}, false);
            }
            auto [tok_discard_35, ok_ce] = SelfhostParser__expect(p, cfire::frontend::TokenKind::ColonEq);
            if (!ok_ce) {
                return std::make_tuple(cfire::frontend::StmtPtr{}, false);
            }
            auto [init, ok_in] = SelfhostParser__parse_expr(p);
            if (!ok_in) {
                return std::make_tuple(cfire::frontend::StmtPtr{}, false);
            }
            decl.init = init;
            return std::make_tuple(cfire::frontend::make_stmt(decl, lp.line), true);
        }
        if (is_destr && !is_decl) {
            auto lp = SelfhostParser__advance(p);
            std::vector<cfire::frontend::ExprPtr> targets = {};
            while ((SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::RParen) && (SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::Eof)) {
                auto [targ, ok_t] = SelfhostParser__parse_expr(p);
                if (!ok_t) {
                    return std::make_tuple(cfire::frontend::StmtPtr{}, false);
                }
                (void)(targets.push_back(targ));
                if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Comma) {
                    (void)(SelfhostParser__advance(p));
                }
            }
            auto [tok_discard_36, ok_rp] = SelfhostParser__expect(p, cfire::frontend::TokenKind::RParen);
            if (!ok_rp) {
                return std::make_tuple(cfire::frontend::StmtPtr{}, false);
            }
            auto [tok_discard_37, ok_eq] = SelfhostParser__expect(p, cfire::frontend::TokenKind::Assign);
            if (!ok_eq) {
                return std::make_tuple(cfire::frontend::StmtPtr{}, false);
            }
            auto [rhs, ok_rhs] = SelfhostParser__parse_expr(p);
            if (!ok_rhs) {
                return std::make_tuple(cfire::frontend::StmtPtr{}, false);
            }
            return std::make_tuple(cfire::frontend::make_stmt(cfire::frontend::DestructureAssignStmt{.targets = targets, .op = "=", .value = rhs, .line = lp.line}, lp.line), true);
        }
    }
    if ((tok.kind == cfire::frontend::TokenKind::Ident) && (SelfhostParser__peek(p, 1).kind == cfire::frontend::TokenKind::ColonEq)) {
        auto id_tok = SelfhostParser__advance(p);
        (void)(SelfhostParser__advance(p));
        auto [init, ok_in] = SelfhostParser__parse_expr(p);
        if (!ok_in) {
            return std::make_tuple(cfire::frontend::StmtPtr{}, false);
        }
        return std::make_tuple(cfire::frontend::make_stmt(cfire::frontend::VarDeclStmt{.name = id_tok.lexeme, .is_mut = false, .type = std::nullopt, .init = init, .was_var = false, .line = id_tok.line, .col = id_tok.col}, id_tok.line), true);
    }
    if ((tok.kind == cfire::frontend::TokenKind::Ident) && (SelfhostParser__peek(p, 1).kind == cfire::frontend::TokenKind::Colon)) {
        auto id_tok = SelfhostParser__advance(p);
        (void)(SelfhostParser__advance(p));
        auto [t_ref, ok_tr] = SelfhostParser__parse_type_ref(p);
        if (!ok_tr) {
            return std::make_tuple(cfire::frontend::StmtPtr{}, false);
        }
        auto init = cfire::frontend::ExprPtr{};
        if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Assign) {
            (void)(SelfhostParser__advance(p));
            auto [ex, ok_ex] = SelfhostParser__parse_expr(p);
            if (!ok_ex) {
                return std::make_tuple(cfire::frontend::StmtPtr{}, false);
            }
            init = ex;
        }
        return std::make_tuple(cfire::frontend::make_stmt(cfire::frontend::VarDeclStmt{.name = id_tok.lexeme, .is_mut = false, .type = t_ref, .init = init, .was_var = false, .line = id_tok.line, .col = id_tok.col}, id_tok.line), true);
    }
    auto [lhs, ok_lhs] = SelfhostParser__parse_expr(p);
    if (!ok_lhs) {
        return std::make_tuple(cfire::frontend::StmtPtr{}, false);
    }
    auto k = SelfhostParser__cur(p).kind;
    if (((k == cfire::frontend::TokenKind::Assign) || (k == cfire::frontend::TokenKind::PlusEq)) || (k == cfire::frontend::TokenKind::MinusEq)) {
        auto op = SelfhostParser__advance(p).lexeme;
        auto [rhs, ok_rhs] = SelfhostParser__parse_expr(p);
        if (!ok_rhs) {
            return std::make_tuple(cfire::frontend::StmtPtr{}, false);
        }
        return std::make_tuple(cfire::frontend::make_stmt(cfire::frontend::AssignStmt{.target = lhs, .op = op, .value = rhs, .line = tok.line}, tok.line), true);
    }
    return std::make_tuple(cfire::frontend::make_stmt(cfire::frontend::ExprStmt{.expr = lhs, .line = tok.line}, tok.line), true);
}

[[nodiscard]] auto SelfhostParser__parse_if_stmt(SelfhostParser& p) -> std::tuple<cfire::frontend::StmtPtr, bool> {
    auto start = SelfhostParser__advance(p);
    auto [cond, ok_cond] = SelfhostParser__parse_expr(p);
    if (!ok_cond) {
        return std::make_tuple(cfire::frontend::StmtPtr{}, false);
    }
    auto [then_body, ok_then] = SelfhostParser__parse_block(p);
    if (!ok_then) {
        return std::make_tuple(cfire::frontend::StmtPtr{}, false);
    }
    std::vector<cfire::frontend::StmtPtr> else_body = {};
    if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::KwElse) {
        (void)(SelfhostParser__advance(p));
        if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::KwIf) {
            auto [elif, ok_elif] = SelfhostParser__parse_if_stmt(p);
            if (!ok_elif) {
                return std::make_tuple(cfire::frontend::StmtPtr{}, false);
            }
            (void)(else_body.push_back(elif));
        } else {
            auto [eb, ok_eb] = SelfhostParser__parse_block(p);
            if (!ok_eb) {
                return std::make_tuple(cfire::frontend::StmtPtr{}, false);
            }
            else_body = eb;
        }
    }
    return std::make_tuple(cfire::frontend::make_stmt(cfire::frontend::IfStmt{.cond = cond, .then_body = then_body, .else_body = else_body, .line = start.line}, start.line), true);
}

[[nodiscard]] auto SelfhostParser__parse_for_stmt(SelfhostParser& p) -> std::tuple<cfire::frontend::StmtPtr, bool> {
    auto start = SelfhostParser__advance(p);
    if ((SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Ident) && ((SelfhostParser__peek(p, 1).kind == cfire::frontend::TokenKind::Comma) || (SelfhostParser__peek(p, 1).kind == cfire::frontend::TokenKind::KwIn))) {
        auto first = SelfhostParser__advance(p).lexeme;
        std::string idx_var = "";
        std::string item_var = "";
        if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Comma) {
            (void)(SelfhostParser__advance(p));
            auto [second, ok_sec] = SelfhostParser__expect(p, cfire::frontend::TokenKind::Ident);
            if (!ok_sec) {
                return std::make_tuple(cfire::frontend::StmtPtr{}, false);
            }
            idx_var = first;
            item_var = second.lexeme;
        } else {
            item_var = first;
        }
        auto [tok_discard_38, ok_in] = SelfhostParser__expect(p, cfire::frontend::TokenKind::KwIn);
        if (!ok_in) {
            return std::make_tuple(cfire::frontend::StmtPtr{}, false);
        }
        auto [iter_ex, ok_iter] = SelfhostParser__parse_expr(p);
        if (!ok_iter) {
            return std::make_tuple(cfire::frontend::StmtPtr{}, false);
        }
        auto [body, ok_b] = SelfhostParser__parse_block(p);
        if (!ok_b) {
            return std::make_tuple(cfire::frontend::StmtPtr{}, false);
        }
        return std::make_tuple(cfire::frontend::make_stmt(cfire::frontend::ForStmt{.index_var = idx_var, .item_var = item_var, .iter_expr = iter_ex, .body = body, .line = start.line}, start.line), true);
    }
    auto [cond, ok_c] = SelfhostParser__parse_expr(p);
    if (!ok_c) {
        return std::make_tuple(cfire::frontend::StmtPtr{}, false);
    }
    auto [body, ok_b] = SelfhostParser__parse_block(p);
    if (!ok_b) {
        return std::make_tuple(cfire::frontend::StmtPtr{}, false);
    }
    return std::make_tuple(cfire::frontend::make_stmt(cfire::frontend::ForStmt{.index_var = "", .item_var = "", .iter_expr = cond, .body = body, .line = start.line}, start.line), true);
}

[[nodiscard]] auto SelfhostParser__parse_match_stmt(SelfhostParser& p) -> std::tuple<cfire::frontend::StmtPtr, bool> {
    auto start = SelfhostParser__advance(p);
    auto is_move = false;
    if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::KwMove) {
        (void)(SelfhostParser__advance(p));
        is_move = true;
    }
    auto [target, ok_tgt] = SelfhostParser__parse_expr(p);
    if (!ok_tgt) {
        return std::make_tuple(cfire::frontend::StmtPtr{}, false);
    }
    auto [tok_discard_39, ok_lb] = SelfhostParser__expect(p, cfire::frontend::TokenKind::LBrace);
    if (!ok_lb) {
        return std::make_tuple(cfire::frontend::StmtPtr{}, false);
    }
    auto ms = cfire::frontend::MatchStmt{};
    ms.target = target;
    ms.is_move = is_move;
    ms.line = start.line;
    ms.col = start.col;
    while ((SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::RBrace) && (SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::Eof)) {
        auto arm = cfire::frontend::MatchArm{};
        if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::LBracket) {
            auto lb = SelfhostParser__advance(p);
            arm.is_slice_pattern = true;
            arm.line = lb.line;
            auto in_tail = false;
            while ((SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::RBracket) && (SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::Eof)) {
                if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::DotDot) {
                    (void)(SelfhostParser__advance(p));
                    arm.slice_has_rest = true;
                    if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Ident) {
                        auto rest_id = SelfhostParser__advance(p).lexeme;
                        if (rest_id != "_") {
                            arm.slice_rest = rest_id;
                        }
                    }
                    in_tail = true;
                } else {
                    if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Ident) {
                        auto id = SelfhostParser__advance(p).lexeme;
                        if (in_tail) {
                            (void)(arm.slice_tail.push_back(id));
                        } else {
                            (void)(arm.slice_head.push_back(id));
                        }
                    } else {
                        auto err = (((((p.file + ":") + parser_i32_to_str(SelfhostParser__cur(p).line)) + ":") + parser_i32_to_str(SelfhostParser__cur(p).col)) + ": unexpected token in slice pattern");
                        (void)(SelfhostParser__record_error(p, err));
                        return std::make_tuple(cfire::frontend::StmtPtr{}, false);
                    }
                }
                if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Comma) {
                    (void)(SelfhostParser__advance(p));
                }
            }
            auto [tok_discard_40, ok_rb] = SelfhostParser__expect(p, cfire::frontend::TokenKind::RBracket);
            if (!ok_rb) {
                return std::make_tuple(cfire::frontend::StmtPtr{}, false);
            }
        } else {
            auto [v_tok, ok_v] = SelfhostParser__expect(p, cfire::frontend::TokenKind::Ident);
            if (!ok_v) {
                return std::make_tuple(cfire::frontend::StmtPtr{}, false);
            }
            arm.line = v_tok.line;
            if ((SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::DoubleColon) || (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Dot)) {
                (void)(SelfhostParser__advance(p));
                auto [sub_tok, ok_sub] = SelfhostParser__expect(p, cfire::frontend::TokenKind::Ident);
                if (!ok_sub) {
                    return std::make_tuple(cfire::frontend::StmtPtr{}, false);
                }
                arm.enum_prefix = v_tok.lexeme;
                arm.variant_name = sub_tok.lexeme;
            } else {
                arm.variant_name = v_tok.lexeme;
            }
            if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::LParen) {
                (void)(SelfhostParser__advance(p));
                while ((SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::RParen) && (SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::Eof)) {
                    auto [b_tok, ok_b] = SelfhostParser__expect(p, cfire::frontend::TokenKind::Ident);
                    if (!ok_b) {
                        return std::make_tuple(cfire::frontend::StmtPtr{}, false);
                    }
                    (void)(arm.bindings.push_back(b_tok.lexeme));
                    if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Comma) {
                        (void)(SelfhostParser__advance(p));
                    }
                }
                auto [tok_discard_41, ok_rp] = SelfhostParser__expect(p, cfire::frontend::TokenKind::RParen);
                if (!ok_rp) {
                    return std::make_tuple(cfire::frontend::StmtPtr{}, false);
                }
            }
        }
        auto [tok_discard_42, ok_fa] = SelfhostParser__expect(p, cfire::frontend::TokenKind::FatArrow);
        if (!ok_fa) {
            return std::make_tuple(cfire::frontend::StmtPtr{}, false);
        }
        if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::LBrace) {
            auto [body, ok_b] = SelfhostParser__parse_block(p);
            if (!ok_b) {
                return std::make_tuple(cfire::frontend::StmtPtr{}, false);
            }
            arm.body = body;
        } else {
            auto [st, ok_st] = SelfhostParser__parse_stmt(p);
            if (!ok_st) {
                return std::make_tuple(cfire::frontend::StmtPtr{}, false);
            }
            (void)(arm.body.push_back(st));
        }
        if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Comma) {
            (void)(SelfhostParser__advance(p));
        }
        (void)(ms.arms.push_back(arm));
    }
    auto [tok_discard_43, ok_rb] = SelfhostParser__expect(p, cfire::frontend::TokenKind::RBrace);
    if (!ok_rb) {
        return std::make_tuple(cfire::frontend::StmtPtr{}, false);
    }
    return std::make_tuple(cfire::frontend::make_stmt(ms, start.line), true);
}

[[nodiscard]] auto SelfhostParser__parse_expr(SelfhostParser& p) -> std::tuple<cfire::frontend::ExprPtr, bool> {
    return SelfhostParser__parse_or_expr(p);
}

[[nodiscard]] auto SelfhostParser__parse_or_expr(SelfhostParser& p) -> std::tuple<cfire::frontend::ExprPtr, bool> {
    auto [left, ok_left] = SelfhostParser__parse_binary_expr(p, 1);
    if (!ok_left) {
        return std::make_tuple(cfire::frontend::ExprPtr{}, false);
    }
    if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::KwOr) {
        auto or_tok = SelfhostParser__advance(p);
        if ((SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Ident) && (SelfhostParser__peek(p, 1).kind == cfire::frontend::TokenKind::LBrace)) {
            auto err_var = SelfhostParser__advance(p).lexeme;
            auto [body, ok_b] = SelfhostParser__parse_block(p);
            if (!ok_b) {
                return std::make_tuple(cfire::frontend::ExprPtr{}, false);
            }
            return std::make_tuple(cfire::frontend::make_expr(cfire::frontend::OrExpr{.inner = left, .err_var = err_var, .handler_body = body, .fallback_val = cfire::frontend::ExprPtr{}}, or_tok.line, or_tok.col), true);
        }
        auto [fb, ok_fb] = SelfhostParser__parse_binary_expr(p, 1);
        if (!ok_fb) {
            return std::make_tuple(cfire::frontend::ExprPtr{}, false);
        }
        return std::make_tuple(cfire::frontend::make_expr(cfire::frontend::OrExpr{.inner = left, .err_var = "", .handler_body = {}, .fallback_val = fb}, or_tok.line, or_tok.col), true);
    }
    return std::make_tuple(left, true);
}

[[nodiscard]] auto SelfhostParser__parse_binary_expr(SelfhostParser& p, std::int32_t min_prec) -> std::tuple<cfire::frontend::ExprPtr, bool> {
    auto [left, ok_left] = SelfhostParser__parse_unary_expr(p);
    if (!ok_left) {
        return std::make_tuple(left, false);
    }
    while (true) {
        auto k = SelfhostParser__cur(p).kind;
        auto prec = get_binary_precedence(k);
        if (prec < min_prec) {
            break;
        }
        auto op = SelfhostParser__advance(p);
        auto [right, ok_right] = SelfhostParser__parse_binary_expr(p, (prec + 1));
        if (!ok_right) {
            return std::make_tuple(cfire::frontend::ExprPtr{}, false);
        }
        left = cfire::frontend::make_expr(cfire::frontend::BinaryExpr{.left = left, .op = op.lexeme, .right = right}, op.line, op.col);
    }
    return std::make_tuple(left, true);
}

[[nodiscard]] auto SelfhostParser__parse_unary_expr(SelfhostParser& p) -> std::tuple<cfire::frontend::ExprPtr, bool> {
    auto tok = SelfhostParser__cur(p);
    if (tok.kind == cfire::frontend::TokenKind::KwTry) {
        (void)(SelfhostParser__advance(p));
        auto [op, ok_op] = SelfhostParser__parse_unary_expr(p);
        if (!ok_op) {
            return std::make_tuple(cfire::frontend::ExprPtr{}, false);
        }
        return std::make_tuple(cfire::frontend::make_expr(cfire::frontend::TryExpr{.inner = op}, tok.line, tok.col), true);
    }
    if (tok.kind == cfire::frontend::TokenKind::KwMove) {
        (void)(SelfhostParser__advance(p));
        auto [op, ok_op] = SelfhostParser__parse_unary_expr(p);
        if (!ok_op) {
            return std::make_tuple(cfire::frontend::ExprPtr{}, false);
        }
        return std::make_tuple(cfire::frontend::make_expr(cfire::frontend::UnaryExpr{.op = "move", .operand = op}, tok.line, tok.col), true);
    }
    if (((tok.kind == cfire::frontend::TokenKind::Bang) || (tok.kind == cfire::frontend::TokenKind::Star)) || (tok.kind == cfire::frontend::TokenKind::Minus)) {
        (void)(SelfhostParser__advance(p));
        auto [op, ok_op] = SelfhostParser__parse_unary_expr(p);
        if (!ok_op) {
            return std::make_tuple(cfire::frontend::ExprPtr{}, false);
        }
        return std::make_tuple(cfire::frontend::make_expr(cfire::frontend::UnaryExpr{.op = tok.lexeme, .operand = op}, tok.line, tok.col), true);
    }
    if (tok.kind == cfire::frontend::TokenKind::Amp) {
        (void)(SelfhostParser__advance(p));
        std::string op_str = "&";
        if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::KwMut) {
            (void)(SelfhostParser__advance(p));
            op_str = "&mut";
        }
        auto [op, ok_op] = SelfhostParser__parse_unary_expr(p);
        if (!ok_op) {
            return std::make_tuple(cfire::frontend::ExprPtr{}, false);
        }
        return std::make_tuple(cfire::frontend::make_expr(cfire::frontend::UnaryExpr{.op = op_str, .operand = op}, tok.line, tok.col), true);
    }
    return SelfhostParser__parse_postfix_expr(p);
}

[[nodiscard]] auto SelfhostParser__parse_postfix_expr(SelfhostParser& p) -> std::tuple<cfire::frontend::ExprPtr, bool> {
    auto [expr, ok_expr] = SelfhostParser__parse_primary_expr(p);
    if (!ok_expr) {
        return std::make_tuple(expr, false);
    }
    while (true) {
        auto k = SelfhostParser__cur(p).kind;
        if ((k == cfire::frontend::TokenKind::Dot) || (k == cfire::frontend::TokenKind::DoubleColon)) {
            auto sep = SelfhostParser__advance(p);
            std::string field = "";
            if ((SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Ident) || ((SelfhostParser__cur(p).kind >= cfire::frontend::TokenKind::KwPackage) && (SelfhostParser__cur(p).kind <= cfire::frontend::TokenKind::KwNone))) {
                field = SelfhostParser__advance(p).lexeme;
            } else {
                auto err = (((((((p.file + ":") + parser_i32_to_str(SelfhostParser__cur(p).line)) + ":") + parser_i32_to_str(SelfhostParser__cur(p).col)) + ": expected member name after '") + sep.lexeme) + "'");
                (void)(SelfhostParser__record_error(p, err));
                return std::make_tuple(cfire::frontend::ExprPtr{}, false);
            }
            expr = cfire::frontend::make_expr(cfire::frontend::MemberExpr{.object = expr, .field = field, .is_namespace = (sep.kind == cfire::frontend::TokenKind::DoubleColon)}, sep.line, sep.col);
            continue;
        }
        if (k == cfire::frontend::TokenKind::LBracket) {
            if (SelfhostParser__is_generic_call_lookahead(p)) {
                auto saved_pos = p.pos;
                (void)(SelfhostParser__advance(p));
                std::vector<cfire::frontend::TypeRef> type_args = {};
                auto ok = true;
                while ((SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::RBracket) && (SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::Eof)) {
                    auto [targ, ok_t] = SelfhostParser__parse_type_ref(p);
                    if (!ok_t) {
                        ok = false;
                        break;
                    }
                    (void)(type_args.push_back(targ));
                    if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Comma) {
                        (void)(SelfhostParser__advance(p));
                    } else {
                        if (SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::RBracket) {
                            ok = false;
                            break;
                        }
                    }
                }
                if (ok && (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::RBracket)) {
                    (void)(SelfhostParser__advance(p));
                    auto [call, ok_call] = SelfhostParser__parse_call_with_callee(p, expr, type_args);
                    if (ok_call) {
                        expr = call;
                        continue;
                    }
                }
                p.pos = saved_pos;
            }
            auto br = SelfhostParser__advance(p);
            if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::DotDot) {
                (void)(SelfhostParser__advance(p));
                auto end_ex = cfire::frontend::ExprPtr{};
                if (SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::RBracket) {
                    auto [e, ok_e] = SelfhostParser__parse_expr(p);
                    if (!ok_e) {
                        return std::make_tuple(cfire::frontend::ExprPtr{}, false);
                    }
                    end_ex = e;
                }
                auto [tok_discard_44, ok_rb] = SelfhostParser__expect(p, cfire::frontend::TokenKind::RBracket);
                if (!ok_rb) {
                    return std::make_tuple(cfire::frontend::ExprPtr{}, false);
                }
                expr = cfire::frontend::make_expr(cfire::frontend::SliceExpr{.object = expr, .start = cfire::frontend::ExprPtr{}, .end = end_ex}, br.line, br.col);
                continue;
            }
            auto [first, ok_first] = SelfhostParser__parse_expr(p);
            if (!ok_first) {
                return std::make_tuple(cfire::frontend::ExprPtr{}, false);
            }
            if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::DotDot) {
                (void)(SelfhostParser__advance(p));
                auto end_ex = cfire::frontend::ExprPtr{};
                if (SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::RBracket) {
                    auto [e, ok_e] = SelfhostParser__parse_expr(p);
                    if (!ok_e) {
                        return std::make_tuple(cfire::frontend::ExprPtr{}, false);
                    }
                    end_ex = e;
                }
                auto [tok_discard_45, ok_rb] = SelfhostParser__expect(p, cfire::frontend::TokenKind::RBracket);
                if (!ok_rb) {
                    return std::make_tuple(cfire::frontend::ExprPtr{}, false);
                }
                expr = cfire::frontend::make_expr(cfire::frontend::SliceExpr{.object = expr, .start = first, .end = end_ex}, br.line, br.col);
                continue;
            }
            auto [tok_discard_46, ok_rb] = SelfhostParser__expect(p, cfire::frontend::TokenKind::RBracket);
            if (!ok_rb) {
                return std::make_tuple(cfire::frontend::ExprPtr{}, false);
            }
            expr = cfire::frontend::make_expr(cfire::frontend::IndexExpr{.object = expr, .index = first}, br.line, br.col);
            continue;
        }
        if (k == cfire::frontend::TokenKind::LParen) {
            auto [is_destr, _] = SelfhostParser__is_destructure_lookahead(p, 0);
            if (is_destr) {
                return std::make_tuple(expr, true);
            }
            auto [call, ok_call] = SelfhostParser__parse_call_with_callee(p, expr, {});
            if (!ok_call) {
                return std::make_tuple(cfire::frontend::ExprPtr{}, false);
            }
            expr = call;
            continue;
        }
        if (k == cfire::frontend::TokenKind::Pipe) {
            auto [cl, ok_cl] = SelfhostParser__parse_closure_expr(p);
            if (!ok_cl) {
                return std::make_tuple(cfire::frontend::ExprPtr{}, false);
            }
            expr = cfire::frontend::make_expr(cfire::frontend::CallExpr{.callee = expr, .type_args = {}, .args = {}, .block_arg = cl}, (*expr).line, (*expr).col);
            continue;
        }
        if (k == cfire::frontend::TokenKind::DoublePipe) {
            if (SelfhostParser__peek(p, 1).kind == cfire::frontend::TokenKind::LBrace) {
                auto [cl, ok_cl] = SelfhostParser__parse_closure_expr(p);
                if (!ok_cl) {
                    return std::make_tuple(cfire::frontend::ExprPtr{}, false);
                }
                expr = cfire::frontend::make_expr(cfire::frontend::CallExpr{.callee = expr, .type_args = {}, .args = {}, .block_arg = cl}, (*expr).line, (*expr).col);
                continue;
            }
            return std::make_tuple(expr, true);
        }
        break;
    }
    return std::make_tuple(expr, true);
}

[[nodiscard]] auto SelfhostParser__parse_call_with_callee(SelfhostParser& p, const cfire::frontend::ExprPtr& callee, const std::vector<cfire::frontend::TypeRef>& type_args) -> std::tuple<cfire::frontend::ExprPtr, bool> {
    auto lp = SelfhostParser__advance(p);
    auto call = cfire::frontend::CallExpr{};
    call.callee = callee;
    call.type_args = type_args;
    if (cfire::frontend::is_make_ident(callee)) {
        auto k = SelfhostParser__cur(p).kind;
        if (((k == cfire::frontend::TokenKind::LBracket) || (k == cfire::frontend::TokenKind::KwMap)) || (((k == cfire::frontend::TokenKind::Ident) && ((SelfhostParser__cur(p).lexeme == "own") || (SelfhostParser__cur(p).lexeme == "shared"))) && (SelfhostParser__peek(p, 1).kind == cfire::frontend::TokenKind::LBracket))) {
            auto [tref, ok_tr] = SelfhostParser__parse_type_ref(p);
            if (!ok_tr) {
                return std::make_tuple(cfire::frontend::ExprPtr{}, false);
            }
            (void)(call.type_args.push_back(tref));
            if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Comma) {
                (void)(SelfhostParser__advance(p));
            }
        }
    }
    while ((SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::RParen) && (SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::Eof)) {
        if ((SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Pipe) || (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::DoublePipe)) {
            auto [cl, ok_cl] = SelfhostParser__parse_closure_expr(p);
            if (!ok_cl) {
                return std::make_tuple(cfire::frontend::ExprPtr{}, false);
            }
            call.block_arg = cl;
            if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Comma) {
                (void)(SelfhostParser__advance(p));
            }
            continue;
        }
        auto arg_tok = SelfhostParser__cur(p);
        std::string intent = "";
        if (((arg_tok.kind == cfire::frontend::TokenKind::KwMut) || (arg_tok.kind == cfire::frontend::TokenKind::KwMove)) || (arg_tok.kind == cfire::frontend::TokenKind::KwOut)) {
            intent = SelfhostParser__advance(p).lexeme;
        }
        std::string label = "";
        if ((SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Ident) && (SelfhostParser__peek(p, 1).kind == cfire::frontend::TokenKind::Colon)) {
            label = SelfhostParser__advance(p).lexeme;
            (void)(SelfhostParser__advance(p));
        }
        auto [val, ok_val] = SelfhostParser__parse_expr(p);
        if (!ok_val) {
            return std::make_tuple(cfire::frontend::ExprPtr{}, false);
        }
        (void)(call.args.push_back(cfire::frontend::CallArg{.intent = intent, .label = label, .value = val, .line = arg_tok.line, .col = arg_tok.col}));
        if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Comma) {
            (void)(SelfhostParser__advance(p));
        }
    }
    auto [tok_discard_47, ok_rp] = SelfhostParser__expect(p, cfire::frontend::TokenKind::RParen);
    if (!ok_rp) {
        return std::make_tuple(cfire::frontend::ExprPtr{}, false);
    }
    return std::make_tuple(cfire::frontend::make_expr(call, lp.line, lp.col), true);
}

[[nodiscard]] auto SelfhostParser__parse_closure_expr(SelfhostParser& p) -> std::tuple<cfire::frontend::ClosureExpr, bool> {
    auto cl = cfire::frontend::ClosureExpr{};
    if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::DoublePipe) {
        (void)(SelfhostParser__advance(p));
    } else {
        auto [tok_discard_48, ok_p1] = SelfhostParser__expect(p, cfire::frontend::TokenKind::Pipe);
        if (!ok_p1) {
            return std::make_tuple(cl, false);
        }
        while ((SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::Pipe) && (SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::Eof)) {
            auto [id, ok_id] = SelfhostParser__expect(p, cfire::frontend::TokenKind::Ident);
            if (!ok_id) {
                return std::make_tuple(cl, false);
            }
            (void)(cl.params.push_back(id.lexeme));
            if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Comma) {
                (void)(SelfhostParser__advance(p));
            }
        }
        auto [tok_discard_49, ok_p2] = SelfhostParser__expect(p, cfire::frontend::TokenKind::Pipe);
        if (!ok_p2) {
            return std::make_tuple(cl, false);
        }
    }
    if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::KwAsync) {
        (void)(SelfhostParser__advance(p));
        cl.is_async = true;
    }
    auto [body, ok_b] = SelfhostParser__parse_block(p);
    if (!ok_b) {
        return std::make_tuple(cl, false);
    }
    cl.body = body;
    return std::make_tuple(cl, true);
}

[[nodiscard]] auto SelfhostParser__parse_primary_expr(SelfhostParser& p) -> std::tuple<cfire::frontend::ExprPtr, bool> {
    auto tok = SelfhostParser__cur(p);
    auto k = tok.kind;
    if (((((((k == cfire::frontend::TokenKind::Int) || (k == cfire::frontend::TokenKind::Float)) || (k == cfire::frontend::TokenKind::String)) || (k == cfire::frontend::TokenKind::RawString)) || (k == cfire::frontend::TokenKind::KwTrue)) || (k == cfire::frontend::TokenKind::KwFalse)) || (k == cfire::frontend::TokenKind::KwNone)) {
        (void)(SelfhostParser__advance(p));
        return std::make_tuple(cfire::frontend::make_expr(cfire::frontend::LiteralExpr{.kind = tok.kind, .value = tok.lexeme}, tok.line, tok.col), true);
    }
    if ((k == cfire::frontend::TokenKind::Ident) || (k == cfire::frontend::TokenKind::KwCpp)) {
        (void)(SelfhostParser__advance(p));
        auto name = tok.lexeme;
        while ((SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::DoubleColon) || ((((((SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Dot) && (SelfhostParser__peek(p, 1).kind == cfire::frontend::TokenKind::Ident)) && (SelfhostParser__peek(p, 1).lexeme.size() > 0)) && (SelfhostParser__peek(p, 1).lexeme[0] >= 65)) && (SelfhostParser__peek(p, 1).lexeme[0] <= 90)) && ((SelfhostParser__peek(p, 2).kind == cfire::frontend::TokenKind::LBrace) || (SelfhostParser__peek(p, 2).kind == cfire::frontend::TokenKind::Dot)))) {
            auto sep = SelfhostParser__advance(p);
            auto [next_id, ok_next] = SelfhostParser__expect(p, cfire::frontend::TokenKind::Ident);
            if (!ok_next) {
                return std::make_tuple(cfire::frontend::ExprPtr{}, false);
            }
            std::string sep_str = "::";
            if (sep.kind == cfire::frontend::TokenKind::Dot) {
                sep_str = ".";
            }
            name = ((name + sep_str) + next_id.lexeme);
        }
        auto is_struct_lit = false;
        if ((SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::LBrace) && (name.size() > 0)) {
            if (name.contains("::") || ((name[0] >= 65) && (name[0] <= 90))) {
                is_struct_lit = true;
            } else {
                if (name.contains(".")) {
                    auto di = (name.size() - 1);
                    while (di > 0) {
                        if (name[di] == 46) {
                            if ((di + 1) < name.size()) {
                                auto c = name[(di + 1)];
                                if ((c >= 65) && (c <= 90)) {
                                    is_struct_lit = true;
                                }
                            }
                            break;
                        }
                        di = (di - 1);
                    }
                }
            }
        }
        if (is_struct_lit) {
            (void)(SelfhostParser__advance(p));
            auto st = cfire::frontend::StructLitExpr{};
            st.type_name = name;
            while ((SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::RBrace) && (SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::Eof)) {
                std::string fn_name = "";
                if ((SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Ident) || ((SelfhostParser__cur(p).kind >= cfire::frontend::TokenKind::KwPackage) && (SelfhostParser__cur(p).kind <= cfire::frontend::TokenKind::KwNone))) {
                    fn_name = SelfhostParser__advance(p).lexeme;
                } else {
                    auto [fn_tok, ok_fn] = SelfhostParser__expect(p, cfire::frontend::TokenKind::Ident);
                    if (!ok_fn) {
                        return std::make_tuple(cfire::frontend::ExprPtr{}, false);
                    }
                    fn_name = fn_tok.lexeme;
                }
                auto [tok_discard_50, ok_col] = SelfhostParser__expect(p, cfire::frontend::TokenKind::Colon);
                if (!ok_col) {
                    return std::make_tuple(cfire::frontend::ExprPtr{}, false);
                }
                auto [fv, ok_fv] = SelfhostParser__parse_expr(p);
                if (!ok_fv) {
                    return std::make_tuple(cfire::frontend::ExprPtr{}, false);
                }
                (void)(st.fields.push_back(cfire::frontend::StructLitField{.name = fn_name, .value = fv}));
                if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Comma) {
                    (void)(SelfhostParser__advance(p));
                }
            }
            auto [tok_discard_51, ok_rb] = SelfhostParser__expect(p, cfire::frontend::TokenKind::RBrace);
            if (!ok_rb) {
                return std::make_tuple(cfire::frontend::ExprPtr{}, false);
            }
            return std::make_tuple(cfire::frontend::make_expr(st, tok.line, tok.col), true);
        }
        return std::make_tuple(cfire::frontend::make_expr(cfire::frontend::IdentExpr{.name = name}, tok.line, tok.col), true);
    }
    if (k == cfire::frontend::TokenKind::LParen) {
        (void)(SelfhostParser__advance(p));
        if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::RParen) {
            (void)(SelfhostParser__advance(p));
            return std::make_tuple(cfire::frontend::make_expr(cfire::frontend::TupleExpr{.elements = {}}, tok.line, tok.col), true);
        }
        auto [first, ok_first] = SelfhostParser__parse_expr(p);
        if (!ok_first) {
            return std::make_tuple(cfire::frontend::ExprPtr{}, false);
        }
        if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Comma) {
            std::vector<cfire::frontend::ExprPtr> elements = {};
            (void)(elements.push_back(first));
            while (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Comma) {
                (void)(SelfhostParser__advance(p));
                if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::RParen) {
                    break;
                }
                auto [next_ex, ok_next] = SelfhostParser__parse_expr(p);
                if (!ok_next) {
                    return std::make_tuple(cfire::frontend::ExprPtr{}, false);
                }
                (void)(elements.push_back(next_ex));
            }
            auto [tok_discard_52, ok_rp] = SelfhostParser__expect(p, cfire::frontend::TokenKind::RParen);
            if (!ok_rp) {
                return std::make_tuple(cfire::frontend::ExprPtr{}, false);
            }
            return std::make_tuple(cfire::frontend::make_expr(cfire::frontend::TupleExpr{.elements = elements}, tok.line, tok.col), true);
        }
        auto [tok_discard_53, ok_rp] = SelfhostParser__expect(p, cfire::frontend::TokenKind::RParen);
        if (!ok_rp) {
            return std::make_tuple(cfire::frontend::ExprPtr{}, false);
        }
        return std::make_tuple(first, true);
    }
    if (k == cfire::frontend::TokenKind::LBrace) {
        (void)(SelfhostParser__advance(p));
        auto st = cfire::frontend::StructLitExpr{};
        while ((SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::RBrace) && (SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::Eof)) {
            std::string key = "";
            if ((SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Ident) || (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::String)) {
                key = SelfhostParser__advance(p).lexeme;
            }
            if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Colon) {
                (void)(SelfhostParser__advance(p));
            }
            auto [fv, ok_fv] = SelfhostParser__parse_expr(p);
            if (!ok_fv) {
                return std::make_tuple(cfire::frontend::ExprPtr{}, false);
            }
            (void)(st.fields.push_back(cfire::frontend::StructLitField{.name = key, .value = fv}));
            if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Comma) {
                (void)(SelfhostParser__advance(p));
            }
        }
        auto [tok_discard_54, ok_rb] = SelfhostParser__expect(p, cfire::frontend::TokenKind::RBrace);
        if (!ok_rb) {
            return std::make_tuple(cfire::frontend::ExprPtr{}, false);
        }
        return std::make_tuple(cfire::frontend::make_expr(st, tok.line, tok.col), true);
    }
    if (k == cfire::frontend::TokenKind::LBracket) {
        (void)(SelfhostParser__advance(p));
        auto arr = cfire::frontend::ArrayLitExpr{};
        while ((SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::RBracket) && (SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::Eof)) {
            auto [el, ok_el] = SelfhostParser__parse_expr(p);
            if (!ok_el) {
                return std::make_tuple(cfire::frontend::ExprPtr{}, false);
            }
            (void)(arr.elements.push_back(el));
            if (SelfhostParser__cur(p).kind == cfire::frontend::TokenKind::Comma) {
                (void)(SelfhostParser__advance(p));
            }
        }
        auto [tok_discard_55, ok_rb] = SelfhostParser__expect(p, cfire::frontend::TokenKind::RBracket);
        if (!ok_rb) {
            return std::make_tuple(cfire::frontend::ExprPtr{}, false);
        }
        return std::make_tuple(cfire::frontend::make_expr(arr, tok.line, tok.col), true);
    }
    auto err = (((((((p.file + ":") + parser_i32_to_str(tok.line)) + ":") + parser_i32_to_str(tok.col)) + ": unexpected token '") + tok.lexeme) + "' in expression");
    (void)(SelfhostParser__record_error(p, err));
    return std::make_tuple(cfire::frontend::ExprPtr{}, false);
}

[[nodiscard]] auto SelfhostParser__parse_file_result(SelfhostParser& p) -> cfire::frontend::ParseResult {
    auto f = cfire::frontend::FileAST{};
    f.path = p.file;
    auto [pkg_kw, ok_pkg] = SelfhostParser__expect(p, cfire::frontend::TokenKind::KwPackage);
    if (!ok_pkg) {
        if (!p.resilient) {
            return cfire::frontend::ParseResult{.ast = f, .errors = p.errors};
        }
        (void)(SelfhostParser__synchronize_decl(p));
    } else {
        auto [pkg_id, ok_id] = SelfhostParser__expect(p, cfire::frontend::TokenKind::Ident);
        if (!ok_id) {
            if (!p.resilient) {
                return cfire::frontend::ParseResult{.ast = f, .errors = p.errors};
            }
            (void)(SelfhostParser__synchronize_decl(p));
        } else {
            f.package_name = pkg_id.lexeme;
        }
    }
    while (SelfhostParser__cur(p).kind != cfire::frontend::TokenKind::Eof) {
        auto k = SelfhostParser__cur(p).kind;
        if (k == cfire::frontend::TokenKind::KwImport) {
            auto [imp, ok_imp] = SelfhostParser__parse_import(p);
            if (!ok_imp) {
                if (!p.resilient) {
                    return cfire::frontend::ParseResult{.ast = f, .errors = p.errors};
                }
                (void)(SelfhostParser__synchronize_decl(p));
            } else {
                (void)(f.imports.push_back(imp));
            }
        } else {
            if (k == cfire::frontend::TokenKind::KwType) {
                auto ok_td = SelfhostParser__parse_type_decl(p, f);
                if (!ok_td) {
                    if (!p.resilient) {
                        return cfire::frontend::ParseResult{.ast = f, .errors = p.errors};
                    }
                    (void)(SelfhostParser__synchronize_decl(p));
                }
            } else {
                if (k == cfire::frontend::TokenKind::KwFn) {
                    auto [fn_decl, ok_fn] = SelfhostParser__parse_fn_decl(p);
                    if (!ok_fn) {
                        if (!p.resilient) {
                            return cfire::frontend::ParseResult{.ast = f, .errors = p.errors};
                        }
                        (void)(SelfhostParser__synchronize_decl(p));
                    } else {
                        (void)(f.functions.push_back(fn_decl));
                    }
                } else {
                    if (k == cfire::frontend::TokenKind::KwVar) {
                        auto [gv, ok_gv] = SelfhostParser__parse_global_var(p);
                        if (!ok_gv) {
                            if (!p.resilient) {
                                return cfire::frontend::ParseResult{.ast = f, .errors = p.errors};
                            }
                            (void)(SelfhostParser__synchronize_decl(p));
                        } else {
                            (void)(f.global_vars.push_back(gv));
                        }
                    } else {
                        if (k == cfire::frontend::TokenKind::KwMut) {
                            if (SelfhostParser__peek(p, 1).kind == cfire::frontend::TokenKind::Ident) {
                                auto [gv, ok_gv] = SelfhostParser__parse_global_var(p);
                                if (!ok_gv) {
                                    if (!p.resilient) {
                                        return cfire::frontend::ParseResult{.ast = f, .errors = p.errors};
                                    }
                                    (void)(SelfhostParser__synchronize_decl(p));
                                } else {
                                    (void)(f.global_vars.push_back(gv));
                                }
                            } else {
                                auto err = (((((p.file + ":") + parser_i32_to_str(SelfhostParser__cur(p).line)) + ":") + parser_i32_to_str(SelfhostParser__cur(p).col)) + ": unexpected 'mut' at top-level");
                                (void)(SelfhostParser__record_error(p, err));
                                if (!p.resilient) {
                                    return cfire::frontend::ParseResult{.ast = f, .errors = p.errors};
                                }
                                (void)(SelfhostParser__synchronize_decl(p));
                            }
                        } else {
                            if (k == cfire::frontend::TokenKind::Ident) {
                                auto p1 = SelfhostParser__peek(p, 1).kind;
                                if ((p1 == cfire::frontend::TokenKind::ColonEq) || (p1 == cfire::frontend::TokenKind::Colon)) {
                                    auto [gv, ok_gv] = SelfhostParser__parse_global_var(p);
                                    if (!ok_gv) {
                                        if (!p.resilient) {
                                            return cfire::frontend::ParseResult{.ast = f, .errors = p.errors};
                                        }
                                        (void)(SelfhostParser__synchronize_decl(p));
                                    } else {
                                        (void)(f.global_vars.push_back(gv));
                                    }
                                } else {
                                    auto err = (((((((p.file + ":") + parser_i32_to_str(SelfhostParser__cur(p).line)) + ":") + parser_i32_to_str(SelfhostParser__cur(p).col)) + ": unexpected identifier '") + SelfhostParser__cur(p).lexeme) + "' at top-level");
                                    (void)(SelfhostParser__record_error(p, err));
                                    if (!p.resilient) {
                                        return cfire::frontend::ParseResult{.ast = f, .errors = p.errors};
                                    }
                                    (void)(SelfhostParser__synchronize_decl(p));
                                }
                            } else {
                                if (k == cfire::frontend::TokenKind::KwTest) {
                                    auto [td, ok_td] = SelfhostParser__parse_test_decl(p);
                                    if (!ok_td) {
                                        if (!p.resilient) {
                                            return cfire::frontend::ParseResult{.ast = f, .errors = p.errors};
                                        }
                                        (void)(SelfhostParser__synchronize_decl(p));
                                    } else {
                                        (void)(f.tests.push_back(td));
                                    }
                                } else {
                                    auto err = (((((((p.file + ":") + parser_i32_to_str(SelfhostParser__cur(p).line)) + ":") + parser_i32_to_str(SelfhostParser__cur(p).col)) + ": unexpected top-level token '") + SelfhostParser__cur(p).lexeme) + "'");
                                    (void)(SelfhostParser__record_error(p, err));
                                    if (!p.resilient) {
                                        return cfire::frontend::ParseResult{.ast = f, .errors = p.errors};
                                    }
                                    (void)(SelfhostParser__synchronize_decl(p));
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return cfire::frontend::ParseResult{.ast = f, .errors = p.errors};
}

[[nodiscard]] auto selfhost_parse_file_result(std::string_view file, const std::vector<cfire::frontend::Token>& tokens, bool resilient) -> cfire::frontend::ParseResult {
    auto parser = make_parser(file, tokens, resilient);
    return SelfhostParser__parse_file_result(parser);
}
