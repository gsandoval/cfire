#pragma once
// src/lexer.hpp — Tokenizer for cfire (.cf)

#include "lexer_types.hpp"
#include <cctype>
#include <expected>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace cfire::frontend {

// Q24 (CF-149): lexer and parser errors are structured at the point of detection. `text()` is the
// rendered `file:line:col: message` form for callers that only print.
struct SourceError {
    std::string file{};
    int line = 1;
    int col = 1;
    std::string message{};
    std::string code{"cfire/SYNTAX"};

    SourceError() = default;
    SourceError(std::string f, int l, int c, std::string msg, std::string cd = "cfire/SYNTAX")
        : file(std::move(f)), line(l), col(c), message(std::move(msg)), code(std::move(cd)) {}
    SourceError(const char* msg) : message(msg ? msg : "") {}  // NOLINT: lets `ok ? "ok" : err` type-check

    [[nodiscard]] auto text() const -> std::string {
        // Non-default codes render as a single trailing "(code)" suffix, matching sema output.
        std::string body = message;
        if (!code.empty() && code != "cfire/SYNTAX" && body.find(code) == std::string::npos)
            body += std::format(" ({})", code);
        if (file.empty()) return body;
        return std::format("{}:{}:{}: {}", file, line, col, body);
    }
    operator std::string() const { return text(); }  // NOLINT: compatibility for callers that print errors
    // Read-only text queries for callers that inspect the rendered error.
    [[nodiscard]] auto contains(std::string_view needle) const -> bool { return text().find(needle) != std::string::npos; }
    [[nodiscard]] auto find(std::string_view needle) const -> std::size_t { return text().find(needle); }
    [[nodiscard]] auto starts_with(std::string_view p) const -> bool { return text().starts_with(p); }
    [[nodiscard]] auto empty() const -> bool { return message.empty(); }
};

} // namespace cfire::frontend

template <>
struct std::formatter<cfire::frontend::SourceError, char> : std::formatter<std::string, char> {
    template <typename FormatContext>
    auto format(const cfire::frontend::SourceError& e, FormatContext& ctx) const {
        return std::formatter<std::string, char>::format(e.text(), ctx);
    }
};

namespace cfire::frontend {

// A comment's byte range `[begin, end)` in the source (`cfire fmt` re-emits comments verbatim).
struct CommentSpan {
    std::size_t begin = 0;
    std::size_t end = 0;
    bool block = false;
};

[[nodiscard]] inline auto tokenize(std::string_view file, std::string_view src, std::vector<CommentSpan>* comments = nullptr)
    -> std::expected<std::vector<Token>, SourceError>
{
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
            const std::size_t begin = i;
            while (i < n && peek() != '\n') {
                advance();
            }
            if (comments) comments->push_back({begin, i, false});
            continue;
        }
        // Q32 (CF-172): `/* ... */` block comments, which nest.
        if (c == '/' && peek(1) == '*') {
            const std::size_t begin = i;
            const int open_line = line;
            const int open_col = col;
            advance();
            advance();
            int depth = 1;
            while (i < n && depth > 0) {
                if (peek() == '/' && peek(1) == '*') {
                    advance();
                    advance();
                    ++depth;
                } else if (peek() == '*' && peek(1) == '/') {
                    advance();
                    advance();
                    --depth;
                } else {
                    advance();
                }
            }
            if (depth > 0) {
                return std::unexpected(SourceError{std::string(file), open_line, open_col, "unterminated block comment; add a closing '*/'", "cfire/SYNTAX"});
            }
            if (comments) comments->push_back({begin, i, true});
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
                    bool in_str = false;
                    bool in_char = false;
                    bool in_line_comment = false;
                    bool in_block_comment = false;
                    while (i < n && depth > 0) {
                        char ch = advance();
                        if (in_line_comment) {
                            if (ch == '\n') in_line_comment = false;
                            cpp_buf.push_back(ch);
                            continue;
                        }
                        if (in_block_comment) {
                            if (ch == '*' && i < n && peek() == '/') {
                                cpp_buf.push_back(ch);
                                cpp_buf.push_back(advance());
                                in_block_comment = false;
                                continue;
                            }
                            cpp_buf.push_back(ch);
                            continue;
                        }
                        if (in_str) {
                            cpp_buf.push_back(ch);
                            if (ch == '\\' && i < n) {
                                cpp_buf.push_back(advance());
                            } else if (ch == '"') {
                                in_str = false;
                            }
                            continue;
                        }
                        if (in_char) {
                            cpp_buf.push_back(ch);
                            if (ch == '\\' && i < n) {
                                cpp_buf.push_back(advance());
                            } else if (ch == '\'') {
                                in_char = false;
                            }
                            continue;
                        }
                        if (ch == '/' && i < n && peek() == '/') {
                            in_line_comment = true;
                            cpp_buf.push_back(ch);
                            continue;
                        }
                        if (ch == '/' && i < n && peek() == '*') {
                            in_block_comment = true;
                            cpp_buf.push_back(ch);
                            cpp_buf.push_back(advance());
                            continue;
                        }
                        if (ch == '"') {
                            in_str = true;
                            cpp_buf.push_back(ch);
                            continue;
                        }
                        if (ch == '\'') {
                            in_char = true;
                            cpp_buf.push_back(ch);
                            continue;
                        }
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
            if (auto kw = find_keyword(word)) {
                tokens.push_back(Token{*kw, std::move(word), start_line, start_col});
            } else {
                tokens.push_back(Token{TokenKind::Ident, std::move(word), start_line, start_col});
            }
            continue;
        }

        // Numbers (stop before '..' range operator!)
        if (std::isdigit(static_cast<unsigned char>(c))) {
            std::string num;
            bool is_float = false;
            if (c == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
                num.push_back(advance()); // '0'
                num.push_back(advance()); // 'x'
                while (i < n && (std::isxdigit(static_cast<unsigned char>(peek())) || peek() == '_')) {
                    char ch = advance();
                    if (ch == '_') continue;
                    num.push_back(ch);
                }
                tokens.push_back(Token{TokenKind::Int, std::move(num), start_line, start_col});
                continue;
            }
            if (c == '0' && (peek(1) == 'b' || peek(1) == 'B')) {
                num.push_back(advance()); // '0'
                num.push_back(advance()); // 'b'
                while (i < n && (peek() == '0' || peek() == '1' || peek() == '_')) {
                    char ch = advance();
                    if (ch == '_') continue;
                    num.push_back(ch);
                }
                tokens.push_back(Token{TokenKind::Int, std::move(num), start_line, start_col});
                continue;
            }
            if (c == '0' && (peek(1) == 'o' || peek(1) == 'O')) {
                num.push_back(advance()); // '0'
                num.push_back(advance()); // 'o'
                while (i < n && ((peek() >= '0' && peek() <= '7') || peek() == '_')) {
                    char ch = advance();
                    if (ch == '_') continue;
                    num.push_back(ch);
                }
                tokens.push_back(Token{TokenKind::Int, std::move(num), start_line, start_col});
                continue;
            }
            while (i < n && (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '_' ||
                             (peek() == '.' && peek(1) != '.'))) {
                char ch = advance();
                if (ch == '_') continue;
                if (ch == '.') is_float = true;
                num.push_back(ch);
            }
            if (i < n && (peek() == 'e' || peek() == 'E')) {
                char next1 = peek(1);
                char next2 = peek(2);
                bool has_exp = std::isdigit(static_cast<unsigned char>(next1)) ||
                    ((next1 == '+' || next1 == '-') && std::isdigit(static_cast<unsigned char>(next2)));
                if (has_exp) {
                    is_float = true;
                    num.push_back(advance()); // 'e' or 'E'
                    if (peek() == '+' || peek() == '-') {
                        num.push_back(advance());
                    }
                    while (i < n && (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '_')) {
                        char ch = advance();
                        if (ch == '_') continue;
                        num.push_back(ch);
                    }
                }
            }
            tokens.push_back(Token{is_float ? TokenKind::Float : TokenKind::Int, std::move(num), start_line, start_col});
            continue;
        }

        // Double-quoted String (supports nested quotes inside `{...}` interpolations, CF-034)
        if (c == '"') {
            advance();
            std::string s;
            int interp_depth = 0;
            bool in_inner_quote = false;
            while (i < n) {
                char cur_ch = peek();
                if (cur_ch == '\\') {
                    s.push_back(advance());
                    if (i < n) s.push_back(advance());
                    continue;
                }
                if (in_inner_quote) {
                    if (cur_ch == '"') {
                        in_inner_quote = false;
                    }
                    s.push_back(advance());
                    continue;
                }
                if (cur_ch == '"') {
                    if (interp_depth > 0) {
                        // Only treat '"' as a nested string quote inside `{ ... }` if a matching '"'
                        // and closing '}' appear before the end of the line.
                        bool has_closing_quote = false;
                        bool has_closing_brace = false;
                        for (std::size_t k = i + 1; k < n && src[k] != '\n'; ++k) {
                            if (src[k] == '\\' && k + 1 < n) {
                                ++k;
                                continue;
                            }
                            if (!has_closing_quote) {
                                if (src[k] == '"') has_closing_quote = true;
                            } else if (src[k] == '}') {
                                has_closing_brace = true;
                                break;
                            }
                        }
                        if (has_closing_quote && has_closing_brace) {
                            in_inner_quote = true;
                            s.push_back(advance());
                            continue;
                        }
                    }
                    break;
                }
                if (cur_ch == '{') {
                    if (peek(1) == '{') {
                        s.push_back(advance());
                        s.push_back(advance());
                        continue;
                    }
                    char next_c = peek(1);
                    if (interp_depth > 0 || std::isalnum(static_cast<unsigned char>(next_c)) ||
                        next_c == '_' || next_c == '(' || next_c == '*' || next_c == '&' || next_c == '!') {
                        ++interp_depth;
                    }
                    s.push_back(advance());
                    continue;
                }
                if (cur_ch == '}' && interp_depth > 0) {
                    --interp_depth;
                    s.push_back(advance());
                    continue;
                }
                if (cur_ch == '\n') {
                    interp_depth = 0;
                    in_inner_quote = false;
                }
                s.push_back(advance());
            }
            if (i >= n) {
                return std::unexpected(SourceError{std::string(file), static_cast<int>(start_line), static_cast<int>(start_col), std::format("unterminated string literal")});
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
                return std::unexpected(SourceError{std::string(file), static_cast<int>(start_line), static_cast<int>(start_col), std::format("unterminated raw string literal")});
            }
            advance();
            tokens.push_back(Token{TokenKind::RawString, std::move(s), start_line, start_col});
            continue;
        }

        // Three-character operators
        char c2 = peek(1);
        char c3 = peek(2);
        if (c == '<' && c2 == '<' && c3 == '=') {
            advance(); advance(); advance();
            tokens.push_back(Token{TokenKind::ShlEq, "<<=", start_line, start_col});
            continue;
        }
        if (c == '>' && c2 == '>' && c3 == '=') {
            advance(); advance(); advance();
            tokens.push_back(Token{TokenKind::ShrEq, ">>=", start_line, start_col});
            continue;
        }
        if (c == '.' && c2 == '.' && c3 == '=') {
            advance(); advance(); advance();
            tokens.push_back(Token{TokenKind::DotDotEq, "..=", start_line, start_col});
            continue;
        }

        // Two-character operators
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
        if (c == '<' && c2 == '<') {
            advance(); advance();
            tokens.push_back(Token{TokenKind::Shl, "<<", start_line, start_col});
            continue;
        }
        if (c == '>' && c2 == '>') {
            advance(); advance();
            tokens.push_back(Token{TokenKind::Shr, ">>", start_line, start_col});
            continue;
        }
        if (c == '*' && c2 == '=') {
            advance(); advance();
            tokens.push_back(Token{TokenKind::StarEq, "*=", start_line, start_col});
            continue;
        }
        if (c == '/' && c2 == '=') {
            advance(); advance();
            tokens.push_back(Token{TokenKind::SlashEq, "/=", start_line, start_col});
            continue;
        }
        if (c == '%' && c2 == '=') {
            advance(); advance();
            tokens.push_back(Token{TokenKind::PercentEq, "%=", start_line, start_col});
            continue;
        }
        if (c == '&' && c2 == '=') {
            advance(); advance();
            tokens.push_back(Token{TokenKind::AmpEq, "&=", start_line, start_col});
            continue;
        }
        if (c == '|' && c2 == '=') {
            advance(); advance();
            tokens.push_back(Token{TokenKind::PipeEq, "|=", start_line, start_col});
            continue;
        }
        if (c == '^' && c2 == '=') {
            advance(); advance();
            tokens.push_back(Token{TokenKind::CaretEq, "^=", start_line, start_col});
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
            case '^': tokens.push_back(Token{TokenKind::Caret, "^", start_line, start_col}); break;
            case '~': tokens.push_back(Token{TokenKind::Tilde, "~", start_line, start_col}); break;
            case '!': tokens.push_back(Token{TokenKind::Bang, "!", start_line, start_col}); break;
            case '?': tokens.push_back(Token{TokenKind::Question, "?", start_line, start_col}); break;
            case '@': tokens.push_back(Token{TokenKind::At, "@", start_line, start_col}); break;
            case '<': tokens.push_back(Token{TokenKind::Lt, "<", start_line, start_col}); break;
            case '>': tokens.push_back(Token{TokenKind::Gt, ">", start_line, start_col}); break;
            case ';': tokens.push_back(Token{TokenKind::Semicolon, ";", start_line, start_col}); break;
            default:
                return std::unexpected(SourceError{std::string(file), static_cast<int>(start_line), static_cast<int>(start_col), std::format("unexpected character '{}'", c)});
        }
    }

    tokens.push_back(Token{TokenKind::Eof, "", line, col});
    return tokens;
}

} // namespace cfire::frontend
