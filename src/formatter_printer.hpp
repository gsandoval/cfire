#pragma once
// src/formatter_printer.hpp — `cfire fmt` layout engine.
//
// The printer re-emits the exact source text of every token and comment and decides only the whitespace between
// them: indentation from bracket depth, one canonical spacing rule per pair of token kinds, at most one blank
// line, and `;`-joined statements split onto their own lines. Because nothing but whitespace (and statement
// `;` separators) can change, formatting can never change what the program means or the C++ it lowers to.

#include "lexer.hpp"
#include <algorithm>
#include <cctype>
#include <cstdint>
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

    // 2. Token ends: everything up to the next item, minus trailing whitespace; also trim trailing whitespace
    //    (e.g. `\r`, `\v`, `\f`, spaces, tabs) from line comments so `LineComment` spans match lexer whitespace.
    auto is_ws = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (items[i].kind == Item::Kind::Token) {
            std::size_t e = (i + 1 < items.size()) ? items[i + 1].begin : src.size();
            while (e > items[i].begin && is_ws(src[e - 1])) --e;
            items[i].end = e;
        } else if (items[i].kind == Item::Kind::LineComment) {
            std::size_t e = items[i].end;
            while (e > items[i].begin && is_ws(src[e - 1])) --e;
            items[i].end = e;
        }
    }
    for (std::size_t i = 0; i < items.size(); ++i) {
        std::size_t from = i == 0 ? 0 : items[i - 1].end;
        items[i].newlines_before = static_cast<std::size_t>(std::count(src.begin() + static_cast<std::ptrdiff_t>(std::min(from, items[i].begin)),
                                                                        src.begin() + static_cast<std::ptrdiff_t>(items[i].begin), '\n'));
    }
    auto text = [&](const Item& it) { return src.substr(it.begin, it.end - it.begin); };
    auto tk = [&](std::size_t i) { return i < items.size() && items[i].kind == Item::Kind::Token ? items[i].tk : TokenKind::Eof; };

    // 3. Two-stage normalization & structure classification:
    //    Stage A normalizes statement-level `;` separators and expands single-line `{ ... }` blocks with `;`,
    //    folding all forced newlines into `items` and `newlines_before`.
    //    Stage B recomputes bracket pairing, `struct_lit`, `type_bracket`, and `closure_pipe` purely from the
    //    normalized `items` so Pass 1 and Pass 2 see identical structure and spacing decisions.
    struct BracketInfo {
        std::vector<std::size_t> next_tok;
        std::vector<std::size_t> partner;
        std::vector<bool> struct_lit;
        std::vector<bool> type_bracket;
        std::vector<bool> in_header;
    };
    auto classify = [&]() -> BracketInfo {
        const std::size_t n = items.size();
        BracketInfo info;
        info.next_tok.assign(n, n);
        info.partner.assign(n, n);
        info.struct_lit.assign(n, false);
        info.type_bracket.assign(n, false);
        info.in_header.assign(n, false);

        std::size_t nt = n;
        for (std::size_t i = n; i-- > 0;) {
            info.next_tok[i] = nt;
            if (items[i].kind == Item::Kind::Token) nt = i;
        }

        auto is_return_type_tok = [](TokenKind k) -> bool {
            switch (k) {
                case TokenKind::Amp: case TokenKind::KwMut: case TokenKind::Star: case TokenKind::KwMap:
                case TokenKind::KwFn: case TokenKind::Ident: case TokenKind::KwCpp: case TokenKind::Dot:
                case TokenKind::DoubleColon: case TokenKind::Question: case TokenKind::Bang:
                case TokenKind::KwAsync: case TokenKind::LParen: case TokenKind::LBracket:
                    return true;
                default:
                    return false;
            }
        };

        struct Open { std::size_t idx; bool prev_was_map; };
        std::vector<Open> stack;
        std::vector<int> header_depths;
        int arrow_depth = -1;
        std::size_t prev_tok = n;
        std::size_t prev_prev_tok = n;

        for (std::size_t i = 0; i < n; ++i) {
            if (items[i].kind != Item::Kind::Token) continue;
            const TokenKind k = items[i].tk;
            const TokenKind p = prev_tok < n ? items[prev_tok].tk : TokenKind::Eof;
            const int depth = static_cast<int>(stack.size());

            if (arrow_depth == depth && k != TokenKind::LBrace) {
                const bool new_stmt_line = items[i].newlines_before > 0 &&
                    p != TokenKind::Arrow && p != TokenKind::Dot && p != TokenKind::DoubleColon &&
                    p != TokenKind::Amp && p != TokenKind::Star && p != TokenKind::KwMut && p != TokenKind::KwMap;
                if (new_stmt_line || !is_return_type_tok(k)) {
                    arrow_depth = -1;
                }
            }

            if ((k == TokenKind::KwIf || k == TokenKind::KwFor || k == TokenKind::KwMatch) &&
                !(items[i].newlines_before == 0 && (p == TokenKind::Dot || p == TokenKind::DoubleColon)) &&
                (info.next_tok[i] >= n || items[info.next_tok[i]].tk != TokenKind::Colon)) {
                if (header_depths.empty() || header_depths.back() != depth) {
                    header_depths.push_back(depth);
                }
            } else if (k == TokenKind::Arrow) {
                arrow_depth = depth;
            } else if (k == TokenKind::FatArrow) {
                // Match guard `if cond => expr` terminates its header at `=>`.
                if (!header_depths.empty() && header_depths.back() == depth) {
                    header_depths.pop_back();
                }
            }

            const bool in_hdr = (!header_depths.empty() && header_depths.back() == depth) || arrow_depth == depth;
            info.in_header[i] = in_hdr;

            if (k == TokenKind::LParen || k == TokenKind::LBracket || k == TokenKind::LBrace) {
                if (k == TokenKind::LBrace) {
                    bool is_or_err_block = false;
                    if (p == TokenKind::Ident && prev_prev_tok < n && items[prev_prev_tok].tk == TokenKind::KwOr) {
                        const std::size_t n1 = info.next_tok[i];
                        const std::size_t n2 = n1 < n ? info.next_tok[n1] : n;
                        const bool is_fallback_lit = n1 < n &&
                            (items[n1].tk == TokenKind::RBrace ||
                             ((items[n1].tk == TokenKind::Ident || items[n1].tk == TokenKind::String) &&
                              n2 < n && items[n2].tk == TokenKind::Colon));
                        is_or_err_block = !is_fallback_lit;
                    }
                    bool lit = false;
                    if (!in_hdr && !is_or_err_block) {
                        const bool same_line_type_lit = items[i].newlines_before == 0 &&
                            ((p == TokenKind::Ident && !text(items[prev_tok]).empty() &&
                              text(items[prev_tok])[0] >= 'A' && text(items[prev_tok])[0] <= 'Z') ||
                             p == TokenKind::RBracket);
                        const bool value_pos_lit = (p == TokenKind::Colon || p == TokenKind::Assign || p == TokenKind::ColonEq ||
                                                    p == TokenKind::Comma || p == TokenKind::LParen || p == TokenKind::LBracket ||
                                                    p == TokenKind::KwReturn);
                        lit = same_line_type_lit || value_pos_lit;
                    }
                    if (!header_depths.empty() && header_depths.back() == depth) header_depths.pop_back();
                    if (arrow_depth == depth) arrow_depth = -1;
                    info.struct_lit[i] = lit;
                } else if (k == TokenKind::LBracket) {
                    const bool postfix_operand = items[i].newlines_before == 0 && ends_operand(p);
                    info.type_bracket[i] = p == TokenKind::KwMap || !postfix_operand ||
                                           (p == TokenKind::RBracket && info.type_bracket[prev_tok]);
                }
                stack.push_back({i, p == TokenKind::KwMap});
            } else if (k == TokenKind::RParen || k == TokenKind::RBracket || k == TokenKind::RBrace) {
                if (!stack.empty()) {
                    const Open o = stack.back();
                    stack.pop_back();
                    info.partner[o.idx] = i;
                    info.partner[i] = o.idx;
                    info.struct_lit[i] = info.struct_lit[o.idx];
                    if (k == TokenKind::RBracket && info.type_bracket[o.idx]) {
                        // `[]` and `[N]` / `map[K]` prefix a type; `[1, 2]` is an array literal.
                        const bool simple = i == o.idx + 1 || (i == o.idx + 2 && tk(o.idx + 1) == TokenKind::Int) || o.prev_was_map;
                        info.type_bracket[o.idx] = info.type_bracket[i] = simple;
                    }
                }
                const int post_depth = static_cast<int>(stack.size());
                while (!header_depths.empty() && header_depths.back() > post_depth) header_depths.pop_back();
                if (arrow_depth > post_depth) arrow_depth = -1;
            }

            prev_prev_tok = prev_tok;
            prev_tok = i;
        }
        return info;
    };

    auto can_continue_expr = [](TokenKind k) -> bool {
        switch (k) {
            case TokenKind::LParen: case TokenKind::LBracket: case TokenKind::LBrace:
            case TokenKind::Pipe: case TokenKind::DoublePipe:
            case TokenKind::Dot: case TokenKind::DoubleColon: case TokenKind::DotDot: case TokenKind::DotDotEq:
            case TokenKind::Question: case TokenKind::Bang:
            case TokenKind::Plus: case TokenKind::Minus: case TokenKind::Star: case TokenKind::Slash: case TokenKind::Percent:
            case TokenKind::Amp: case TokenKind::DoubleAmp: case TokenKind::Caret: case TokenKind::Tilde:
            case TokenKind::Shl: case TokenKind::Shr:
            case TokenKind::Eq: case TokenKind::Neq: case TokenKind::Lt: case TokenKind::Gt: case TokenKind::Le: case TokenKind::Ge:
            case TokenKind::KwOr: case TokenKind::KwAs:
            case TokenKind::Assign: case TokenKind::ColonEq:
            case TokenKind::PlusEq: case TokenKind::MinusEq: case TokenKind::StarEq: case TokenKind::SlashEq:
            case TokenKind::PercentEq: case TokenKind::AmpEq: case TokenKind::PipeEq: case TokenKind::CaretEq:
            case TokenKind::ShlEq: case TokenKind::ShrEq:
            case TokenKind::Colon: case TokenKind::Comma: case TokenKind::Arrow: case TokenKind::FatArrow:
                return true;
            default:
                return false;
        }
    };

    // Stage A: Normalize statement-level `;` and expand `{ ... }` blocks containing `;`.
    {
        const BracketInfo pre = classify();
        const std::size_t n = items.size();
        std::vector<bool> expand_brace(n, false);
        std::vector<bool> drop_semi(n, false);
        std::vector<bool> split_after_semi(n, false);
        std::vector<std::size_t> brace_stack;
        std::size_t surviving_prev_tok = n;

        for (std::size_t i = 0; i < n; ++i) {
            if (items[i].kind != Item::Kind::Token) continue;
            const TokenKind k = items[i].tk;
            if (k == TokenKind::LParen || k == TokenKind::LBracket || k == TokenKind::LBrace) {
                brace_stack.push_back(i);
            } else if (k == TokenKind::RParen || k == TokenKind::RBracket || k == TokenKind::RBrace) {
                if (!brace_stack.empty()) brace_stack.pop_back();
            } else if (k == TokenKind::Semicolon && !pre.in_header[i]) {
                if (!brace_stack.empty() && items[brace_stack.back()].tk == TokenKind::LBrace &&
                    !pre.struct_lit[brace_stack.back()]) {
                    expand_brace[brace_stack.back()] = true;
                }
                const TokenKind prev_tk = surviving_prev_tok < n ? items[surviving_prev_tok].tk : TokenKind::Eof;
                const std::size_t nt = pre.next_tok[i];
                const TokenKind next_tk = nt < n ? items[nt].tk : TokenKind::Eof;
                const bool can_drop =
                    prev_tk == TokenKind::Eof || prev_tk == TokenKind::LBrace || prev_tk == TokenKind::Semicolon ||
                    next_tk == TokenKind::Eof || next_tk == TokenKind::RBrace || next_tk == TokenKind::Semicolon ||
                    (prev_tk != TokenKind::KwReturn && !can_continue_expr(next_tk));
                if (can_drop) {
                    drop_semi[i] = true;
                    continue;
                }
                split_after_semi[i] = true;
            }
            surviving_prev_tok = i;
        }

        for (std::size_t i = 0; i < n; ++i) {
            if (expand_brace[i]) {
                if (i + 1 < n) {
                    items[i + 1].newlines_before = std::max<std::size_t>(items[i + 1].newlines_before, 1);
                }
                if (pre.partner[i] < n) {
                    items[pre.partner[i]].newlines_before = std::max<std::size_t>(items[pre.partner[i]].newlines_before, 1);
                }
            }
            if (split_after_semi[i] && i + 1 < n) {
                if (!(items[i + 1].kind == Item::Kind::LineComment && items[i + 1].newlines_before == 0)) {
                    items[i + 1].newlines_before = std::max<std::size_t>(items[i + 1].newlines_before, 1);
                }
            }
        }

        std::vector<Item> norm_items;
        norm_items.reserve(n);
        std::size_t carry_nl = 0;
        for (std::size_t i = 0; i < n; ++i) {
            if (drop_semi[i]) {
                carry_nl = std::max<std::size_t>(carry_nl, std::max<std::size_t>(1, items[i].newlines_before));
                continue;
            }
            Item it = items[i];
            if (carry_nl > 0) {
                it.newlines_before = std::max(it.newlines_before, carry_nl);
                carry_nl = 0;
            }
            norm_items.push_back(it);
        }
        items = std::move(norm_items);
        if (items.empty()) return "\n";
        items[0].newlines_before = 0;
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (i > 0 && items[i - 1].kind == Item::Kind::LineComment) {
                items[i].newlines_before = std::max<std::size_t>(items[i].newlines_before, 1);
            }
            items[i].newlines_before = std::min<std::size_t>(items[i].newlines_before, 2);
        }
    }

    // Stage B: Classify structure purely from normalized `items`.
    const BracketInfo info = classify();
    const std::vector<bool>& struct_lit = info.struct_lit;
    const std::vector<bool>& type_bracket = info.type_bracket;
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
        if (params_only && j > i + 1 && j + 1 < items.size() && tk(j) == TokenKind::Pipe &&
            (tk(j + 1) == TokenKind::LBrace || tk(j + 1) == TokenKind::Arrow)) {
            closure_pipe[i] = 1;
            closure_pipe[j] = 2;
            i = j;
        }
    }

    auto starts_with_assign = [](TokenKind k) -> bool {
        return k == TokenKind::Assign || k == TokenKind::Eq || k == TokenKind::FatArrow;
    };
    auto starts_with_dot = [](TokenKind k) -> bool {
        return k == TokenKind::Dot || k == TokenKind::DotDot || k == TokenKind::DotDotEq;
    };
    auto starts_with_colon = [](TokenKind k) -> bool {
        return k == TokenKind::Colon || k == TokenKind::ColonEq || k == TokenKind::DoubleColon;
    };
    auto starts_with_amp = [](TokenKind k) -> bool {
        return k == TokenKind::Amp || k == TokenKind::AmpEq || k == TokenKind::DoubleAmp;
    };
    auto starts_with_pipe = [](TokenKind k) -> bool {
        return k == TokenKind::Pipe || k == TokenKind::PipeEq || k == TokenKind::DoublePipe;
    };
    auto starts_with_lt = [](TokenKind k) -> bool {
        return k == TokenKind::Lt || k == TokenKind::Le || k == TokenKind::Shl || k == TokenKind::ShlEq;
    };
    auto starts_with_gt = [](TokenKind k) -> bool {
        return k == TokenKind::Gt || k == TokenKind::Ge || k == TokenKind::Shr || k == TokenKind::ShrEq;
    };
    auto would_glue = [&](const Item& A, TokenKind x, TokenKind y) -> bool {
        if (x == TokenKind::Int && y == TokenKind::Dot) return true;
        if (x == TokenKind::Float && starts_with_dot(y)) {
            const auto t = text(A);
            if (y == TokenKind::Dot || (!t.empty() && t.back() == '.')) return true;
        }
        if (x == TokenKind::Dot && starts_with_dot(y)) return true;
        if (x == TokenKind::DotDot && (starts_with_dot(y) || starts_with_assign(y))) return true;
        if (x == TokenKind::Colon && starts_with_colon(y)) return true;
        if (x == TokenKind::Amp && starts_with_amp(y)) return true;
        if (x == TokenKind::Pipe && starts_with_pipe(y)) return true;
        if (x == TokenKind::Lt && starts_with_lt(y)) return true;
        if (x == TokenKind::Gt && starts_with_gt(y)) return true;
        if ((x == TokenKind::Minus || x == TokenKind::Assign) && starts_with_gt(y)) return true;
        if (x == TokenKind::Slash && (y == TokenKind::Star || y == TokenKind::StarEq || y == TokenKind::Slash || y == TokenKind::SlashEq)) return true;
        if ((x == TokenKind::Colon || x == TokenKind::Plus || x == TokenKind::Minus || x == TokenKind::Star ||
             x == TokenKind::Slash || x == TokenKind::Percent || x == TokenKind::Caret || x == TokenKind::Amp ||
             x == TokenKind::Pipe || x == TokenKind::Bang || x == TokenKind::Lt || x == TokenKind::Gt ||
             x == TokenKind::Shl || x == TokenKind::Shr || x == TokenKind::Assign) && starts_with_assign(y)) return true;
        return false;
    };

    // 4. Spacing between two items on the same output line.
    auto space_between = [&](std::size_t a, std::size_t b) -> bool {
        const Item& A = items[a];
        const Item& B = items[b];
        if (B.kind == Item::Kind::LineComment || B.kind == Item::Kind::BlockComment || A.kind != Item::Kind::Token) return true;
        const TokenKind x = A.tk;
        const TokenKind y = B.tk;
        if (would_glue(A, x, y)) return true;
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
        if ((x == TokenKind::Amp || x == TokenKind::Star) && y == TokenKind::KwMut && (!ends_operand(before_x) || A.newlines_before > 0 || a == 0)) return false;
        if (x == TokenKind::Pipe && closure_pipe[a] == 1) return false;  // `|x`
        if (y == TokenKind::Pipe && closure_pipe[b] == 2) return false;  // `x|`
        if (y == TokenKind::Pipe && closure_pipe[b] == 1) return x != TokenKind::LParen && x != TokenKind::LBracket;
        return true;
    };

    // 5. Emit. Every opener indents the lines inside it one level past the line it opens on, so a line that
    //    opens several brackets (`f(|x| {`) still indents its body once. A line that continues an expression
    //    (after a trailing binary operator, or starting with `.`, `&&`, `||` or `or`) is indented one more level,
    //    while a block it opens (`if a &&\n    b {`) still indents from the statement's own level.
    std::string out;
    std::vector<int> indent_stack;
    int line_indent = 0;  // the statement level of the current line, before any continuation indent
    bool at_line_start = true;
    std::size_t prev = items.size();
    for (std::size_t i = 0; i < items.size(); ++i) {
        const Item& it = items[i];
        if (i > 0 && it.newlines_before > 0 && !out.empty()) {
            while (!out.empty() && out.back() == ' ') out.pop_back();
            out.append(it.newlines_before, '\n');
            at_line_start = true;
        }
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
            const int open = static_cast<int>(indent_stack.size());
            line_indent = open > leading ? indent_stack[static_cast<std::size_t>(open - leading - 1)] : 0;
            const bool continues = leading == 0 && it.kind == Item::Kind::Token && prev < items.size() &&
                                   ((items[prev].kind == Item::Kind::Token && is_binary_operator(items[prev].tk)) ||
                                    it.tk == TokenKind::Dot || it.tk == TokenKind::DoubleAmp || it.tk == TokenKind::DoublePipe ||
                                    it.tk == TokenKind::KwOr);
            out.append(static_cast<std::size_t>(line_indent + (continues ? 1 : 0)) * 4, ' ');
        } else if (prev < items.size() && space_between(prev, i)) {
            out.push_back(' ');
        }
        out.append(text(it));
        at_line_start = false;
        if (it.kind == Item::Kind::Token) {
            if (it.tk == TokenKind::LParen || it.tk == TokenKind::LBracket || it.tk == TokenKind::LBrace) indent_stack.push_back(line_indent + 1);
            else if (closer && !indent_stack.empty()) indent_stack.pop_back();
        }
        prev = i;
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '\n')) out.pop_back();
    out.push_back('\n');
    return out;
}

} // namespace cfire::fmt::printer
