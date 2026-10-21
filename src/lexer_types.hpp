#pragma once
// src/lexer_types.hpp — Token Definitions and Keyword Tables for cfire (.cf)

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace cfire::frontend {

enum class TokenKind : int {
    Eof = 0,
    Ident,
    Int,
    Float,
    String,
    RawString, // Backtick `...`
    CppBlock,  // Balanced `{ ... }` after `cplusplus` or `cpp`

    // Keywords (28)
    KwPackage,
    KwImport,
    KwCpp,
    KwAs,
    KwType,
    KwStruct,
    KwEnum,
    KwInterface,
    KwMap,
    KwFn,
    KwAsync,
    KwIn,
    KwMut,
    KwOut,
    KwMove,
    KwVar,
    KwIf,
    KwElse,
    KwFor,
    KwMatch,
    KwDefer,
    KwReturn,
    KwTry,
    KwOr,
    KwTest,
    KwTrue,
    KwFalse,
    KwNone,

    // Symbols & Operators (36)
    LParen,
    RParen,
    LBrace,
    RBrace,
    LBracket,
    RBracket,
    Colon,
    ColonEq,
    DoubleColon,
    Comma,
    Dot,
    DotDot,
    Arrow,
    FatArrow,
    Assign,
    PlusEq,
    MinusEq,
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    Amp,
    DoubleAmp,
    Pipe,
    DoublePipe,
    Bang,
    Question,
    At,
    Eq,
    Neq,
    Lt,
    Gt,
    Le,
    Ge,
    StarEq,
    SlashEq,
    PercentEq,
    AmpEq,
    PipeEq,
    Caret,
    CaretEq,
    Tilde,
    Shl,
    Shr,
    ShlEq,
    ShrEq,
    Semicolon,
};

struct Token {
    TokenKind kind = TokenKind::Eof;
    std::string lexeme{};
    int line = 1;
    int col = 1;
};

[[nodiscard]] inline auto make_tok(TokenKind k, std::string_view lx, int l, int c) -> Token {
    return Token{k, std::string(lx), l, c};
}

[[nodiscard]] inline auto token_kind_name(TokenKind k) -> std::string_view {
    switch (k) {
        case TokenKind::Eof: return "EOF";
        case TokenKind::Ident: return "IDENT";
        case TokenKind::Int: return "INT";
        case TokenKind::Float: return "FLOAT";
        case TokenKind::String: return "STRING";
        case TokenKind::RawString: return "RAW_STRING";
        case TokenKind::CppBlock: return "CPP_BLOCK";
        case TokenKind::KwPackage: return "package";
        case TokenKind::KwImport: return "import";
        case TokenKind::KwCpp: return "cpp";
        case TokenKind::KwAs: return "as";
        case TokenKind::KwType: return "type";
        case TokenKind::KwStruct: return "struct";
        case TokenKind::KwEnum: return "enum";
        case TokenKind::KwInterface: return "interface";
        case TokenKind::KwMap: return "map";
        case TokenKind::KwFn: return "fn";
        case TokenKind::KwAsync: return "async";
        case TokenKind::KwIn: return "in";
        case TokenKind::KwMut: return "mut";
        case TokenKind::KwOut: return "out";
        case TokenKind::KwMove: return "move";
        case TokenKind::KwVar: return "var";
        case TokenKind::KwIf: return "if";
        case TokenKind::KwElse: return "else";
        case TokenKind::KwFor: return "for";
        case TokenKind::KwMatch: return "match";
        case TokenKind::KwDefer: return "defer";
        case TokenKind::KwReturn: return "return";
        case TokenKind::KwTry: return "try";
        case TokenKind::KwOr: return "or";
        case TokenKind::KwTest: return "test";
        case TokenKind::KwTrue: return "true";
        case TokenKind::KwFalse: return "false";
        case TokenKind::KwNone: return "none";
        case TokenKind::LParen: return "(";
        case TokenKind::RParen: return ")";
        case TokenKind::LBrace: return "{";
        case TokenKind::RBrace: return "}";
        case TokenKind::LBracket: return "[";
        case TokenKind::RBracket: return "]";
        case TokenKind::Colon: return ":";
        case TokenKind::ColonEq: return ":=";
        case TokenKind::DoubleColon: return "::";
        case TokenKind::Comma: return ",";
        case TokenKind::Dot: return ".";
        case TokenKind::DotDot: return "..";
        case TokenKind::Arrow: return "->";
        case TokenKind::FatArrow: return "=>";
        case TokenKind::Assign: return "=";
        case TokenKind::PlusEq: return "+=";
        case TokenKind::MinusEq: return "-=";
        case TokenKind::StarEq: return "*=";
        case TokenKind::SlashEq: return "/=";
        case TokenKind::PercentEq: return "%=";
        case TokenKind::AmpEq: return "&=";
        case TokenKind::PipeEq: return "|=";
        case TokenKind::Caret: return "^";
        case TokenKind::CaretEq: return "^=";
        case TokenKind::Tilde: return "~";
        case TokenKind::Shl: return "<<";
        case TokenKind::Shr: return ">>";
        case TokenKind::ShlEq: return "<<=";
        case TokenKind::ShrEq: return ">>=";
        case TokenKind::Plus: return "+";
        case TokenKind::Minus: return "-";
        case TokenKind::Star: return "*";
        case TokenKind::Slash: return "/";
        case TokenKind::Percent: return "%";
        case TokenKind::Amp: return "&";
        case TokenKind::DoubleAmp: return "&&";
        case TokenKind::Pipe: return "|";
        case TokenKind::DoublePipe: return "||";
        case TokenKind::Bang: return "!";
        case TokenKind::Question: return "?";
        case TokenKind::At: return "@";
        case TokenKind::Eq: return "==";
        case TokenKind::Neq: return "!=";
        case TokenKind::Lt: return "<";
        case TokenKind::Gt: return ">";
        case TokenKind::Le: return "<=";
        case TokenKind::Ge: return ">=";
        case TokenKind::Semicolon: return ";";
    }
    return "?";
}

inline const std::unordered_map<std::string_view, TokenKind>& keyword_map() {
    static const std::unordered_map<std::string_view, TokenKind> kKeywords = {
        {"package", TokenKind::KwPackage},
        {"import", TokenKind::KwImport},
        {"cpp", TokenKind::KwCpp},
        {"as", TokenKind::KwAs},
        {"type", TokenKind::KwType},
        {"struct", TokenKind::KwStruct},
        {"enum", TokenKind::KwEnum},
        {"interface", TokenKind::KwInterface},
        {"map", TokenKind::KwMap},
        {"fn", TokenKind::KwFn},
        {"async", TokenKind::KwAsync},
        {"in", TokenKind::KwIn},
        {"mut", TokenKind::KwMut},
        {"out", TokenKind::KwOut},
        {"move", TokenKind::KwMove},
        {"var", TokenKind::KwVar},
        {"if", TokenKind::KwIf},
        {"else", TokenKind::KwElse},
        {"for", TokenKind::KwFor},
        {"match", TokenKind::KwMatch},
        {"defer", TokenKind::KwDefer},
        {"return", TokenKind::KwReturn},
        {"try", TokenKind::KwTry},
        {"or", TokenKind::KwOr},
        {"test", TokenKind::KwTest},
        {"true", TokenKind::KwTrue},
        {"false", TokenKind::KwFalse},
        {"none", TokenKind::KwNone},
    };
    return kKeywords;
}

[[nodiscard]] inline auto find_keyword(std::string_view w) -> std::optional<TokenKind> {
    const auto& map = keyword_map();
    auto it = map.find(w);
    if (it != map.end()) return it->second;
    return std::nullopt;
}

} // namespace cfire::frontend
