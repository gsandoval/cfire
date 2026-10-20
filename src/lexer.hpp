#pragma once
// src/lexer.hpp — Tokenizer for cfire (.cf)

#include "lexer_types.hpp"
#include "bootstrap/lexer.generated.hpp"
#include <cctype>
#include <expected>
#include <format>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cfire::frontend {

[[nodiscard]] inline auto tokenize_cxx(std::string_view file, std::string_view src)
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
        if (c == '&' && c2 == '&') {
            advance(); advance();
            tokens.push_back(Token{TokenKind::DoubleAmp, "&&", start_line, start_col});
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

[[nodiscard]] inline auto selfhost_tokenize_adapter(std::string_view file, std::string_view src)
    -> std::expected<std::vector<Token>, std::string>
{
    auto res = ::selfhost_tokenize(file, src);
    if (!res) {
        return std::unexpected(std::string(res.error().message()));
    }
    return std::move(res.value());
}

[[nodiscard]] inline auto tokenize(std::string_view file, std::string_view src)
    -> std::expected<std::vector<Token>, std::string>
{
    return selfhost_tokenize_adapter(file, src);
}

} // namespace cfire::frontend
