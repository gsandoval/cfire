#include "cfire_prelude.hpp"
#include <lexer_types.hpp>

[[nodiscard]] inline auto selfhost_tokenize(std::string_view file, std::string_view src) -> std::expected<std::vector<cfire::frontend::Token>, cfire::Error>;

[[nodiscard]] inline auto selfhost_tokenize(std::string_view file, std::string_view src) -> std::expected<std::vector<cfire::frontend::Token>, cfire::Error> {
    std::vector<cfire::frontend::Token> tokens = {};
    std::size_t i = 0;
    auto n = src.size();
    std::int32_t line = 1;
    std::int32_t col = 1;
    while (i < n) {
        auto c = src[i];
        if (((((c == 32) || (c == 9)) || (c == 13)) || (c == 11)) || (c == 12)) {
            i = (i + 1);
            col = (col + 1);
            continue;
        }
        if (c == 10) {
            i = (i + 1);
            line = (line + 1);
            col = 1;
            continue;
        }
        if (((c == 47) && ((i + 1) < n)) && (src[(i + 1)] == 47)) {
            while ((i < n) && (src[i] != 10)) {
                i = (i + 1);
            }
            continue;
        }
        auto start_line = line;
        auto start_col = col;
        if ((((c >= 65) && (c <= 90)) || ((c >= 97) && (c <= 122))) || (c == 95)) {
            auto start_i = i;
            while (i < n) {
                auto ch = src[i];
                if (((((ch >= 65) && (ch <= 90)) || ((ch >= 97) && (ch <= 122))) || ((ch >= 48) && (ch <= 57))) || (ch == 95)) {
                    i = (i + 1);
                    col = (col + 1);
                } else {
                    break;
                }
            }
            auto word = cfire::slice(src, start_i, i);
            if (word == "cpp") {
                auto probe = i;
                while ((probe < n) && ((((src[probe] == 32) || (src[probe] == 9)) || (src[probe] == 10)) || (src[probe] == 13))) {
                    probe = (probe + 1);
                }
                if ((probe < n) && (src[probe] == 123)) {
                    while (i < probe) {
                        if (src[i] == 10) {
                            line = (line + 1);
                            col = 1;
                        } else {
                            col = (col + 1);
                        }
                        i = (i + 1);
                    }
                    i = (i + 1);
                    col = (col + 1);
                    std::int32_t depth = 1;
                    auto start_cpp = i;
                    while ((i < n) && (depth > 0)) {
                        auto ch = src[i];
                        if (ch == 10) {
                            line = (line + 1);
                            col = 1;
                        } else {
                            col = (col + 1);
                        }
                        i = (i + 1);
                        if (ch == 123) {
                            depth = (depth + 1);
                        } else {
                            if (ch == 125) {
                                depth = (depth - 1);
                                if (depth == 0) {
                                    break;
                                }
                            }
                        }
                    }
                    auto cpp_slice = cfire::slice(src, start_cpp, (i - 1));
                    (void)(tokens.push_back(cfire::frontend::make_tok(cfire::frontend::TokenKind::CppBlock, cpp_slice, start_line, start_col)));
                    continue;
                }
                (void)(tokens.push_back(cfire::frontend::make_tok(cfire::frontend::TokenKind::KwCpp, word, start_line, start_col)));
                continue;
            }
            auto kw_opt = cfire::frontend::find_keyword(word);
            if (kw_opt.has_value()) {
                (void)(tokens.push_back(cfire::frontend::make_tok((*kw_opt), word, start_line, start_col)));
            } else {
                (void)(tokens.push_back(cfire::frontend::make_tok(cfire::frontend::TokenKind::Ident, word, start_line, start_col)));
            }
            continue;
        }
        if ((c >= 48) && (c <= 57)) {
            auto is_float = false;
            std::string num_str = "";
            while (i < n) {
                auto ch = src[i];
                if (((ch >= 48) && (ch <= 57)) || (ch == 95)) {
                    if (ch != 95) {
                        num_str = (num_str + cfire::slice(src, i, (i + 1)));
                    }
                    i = (i + 1);
                    col = (col + 1);
                } else {
                    if (ch == 46) {
                        if (((i + 1) < n) && (src[(i + 1)] == 46)) {
                            break;
                        }
                        is_float = true;
                        num_str = (num_str + cfire::slice(src, i, (i + 1)));
                        i = (i + 1);
                        col = (col + 1);
                    } else {
                        break;
                    }
                }
            }
            auto tok_kind = cfire::frontend::TokenKind::Int;
            if (is_float) {
                tok_kind = cfire::frontend::TokenKind::Float;
            }
            (void)(tokens.push_back(cfire::frontend::make_tok(tok_kind, num_str, start_line, start_col)));
            continue;
        }
        if (c == 34) {
            i = (i + 1);
            col = (col + 1);
            std::string s = "";
            auto unterminated = true;
            while (i < n) {
                auto ch = src[i];
                if (ch == 34) {
                    i = (i + 1);
                    col = (col + 1);
                    unterminated = false;
                    break;
                }
                if (ch == 92) {
                    s = (s + "\\");
                    if (ch == 10) {
                        line = (line + 1);
                        col = 1;
                    } else {
                        col = (col + 1);
                    }
                    i = (i + 1);
                    if (i >= n) {
                        break;
                    }
                    ch = src[i];
                }
                s = (s + cfire::slice(src, i, (i + 1)));
                if (ch == 10) {
                    line = (line + 1);
                    col = 1;
                } else {
                    col = (col + 1);
                }
                i = (i + 1);
            }
            if (unterminated) {
                std::string err_msg = std::format("{}:{}:{}: unterminated string literal", file, start_line, start_col);
                return std::unexpected(err_msg);
            }
            (void)(tokens.push_back(cfire::frontend::make_tok(cfire::frontend::TokenKind::String, s, start_line, start_col)));
            continue;
        }
        if (c == 96) {
            i = (i + 1);
            col = (col + 1);
            auto start_raw = i;
            auto unterminated = true;
            while (i < n) {
                auto ch = src[i];
                if (ch == 96) {
                    unterminated = false;
                    break;
                }
                if (ch == 10) {
                    line = (line + 1);
                    col = 1;
                } else {
                    col = (col + 1);
                }
                i = (i + 1);
            }
            if (unterminated) {
                std::string err_msg = std::format("{}:{}:{}: unterminated raw string literal", file, start_line, start_col);
                return std::unexpected(err_msg);
            }
            auto raw_slice = cfire::slice(src, start_raw, i);
            i = (i + 1);
            col = (col + 1);
            (void)(tokens.push_back(cfire::frontend::make_tok(cfire::frontend::TokenKind::RawString, raw_slice, start_line, start_col)));
            continue;
        }
        if ((i + 1) < n) {
            auto c2 = src[(i + 1)];
            if ((c == 46) && (c2 == 46)) {
                i = (i + 2);
                col = (col + 2);
                (void)(tokens.push_back(cfire::frontend::make_tok(cfire::frontend::TokenKind::DotDot, "..", start_line, start_col)));
                continue;
            }
            if ((c == 61) && (c2 == 62)) {
                i = (i + 2);
                col = (col + 2);
                (void)(tokens.push_back(cfire::frontend::make_tok(cfire::frontend::TokenKind::FatArrow, "=>", start_line, start_col)));
                continue;
            }
            if ((c == 58) && (c2 == 61)) {
                i = (i + 2);
                col = (col + 2);
                (void)(tokens.push_back(cfire::frontend::make_tok(cfire::frontend::TokenKind::ColonEq, ":=", start_line, start_col)));
                continue;
            }
            if ((c == 58) && (c2 == 58)) {
                i = (i + 2);
                col = (col + 2);
                (void)(tokens.push_back(cfire::frontend::make_tok(cfire::frontend::TokenKind::DoubleColon, "::", start_line, start_col)));
                continue;
            }
            if ((c == 45) && (c2 == 62)) {
                i = (i + 2);
                col = (col + 2);
                (void)(tokens.push_back(cfire::frontend::make_tok(cfire::frontend::TokenKind::Arrow, "->", start_line, start_col)));
                continue;
            }
            if ((c == 43) && (c2 == 61)) {
                i = (i + 2);
                col = (col + 2);
                (void)(tokens.push_back(cfire::frontend::make_tok(cfire::frontend::TokenKind::PlusEq, "+=", start_line, start_col)));
                continue;
            }
            if ((c == 45) && (c2 == 61)) {
                i = (i + 2);
                col = (col + 2);
                (void)(tokens.push_back(cfire::frontend::make_tok(cfire::frontend::TokenKind::MinusEq, "-=", start_line, start_col)));
                continue;
            }
            if ((c == 61) && (c2 == 61)) {
                i = (i + 2);
                col = (col + 2);
                (void)(tokens.push_back(cfire::frontend::make_tok(cfire::frontend::TokenKind::Eq, "==", start_line, start_col)));
                continue;
            }
            if ((c == 33) && (c2 == 61)) {
                i = (i + 2);
                col = (col + 2);
                (void)(tokens.push_back(cfire::frontend::make_tok(cfire::frontend::TokenKind::Neq, "!=", start_line, start_col)));
                continue;
            }
            if ((c == 60) && (c2 == 61)) {
                i = (i + 2);
                col = (col + 2);
                (void)(tokens.push_back(cfire::frontend::make_tok(cfire::frontend::TokenKind::Le, "<=", start_line, start_col)));
                continue;
            }
            if ((c == 62) && (c2 == 61)) {
                i = (i + 2);
                col = (col + 2);
                (void)(tokens.push_back(cfire::frontend::make_tok(cfire::frontend::TokenKind::Ge, ">=", start_line, start_col)));
                continue;
            }
            if ((c == 38) && (c2 == 38)) {
                i = (i + 2);
                col = (col + 2);
                (void)(tokens.push_back(cfire::frontend::make_tok(cfire::frontend::TokenKind::DoubleAmp, "&&", start_line, start_col)));
                continue;
            }
            if ((c == 124) && (c2 == 124)) {
                i = (i + 2);
                col = (col + 2);
                (void)(tokens.push_back(cfire::frontend::make_tok(cfire::frontend::TokenKind::DoublePipe, "||", start_line, start_col)));
                continue;
            }
        }
        i = (i + 1);
        col = (col + 1);
        if (c == 40) {
            (void)(tokens.push_back(cfire::frontend::make_tok(cfire::frontend::TokenKind::LParen, "(", start_line, start_col)));
            continue;
        }
        if (c == 41) {
            (void)(tokens.push_back(cfire::frontend::make_tok(cfire::frontend::TokenKind::RParen, ")", start_line, start_col)));
            continue;
        }
        if (c == 123) {
            (void)(tokens.push_back(cfire::frontend::make_tok(cfire::frontend::TokenKind::LBrace, "{", start_line, start_col)));
            continue;
        }
        if (c == 125) {
            (void)(tokens.push_back(cfire::frontend::make_tok(cfire::frontend::TokenKind::RBrace, "}", start_line, start_col)));
            continue;
        }
        if (c == 91) {
            (void)(tokens.push_back(cfire::frontend::make_tok(cfire::frontend::TokenKind::LBracket, "[", start_line, start_col)));
            continue;
        }
        if (c == 93) {
            (void)(tokens.push_back(cfire::frontend::make_tok(cfire::frontend::TokenKind::RBracket, "]", start_line, start_col)));
            continue;
        }
        if (c == 58) {
            (void)(tokens.push_back(cfire::frontend::make_tok(cfire::frontend::TokenKind::Colon, ":", start_line, start_col)));
            continue;
        }
        if (c == 44) {
            (void)(tokens.push_back(cfire::frontend::make_tok(cfire::frontend::TokenKind::Comma, ",", start_line, start_col)));
            continue;
        }
        if (c == 46) {
            (void)(tokens.push_back(cfire::frontend::make_tok(cfire::frontend::TokenKind::Dot, ".", start_line, start_col)));
            continue;
        }
        if (c == 61) {
            (void)(tokens.push_back(cfire::frontend::make_tok(cfire::frontend::TokenKind::Assign, "=", start_line, start_col)));
            continue;
        }
        if (c == 43) {
            (void)(tokens.push_back(cfire::frontend::make_tok(cfire::frontend::TokenKind::Plus, "+", start_line, start_col)));
            continue;
        }
        if (c == 45) {
            (void)(tokens.push_back(cfire::frontend::make_tok(cfire::frontend::TokenKind::Minus, "-", start_line, start_col)));
            continue;
        }
        if (c == 42) {
            (void)(tokens.push_back(cfire::frontend::make_tok(cfire::frontend::TokenKind::Star, "*", start_line, start_col)));
            continue;
        }
        if (c == 47) {
            (void)(tokens.push_back(cfire::frontend::make_tok(cfire::frontend::TokenKind::Slash, "/", start_line, start_col)));
            continue;
        }
        if (c == 37) {
            (void)(tokens.push_back(cfire::frontend::make_tok(cfire::frontend::TokenKind::Percent, "%", start_line, start_col)));
            continue;
        }
        if (c == 38) {
            (void)(tokens.push_back(cfire::frontend::make_tok(cfire::frontend::TokenKind::Amp, "&", start_line, start_col)));
            continue;
        }
        if (c == 124) {
            (void)(tokens.push_back(cfire::frontend::make_tok(cfire::frontend::TokenKind::Pipe, "|", start_line, start_col)));
            continue;
        }
        if (c == 33) {
            (void)(tokens.push_back(cfire::frontend::make_tok(cfire::frontend::TokenKind::Bang, "!", start_line, start_col)));
            continue;
        }
        if (c == 63) {
            (void)(tokens.push_back(cfire::frontend::make_tok(cfire::frontend::TokenKind::Question, "?", start_line, start_col)));
            continue;
        }
        if (c == 64) {
            (void)(tokens.push_back(cfire::frontend::make_tok(cfire::frontend::TokenKind::At, "@", start_line, start_col)));
            continue;
        }
        if (c == 60) {
            (void)(tokens.push_back(cfire::frontend::make_tok(cfire::frontend::TokenKind::Lt, "<", start_line, start_col)));
            continue;
        }
        if (c == 62) {
            (void)(tokens.push_back(cfire::frontend::make_tok(cfire::frontend::TokenKind::Gt, ">", start_line, start_col)));
            continue;
        }
        if (c == 59) {
            (void)(tokens.push_back(cfire::frontend::make_tok(cfire::frontend::TokenKind::Semicolon, ";", start_line, start_col)));
            continue;
        }
        auto bad_char = cfire::slice(src, (i - 1), i);
        std::string err_msg = std::format("{}:{}:{}: unexpected character '{}'", file, start_line, start_col, bad_char);
        return std::unexpected(err_msg);
    }
    (void)(tokens.push_back(cfire::frontend::make_tok(cfire::frontend::TokenKind::Eof, "", line, col)));
    return tokens;
}
