#pragma once
// src/lexer.hpp — Native C++23 Tokenizer for cfire (.cf)

#include <cctype>
#include <expected>
#include <format>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cfire::frontend {

enum class TokenKind {
    Eof,
    Ident,
    Int,
    Float,
    String,
    RawString, // Backtick `...`
    CppBlock,  // Balanced `{ ... }` after `cplusplus`

    // Keywords
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

    // Symbols & Operators
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
    Semicolon,
};

struct Token {
    TokenKind kind = TokenKind::Eof;
    std::string lexeme{};
    int line = 1;
    int col = 1;
};

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
        case TokenKind::Plus: return "+";
        case TokenKind::Minus: return "-";
        case TokenKind::Star: return "*";
        case TokenKind::Slash: return "/";
        case TokenKind::Percent: return "%";
        case TokenKind::Amp: return "&";
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

[[nodiscard]] inline auto tokenize(std::string_view file, std::string_view src)
    -> std::expected<std::vector<Token>, std::string>
{
    static const std::unordered_map<std::string_view, TokenKind> keywords = {
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

    std::vector<Token> tokens;
    std::size_t i = 0;
    const std::size_t n = src.size();
    int line = 1;
    int col = 1;

    auto peek = [&](std::size_t offset = 0) -> char {
        return (i + offset < n) ? src[i + offset] : '\0';
    };

    auto advance = [&]() -> char {
        char c = src[i++];
        if (c == '\n') {
            ++line;
            col = 1;
        } else {
            ++col;
        }
        return c;
    };

    while (i < n) {
        char c = peek();
        if (std::isspace(static_cast<unsigned char>(c))) {
            advance();
            continue;
        }
        if (c == '/' && peek(1) == '/') {
            while (i < n && peek() != '\n') {
                advance();
            }
            continue;
        }

        int start_line = line;
        int start_col = col;

        // Identifiers & Keywords
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            std::string word;
            while (i < n && (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_')) {
                word.push_back(advance());
            }
            if (word == "cpp") {
                std::size_t probe = i;
                while (probe < n && std::isspace(static_cast<unsigned char>(src[probe]))) {
                    ++probe;
                }
                if (probe < n && src[probe] == '{') {
                    while (i < probe) advance();
                    advance(); // '{'
                    int depth = 1;
                    std::string cpp_buf;
                    while (i < n && depth > 0) {
                        char ch = advance();
                        if (ch == '{') {
                            ++depth;
                        } else if (ch == '}') {
                            --depth;
                            if (depth == 0) break;
                        }
                        cpp_buf.push_back(ch);
                    }
                    tokens.push_back(Token{TokenKind::CppBlock, std::move(cpp_buf), start_line, start_col});
                    continue;
                }
                tokens.push_back(Token{TokenKind::KwCpp, std::move(word), start_line, start_col});
                continue;
            }
            if (auto it = keywords.find(word); it != keywords.end()) {
                tokens.push_back(Token{it->second, std::move(word), start_line, start_col});
            } else {
                tokens.push_back(Token{TokenKind::Ident, std::move(word), start_line, start_col});
            }
            continue;
        }

        // Numbers (stop before '..' range operator!)
        if (std::isdigit(static_cast<unsigned char>(c))) {
            std::string num;
            bool is_float = false;
            while (i < n && (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '_' ||
                             (peek() == '.' && peek(1) != '.'))) {
                char ch = advance();
                if (ch == '_') continue;
                if (ch == '.') is_float = true;
                num.push_back(ch);
            }
            tokens.push_back(Token{is_float ? TokenKind::Float : TokenKind::Int, std::move(num), start_line, start_col});
            continue;
        }

        // Double-quoted String
        if (c == '"') {
            advance();
            std::string s;
            while (i < n && peek() != '"') {
                if (peek() == '\\') {
                    s.push_back(advance());
                }
                s.push_back(advance());
            }
            if (i >= n) {
                return std::unexpected(std::format("{}:{}:{}: unterminated string literal", file, start_line, start_col));
            }
            advance();
            tokens.push_back(Token{TokenKind::String, std::move(s), start_line, start_col});
            continue;
        }

        // Backtick Raw String / Struct Tag
        if (c == '`') {
            advance();
            std::string s;
            while (i < n && peek() != '`') {
                s.push_back(advance());
            }
            if (i >= n) {
                return std::unexpected(std::format("{}:{}:{}: unterminated raw string literal", file, start_line, start_col));
            }
            advance();
            tokens.push_back(Token{TokenKind::RawString, std::move(s), start_line, start_col});
            continue;
        }

        // Two-character operators
        char c2 = peek(1);
        if (c == '.' && c2 == '.') {
            advance(); advance();
            tokens.push_back(Token{TokenKind::DotDot, "..", start_line, start_col});
            continue;
        }
        if (c == '=' && c2 == '>') {
            advance(); advance();
            tokens.push_back(Token{TokenKind::FatArrow, "=>", start_line, start_col});
            continue;
        }
        if (c == ':' && c2 == '=') {
            advance(); advance();
            tokens.push_back(Token{TokenKind::ColonEq, ":=", start_line, start_col});
            continue;
        }
        if (c == ':' && c2 == ':') {
            advance(); advance();
            tokens.push_back(Token{TokenKind::DoubleColon, "::", start_line, start_col});
            continue;
        }
        if (c == '-' && c2 == '>') {
            advance(); advance();
            tokens.push_back(Token{TokenKind::Arrow, "->", start_line, start_col});
            continue;
        }
        if (c == '+' && c2 == '=') {
            advance(); advance();
            tokens.push_back(Token{TokenKind::PlusEq, "+=", start_line, start_col});
            continue;
        }
        if (c == '-' && c2 == '=') {
            advance(); advance();
            tokens.push_back(Token{TokenKind::MinusEq, "-=", start_line, start_col});
            continue;
        }
        if (c == '=' && c2 == '=') {
            advance(); advance();
            tokens.push_back(Token{TokenKind::Eq, "==", start_line, start_col});
            continue;
        }
        if (c == '!' && c2 == '=') {
            advance(); advance();
            tokens.push_back(Token{TokenKind::Neq, "!=", start_line, start_col});
            continue;
        }
        if (c == '<' && c2 == '=') {
            advance(); advance();
            tokens.push_back(Token{TokenKind::Le, "<=", start_line, start_col});
            continue;
        }
        if (c == '>' && c2 == '=') {
            advance(); advance();
            tokens.push_back(Token{TokenKind::Ge, ">=", start_line, start_col});
            continue;
        }
        if (c == '|' && c2 == '|') {
            advance(); advance();
            tokens.push_back(Token{TokenKind::DoublePipe, "||", start_line, start_col});
            continue;
        }

        // Single-character symbols
        advance();
        switch (c) {
            case '(': tokens.push_back(Token{TokenKind::LParen, "(", start_line, start_col}); break;
            case ')': tokens.push_back(Token{TokenKind::RParen, ")", start_line, start_col}); break;
            case '{': tokens.push_back(Token{TokenKind::LBrace, "{", start_line, start_col}); break;
            case '}': tokens.push_back(Token{TokenKind::RBrace, "}", start_line, start_col}); break;
            case '[': tokens.push_back(Token{TokenKind::LBracket, "[", start_line, start_col}); break;
            case ']': tokens.push_back(Token{TokenKind::RBracket, "]", start_line, start_col}); break;
            case ':': tokens.push_back(Token{TokenKind::Colon, ":", start_line, start_col}); break;
            case ',': tokens.push_back(Token{TokenKind::Comma, ",", start_line, start_col}); break;
            case '.': tokens.push_back(Token{TokenKind::Dot, ".", start_line, start_col}); break;
            case '=': tokens.push_back(Token{TokenKind::Assign, "=", start_line, start_col}); break;
            case '+': tokens.push_back(Token{TokenKind::Plus, "+", start_line, start_col}); break;
            case '-': tokens.push_back(Token{TokenKind::Minus, "-", start_line, start_col}); break;
            case '*': tokens.push_back(Token{TokenKind::Star, "*", start_line, start_col}); break;
            case '/': tokens.push_back(Token{TokenKind::Slash, "/", start_line, start_col}); break;
            case '%': tokens.push_back(Token{TokenKind::Percent, "%", start_line, start_col}); break;
            case '&': tokens.push_back(Token{TokenKind::Amp, "&", start_line, start_col}); break;
            case '|': tokens.push_back(Token{TokenKind::Pipe, "|", start_line, start_col}); break;
            case '!': tokens.push_back(Token{TokenKind::Bang, "!", start_line, start_col}); break;
            case '?': tokens.push_back(Token{TokenKind::Question, "?", start_line, start_col}); break;
            case '@': tokens.push_back(Token{TokenKind::At, "@", start_line, start_col}); break;
            case '<': tokens.push_back(Token{TokenKind::Lt, "<", start_line, start_col}); break;
            case '>': tokens.push_back(Token{TokenKind::Gt, ">", start_line, start_col}); break;
            case ';': tokens.push_back(Token{TokenKind::Semicolon, ";", start_line, start_col}); break;
            default:
                return std::unexpected(std::format("{}:{}:{}: unexpected character '{}'", file, start_line, start_col, c));
        }
    }

    tokens.push_back(Token{TokenKind::Eof, "", line, col});
    return tokens;
}

} // namespace cfire::frontend
