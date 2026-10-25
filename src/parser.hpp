#pragma once
// src/parser.hpp — Recursive-Descent Parser in C++23 for cfire (.cf)

#include "ast.hpp"
#include <expected>
#include <format>
#include <string>
#include <vector>

#include <regex>

namespace cfire::frontend {

struct ParseResult {
    FileAST ast;
    std::vector<std::string> errors;

    [[nodiscard]] bool ok() const noexcept { return errors.empty(); }
};

[[nodiscard]] inline auto is_make_ident(const ExprPtr& callee) -> bool {
    if (!callee) return false;
    if (const auto* id = std::get_if<IdentExpr>(&callee->node)) {
        return id->name == "make";
    }
    return false;
}

class Parser {
public:
    Parser(std::string file, std::vector<Token> tokens, bool resilient = true)
        : file_(std::move(file)), tokens_(std::move(tokens)), resilient_(resilient) {}

    [[nodiscard]] auto parse_file_result() -> ParseResult {
        FileAST f;
        f.path = file_;
        auto pkg_kw = expect(TokenKind::KwPackage);
        if (!pkg_kw) {
            record_error(pkg_kw.error());
            if (!resilient_) return ParseResult{std::move(f), std::move(errors_)};
            synchronize_decl();
        } else {
            auto pkg_id = expect(TokenKind::Ident);
            if (!pkg_id) {
                record_error(pkg_id.error());
                if (!resilient_) return ParseResult{std::move(f), std::move(errors_)};
                synchronize_decl();
            } else {
                f.package_name = pkg_id->lexeme;
            }
        }

        while (cur().kind != TokenKind::Eof) {
            switch (cur().kind) {
                case TokenKind::KwImport: {
                    auto imp = parse_import();
                    if (!imp) {
                        record_error(imp.error());
                        if (!resilient_) return ParseResult{std::move(f), std::move(errors_)};
                        synchronize_decl();
                    } else {
                        f.imports.push_back(std::move(*imp));
                    }
                    break;
                }
                case TokenKind::KwType: {
                    if (auto r = parse_type_decl(f); !r) {
                        record_error(r.error());
                        if (!resilient_) return ParseResult{std::move(f), std::move(errors_)};
                        synchronize_decl();
                    }
                    break;
                }
                case TokenKind::KwFn: {
                    auto fn = parse_fn_decl();
                    if (!fn) {
                        record_error(fn.error());
                        if (!resilient_) return ParseResult{std::move(f), std::move(errors_)};
                        synchronize_decl();
                    } else {
                        f.functions.push_back(std::move(*fn));
                    }
                    break;
                }
                case TokenKind::KwVar: {
                    auto gv = parse_global_var();
                    if (!gv) {
                        record_error(gv.error());
                        if (!resilient_) return ParseResult{std::move(f), std::move(errors_)};
                        synchronize_decl();
                    } else {
                        f.global_vars.push_back(std::move(*gv));
                    }
                    break;
                }
                case TokenKind::KwMut: {
                    if (peek(1).kind == TokenKind::Ident) {
                        auto gv = parse_global_var();
                        if (!gv) {
                            record_error(gv.error());
                            if (!resilient_) return ParseResult{std::move(f), std::move(errors_)};
                            synchronize_decl();
                        } else {
                            f.global_vars.push_back(std::move(*gv));
                        }
                    } else {
                        record_error(std::format("{}:{}:{}: unexpected 'mut' at top-level",
                            file_, cur().line, cur().col));
                        if (!resilient_) return ParseResult{std::move(f), std::move(errors_)};
                        synchronize_decl();
                    }
                    break;
                }
                case TokenKind::Ident: {
                    if (peek(1).kind == TokenKind::ColonEq || peek(1).kind == TokenKind::Colon) {
                        auto gv = parse_global_var();
                        if (!gv) {
                            record_error(gv.error());
                            if (!resilient_) return ParseResult{std::move(f), std::move(errors_)};
                            synchronize_decl();
                        } else {
                            f.global_vars.push_back(std::move(*gv));
                        }
                    } else {
                        record_error(std::format("{}:{}:{}: unexpected identifier '{}' at top-level",
                            file_, cur().line, cur().col, cur().lexeme));
                        if (!resilient_) return ParseResult{std::move(f), std::move(errors_)};
                        synchronize_decl();
                    }
                    break;
                }
                case TokenKind::KwTest: {
                    auto td = parse_test_decl();
                    if (!td) {
                        record_error(td.error());
                        if (!resilient_) return ParseResult{std::move(f), std::move(errors_)};
                        synchronize_decl();
                    } else {
                        f.tests.push_back(std::move(*td));
                    }
                    break;
                }
                default:
                    record_error(std::format("{}:{}:{}: unexpected top-level token '{}'",
                        file_, cur().line, cur().col, cur().lexeme));
                    if (!resilient_) return ParseResult{std::move(f), std::move(errors_)};
                    synchronize_decl();
                    break;
            }
        }
        return ParseResult{std::move(f), std::move(errors_)};
    }

    [[nodiscard]] auto parse_file() -> std::expected<FileAST, std::string> {
        auto res = parse_file_result();
        if (!res.ok()) {
            return std::unexpected(res.errors.front());
        }
        return std::move(res.ast);
    }

private:
    std::string file_;
    std::vector<Token> tokens_;
    std::size_t pos_ = 0;
    std::vector<std::string> errors_;
    bool resilient_ = true;
    bool allow_struct_lit_ = true;
    int recursion_depth_ = 0;
    static constexpr int kMaxParseDepth = 256;

    struct DepthGuard {
        int& d;
        explicit DepthGuard(int& depth) noexcept : d(depth) { ++d; }
        ~DepthGuard() { --d; }
    };

    struct StructLitScope {
        Parser& p;
        bool prev;
        explicit StructLitScope(Parser& parser, bool allow)
            : p(parser), prev(parser.allow_struct_lit_) {
            p.allow_struct_lit_ = allow;
        }
        ~StructLitScope() {
            p.allow_struct_lit_ = prev;
        }
    };

    void record_error(std::string err) {
        if (errors_.size() < 20) {
            errors_.push_back(std::move(err));
        }
    }

    void synchronize_decl() {
        if (cur().kind != TokenKind::Eof) {
            advance();
        }
        while (cur().kind != TokenKind::Eof) {
            switch (cur().kind) {
                case TokenKind::KwPackage:
                case TokenKind::KwImport:
                case TokenKind::KwType:
                case TokenKind::KwFn:
                case TokenKind::KwVar:
                case TokenKind::KwMut:
                case TokenKind::KwTest:
                    return;
                case TokenKind::Ident:
                    if (peek(1).kind == TokenKind::ColonEq || peek(1).kind == TokenKind::Colon) {
                        return;
                    }
                    advance();
                    break;
                default:
                    advance();
                    break;
            }
        }
    }

    void synchronize_stmt() {
        if (cur().kind == TokenKind::Semicolon) {
            advance();
            return;
        }
        if (cur().kind != TokenKind::Eof && cur().kind != TokenKind::RBrace) {
            advance();
        }
        while (cur().kind != TokenKind::Eof && cur().kind != TokenKind::RBrace) {
            if (cur().kind == TokenKind::Semicolon) {
                advance();
                return;
            }
            switch (cur().kind) {
                case TokenKind::KwReturn:
                case TokenKind::KwIf:
                case TokenKind::KwFor:
                case TokenKind::KwMatch:
                case TokenKind::KwVar:
                case TokenKind::KwMut:
                case TokenKind::KwDefer:
                case TokenKind::CppBlock:
                case TokenKind::KwAsync:
                    return;
                case TokenKind::Ident:
                    if (peek(1).kind == TokenKind::ColonEq || peek(1).kind == TokenKind::Colon ||
                        peek(1).kind == TokenKind::Assign || peek(1).kind == TokenKind::PlusEq ||
                        peek(1).kind == TokenKind::MinusEq) {
                        return;
                    }
                    break;
                case TokenKind::KwFn:
                case TokenKind::KwType:
                case TokenKind::KwTest:
                case TokenKind::KwImport:
                    return;
                default:
                    break;
            }
            advance();
        }
    }

    [[nodiscard]] auto cur() const -> const Token& {
        return pos_ < tokens_.size() ? tokens_[pos_] : tokens_.back();
    }

    [[nodiscard]] auto peek(std::size_t offset) const -> const Token& {
        std::size_t idx = pos_ + offset;
        return idx < tokens_.size() ? tokens_[idx] : tokens_.back();
    }

    auto advance() -> Token {
        Token t = cur();
        if (pos_ + 1 < tokens_.size()) ++pos_;
        return t;
    }

    [[nodiscard]] auto is_generic_call_lookahead() const -> bool {
        if (cur().kind != TokenKind::LBracket) return false;
        int depth = 0;
        std::size_t i = pos_;
        while (i < tokens_.size()) {
            if (tokens_[i].kind == TokenKind::LBracket) {
                ++depth;
            } else if (tokens_[i].kind == TokenKind::RBracket) {
                --depth;
                if (depth == 0) {
                    return (i + 1 < tokens_.size() && tokens_[i + 1].kind == TokenKind::LParen);
                }
            } else if (tokens_[i].kind == TokenKind::Semicolon || tokens_[i].kind == TokenKind::Eof ||
                       tokens_[i].kind == TokenKind::RBrace) {
                break;
            }
            ++i;
        }
        return false;
    }

    [[nodiscard]] auto is_destructure_lookahead(std::size_t offset = 0) const -> std::pair<bool, bool> {
        // returns {is_destructure, is_decl} where is_decl is true for `:=` and false for `=`
        std::size_t start = pos_ + offset;
        if (start >= tokens_.size() || tokens_[start].kind != TokenKind::LParen) return {false, false};
        std::size_t i = start + 1;
        int depth = 1;
        while (i < tokens_.size() && depth > 0) {
            if (tokens_[i].kind == TokenKind::LParen) {
                ++depth;
            } else if (tokens_[i].kind == TokenKind::RParen) {
                --depth;
            } else if (tokens_[i].kind == TokenKind::Semicolon || tokens_[i].kind == TokenKind::Eof ||
                       tokens_[i].kind == TokenKind::RBrace) {
                return {false, false};
            }
            ++i;
        }
        if (depth == 0 && i < tokens_.size()) {
            if (tokens_[i].kind == TokenKind::ColonEq) return {true, true};
            if (tokens_[i].kind == TokenKind::Assign) return {true, false};
        }
        return {false, false};
    }

    [[nodiscard]] auto is_trailing_closure_lookahead() const -> bool {
        if (cur().kind != TokenKind::Pipe) return false;
        std::size_t offset = 1;
        if (peek(offset).kind != TokenKind::Ident) return false;
        while (peek(offset).kind == TokenKind::Ident) {
            ++offset;
            if (peek(offset).kind == TokenKind::Comma) {
                ++offset;
                if (peek(offset).kind != TokenKind::Ident) return false;
            } else {
                break;
            }
        }
        if (peek(offset).kind != TokenKind::Pipe) return false;
        ++offset;
        if (peek(offset).kind == TokenKind::KwAsync) ++offset;
        return peek(offset).kind == TokenKind::LBrace;
    }

    [[nodiscard]] auto expect(TokenKind kind) -> std::expected<Token, std::string> {
        const Token& t = cur();
        if (t.kind != kind) {
            return std::unexpected(std::format("{}:{}:{}: expected '{}', got '{}' ({})",
                file_, t.line, t.col, token_kind_name(kind), t.lexeme, token_kind_name(t.kind)));
        }
        return advance();
    }

    [[nodiscard]] auto parse_import() -> std::expected<ImportDecl, std::string> {
        Token start = advance(); // 'import'
        ImportDecl imp;
        imp.line = start.line;
        if (cur().kind == TokenKind::KwCpp) {
            advance();
            imp.is_cpp = true;
        }
        auto path_tok = expect(TokenKind::String);
        if (!path_tok) return std::unexpected(path_tok.error());
        imp.path = path_tok->lexeme;

        if (cur().kind == TokenKind::KwAs) {
            advance();
            auto alias_tok = expect(TokenKind::Ident);
            if (!alias_tok) return std::unexpected(alias_tok.error());
            imp.alias = alias_tok->lexeme;
        }

        while (cur().kind == TokenKind::At) {
            advance(); // '@'
            auto attr_name = expect(TokenKind::Ident);
            if (!attr_name) return std::unexpected(attr_name.error());
            if (auto lp = expect(TokenKind::LParen); !lp) return std::unexpected(lp.error());
            ImportAttr attr;
            attr.name = attr_name->lexeme;
            while (cur().kind != TokenKind::RParen && cur().kind != TokenKind::Eof) {
                if (cur().kind == TokenKind::String || cur().kind == TokenKind::Ident) {
                    attr.args.push_back(advance().lexeme);
                } else {
                    advance();
                }
                if (cur().kind == TokenKind::Comma) advance();
            }
            if (auto rp = expect(TokenKind::RParen); !rp) return std::unexpected(rp.error());
            imp.attrs.push_back(std::move(attr));
        }
        return imp;
    }

    [[nodiscard]] auto parse_type_decl(FileAST& f) -> std::expected<void, std::string> {
        Token start = advance(); // 'type'
        auto name_tok = expect(TokenKind::Ident);
        if (!name_tok) return std::unexpected(name_tok.error());

        if (cur().kind == TokenKind::KwStruct) {
            advance(); // 'struct'
            if (auto lb = expect(TokenKind::LBrace); !lb) return std::unexpected(lb.error());
            StructDecl st;
            st.name = name_tok->lexeme;
            st.line = start.line;
            while (cur().kind != TokenKind::RBrace && cur().kind != TokenKind::Eof) {
                auto f_tok = expect(TokenKind::Ident);
                if (!f_tok) return std::unexpected(f_tok.error());
                if (cur().kind == TokenKind::Colon) advance();
                auto t_ref = parse_type_ref();
                if (!t_ref) return std::unexpected(t_ref.error());

                StructField field;
                field.name = f_tok->lexeme;
                field.type = std::move(*t_ref);
                field.line = f_tok->line;

                if (cur().kind == TokenKind::Assign) {
                    advance();
                    auto def_val = parse_primary_expr();
                    if (!def_val) return std::unexpected(def_val.error());
                    field.default_val = std::move(*def_val);
                }
                if (cur().kind == TokenKind::RawString) {
                    field.raw_tag = advance().lexeme;
                    static const std::regex s_tag_re(R"re((\w+):"([^"]*)")re");
                    for (auto it = std::sregex_iterator(field.raw_tag.begin(), field.raw_tag.end(), s_tag_re);
                         it != std::sregex_iterator(); ++it) {
                        field.tags[(*it)[1].str()] = (*it)[2].str();
                    }
                }
                st.fields.push_back(std::move(field));
            }
            if (auto rb = expect(TokenKind::RBrace); !rb) return std::unexpected(rb.error());
            f.structs.push_back(std::move(st));
            return {};
        }

        if (cur().kind == TokenKind::KwEnum) {
            advance(); // 'enum'
            if (auto lb = expect(TokenKind::LBrace); !lb) return std::unexpected(lb.error());
            EnumDecl en;
            en.name = name_tok->lexeme;
            en.line = start.line;
            while (cur().kind != TokenKind::RBrace && cur().kind != TokenKind::Eof) {
                auto v_tok = expect(TokenKind::Ident);
                if (!v_tok) return std::unexpected(v_tok.error());
                EnumVariant v;
                v.name = v_tok->lexeme;
                v.line = v_tok->line;
                if (cur().kind == TokenKind::LParen) {
                    advance(); // '('
                    while (cur().kind != TokenKind::RParen && cur().kind != TokenKind::Eof) {
                        // Allow optional field label `radius: f64` or plain `f64`
                        if (cur().kind == TokenKind::Ident && peek(1).kind == TokenKind::Colon) {
                            advance();
                            advance();
                        }
                        auto pt = parse_type_ref();
                        if (!pt) return std::unexpected(pt.error());
                        v.payload_types.push_back(std::move(*pt));
                        if (cur().kind == TokenKind::Comma) advance();
                    }
                    if (auto rp = expect(TokenKind::RParen); !rp) return std::unexpected(rp.error());
                }
                if (cur().kind == TokenKind::Comma) advance();
                en.variants.push_back(std::move(v));
            }
            if (auto rb = expect(TokenKind::RBrace); !rb) return std::unexpected(rb.error());
            f.enums.push_back(std::move(en));
            return {};
        }

        if (cur().kind == TokenKind::KwInterface) {
            advance(); // 'interface'
            if (auto lb = expect(TokenKind::LBrace); !lb) return std::unexpected(lb.error());
            InterfaceDecl iface;
            iface.name = name_tok->lexeme;
            iface.line = start.line;
            while (cur().kind != TokenKind::RBrace && cur().kind != TokenKind::Eof) {
                if (cur().kind == TokenKind::KwFn) advance();
                auto m_tok = expect(TokenKind::Ident);
                if (!m_tok) return std::unexpected(m_tok.error());
                if (auto lp = expect(TokenKind::LParen); !lp) return std::unexpected(lp.error());
                InterfaceMethodSig sig;
                sig.name = m_tok->lexeme;
                while (cur().kind != TokenKind::RParen && cur().kind != TokenKind::Eof) {
                    if (cur().kind == TokenKind::Ident && peek(1).kind == TokenKind::Colon) {
                        advance();
                        advance();
                    }
                    auto pt = parse_type_ref();
                    if (!pt) return std::unexpected(pt.error());
                    sig.param_types.push_back(std::move(*pt));
                    if (cur().kind == TokenKind::Comma) advance();
                }
                if (auto rp = expect(TokenKind::RParen); !rp) return std::unexpected(rp.error());
                if (cur().kind == TokenKind::Arrow) {
                    advance();
                    auto rt = parse_type_ref();
                    if (!rt) return std::unexpected(rt.error());
                    sig.return_type = std::move(*rt);
                    if (sig.return_type.is_slice && sig.return_type.elem_type && sig.return_type.elem_type->is_fallible) {
                        sig.return_type.elem_type->is_fallible = false;
                        sig.return_type.is_fallible = true;
                    } else if (sig.return_type.is_slice && sig.return_type.elem_type && sig.return_type.elem_type->is_optional) {
                        sig.return_type.elem_type->is_optional = false;
                        sig.return_type.is_optional = true;
                    }
                }
                iface.methods.push_back(std::move(sig));
            }
            if (auto rb = expect(TokenKind::RBrace); !rb) return std::unexpected(rb.error());
            f.interfaces.push_back(std::move(iface));
            return {};
        }

        return std::unexpected(std::format("{}:{}:{}: expected 'struct', 'enum', or 'interface' after type name",
            file_, cur().line, cur().col));
    }

    [[nodiscard]] auto parse_type_ref() -> std::expected<TypeRef, std::string> {
        if (recursion_depth_ >= kMaxParseDepth) {
            return std::unexpected(std::format("{}:{}:{}: maximum parser nesting depth (256) exceeded",
                file_, cur().line, cur().col));
        }
        DepthGuard depth_guard(recursion_depth_);
        TypeRef tr;
        if (cur().kind == TokenKind::Amp) {
            advance();
            if (cur().kind == TokenKind::KwMut) {
                advance();
                tr.is_mut_borrow = true;
            } else {
                tr.is_borrow = true;
            }
            auto inner = parse_type_ref();
            if (!inner) return std::unexpected(inner.error());
            tr.name = inner->name;
            tr.is_slice = inner->is_slice;
            tr.is_array = inner->is_array;
            tr.array_len = inner->array_len;
            tr.is_map = inner->is_map;
            tr.key_type = inner->key_type;
            tr.elem_type = inner->elem_type ? inner->elem_type : std::make_shared<TypeRef>(std::move(*inner));
        } else if (cur().kind == TokenKind::Star) {
            advance();
            if (cur().kind == TokenKind::KwMut) {
                advance();
                tr.is_mut_ptr = true;
            } else {
                tr.is_ptr = true;
            }
            auto inner = parse_type_ref();
            if (!inner) return std::unexpected(inner.error());
            tr.name = inner->name;
            tr.elem_type = std::make_shared<TypeRef>(std::move(*inner));
        } else if (cur().kind == TokenKind::KwMap) {
            advance(); // 'map'
            if (auto lb = expect(TokenKind::LBracket); !lb) return std::unexpected(lb.error());
            auto ktype = parse_type_ref();
            if (!ktype) return std::unexpected(ktype.error());
            if (auto rb = expect(TokenKind::RBracket); !rb) return std::unexpected(rb.error());
            auto vtype = parse_type_ref();
            if (!vtype) return std::unexpected(vtype.error());
            tr.is_map = true;
            tr.name = std::format("map[{}]{}", ktype->name, vtype->name);
            tr.key_type = std::make_shared<TypeRef>(std::move(*ktype));
            tr.elem_type = std::make_shared<TypeRef>(std::move(*vtype));
        } else if (cur().kind == TokenKind::LBracket) {
            advance();
            if (cur().kind == TokenKind::Int) {
                std::string len_str = advance().lexeme;
                if (auto rb = expect(TokenKind::RBracket); !rb) return std::unexpected(rb.error());
                auto elem = parse_type_ref();
                if (!elem) return std::unexpected(elem.error());
                tr.is_array = true;
                tr.array_len = std::move(len_str);
                tr.name = std::format("[{}]{}", tr.array_len, elem->name);
                tr.elem_type = std::make_shared<TypeRef>(std::move(*elem));
            } else {
                if (auto rb = expect(TokenKind::RBracket); !rb) return std::unexpected(rb.error());
                auto elem = parse_type_ref();
                if (!elem) return std::unexpected(elem.error());
                tr.is_slice = true;
                tr.name = "[]" + elem->name;
                tr.elem_type = std::make_shared<TypeRef>(std::move(*elem));
            }
        } else if (cur().kind == TokenKind::LParen) {
            advance(); // '('
            std::vector<TypeRef> types;
            while (cur().kind != TokenKind::RParen && cur().kind != TokenKind::Eof) {
                auto t = parse_type_ref();
                if (!t) return std::unexpected(t.error());
                types.push_back(std::move(*t));
                if (cur().kind == TokenKind::Comma) {
                    advance();
                } else if (cur().kind != TokenKind::RParen) {
                    return std::unexpected(std::format("{}:{}:{}: expected ',' or ')' in tuple type",
                        file_, cur().line, cur().col));
                }
            }
            if (auto rp = expect(TokenKind::RParen); !rp) return std::unexpected(rp.error());
            tr.is_tuple = true;
            tr.tuple_types = std::move(types);
            tr.name = "(";
            for (std::size_t i = 0; i < tr.tuple_types.size(); ++i) {
                if (i > 0) tr.name += ", ";
                tr.name += tr.tuple_types[i].name;
            }
            tr.name += ")";
        } else {
            if (cur().kind == TokenKind::Ident || cur().kind == TokenKind::KwCpp) {
                tr.name = advance().lexeme;
            } else {
                return std::unexpected(std::format("{}:{}:{}: expected type name, got '{}'",
                    file_, cur().line, cur().col, cur().lexeme));
            }
            while (cur().kind == TokenKind::DoubleColon || cur().kind == TokenKind::Dot) {
                advance();
                auto next_id = expect(TokenKind::Ident);
                if (!next_id) return std::unexpected(next_id.error());
                tr.name += "::" + next_id->lexeme;
            }
            if (cur().kind == TokenKind::LBracket) {
                advance();
                auto inner = parse_type_ref();
                if (!inner) return std::unexpected(inner.error());
                if (auto rb = expect(TokenKind::RBracket); !rb) return std::unexpected(rb.error());
                if (tr.name == "own" || tr.name == "shared") {
                    tr.smart_wrap = tr.name;
                }
                tr.elem_type = std::make_shared<TypeRef>(std::move(*inner));
            }
        }

        if (cur().kind == TokenKind::Question) {
            advance();
            tr.is_optional = true;
        }
        if (cur().kind == TokenKind::Bang) {
            advance();
            tr.is_fallible = true;
        }
        return tr;
    }

    [[nodiscard]] auto parse_fn_decl() -> std::expected<FnDecl, std::string> {
        Token start = advance(); // 'fn'
        FnDecl fn;
        fn.return_type = TypeRef{"void"};
        fn.line = start.line;

        // Optional method receiver: `fn (s: Type)` or `fn (mut s: Type)`
        if (cur().kind == TokenKind::LParen) {
            advance(); // '('
            ReceiverDecl recv;
            if (cur().kind == TokenKind::KwMut || cur().kind == TokenKind::KwIn) {
                recv.intent = advance().lexeme;
            }
            auto r_name = expect(TokenKind::Ident);
            if (!r_name) return std::unexpected(r_name.error());
            recv.name = r_name->lexeme;
            if (auto col = expect(TokenKind::Colon); !col) return std::unexpected(col.error());
            if (cur().kind == TokenKind::KwMut) {
                advance();
                recv.intent = "mut";
            }
            auto r_type = parse_type_ref();
            if (!r_type) return std::unexpected(r_type.error());
            recv.type = std::move(*r_type);
            if (auto rp = expect(TokenKind::RParen); !rp) return std::unexpected(rp.error());
            fn.receiver = std::move(recv);
        }

        auto name_tok = expect(TokenKind::Ident);
        if (!name_tok) return std::unexpected(name_tok.error());
        fn.name = name_tok->lexeme;

        // Optional generic type parameters: `fn foo[T: Hasher](...)`
        if (cur().kind == TokenKind::LBracket) {
            advance(); // '['
            while (cur().kind != TokenKind::RBracket && cur().kind != TokenKind::Eof) {
                auto tp_name = expect(TokenKind::Ident);
                if (!tp_name) return std::unexpected(tp_name.error());
                TypeParam tp{tp_name->lexeme, ""};
                if (cur().kind == TokenKind::Colon) {
                    advance();
                    auto c_name = expect(TokenKind::Ident);
                    if (!c_name) return std::unexpected(c_name.error());
                    tp.constraint = c_name->lexeme;
                }
                fn.type_params.push_back(std::move(tp));
                if (cur().kind == TokenKind::Comma) advance();
            }
            if (auto rb = expect(TokenKind::RBracket); !rb) return std::unexpected(rb.error());
        }

        if (auto lp = expect(TokenKind::LParen); !lp) return std::unexpected(lp.error());

        while (cur().kind != TokenKind::RParen && cur().kind != TokenKind::Eof) {
            std::string intent = "in";
            switch (cur().kind) {
                case TokenKind::KwIn:
                case TokenKind::KwMut:
                case TokenKind::KwOut:
                case TokenKind::KwMove:
                    intent = advance().lexeme;
                    break;
                default:
                    break;
            }
            auto p_tok = expect(TokenKind::Ident);
            if (!p_tok) return std::unexpected(p_tok.error());
            if (auto col = expect(TokenKind::Colon); !col) return std::unexpected(col.error());
            if (cur().kind == TokenKind::KwMut) {
                advance();
                intent = "mut";
            }
            auto t_ref = parse_type_ref();
            if (!t_ref) return std::unexpected(t_ref.error());
            fn.params.push_back(ParamDecl{
                .intent = std::move(intent),
                .name = p_tok->lexeme,
                .type = std::move(*t_ref),
                .line = p_tok->line,
                .col = p_tok->col,
            });
            if (cur().kind == TokenKind::Comma) advance();
        }
        if (auto rp = expect(TokenKind::RParen); !rp) return std::unexpected(rp.error());

        if (cur().kind == TokenKind::KwAsync) {
            advance();
            fn.is_async = true;
        }
        if (cur().kind == TokenKind::Arrow) {
            advance();
            auto ret = parse_type_ref();
            if (!ret) return std::unexpected(ret.error());
            fn.return_type = std::move(*ret);
            if (fn.return_type.is_slice && fn.return_type.elem_type && fn.return_type.elem_type->is_fallible) {
                fn.return_type.elem_type->is_fallible = false;
                fn.return_type.is_fallible = true;
            } else if (fn.return_type.is_slice && fn.return_type.elem_type && fn.return_type.elem_type->is_optional) {
                fn.return_type.elem_type->is_optional = false;
                fn.return_type.is_optional = true;
            }
        }
        auto body = parse_block();
        if (!body) return std::unexpected(body.error());
        fn.body = std::move(*body);
        return fn;
    }

    [[nodiscard]] auto parse_global_var() -> std::expected<GlobalVarDecl, std::string> {
        bool was_var = false;
        bool is_mut = false;
        int line = cur().line;
        if (cur().kind == TokenKind::KwVar) {
            advance(); // 'var'
            was_var = true;
            if (cur().kind == TokenKind::KwMut) {
                advance();
                is_mut = true;
            }
        } else if (cur().kind == TokenKind::KwMut) {
            advance(); // 'mut'
            is_mut = true;
        }

        auto name_tok = expect(TokenKind::Ident);
        if (!name_tok) return std::unexpected(name_tok.error());

        std::optional<TypeRef> type_annot;
        if (cur().kind == TokenKind::Colon) {
            advance(); // ':'
            auto tr = parse_type_ref();
            if (!tr) return std::unexpected(tr.error());
            type_annot = std::move(*tr);
        }

        if (cur().kind == TokenKind::ColonEq || cur().kind == TokenKind::Assign) {
            advance(); // ':=' or '='
        } else {
            return std::unexpected(std::format("{}:{}:{}: expected ':=' or '=' in variable declaration",
                file_, cur().line, cur().col));
        }

        auto val = parse_expr();
        if (!val) return std::unexpected(val.error());
        return GlobalVarDecl{name_tok->lexeme, is_mut, std::move(type_annot), std::move(*val), was_var, line};
    }

    [[nodiscard]] auto parse_test_decl() -> std::expected<TestDecl, std::string> {
        Token start = advance(); // 'test'
        auto name_tok = expect(TokenKind::String);
        if (!name_tok) return std::unexpected(name_tok.error());
        auto body = parse_block();
        if (!body) return std::unexpected(body.error());
        return TestDecl{name_tok->lexeme, std::move(*body), start.line};
    }

    [[nodiscard]] auto parse_block() -> std::expected<std::vector<StmtPtr>, std::string> {
        if (recursion_depth_ >= kMaxParseDepth) {
            return std::unexpected(std::format("{}:{}:{}: maximum parser nesting depth (256) exceeded",
                file_, cur().line, cur().col));
        }
        DepthGuard depth_guard(recursion_depth_);
        if (auto lb = expect(TokenKind::LBrace); !lb) return std::unexpected(lb.error());
        std::vector<StmtPtr> stmts;
        while (cur().kind != TokenKind::RBrace && cur().kind != TokenKind::Eof) {
            if (cur().kind == TokenKind::Semicolon) {
                advance();
                continue;
            }
            auto st = parse_stmt();
            if (!st) {
                if (!resilient_) return std::unexpected(st.error());
                record_error(st.error());
                synchronize_stmt();
            } else {
                stmts.push_back(std::move(*st));
            }
            while (cur().kind == TokenKind::Semicolon) {
                advance();
            }
        }
        if (cur().kind == TokenKind::RBrace) {
            advance();
        } else if (!resilient_) {
            if (auto rb = expect(TokenKind::RBrace); !rb) return std::unexpected(rb.error());
        } else {
            record_error(std::format("{}:{}:{}: expected '}}' at end of block",
                file_, cur().line, cur().col));
        }
        return stmts;
    }

    [[nodiscard]] auto parse_stmt() -> std::expected<StmtPtr, std::string> {
        Token tok = cur();
        switch (tok.kind) {
            case TokenKind::CppBlock: {
                advance();
                return make_stmt(CppBlockStmt{tok.lexeme, tok.line}, tok.line);
            }
            case TokenKind::KwDefer: {
                advance();
                auto call = parse_expr();
                if (!call) return std::unexpected(call.error());
                return make_stmt(DeferStmt{std::move(*call), tok.line}, tok.line);
            }
            case TokenKind::KwReturn: {
                advance();
                if (cur().kind == TokenKind::RBrace) {
                    return make_stmt(ReturnStmt{nullptr, tok.line}, tok.line);
                }
                auto val = parse_expr();
                if (!val) return std::unexpected(val.error());
                return make_stmt(ReturnStmt{std::move(*val), tok.line}, tok.line);
            }
            case TokenKind::KwIf:
                return parse_if_stmt();
            case TokenKind::KwFor:
                return parse_for_stmt();
            case TokenKind::KwMatch:
                return parse_match_stmt();
            case TokenKind::KwVar: {
                advance();
                bool is_mut = false;
                if (cur().kind == TokenKind::KwMut) {
                    advance();
                    is_mut = true;
                }
                auto id_tok = expect(TokenKind::Ident);
                if (!id_tok) return std::unexpected(id_tok.error());
                std::optional<TypeRef> t_ref;
                if (cur().kind == TokenKind::Colon) {
                    advance();
                    auto tr = parse_type_ref();
                    if (!tr) return std::unexpected(tr.error());
                    t_ref = std::move(*tr);
                }
                ExprPtr init = nullptr;
                if (cur().kind == TokenKind::Assign || cur().kind == TokenKind::ColonEq) {
                    advance();
                    auto ex = parse_expr();
                    if (!ex) return std::unexpected(ex.error());
                    init = std::move(*ex);
                }
                return make_stmt(VarDeclStmt{id_tok->lexeme, is_mut, std::move(t_ref), std::move(init), /*was_var=*/true, id_tok->line, id_tok->col}, id_tok->line);
            }
            case TokenKind::KwMut: {
                advance();
                auto id_tok = expect(TokenKind::Ident);
                if (!id_tok) return std::unexpected(id_tok.error());
                if (cur().kind == TokenKind::Colon) {
                    advance();
                    auto t_ref = parse_type_ref();
                    if (!t_ref) return std::unexpected(t_ref.error());
                    ExprPtr init = nullptr;
                    if (cur().kind == TokenKind::Assign) {
                        advance();
                        auto ex = parse_expr();
                        if (!ex) return std::unexpected(ex.error());
                        init = std::move(*ex);
                    }
                    return make_stmt(VarDeclStmt{id_tok->lexeme, true, std::move(*t_ref), std::move(init), /*was_var=*/false, id_tok->line, id_tok->col}, id_tok->line);
                }
                if (auto ce = expect(TokenKind::ColonEq); !ce) return std::unexpected(ce.error());
                auto init = parse_expr();
                if (!init) return std::unexpected(init.error());
                return make_stmt(VarDeclStmt{id_tok->lexeme, true, std::nullopt, std::move(*init), /*was_var=*/false, id_tok->line, id_tok->col}, id_tok->line);
            }
            case TokenKind::KwAsync: {
                advance();
                if (auto dot = expect(TokenKind::Dot); !dot) return std::unexpected(dot.error());
                auto m = expect(TokenKind::Ident);
                if (!m || m->lexeme != "scope") {
                    return std::unexpected(std::format("{}:{}:{}: expected async.scope", file_, tok.line, tok.col));
                }
                if (auto lp = expect(TokenKind::LParen); !lp) return std::unexpected(lp.error());
                if (cur().kind == TokenKind::Ident && peek(1).kind == TokenKind::Colon) {
                    advance();
                    advance();
                }
                auto workers = parse_expr();
                if (!workers) return std::unexpected(workers.error());
                if (auto rp = expect(TokenKind::RParen); !rp) return std::unexpected(rp.error());
                if (auto p1 = expect(TokenKind::Pipe); !p1) return std::unexpected(p1.error());
                auto scope_var = expect(TokenKind::Ident);
                if (!scope_var) return std::unexpected(scope_var.error());
                if (auto p2 = expect(TokenKind::Pipe); !p2) return std::unexpected(p2.error());
                auto body = parse_block();
                if (!body) return std::unexpected(body.error());
                return make_stmt(AsyncScopeStmt{std::move(*workers), scope_var->lexeme, std::move(*body), tok.line}, tok.line);
            }
            default:
                break;
        }

        // Destructuring declaration: `mut (x, y) := expr` or `(x, y) := expr` or `(mut x, mut y) := expr`
        if (tok.kind == TokenKind::KwMut && peek(1).kind == TokenKind::LParen) {
            auto [is_destr, is_decl] = is_destructure_lookahead(1);
            if (is_destr && is_decl) {
                advance(); // 'mut'
                Token lp = advance(); // '('
                std::vector<std::pair<std::string, bool>> bindings;
                while (cur().kind != TokenKind::RParen && cur().kind != TokenKind::Eof) {
                    if (cur().kind == TokenKind::KwMut) advance();
                    auto id = expect(TokenKind::Ident);
                    if (!id) return std::unexpected(id.error());
                    bindings.push_back({id->lexeme, true});
                    if (cur().kind == TokenKind::Comma) advance();
                }
                if (auto rp = expect(TokenKind::RParen); !rp) return std::unexpected(rp.error());
                if (auto ce = expect(TokenKind::ColonEq); !ce) return std::unexpected(ce.error());
                auto init = parse_expr();
                if (!init) return std::unexpected(init.error());
                return make_stmt(DestructureVarDeclStmt{std::move(bindings), std::move(*init), tok.line, tok.col}, tok.line);
            }
        }

        if (tok.kind == TokenKind::LParen) {
            auto [is_destr, is_decl] = is_destructure_lookahead(0);
            if (is_destr && is_decl) {
                Token lp = advance(); // '('
                std::vector<std::pair<std::string, bool>> bindings;
                while (cur().kind != TokenKind::RParen && cur().kind != TokenKind::Eof) {
                    bool m = false;
                    if (cur().kind == TokenKind::KwMut) {
                        advance();
                        m = true;
                    }
                    auto id = expect(TokenKind::Ident);
                    if (!id) return std::unexpected(id.error());
                    bindings.push_back({id->lexeme, m});
                    if (cur().kind == TokenKind::Comma) advance();
                }
                if (auto rp = expect(TokenKind::RParen); !rp) return std::unexpected(rp.error());
                if (auto ce = expect(TokenKind::ColonEq); !ce) return std::unexpected(ce.error());
                auto init = parse_expr();
                if (!init) return std::unexpected(init.error());
                return make_stmt(DestructureVarDeclStmt{std::move(bindings), std::move(*init), lp.line, lp.col}, lp.line);
            }
            if (is_destr && !is_decl) {
                Token lp = advance(); // '('
                std::vector<ExprPtr> targets;
                while (cur().kind != TokenKind::RParen && cur().kind != TokenKind::Eof) {
                    auto targ = parse_expr();
                    if (!targ) return std::unexpected(targ.error());
                    targets.push_back(std::move(*targ));
                    if (cur().kind == TokenKind::Comma) advance();
                }
                if (auto rp = expect(TokenKind::RParen); !rp) return std::unexpected(rp.error());
                if (auto eq = expect(TokenKind::Assign); !eq) return std::unexpected(eq.error());
                auto rhs = parse_expr();
                if (!rhs) return std::unexpected(rhs.error());
                return make_stmt(DestructureAssignStmt{std::move(targets), "=", std::move(*rhs), lp.line}, lp.line);
            }
        }

        if (tok.kind == TokenKind::Ident && peek(1).kind == TokenKind::ColonEq) {
            Token id_tok = advance();
            advance(); // ':='
            auto init = parse_expr();
            if (!init) return std::unexpected(init.error());
            return make_stmt(VarDeclStmt{id_tok.lexeme, false, std::nullopt, std::move(*init), /*was_var=*/false, id_tok.line, id_tok.col}, id_tok.line);
        }

        if (tok.kind == TokenKind::Ident && peek(1).kind == TokenKind::Colon) {
            Token id_tok = advance();
            advance(); // ':'
            auto t_ref = parse_type_ref();
            if (!t_ref) return std::unexpected(t_ref.error());
            ExprPtr init = nullptr;
            if (cur().kind == TokenKind::Assign) {
                advance();
                auto ex = parse_expr();
                if (!ex) return std::unexpected(ex.error());
                init = std::move(*ex);
            }
            return make_stmt(VarDeclStmt{id_tok.lexeme, false, std::move(*t_ref), std::move(init), /*was_var=*/false, id_tok.line, id_tok.col}, id_tok.line);
        }

        auto lhs = parse_expr();
        if (!lhs) return std::unexpected(lhs.error());
        if (cur().kind == TokenKind::Assign || cur().kind == TokenKind::PlusEq || cur().kind == TokenKind::MinusEq ||
            cur().kind == TokenKind::StarEq || cur().kind == TokenKind::SlashEq || cur().kind == TokenKind::PercentEq ||
            cur().kind == TokenKind::AmpEq || cur().kind == TokenKind::PipeEq || cur().kind == TokenKind::CaretEq ||
            cur().kind == TokenKind::ShlEq || cur().kind == TokenKind::ShrEq) {
            std::string op = advance().lexeme;
            auto rhs = parse_expr();
            if (!rhs) return std::unexpected(rhs.error());
            return make_stmt(AssignStmt{std::move(*lhs), std::move(op), std::move(*rhs), tok.line}, tok.line);
        }
        return make_stmt(ExprStmt{std::move(*lhs), tok.line}, tok.line);
    }

    [[nodiscard]] auto parse_if_stmt() -> std::expected<StmtPtr, std::string> {
        Token start = advance(); // 'if'
        ExprPtr cond = nullptr;
        {
            StructLitScope scope(*this, false);
            auto c = parse_expr();
            if (!c) return std::unexpected(c.error());
            cond = std::move(*c);
        }
        auto then_body = parse_block();
        if (!then_body) return std::unexpected(then_body.error());
        std::vector<StmtPtr> else_body;
        if (cur().kind == TokenKind::KwElse) {
            advance();
            if (cur().kind == TokenKind::KwIf) {
                auto elif = parse_if_stmt();
                if (!elif) return std::unexpected(elif.error());
                else_body.push_back(std::move(*elif));
            } else {
                auto eb = parse_block();
                if (!eb) return std::unexpected(eb.error());
                else_body = std::move(*eb);
            }
        }
        return make_stmt(IfStmt{std::move(cond), std::move(*then_body), std::move(else_body), start.line}, start.line);
    }

    [[nodiscard]] auto parse_for_stmt() -> std::expected<StmtPtr, std::string> {
        Token start = advance(); // 'for'
        if (cur().kind == TokenKind::Ident && (peek(1).kind == TokenKind::Comma || peek(1).kind == TokenKind::KwIn)) {
            std::string first = advance().lexeme;
            std::string idx_var, item_var;
            if (cur().kind == TokenKind::Comma) {
                advance();
                auto second = expect(TokenKind::Ident);
                if (!second) return std::unexpected(second.error());
                idx_var = std::move(first);
                item_var = second->lexeme;
            } else {
                item_var = std::move(first);
            }
            if (auto in_kw = expect(TokenKind::KwIn); !in_kw) return std::unexpected(in_kw.error());
            ExprPtr iter_ex = nullptr;
            {
                StructLitScope scope(*this, false);
                auto ie = parse_expr();
                if (!ie) return std::unexpected(ie.error());
                iter_ex = std::move(*ie);
            }
            auto body = parse_block();
            if (!body) return std::unexpected(body.error());
            return make_stmt(ForStmt{std::move(idx_var), std::move(item_var), std::move(iter_ex), std::move(*body), start.line}, start.line);
        }
        ExprPtr cond = nullptr;
        {
            StructLitScope scope(*this, false);
            auto c = parse_expr();
            if (!c) return std::unexpected(c.error());
            cond = std::move(*c);
        }
        auto body = parse_block();
        if (!body) return std::unexpected(body.error());
        return make_stmt(ForStmt{"", "", std::move(cond), std::move(*body), start.line}, start.line);
    }

    [[nodiscard]] auto parse_match_arms() -> std::expected<std::vector<MatchArm>, std::string> {
        std::vector<MatchArm> arms;
        while (cur().kind != TokenKind::RBrace && cur().kind != TokenKind::Eof) {
            MatchArm arm;
            if (cur().kind == TokenKind::LBracket) {
                Token lb = advance(); // '['
                arm.is_slice_pattern = true;
                arm.line = lb.line;
                bool in_tail = false;
                while (cur().kind != TokenKind::RBracket && cur().kind != TokenKind::Eof) {
                    if (cur().kind == TokenKind::DotDot) {
                        advance(); // '..'
                        arm.slice_has_rest = true;
                        if (cur().kind == TokenKind::Ident) {
                            std::string rest_id = advance().lexeme;
                            if (rest_id != "_") {
                                arm.slice_rest = rest_id;
                            }
                        }
                        in_tail = true;
                    } else if (cur().kind == TokenKind::Ident) {
                        std::string id = advance().lexeme;
                        if (in_tail) {
                            arm.slice_tail.push_back(id);
                        } else {
                            arm.slice_head.push_back(id);
                        }
                    } else {
                        return std::unexpected(std::format("{}:{}: unexpected token in slice pattern", cur().line, cur().col));
                    }
                    if (cur().kind == TokenKind::Comma) advance();
                }
                if (auto rb = expect(TokenKind::RBracket); !rb) return std::unexpected(rb.error());
            } else if (cur().kind == TokenKind::String || cur().kind == TokenKind::RawString || cur().kind == TokenKind::Int ||
                       (cur().kind == TokenKind::Minus && peek(1).kind == TokenKind::Int)) {
                if (cur().kind == TokenKind::Minus) {
                    Token m = advance();
                    Token num = advance();
                    arm.line = m.line;
                    arm.literal_val = "-" + num.lexeme;
                } else if (cur().kind == TokenKind::String) {
                    Token s = advance();
                    arm.line = s.line;
                    arm.literal_val = std::format("\"{}\"", s.lexeme);
                } else if (cur().kind == TokenKind::RawString) {
                    Token s = advance();
                    arm.line = s.line;
                    arm.literal_val = std::format("R\"({})\"", s.lexeme);
                } else {
                    Token num = advance();
                    arm.line = num.line;
                    arm.literal_val = num.lexeme;
                }
            } else {
                auto v_tok = expect(TokenKind::Ident);
                if (!v_tok) return std::unexpected(v_tok.error());
                arm.line = v_tok->line;
                if (cur().kind == TokenKind::DoubleColon || cur().kind == TokenKind::Dot) {
                    advance();
                    auto sub_tok = expect(TokenKind::Ident);
                    if (!sub_tok) return std::unexpected(sub_tok.error());
                    arm.enum_prefix = v_tok->lexeme;
                    arm.variant_name = sub_tok->lexeme;
                } else {
                    arm.variant_name = v_tok->lexeme;
                }

                if (cur().kind == TokenKind::LParen) {
                    advance(); // '('
                    while (cur().kind != TokenKind::RParen && cur().kind != TokenKind::Eof) {
                        auto b_tok = expect(TokenKind::Ident);
                        if (!b_tok) return std::unexpected(b_tok.error());
                        arm.bindings.push_back(b_tok->lexeme);
                        if (cur().kind == TokenKind::Comma) advance();
                    }
                    if (auto rp = expect(TokenKind::RParen); !rp) return std::unexpected(rp.error());
                }
            }

            if (auto fa = expect(TokenKind::FatArrow); !fa) return std::unexpected(fa.error());
            if (cur().kind == TokenKind::LBrace) {
                auto body = parse_block();
                if (!body) return std::unexpected(body.error());
                arm.body = std::move(*body);
            } else {
                auto st = parse_stmt();
                if (!st) return std::unexpected(st.error());
                arm.body.push_back(std::move(*st));
            }
            if (cur().kind == TokenKind::Comma) advance();
            arms.push_back(std::move(arm));
        }
        return arms;
    }

    [[nodiscard]] auto parse_match_stmt() -> std::expected<StmtPtr, std::string> {
        Token start = advance(); // 'match'
        bool is_move = false;
        if (cur().kind == TokenKind::KwMove) {
            advance();
            is_move = true;
        }
        ExprPtr target = nullptr;
        {
            StructLitScope scope(*this, false);
            auto t = parse_expr();
            if (!t) return std::unexpected(t.error());
            target = std::move(*t);
        }
        if (auto lb = expect(TokenKind::LBrace); !lb) return std::unexpected(lb.error());

        auto arms = parse_match_arms();
        if (!arms) return std::unexpected(arms.error());

        if (auto rb = expect(TokenKind::RBrace); !rb) return std::unexpected(rb.error());
        return make_stmt(MatchStmt{std::move(target), is_move, std::move(*arms), start.line, start.col}, start.line);
    }

    // --- Expression Parsing ---

    [[nodiscard]] auto parse_expr() -> std::expected<ExprPtr, std::string> {
        if (recursion_depth_ >= kMaxParseDepth) {
            return std::unexpected(std::format("{}:{}:{}: maximum parser nesting depth (256) exceeded",
                file_, cur().line, cur().col));
        }
        DepthGuard depth_guard(recursion_depth_);
        return parse_or_expr();
    }

    [[nodiscard]] auto parse_or_expr() -> std::expected<ExprPtr, std::string> {
        auto left = parse_binary_expr();
        if (!left) return std::unexpected(left.error());
        if (cur().kind == TokenKind::KwOr) {
            Token or_tok = advance();
            if (cur().kind == TokenKind::Ident && peek(1).kind == TokenKind::LBrace) {
                std::string err_var = advance().lexeme;
                auto body = parse_block();
                if (!body) return std::unexpected(body.error());
                return make_expr(OrExpr{std::move(*left), std::move(err_var), std::move(*body), nullptr}, or_tok.line, or_tok.col);
            }
            auto fb = parse_binary_expr();
            if (!fb) return std::unexpected(fb.error());
            return make_expr(OrExpr{std::move(*left), "", {}, std::move(*fb)}, or_tok.line, or_tok.col);
        }
        return left;
    }

    static int get_binary_precedence(TokenKind k) {
        switch (k) {
            case TokenKind::DoublePipe: return 1;
            case TokenKind::DoubleAmp: return 2;
            case TokenKind::Pipe: return 3;
            case TokenKind::Caret: return 4;
            case TokenKind::Amp: return 5;
            case TokenKind::Eq:
            case TokenKind::Neq: return 6;
            case TokenKind::Lt:
            case TokenKind::Gt:
            case TokenKind::Le:
            case TokenKind::Ge: return 7;
            case TokenKind::Shl:
            case TokenKind::Shr: return 8;
            case TokenKind::Plus:
            case TokenKind::Minus: return 9;
            case TokenKind::Star:
            case TokenKind::Slash:
            case TokenKind::Percent: return 10;
            default: return 0;
        }
    }

    [[nodiscard]] auto parse_binary_expr(int min_prec = 1) -> std::expected<ExprPtr, std::string> {
        auto left = parse_unary_expr();
        if (!left) return std::unexpected(left.error());
        for (;;) {
            TokenKind k = cur().kind;
            int prec = get_binary_precedence(k);
            if (prec < min_prec) break;
            if (pos_ > 0 && cur().line > tokens_[pos_ - 1].line) break;
            Token op = advance();
            auto right = parse_binary_expr(prec + 1);
            if (!right) return std::unexpected(right.error());
            left = make_expr(BinaryExpr{std::move(*left), op.lexeme, std::move(*right)}, op.line, op.col);
        }
        return left;
    }

    [[nodiscard]] auto parse_unary_expr() -> std::expected<ExprPtr, std::string> {
        if (recursion_depth_ >= kMaxParseDepth) {
            return std::unexpected(std::format("{}:{}:{}: maximum parser nesting depth (256) exceeded",
                file_, cur().line, cur().col));
        }
        DepthGuard depth_guard(recursion_depth_);
        Token tok = cur();
        if (tok.kind == TokenKind::KwTry) {
            advance();
            auto op = parse_unary_expr();
            if (!op) return std::unexpected(op.error());
            return make_expr(TryExpr{std::move(*op)}, tok.line, tok.col);
        }
        if (tok.kind == TokenKind::KwMove) {
            advance();
            auto op = parse_unary_expr();
            if (!op) return std::unexpected(op.error());
            return make_expr(UnaryExpr{"move", std::move(*op)}, tok.line, tok.col);
        }
        if (tok.kind == TokenKind::Bang || tok.kind == TokenKind::Star || tok.kind == TokenKind::Minus || tok.kind == TokenKind::Tilde) {
            advance();
            auto op = parse_unary_expr();
            if (!op) return std::unexpected(op.error());
            return make_expr(UnaryExpr{tok.lexeme, std::move(*op)}, tok.line, tok.col);
        }
        if (tok.kind == TokenKind::Amp) {
            advance();
            std::string op_str = "&";
            if (cur().kind == TokenKind::KwMut) {
                advance();
                op_str = "&mut";
            }
            auto op = parse_unary_expr();
            if (!op) return std::unexpected(op.error());
            return make_expr(UnaryExpr{std::move(op_str), std::move(*op)}, tok.line, tok.col);
        }
        return parse_postfix_expr();
    }

    [[nodiscard]] auto parse_postfix_expr() -> std::expected<ExprPtr, std::string> {
        auto expr = parse_primary_expr();
        if (!expr) return std::unexpected(expr.error());

        for (;;) {
            switch (cur().kind) {
                case TokenKind::Dot:
                case TokenKind::DoubleColon: {
                    Token sep = advance();
                    std::string field;
                    if (cur().kind == TokenKind::Ident ||
                        (cur().kind >= TokenKind::KwPackage && cur().kind <= TokenKind::KwNone)) {
                        field = advance().lexeme;
                    } else {
                        return std::unexpected(std::format("{}:{}:{}: expected member name after '{}'",
                            file_, cur().line, cur().col, sep.lexeme));
                    }
                    expr = make_expr(MemberExpr{std::move(*expr), std::move(field), sep.kind == TokenKind::DoubleColon}, sep.line, sep.col);
                    break;
                }
                case TokenKind::LBracket: {
                    if (is_generic_call_lookahead()) {
                        std::size_t saved_pos = pos_;
                        advance(); // '['
                        std::vector<TypeRef> type_args;
                        bool ok = true;
                        while (cur().kind != TokenKind::RBracket && cur().kind != TokenKind::Eof) {
                            auto targ = parse_type_ref();
                            if (!targ) {
                                ok = false;
                                break;
                            }
                            type_args.push_back(std::move(*targ));
                            if (cur().kind == TokenKind::Comma) {
                                advance();
                            } else if (cur().kind != TokenKind::RBracket) {
                                ok = false;
                                break;
                            }
                        }
                        if (ok && cur().kind == TokenKind::RBracket) {
                            advance(); // ']'
                            auto call = parse_call_with_callee(std::move(*expr), std::move(type_args));
                            if (call) {
                                expr = std::move(*call);
                                break;
                            }
                        }
                        pos_ = saved_pos;
                    }
                    Token br = advance(); // '['
                    StructLitScope bracket_scope(*this, true);
                    // Check for `[..end]` or `[..]`
                        if (cur().kind == TokenKind::DotDot) {
                            advance(); // '..'
                            ExprPtr end_ex = nullptr;
                            if (cur().kind != TokenKind::RBracket) {
                                auto e = parse_expr();
                                if (!e) return std::unexpected(e.error());
                                end_ex = std::move(*e);
                            }
                            if (auto rb = expect(TokenKind::RBracket); !rb) return std::unexpected(rb.error());
                            expr = make_expr(SliceExpr{std::move(*expr), nullptr, std::move(end_ex)}, br.line, br.col);
                            break;
                        }
                        auto first = parse_expr();
                        if (!first) return std::unexpected(first.error());
                        if (cur().kind == TokenKind::DotDot) {
                            advance(); // '..'
                            ExprPtr end_ex = nullptr;
                            if (cur().kind != TokenKind::RBracket) {
                                auto e = parse_expr();
                                if (!e) return std::unexpected(e.error());
                                end_ex = std::move(*e);
                            }
                            if (auto rb = expect(TokenKind::RBracket); !rb) return std::unexpected(rb.error());
                            expr = make_expr(SliceExpr{std::move(*expr), std::move(*first), std::move(end_ex)}, br.line, br.col);
                            break;
                        }
                        if (auto rb = expect(TokenKind::RBracket); !rb) return std::unexpected(rb.error());
                        expr = make_expr(IndexExpr{std::move(*expr), std::move(*first)}, br.line, br.col);
                        break;
                    }
                case TokenKind::LParen: {
                    if (is_destructure_lookahead(0).first) {
                        return expr;
                    }
                    auto call = parse_call_with_callee(std::move(*expr), {});
                    if (!call) return std::unexpected(call.error());
                    expr = std::move(*call);
                    break;
                }
                case TokenKind::Pipe: {
                    if (is_trailing_closure_lookahead()) {
                        auto cl = parse_closure_expr();
                        if (!cl) return std::unexpected(cl.error());
                        int ln = (*expr)->line;
                        int cln = (*expr)->col;
                        expr = make_expr(CallExpr{std::move(*expr), {}, {}, std::move(*cl)}, ln, cln);
                        break;
                    }
                    return expr;
                }
                case TokenKind::DoublePipe: {
                    if (peek(1).kind == TokenKind::LBrace) {
                        auto cl = parse_closure_expr();
                        if (!cl) return std::unexpected(cl.error());
                        int ln = (*expr)->line;
                        int cln = (*expr)->col;
                        expr = make_expr(CallExpr{std::move(*expr), {}, {}, std::move(*cl)}, ln, cln);
                        break;
                    }
                    return expr;
                }
                default:
                    return expr;
            }
        }
    }

    [[nodiscard]] auto parse_call_with_callee(ExprPtr callee, std::vector<TypeRef> type_args)
        -> std::expected<ExprPtr, std::string>
    {
        Token lp = advance(); // '('
        StructLitScope arg_scope(*this, true);
        CallExpr call;
        call.callee = std::move(callee);
        call.type_args = std::move(type_args);

        if (auto* id = std::get_if<IdentExpr>(&call.callee->node);
            id && id->name == "make" &&
            (cur().kind == TokenKind::LBracket || cur().kind == TokenKind::KwMap ||
             (cur().kind == TokenKind::Ident && (cur().lexeme == "own" || cur().lexeme == "shared") && peek(1).kind == TokenKind::LBracket))) {
            auto tref = parse_type_ref();
            if (!tref) return std::unexpected(tref.error());
            call.type_args.push_back(std::move(*tref));
            if (cur().kind == TokenKind::Comma) advance();
        }

        while (cur().kind != TokenKind::RParen && cur().kind != TokenKind::Eof) {
            if (cur().kind == TokenKind::Pipe || cur().kind == TokenKind::DoublePipe) {
                Token cl_tok = cur();
                auto cl = parse_closure_expr();
                if (!cl) return std::unexpected(cl.error());
                if (cur().kind == TokenKind::Comma) advance();
                if (cur().kind != TokenKind::RParen && cur().kind != TokenKind::Eof) {
                    call.args.push_back(CallArg{"", "", make_expr(std::move(*cl), cl_tok.line, cl_tok.col), cl_tok.line, cl_tok.col});
                } else {
                    call.block_arg = std::move(*cl);
                }
                continue;
            }
            Token arg_tok = cur();
            std::string intent;
            if (arg_tok.kind == TokenKind::KwMut || arg_tok.kind == TokenKind::KwMove || arg_tok.kind == TokenKind::KwOut) {
                intent = advance().lexeme;
            }
            std::string label;
            if (cur().kind == TokenKind::Ident && peek(1).kind == TokenKind::Colon) {
                label = advance().lexeme;
                advance(); // ':'
            }
            auto val = parse_expr();
            if (!val) return std::unexpected(val.error());
            call.args.push_back(CallArg{std::move(intent), std::move(label), std::move(*val), arg_tok.line, arg_tok.col});
            if (cur().kind == TokenKind::Comma) advance();
        }
        if (auto rp = expect(TokenKind::RParen); !rp) return std::unexpected(rp.error());
        return make_expr(std::move(call), lp.line, lp.col);
    }

    [[nodiscard]] auto parse_closure_expr() -> std::expected<ClosureExpr, std::string> {
        ClosureExpr cl;
        if (cur().kind == TokenKind::DoublePipe) {
            advance();
        } else {
            if (auto p1 = expect(TokenKind::Pipe); !p1) return std::unexpected(p1.error());
            while (cur().kind != TokenKind::Pipe && cur().kind != TokenKind::Eof) {
                std::string p_intent = "in";
                if (cur().kind == TokenKind::KwMut || cur().kind == TokenKind::KwMove ||
                    cur().kind == TokenKind::KwOut || cur().kind == TokenKind::KwIn) {
                    p_intent = advance().lexeme;
                }
                auto id = expect(TokenKind::Ident);
                if (!id) return std::unexpected(id.error());
                cl.params.push_back(id->lexeme);
                cl.param_intents.push_back(std::move(p_intent));
                if (cur().kind == TokenKind::Colon) {
                    advance(); // ':'
                    auto tref = parse_type_ref();
                    if (!tref) return std::unexpected(tref.error());
                    cl.param_types.push_back(std::move(*tref));
                } else {
                    cl.param_types.push_back(std::nullopt);
                }
                if (cur().kind == TokenKind::Comma) advance();
            }
            if (auto p2 = expect(TokenKind::Pipe); !p2) return std::unexpected(p2.error());
        }
        if (cur().kind == TokenKind::KwAsync) {
            advance();
            cl.is_async = true;
        }
        if (cur().kind == TokenKind::Arrow) {
            advance(); // '->'
            StructLitScope scope(*this, false);
            auto rtype = parse_type_ref();
            if (!rtype) return std::unexpected(rtype.error());
            cl.return_type = std::move(*rtype);
        }
        if (cur().kind == TokenKind::KwAsync) {
            advance();
            cl.is_async = true;
        }
        auto body = parse_block();
        if (!body) return std::unexpected(body.error());
        cl.body = std::move(*body);
        return cl;
    }

    [[nodiscard]] auto parse_primary_expr() -> std::expected<ExprPtr, std::string> {
        Token tok = cur();
        switch (tok.kind) {
            case TokenKind::Int:
            case TokenKind::Float:
            case TokenKind::String:
            case TokenKind::RawString:
            case TokenKind::KwTrue:
            case TokenKind::KwFalse:
            case TokenKind::KwNone: {
                advance();
                return make_expr(LiteralExpr{tok.kind, tok.lexeme}, tok.line, tok.col);
            }
            case TokenKind::KwMatch: {
                Token start = advance(); // 'match'
                bool is_move = false;
                if (cur().kind == TokenKind::KwMove) {
                    advance();
                    is_move = true;
                }
                ExprPtr target = nullptr;
                {
                    StructLitScope scope(*this, false);
                    auto t = parse_expr();
                    if (!t) return std::unexpected(t.error());
                    target = std::move(*t);
                }
                if (auto lb = expect(TokenKind::LBrace); !lb) return std::unexpected(lb.error());
                auto arms = parse_match_arms();
                if (!arms) return std::unexpected(arms.error());
                if (auto rb = expect(TokenKind::RBrace); !rb) return std::unexpected(rb.error());
                return make_expr(MatchExpr{std::move(target), is_move, std::move(*arms), start.line, start.col}, start.line, start.col);
            }
            case TokenKind::Ident:
            case TokenKind::KwCpp: {
                advance();
                std::string name = tok.lexeme;
                while (cur().kind == TokenKind::DoubleColon ||
                       (cur().kind == TokenKind::Dot && peek(1).kind == TokenKind::Ident &&
                        !peek(1).lexeme.empty() && peek(1).lexeme[0] >= 'A' && peek(1).lexeme[0] <= 'Z' &&
                        (peek(2).kind == TokenKind::LBrace || peek(2).kind == TokenKind::Dot))) {
                    auto sep = advance();
                    auto next_id = expect(TokenKind::Ident);
                    if (!next_id) return std::unexpected(next_id.error());
                    name += (sep.kind == TokenKind::DoubleColon ? "::" : ".") + next_id->lexeme;
                }
                if (allow_struct_lit_ && cur().kind == TokenKind::LBrace && !name.empty() &&
                    (name.contains("::") || (name.contains(".") && name.substr(name.rfind('.') + 1)[0] >= 'A' && name.substr(name.rfind('.') + 1)[0] <= 'Z') || (name[0] >= 'A' && name[0] <= 'Z'))) {
                    advance(); // '{'
                    StructLitExpr st;
                    st.type_name = std::move(name);
                    while (cur().kind != TokenKind::RBrace && cur().kind != TokenKind::Eof) {
                        std::string fn_name;
                        if (cur().kind == TokenKind::Ident ||
                            (cur().kind >= TokenKind::KwPackage && cur().kind <= TokenKind::KwNone)) {
                            fn_name = advance().lexeme;
                        } else {
                            auto fn = expect(TokenKind::Ident);
                            if (!fn) return std::unexpected(fn.error());
                            fn_name = fn->lexeme;
                        }
                        if (auto col = expect(TokenKind::Colon); !col) return std::unexpected(col.error());
                        auto fv = parse_expr();
                        if (!fv) return std::unexpected(fv.error());
                        st.fields.push_back(StructLitField{std::move(fn_name), std::move(*fv)});
                        if (cur().kind == TokenKind::Comma) advance();
                    }
                    if (auto rb = expect(TokenKind::RBrace); !rb) return std::unexpected(rb.error());
                    return make_expr(std::move(st), tok.line, tok.col);
                }
                return make_expr(IdentExpr{std::move(name)}, tok.line, tok.col);
            }
            case TokenKind::LParen: {
                Token lp = advance(); // '('
                StructLitScope paren_scope(*this, true);
                if (cur().kind == TokenKind::RParen) {
                    advance(); // ')'
                    return make_expr(TupleExpr{}, tok.line, tok.col);
                }
                auto first = parse_expr();
                if (!first) return std::unexpected(first.error());
                if (cur().kind == TokenKind::Comma) {
                    std::vector<ExprPtr> elements;
                    elements.push_back(std::move(*first));
                    while (cur().kind == TokenKind::Comma) {
                        advance(); // ','
                        if (cur().kind == TokenKind::RParen) break; // trailing comma
                        auto next_ex = parse_expr();
                        if (!next_ex) return std::unexpected(next_ex.error());
                        elements.push_back(std::move(*next_ex));
                    }
                    if (auto rp = expect(TokenKind::RParen); !rp) return std::unexpected(rp.error());
                    return make_expr(TupleExpr{std::move(elements)}, tok.line, tok.col);
                }
                if (auto rp = expect(TokenKind::RParen); !rp) return std::unexpected(rp.error());
                return first;
            }
            case TokenKind::LBrace: {
                if (!allow_struct_lit_) {
                    return std::unexpected(std::format("{}:{}:{}: unexpected '{{' in expression", file_, tok.line, tok.col));
                }
                advance();
                StructLitExpr st;
                while (cur().kind != TokenKind::RBrace && cur().kind != TokenKind::Eof) {
                    std::string key;
                    if (cur().kind == TokenKind::Ident || cur().kind == TokenKind::String) {
                        key = advance().lexeme;
                    }
                    if (cur().kind == TokenKind::Colon) advance();
                    auto fv = parse_expr();
                    if (!fv) return std::unexpected(fv.error());
                    st.fields.push_back(StructLitField{std::move(key), std::move(*fv)});
                    if (cur().kind == TokenKind::Comma) advance();
                }
                if (auto rb = expect(TokenKind::RBrace); !rb) return std::unexpected(rb.error());
                return make_expr(std::move(st), tok.line, tok.col);
            }
            case TokenKind::LBracket: {
                Token lb = advance(); // '['
                StructLitScope bracket_scope(*this, true);
                ArrayLitExpr arr;
                while (cur().kind != TokenKind::RBracket && cur().kind != TokenKind::Eof) {
                    auto el = parse_expr();
                    if (!el) return std::unexpected(el.error());
                    arr.elements.push_back(std::move(*el));
                    if (cur().kind == TokenKind::Comma) advance();
                }
                if (auto rb = expect(TokenKind::RBracket); !rb) return std::unexpected(rb.error());
                return make_expr(std::move(arr), tok.line, tok.col);
            }
            case TokenKind::Pipe:
            case TokenKind::DoublePipe: {
                auto cl = parse_closure_expr();
                if (!cl) return std::unexpected(cl.error());
                return make_expr(std::move(*cl), tok.line, tok.col);
            }
            default:
                return std::unexpected(std::format("{}:{}:{}: unexpected token '{}' in expression",
                    file_, tok.line, tok.col, tok.lexeme));
        }
    }
};

[[nodiscard]] inline auto parse_source_resilient(std::string_view path, std::string_view src)
    -> ParseResult
{
    auto arena = std::make_shared<AstArena>();
    ArenaScope scope(arena.get());
    auto toks = tokenize(path, src);
    if (!toks) {
        return ParseResult{FileAST{std::string(path), "", {}, {}, {}, {}, {}, {}, {}, arena}, {toks.error()}};
    }
    Parser p(std::string(path), std::move(*toks), true);
    auto res = p.parse_file_result();
    res.ast.arena = arena;
    return res;
}

[[nodiscard]] inline auto parse_source(std::string_view path, std::string_view src)
    -> std::expected<FileAST, std::string>
{
    auto res = parse_source_resilient(path, src);
    if (!res.ok()) {
        return std::unexpected(res.errors.front());
    }
    return std::move(res.ast);
}

} // namespace cfire::frontend
