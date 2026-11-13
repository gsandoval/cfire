#pragma once
// src/formatter_printer.hpp — `cfire fmt` layout engine (Q34, CF-156).
//
// The printer re-emits the exact source text of every token and comment and decides only the whitespace between
// them: indentation from bracket depth, one canonical spacing rule per pair of token kinds, at most one blank
// line, and `;`-joined statements split onto their own lines. Because nothing but whitespace (and statement
// `;` separators) can change, formatting can never change what the program means or the C++ it lowers to.

#include "lexer.hpp"
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace cfire::fmt::printer {

using frontend::TokenKind;

struct Item {
    std::size_t begin = 0;
    std::size_t end = 0;
    enum class Kind : std::uint8_t { Token, LineComment, BlockComment } kind = Kind::Token;
    TokenKind tk = TokenKind::Eof;
    std::size_t newlines_before = 0;  // newlines in the original source between the previous item and this one
};

[[nodiscard]] inline auto is_keyword(TokenKind k) -> bool {
    return k >= TokenKind::KwPackage && k <= TokenKind::KwNone;
}

// A token that ends an operand, so a following `-`, `*`, `&` or `!` is binary (or a type suffix).
[[nodiscard]] inline auto ends_operand(TokenKind k) -> bool {
    switch (k) {
        case TokenKind::Ident: case TokenKind::Int: case TokenKind::Float: case TokenKind::String:
        case TokenKind::RawString: case TokenKind::Char: case TokenKind::RParen: case TokenKind::RBracket: case TokenKind::RBrace:
        case TokenKind::KwTrue: case TokenKind::KwFalse: case TokenKind::KwNone: case TokenKind::Question:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] inline auto is_binary_operator(TokenKind k) -> bool {
    switch (k) {
        case TokenKind::ColonEq: case TokenKind::Assign: case TokenKind::PlusEq: case TokenKind::MinusEq:
        case TokenKind::StarEq: case TokenKind::SlashEq: case TokenKind::PercentEq: case TokenKind::AmpEq:
        case TokenKind::PipeEq: case TokenKind::CaretEq: case TokenKind::ShlEq: case TokenKind::ShrEq:
        case TokenKind::Plus: case TokenKind::Minus: case TokenKind::Star: case TokenKind::Slash:
        case TokenKind::Percent: case TokenKind::Amp: case TokenKind::DoubleAmp: case TokenKind::DoublePipe:
        case TokenKind::Caret: case TokenKind::Shl: case TokenKind::Shr: case TokenKind::Eq: case TokenKind::Neq:
        case TokenKind::Lt: case TokenKind::Gt: case TokenKind::Le: case TokenKind::Ge: case TokenKind::Arrow:
        case TokenKind::FatArrow: case TokenKind::KwOr:
            return true;
        default:
            return false;
    }
}

// Formats `src` (already known to parse). Returns the formatted text.
[[nodiscard]] inline auto format(std::string_view src, const std::vector<frontend::Token>& toks,
                                 const std::vector<frontend::CommentSpan>& comments) -> std::string {
    // 1. Byte offsets of every token start, from 1-based line/col.
    std::vector<std::size_t> line_start{0};
    for (std::size_t i = 0; i < src.size(); ++i) {
        if (src[i] == '\n') line_start.push_back(i + 1);
    }
    auto offset_of = [&](const frontend::Token& t) -> std::size_t {
        std::size_t ln = static_cast<std::size_t>(std::max(1, t.line)) - 1;
        if (ln >= line_start.size()) return src.size();
        return std::min(src.size(), line_start[ln] + static_cast<std::size_t>(std::max(1, t.col)) - 1);
    };

    std::vector<Item> items;
    items.reserve(toks.size() + comments.size());
    for (const auto& t : toks) {
        if (t.kind == TokenKind::Eof) continue;
        Item it;
        it.begin = offset_of(t);
        it.tk = t.kind;
        items.push_back(it);
    }
    for (const auto& c : comments) {
        Item it;
        it.begin = c.begin;
        it.end = c.end;
        it.kind = c.block ? Item::Kind::BlockComment : Item::Kind::LineComment;
        items.push_back(it);
    }
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.begin < b.begin; });

    // 2. Token ends: everything up to the next item, minus trailing whitespace (comments already have ends).
    auto is_ws = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (items[i].kind != Item::Kind::Token) continue;
        std::size_t e = (i + 1 < items.size()) ? items[i + 1].begin : src.size();
        while (e > items[i].begin && is_ws(src[e - 1])) --e;
        items[i].end = e;
    }
    for (std::size_t i = 0; i < items.size(); ++i) {
        std::size_t from = i == 0 ? 0 : items[i - 1].end;
        items[i].newlines_before = static_cast<std::size_t>(std::count(src.begin() + static_cast<std::ptrdiff_t>(std::min(from, items[i].begin)),
                                                                        src.begin() + static_cast<std::ptrdiff_t>(items[i].begin), '\n'));
    }
    auto text = [&](const Item& it) { return src.substr(it.begin, it.end - it.begin); };
    auto tk = [&](std::size_t i) { return i < items.size() && items[i].kind == Item::Kind::Token ? items[i].tk : TokenKind::Eof; };

    // 3. Bracket pairing, struct-literal braces, type brackets (`[]T`, `[4]u8`, `map[K]V`), and the statement `;`
    //    separators that get their own line. Struct literals follow a type name outside a control header.
    std::vector<std::size_t> partner(items.size(), items.size());
    std::vector<bool> struct_lit(items.size(), false);
    std::vector<bool> type_bracket(items.size(), false);
    std::vector<bool> split_semicolon(items.size(), false);
    std::vector<bool> expand_brace(items.size(), false);
    {
        struct Open { std::size_t idx; bool header_brace; };
        std::vector<Open> stack;
        int header_depth = -1;  // bracket depth at which an `if`/`for`/`match`/`fn`/... header is open, or -1
        std::size_t prev_tok = items.size();
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (items[i].kind != Item::Kind::Token) continue;
            const TokenKind k = items[i].tk;
            const TokenKind p = prev_tok < items.size() ? items[prev_tok].tk : TokenKind::Eof;
            const int depth = static_cast<int>(stack.size());
            // Tokens after which the next `{` at this depth opens a block, never a struct literal: control and
            // declaration headers, a return type (`-> T {`), and an `or err {` handler.
            if (header_depth < 0 && (k == TokenKind::KwIf || k == TokenKind::KwFor || k == TokenKind::KwMatch || k == TokenKind::KwFn ||
                                     k == TokenKind::KwType || k == TokenKind::KwTest || k == TokenKind::KwElse ||
                                     k == TokenKind::KwDefer || k == TokenKind::KwStruct || k == TokenKind::KwEnum ||
                                     k == TokenKind::KwInterface || k == TokenKind::Arrow || k == TokenKind::KwOr)) {
                header_depth = depth;
            }
            if (k == TokenKind::LParen || k == TokenKind::LBracket || k == TokenKind::LBrace) {
                bool lit = false;
                if (k == TokenKind::LBrace) {
                    // `P{...}` / `Box[i32]{...}` struct literals and `{...}` map/array literals in value position.
                    lit = header_depth != depth && (p == TokenKind::Ident || p == TokenKind::RBracket || p == TokenKind::Colon ||
                                                    p == TokenKind::Assign || p == TokenKind::ColonEq || p == TokenKind::Comma ||
                                                    p == TokenKind::LParen || p == TokenKind::LBracket || p == TokenKind::KwReturn);
                    if (header_depth == depth) header_depth = -1;  // the header ends at its block
                    struct_lit[i] = lit;
                } else if (k == TokenKind::LBracket) {
                    type_bracket[i] = p == TokenKind::KwMap || !ends_operand(p);
                }
                stack.push_back({i, false});
            } else if (k == TokenKind::RParen || k == TokenKind::RBracket || k == TokenKind::RBrace) {
                if (!stack.empty()) {
                    std::size_t o = stack.back().idx;
                    stack.pop_back();
                    partner[o] = i;
                    partner[i] = o;
                    struct_lit[i] = struct_lit[o];
                    if (k == TokenKind::RBracket && type_bracket[o]) {
                        // `[]` and `[N]` / `map[K]` prefix a type; `[1, 2]` is an array literal.
                        bool simple = i == o + 1 || (i == o + 2 && tk(o + 1) == TokenKind::Int) || (o > 0 && tk(o - 1) == TokenKind::KwMap);
                        type_bracket[o] = type_bracket[i] = simple;
                    }
                }
                if (header_depth > static_cast<int>(stack.size())) header_depth = -1;
            } else if (k == TokenKind::Semicolon && header_depth < 0) {
                split_semicolon[i] = true;
                if (!stack.empty() && items[stack.back().idx].tk == TokenKind::LBrace && !struct_lit[stack.back().idx]) {
                    expand_brace[stack.back().idx] = true;
                }
            }
            prev_tok = i;
        }
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (expand_brace[i] && partner[i] < items.size()) expand_brace[partner[i]] = true;
        }
    }
    // Closure parameter pipes: `|a, mut b: T|` followed by a body `{` or a return type `->`.
    std::vector<int> closure_pipe(items.size(), 0);  // 1 = opener, 2 = closer
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (tk(i) != TokenKind::Pipe || closure_pipe[i] != 0) continue;
        std::size_t j = i + 1;
        bool params_only = true;
        for (; j < items.size() && tk(j) != TokenKind::Pipe; ++j) {
            const TokenKind k = tk(j);
            if (!(k == TokenKind::Ident || k == TokenKind::Colon || k == TokenKind::Comma || k == TokenKind::KwMut || k == TokenKind::KwMove ||
                  k == TokenKind::LBracket || k == TokenKind::RBracket || k == TokenKind::Star || k == TokenKind::Amp ||
                  k == TokenKind::Question || k == TokenKind::Bang || k == TokenKind::Dot || k == TokenKind::KwMap || k == TokenKind::Int ||
                  k == TokenKind::KwIn || k == TokenKind::Float || k == TokenKind::String)) {  // also `bench |n in [8, 32]|`
                params_only = false;
                break;
            }
        }
        if (params_only && j + 1 < items.size() && tk(j) == TokenKind::Pipe && (tk(j + 1) == TokenKind::LBrace || tk(j + 1) == TokenKind::Arrow)) {
            closure_pipe[i] = 1;
            closure_pipe[j] = 2;
            i = j;
        }
    }

    // 4. Spacing between two items on the same output line.
    auto space_between = [&](std::size_t a, std::size_t b) -> bool {
        const Item& A = items[a];
        const Item& B = items[b];
        if (B.kind == Item::Kind::LineComment || B.kind == Item::Kind::BlockComment || A.kind != Item::Kind::Token) return true;
        const TokenKind x = A.tk;
        const TokenKind y = B.tk;
        const TokenKind before_x = a > 0 ? tk(a - 1) : TokenKind::Eof;
        // Prefix operators: after a non-operand, or first on their line (`*g += 1` starts a statement).
        const bool x_unary = (x == TokenKind::Minus || x == TokenKind::Star || x == TokenKind::Amp || x == TokenKind::Bang ||
                              x == TokenKind::Tilde) && (!ends_operand(before_x) || A.newlines_before > 0 || a == 0);
        if (x_unary) return false;
        if (x == TokenKind::At) return false;
        // Openers and closers hug their contents.
        if (x == TokenKind::LParen || x == TokenKind::LBracket) return false;
        if (y == TokenKind::RParen || y == TokenKind::RBracket) return false;
        if (x == TokenKind::LBrace) return y != TokenKind::RBrace && !struct_lit[a];
        if (y == TokenKind::RBrace) return x != TokenKind::LBrace && !struct_lit[b];
        if (y == TokenKind::Comma || y == TokenKind::Colon || y == TokenKind::Semicolon) return false;
        if (x == TokenKind::Dot || y == TokenKind::Dot || x == TokenKind::DoubleColon || y == TokenKind::DoubleColon) return false;
        if (x == TokenKind::DotDot || y == TokenKind::DotDot || x == TokenKind::DotDotEq || y == TokenKind::DotDotEq) return false;
        if (y == TokenKind::Question) return false;
        if (y == TokenKind::Bang && ends_operand(x)) return false;  // `T!` fallible suffix
        if (x == TokenKind::RBracket && type_bracket[a]) return false;  // `[]T`, `[4]u8`, `map[K]V`
        if (y == TokenKind::LParen && x == TokenKind::KwFn) {
            // `fn (r: T) name(...)` declares a method; `fn(i32) -> T` inside a type has no space.
            return A.newlines_before > 0 || a == 0;
        }
        if (y == TokenKind::LParen) return !(x == TokenKind::Ident || x == TokenKind::RParen || x == TokenKind::RBracket ||
                                             x == TokenKind::Question || (is_keyword(x) && x != TokenKind::KwFn && x != TokenKind::KwIf &&
                                              x != TokenKind::KwFor && x != TokenKind::KwMatch && x != TokenKind::KwReturn &&
                                              x != TokenKind::KwIn && x != TokenKind::KwOr && x != TokenKind::KwAs && x != TokenKind::KwTry));
        if (y == TokenKind::LBracket) return !(x == TokenKind::Ident || x == TokenKind::RParen || x == TokenKind::RBracket ||
                                               x == TokenKind::KwMap || x == TokenKind::String);
        if (y == TokenKind::LBrace) return !struct_lit[b] || !(x == TokenKind::Ident || x == TokenKind::RBracket);  // `P{`, `xs: {`
        if ((x == TokenKind::Amp || x == TokenKind::Star) && y == TokenKind::KwMut && (!ends_operand(before_x) || A.newlines_before > 0)) return false;
        if (x == TokenKind::Pipe && closure_pipe[a] == 1) return false;  // `|x`
        if (y == TokenKind::Pipe && closure_pipe[b] == 2) return false;  // `x|`
        if (y == TokenKind::Pipe && closure_pipe[b] == 1) return x != TokenKind::LParen && x != TokenKind::LBracket;
        return true;
    };

    // 5. Emit.
    std::string out;
    int depth = 0;
    bool at_line_start = true;
    std::size_t pending_newlines = 0;
    auto newline = [&](std::size_t count) {
        if (out.empty()) return;
        pending_newlines = std::max(pending_newlines, std::min<std::size_t>(count, 2));
    };
    std::size_t prev = items.size();
    for (std::size_t i = 0; i < items.size(); ++i) {
        const Item& it = items[i];
        if (it.kind == Item::Kind::Token && it.tk == TokenKind::Semicolon && split_semicolon[i]) {
            newline(1);
            continue;  // a statement separator becomes a line break
        }
        if (it.newlines_before > 0) newline(it.newlines_before);
        if (prev < items.size() && items[prev].kind == Item::Kind::Token && items[prev].tk == TokenKind::LBrace && expand_brace[prev]) newline(1);
        if (it.kind == Item::Kind::Token && it.tk == TokenKind::RBrace && expand_brace[i]) newline(1);
        if (pending_newlines > 0 && !out.empty()) {
            while (!out.empty() && out.back() == ' ') out.pop_back();
            out.append(pending_newlines, '\n');
            at_line_start = true;
        }
        pending_newlines = 0;
        // Leading closers dedent their own line.
        const bool closer = it.kind == Item::Kind::Token &&
                            (it.tk == TokenKind::RBrace || it.tk == TokenKind::RParen || it.tk == TokenKind::RBracket);
        if (at_line_start) {
            // A line that starts with closers (`})`, `]}`) is dedented once per leading closer.
            int leading = 0;
            for (std::size_t j = i; j < items.size() && items[j].kind == Item::Kind::Token; ++j) {
                if (j > i && items[j].newlines_before > 0) break;
                if (items[j].tk != TokenKind::RBrace && items[j].tk != TokenKind::RParen && items[j].tk != TokenKind::RBracket) break;
                ++leading;
            }
            int line_depth = std::max(0, depth - leading);
            out.append(static_cast<std::size_t>(line_depth) * 4, ' ');
        } else if (prev < items.size() && space_between(prev, i)) {
            out.push_back(' ');
        }
        out.append(text(it));
        at_line_start = false;
        if (it.kind == Item::Kind::Token) {
            if (it.tk == TokenKind::LParen || it.tk == TokenKind::LBracket || it.tk == TokenKind::LBrace) ++depth;
            else if (closer) depth = std::max(0, depth - 1);
        }
        prev = i;
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '\n')) out.pop_back();
    out.push_back('\n');
    return out;
}

} // namespace cfire::fmt::printer
