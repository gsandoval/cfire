#pragma once
// src/lsp/json_rpc.hpp — JSON-RPC 2.0 Framing, Structure-Aware Key Extraction & AST Scope Walkers

#include "../clang_bridge.hpp"
#include "../docgen/catalog_types.hpp"
#include "../formatter.hpp"
#include "../sema.hpp"
#include "type_query.hpp"
#include "layout.hpp"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <format>
#include <functional>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifndef CFIRE_LSP_RPC_INLINE
#ifdef CFIRE_SPLIT_TUS
#define CFIRE_LSP_RPC_INLINE
#else
#define CFIRE_LSP_RPC_INLINE inline
#endif
#endif

namespace cfire::lsp {

namespace detail {

struct CursorSymbol {
    std::string qualifier; // e.g. "fs", "m_out", "checksum", "cpp", "order.customer"
    std::string name;      // e.g. "read_all", "Service", "compute_tag"
    int start_col = 0;     // 0-based UTF-8 byte start column of `name`
    int end_col = 0;       // 0-based UTF-8 byte end column of `name`
    bool is_double_colon = false; // true when separator before `name` is `::`
    int sep_col = -1;      // 0-based UTF-8 byte column of `.` or first `:` of `::`
};

struct ContentChangeEvent {
    bool has_range = false;
    int start_line = 0;
    int start_char = 0;
    int end_line = 0;
    int end_char = 0;
    std::string text{};
};

struct StringInterpolationSpan {
    bool in_string = false;
    bool in_interp_expr = false;
    int min_col = 0;
    int max_col = 0;
    std::string expr_text;
    std::string fmt_spec;
};

struct CallContext {
    std::string qualifier;
    std::string callee;
    int active_param = 0;
};

struct DiagnosticEntry {
    int line = 0;      // 0-based
    int col = 0;       // 0-based
    std::string code;  // e.g. "cfire/MUT-CALLSITE"
    std::string message;
    int severity = 1;  // 1 = Error, 2 = Warning
    std::unordered_map<std::string, std::string> facts{};  // typed fix-it inputs (see cfire::diag::fact_of)
};

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto json_escape(std::string_view sv) -> std::string;

CFIRE_LSP_RPC_INLINE void append_utf8(std::string& out, uint32_t cp);

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto json_unescape(std::string_view sv) -> std::string;

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto find_json_key(std::string_view json, std::string_view key, std::size_t start_pos = 0)
    -> std::size_t;

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto extract_json_string(std::string_view json, std::string_view key, std::size_t start_pos = 0)
    -> std::optional<std::string>;

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto extract_json_id(std::string_view json) -> std::optional<std::string>;

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto extract_json_int_after(std::string_view json, std::string_view key, std::size_t start_pos = 0)
    -> std::optional<int>;

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto extract_json_bool(std::string_view json, std::string_view key, std::size_t start_pos = 0)
    -> std::optional<bool>;

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto extract_content_changes(std::string_view json)
    -> std::vector<ContentChangeEvent>;

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto uri_to_path(std::string_view uri) -> std::string;

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto utf8_col_to_utf16_col(std::string_view line, int utf8_col) -> int;

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto utf16_col_to_utf8_col(std::string_view line, int u16_col) -> int;

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto lsp_pos_to_byte_offset(std::string_view doc, int target_line, int target_u16_col)
    -> std::size_t;

CFIRE_LSP_RPC_INLINE void apply_incremental_edit(std::string& doc, int start_line, int start_col, int end_line, int end_col, std::string_view new_text);

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto path_to_uri(const std::filesystem::path& p) -> std::string;

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto split_lines(std::string_view text) -> std::vector<std::string>;

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto extract_leading_doc_comments(const std::vector<std::string>& lines, int one_based_line) -> std::string;

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto find_string_interpolation_span_at(std::string_view ln, int character) -> StringInterpolationSpan;

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto symbol_at_position(const std::vector<std::string>& lines, int line, int character) -> CursorSymbol;

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto find_call_context(const std::vector<std::string>& lines, int line, int character) -> std::optional<CallContext>;

CFIRE_LSP_RPC_INLINE auto expr_to_string(const cfire::frontend::ExprPtr& e) -> std::string;

CFIRE_LSP_RPC_INLINE auto interp_expr_arena_cache() -> std::vector<std::shared_ptr<cfire::frontend::AstArena>>&;

CFIRE_LSP_RPC_INLINE void walk_exprs(
    const cfire::frontend::ExprPtr& e,
    const std::function<void(const cfire::frontend::ExprPtr&)>& visitor
);

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto expand_tokens_with_interpolations(
    std::string_view path,
    const std::vector<cfire::frontend::Token>& raw_toks
) -> std::vector<cfire::frontend::Token>;

CFIRE_LSP_RPC_INLINE void walk_stmt_exprs(
    const std::vector<cfire::frontend::StmtPtr>& stmts,
    const std::function<void(const cfire::frontend::ExprPtr&)>& visitor
);

CFIRE_LSP_RPC_INLINE void collect_all_exprs(
    const cfire::frontend::ExprPtr& e,
    std::vector<cfire::frontend::ExprPtr>& out
);

CFIRE_LSP_RPC_INLINE void collect_stmt_exprs(
    const std::vector<cfire::frontend::StmtPtr>& stmts,
    std::vector<cfire::frontend::ExprPtr>& out
);

CFIRE_LSP_RPC_INLINE int get_block_end_line(
    const std::vector<cfire::frontend::StmtPtr>& body,
    const std::vector<std::string>& lines,
    int start_line
);

CFIRE_LSP_RPC_INLINE void collect_stmt_bindings(
    const std::vector<cfire::frontend::StmtPtr>& stmts,
    const cfire::frontend::FileAST& ast,
    const WorkspaceContext& ws,
    const std::vector<std::string>& lines,
    int max_line,
    std::unordered_map<std::string, LocalBindingInfo>& env,
    std::vector<LocalBindingInfo>& ordered_out,
    const cfire::frontend::TypeTable* sema_types = nullptr
);

CFIRE_LSP_RPC_INLINE void collect_stmt_bindings(
    const std::vector<cfire::frontend::StmtPtr>& stmts,
    const cfire::frontend::FileAST& ast,
    const WorkspaceContext& ws,
    const std::vector<std::string>& lines,
    std::unordered_map<std::string, LocalBindingInfo>& env,
    std::vector<LocalBindingInfo>& ordered_out,
    const cfire::frontend::TypeTable* sema_types = nullptr
);

CFIRE_LSP_RPC_INLINE void walk_expr_calls(const cfire::frontend::ExprPtr& expr, const std::function<void(const cfire::frontend::CallExpr&)>& visitor);

CFIRE_LSP_RPC_INLINE void walk_stmts_calls(const std::vector<cfire::frontend::StmtPtr>& stmts, const std::function<void(const cfire::frontend::CallExpr&)>& visitor);

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto build_scope_bindings(
    const cfire::frontend::FileAST& ast,
    const WorkspaceContext& ws,
    const std::vector<std::string>& lines,
    int cursor_zero_based_line,
    const cfire::frontend::TypeTable* sema_types = nullptr
) -> std::unordered_map<std::string, LocalBindingInfo>;

CFIRE_LSP_RPC_INLINE void write_rpc_message(std::ostream& out, std::string_view body);

} // namespace detail


#if !defined(CFIRE_SPLIT_TUS) || defined(CFIRE_LSP_SERVER_IMPL)

namespace detail {

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto json_escape(std::string_view sv) -> std::string {
    return cfire::detail::json_escape(sv);
}

CFIRE_LSP_RPC_INLINE void append_utf8(std::string& out, uint32_t cp) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0x10FFFF) {
        out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto json_unescape(std::string_view sv) -> std::string {
    std::string out;
    out.reserve(sv.size());
    auto hex_val = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < sv.size(); ++i) {
        if (sv[i] == '\\' && i + 1 < sv.size()) {
            char next = sv[++i];
            switch (next) {
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'u': {
                    if (i + 4 < sv.size()) {
                        int h0 = hex_val(sv[i + 1]);
                        int h1 = hex_val(sv[i + 2]);
                        int h2 = hex_val(sv[i + 3]);
                        int h3 = hex_val(sv[i + 4]);
                        if (h0 >= 0 && h1 >= 0 && h2 >= 0 && h3 >= 0) {
                            uint32_t cp = static_cast<uint32_t>((h0 << 12) | (h1 << 8) | (h2 << 4) | h3);
                            i += 4;
                            // Check for UTF-16 surrogate pair \uD800..\uDBFF followed by \uDC00..\uDFFF
                            if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 < sv.size() && sv[i + 1] == '\\' && sv[i + 2] == 'u') {
                                int l0 = hex_val(sv[i + 3]);
                                int l1 = hex_val(sv[i + 4]);
                                int l2 = hex_val(sv[i + 5]);
                                int l3 = hex_val(sv[i + 6]);
                                if (l0 >= 0 && l1 >= 0 && l2 >= 0 && l3 >= 0) {
                                    uint32_t low = static_cast<uint32_t>((l0 << 12) | (l1 << 8) | (l2 << 4) | l3);
                                    if (low >= 0xDC00 && low <= 0xDFFF) {
                                        cp = 0x10000 + (((cp - 0xD800) << 10) | (low - 0xDC00));
                                        i += 6;
                                    }
                                }
                            }
                            append_utf8(out, cp);
                            break;
                        }
                    }
                    out.push_back('u');
                    break;
                }
                default: out.push_back(next); break;
            }
        } else {
            out.push_back(sv[i]);
        }
    }
    return out;
}

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto find_json_key(std::string_view json, std::string_view key, std::size_t start_pos)
    -> std::size_t {
    std::size_t i = 0;
    const std::size_t n = json.size();
    while (i < n) {
        if (json[i] == '"') {
            std::size_t str_start = i++;
            while (i < n) {
                if (json[i] == '\\') {
                    i += 2;
                    continue;
                }
                if (json[i] == '"') break;
                ++i;
            }
            if (i >= n) return std::string_view::npos;
            std::size_t str_end = i++;
            std::size_t after = i;
            while (after < n && (json[after] == ' ' || json[after] == '\t' || json[after] == '\n' || json[after] == '\r')) {
                ++after;
            }
            if (after < n && json[after] == ':') {
                if (str_start >= start_pos && json.substr(str_start + 1, str_end - str_start - 1) == key) {
                    return after + 1;
                }
                i = after + 1;
            }
        } else {
            ++i;
        }
    }
    return std::string_view::npos;
}

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto extract_json_string(std::string_view json, std::string_view key, std::size_t start_pos)
    -> std::optional<std::string> {
    std::size_t pos = find_json_key(json, key, start_pos);
    if (pos == std::string_view::npos) return std::nullopt;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) ++pos;
    if (pos >= json.size() || json[pos] != '"') return std::nullopt;
    ++pos;
    std::size_t end = pos;
    while (end < json.size()) {
        if (json[end] == '\\') {
            end += 2;
            continue;
        }
        if (json[end] == '"') break;
        ++end;
    }
    if (end >= json.size()) return std::nullopt;
    return json_unescape(json.substr(pos, end - pos));
}

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto extract_json_id(std::string_view json) -> std::optional<std::string> {
    std::size_t pos = find_json_key(json, "id", 0);
    if (pos == std::string_view::npos) return std::nullopt;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) ++pos;
    if (pos >= json.size()) return std::nullopt;
    if (json[pos] == '"') {
        std::size_t end = pos + 1;
        while (end < json.size() && json[end] != '"') {
            if (json[end] == '\\') ++end;
            ++end;
        }
        if (end < json.size()) return std::string(json.substr(pos, end - pos + 1));
    } else {
        std::size_t end = pos;
        while (end < json.size() && (std::isdigit(static_cast<unsigned char>(json[end])) || json[end] == '-')) ++end;
        if (end > pos) return std::string(json.substr(pos, end - pos));
    }
    return std::nullopt;
}

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto extract_json_int_after(std::string_view json, std::string_view key, std::size_t start_pos)
    -> std::optional<int> {
    std::size_t pos = find_json_key(json, key, start_pos);
    if (pos == std::string_view::npos) return std::nullopt;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) ++pos;
    std::size_t end = pos;
    while (end < json.size() && (std::isdigit(static_cast<unsigned char>(json[end])) || json[end] == '-')) ++end;
    if (end == pos) return std::nullopt;
    int val = 0;
    auto res = std::from_chars(json.data() + pos, json.data() + end, val);
    if (res.ec == std::errc{}) {
        return val;
    }
    return std::nullopt;
}

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto extract_json_bool(std::string_view json, std::string_view key, std::size_t start_pos)
    -> std::optional<bool> {
    std::size_t pos = find_json_key(json, key, start_pos);
    if (pos == std::string_view::npos) return std::nullopt;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) ++pos;
    if (json.substr(pos).starts_with("true")) return true;
    if (json.substr(pos).starts_with("false")) return false;
    return std::nullopt;
}

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto extract_content_changes(std::string_view json)
    -> std::vector<ContentChangeEvent> {
    std::vector<ContentChangeEvent> out;
    auto skip_ws = [](std::string_view sv, std::size_t p) -> std::size_t {
        while (p < sv.size() && (sv[p] == ' ' || sv[p] == '\t' || sv[p] == '\n' || sv[p] == '\r')) ++p;
        return p;
    };
    auto extract_balanced_span = [](std::string_view sv, std::size_t open_idx, char open_ch, char close_ch) -> std::string_view {
        if (open_idx >= sv.size() || sv[open_idx] != open_ch) return {};
        int depth = 0;
        bool in_str = false;
        for (std::size_t i = open_idx; i < sv.size(); ++i) {
            char c = sv[i];
            if (in_str) {
                if (c == '\\') {
                    ++i;
                    continue;
                }
                if (c == '"') in_str = false;
                continue;
            }
            if (c == '"') {
                in_str = true;
            } else if (c == open_ch) {
                ++depth;
            } else if (c == close_ch) {
                --depth;
                if (depth == 0) return sv.substr(open_idx, i - open_idx + 1);
            }
        }
        return {};
    };
    auto parse_change_obj = [&](std::string_view obj_sv) -> std::optional<ContentChangeEvent> {
        auto text_opt = extract_json_string(obj_sv, "text", 0);
        if (!text_opt) return std::nullopt;
        ContentChangeEvent ev;
        ev.text = std::move(*text_opt);
        if (std::size_t r_pos = find_json_key(obj_sv, "range", 0); r_pos != std::string_view::npos) {
            r_pos = skip_ws(obj_sv, r_pos);
            std::string_view range_sv = extract_balanced_span(obj_sv, r_pos, '{', '}');
            if (!range_sv.empty()) {
                std::size_t s_pos = find_json_key(range_sv, "start", 0);
                std::size_t e_pos = find_json_key(range_sv, "end", 0);
                if (s_pos != std::string_view::npos && e_pos != std::string_view::npos) {
                    s_pos = skip_ws(range_sv, s_pos);
                    e_pos = skip_ws(range_sv, e_pos);
                    std::string_view start_sv = extract_balanced_span(range_sv, s_pos, '{', '}');
                    std::string_view end_sv = extract_balanced_span(range_sv, e_pos, '{', '}');
                    if (!start_sv.empty() && !end_sv.empty()) {
                        ev.has_range = true;
                        ev.start_line = extract_json_int_after(start_sv, "line", 0).value_or(0);
                        ev.start_char = extract_json_int_after(start_sv, "character", 0).value_or(0);
                        ev.end_line = extract_json_int_after(end_sv, "line", 0).value_or(ev.start_line);
                        ev.end_char = extract_json_int_after(end_sv, "character", 0).value_or(ev.start_char);
                    }
                }
            }
        }
        return ev;
    };

    std::size_t cc_pos = find_json_key(json, "contentChanges", 0);
    if (cc_pos != std::string_view::npos) {
        cc_pos = skip_ws(json, cc_pos);
        std::string_view arr_sv = extract_balanced_span(json, cc_pos, '[', ']');
        if (!arr_sv.empty()) {
            std::size_t i = 1;
            while (i + 1 < arr_sv.size()) {
                i = skip_ws(arr_sv, i);
                if (i >= arr_sv.size()) break;
                if (arr_sv[i] == ',') {
                    ++i;
                    continue;
                }
                if (arr_sv[i] == ']') break;
                if (arr_sv[i] == '{') {
                    std::string_view obj_sv = extract_balanced_span(arr_sv, i, '{', '}');
                    if (obj_sv.empty()) break;
                    if (auto ev = parse_change_obj(obj_sv)) {
                        out.push_back(std::move(*ev));
                    }
                    i += obj_sv.size();
                } else {
                    ++i;
                }
            }
            return out;
        }
    }

    // Legacy fallback: single `"text"` (and optional `"range"`) directly on `params`
    if (std::size_t p_pos = find_json_key(json, "params", 0); p_pos != std::string_view::npos) {
        p_pos = skip_ws(json, p_pos);
        std::string_view params_sv = extract_balanced_span(json, p_pos, '{', '}');
        if (!params_sv.empty()) {
            if (auto ev = parse_change_obj(params_sv)) {
                out.push_back(std::move(*ev));
                return out;
            }
        }
    }
    if (auto ev = parse_change_obj(json)) {
        out.push_back(std::move(*ev));
    }
    return out;
}

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto uri_to_path(std::string_view uri) -> std::string {
    std::string_view path_part = uri;
    if (path_part.starts_with("file://")) {
        path_part.remove_prefix(7);
        #if defined(_WIN32)
        if (path_part.starts_with('/') && path_part.size() >= 3 &&
            std::isalpha(static_cast<unsigned char>(path_part[1])) && path_part[2] == ':') {
            path_part.remove_prefix(1);
        }
        #endif
    }
    std::string out;
    out.reserve(path_part.size());
    for (std::size_t i = 0; i < path_part.size(); ++i) {
        if (path_part[i] == '%' && i + 2 < path_part.size()) {
            auto hex_val = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int h1 = hex_val(path_part[i + 1]);
            int h2 = hex_val(path_part[i + 2]);
            if (h1 >= 0 && h2 >= 0) {
                out.push_back(static_cast<char>((h1 << 4) | h2));
                i += 2;
                continue;
            }
        }
        out.push_back(path_part[i]);
    }
    return out;
}

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto utf8_col_to_utf16_col(std::string_view line, int utf8_col) -> int {
    if (utf8_col <= 0 || line.empty()) return 0;
    int u16_col = 0;
    std::size_t byte_idx = 0;
    std::size_t target_byte = static_cast<std::size_t>(utf8_col);
    while (byte_idx < line.size() && byte_idx < target_byte) {
        unsigned char c = static_cast<unsigned char>(line[byte_idx]);
        if (c < 0x80) {
            byte_idx += 1;
            u16_col += 1;
        } else if ((c & 0xE0) == 0xC0) {
            byte_idx += 2;
            u16_col += 1;
        } else if ((c & 0xF0) == 0xE0) {
            byte_idx += 3;
            u16_col += 1;
        } else if ((c & 0xF8) == 0xF0) {
            byte_idx += 4;
            u16_col += 2;
        } else {
            byte_idx += 1;
            u16_col += 1;
        }
    }
    return u16_col;
}

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto utf16_col_to_utf8_col(std::string_view line, int u16_col) -> int {
    if (u16_col <= 0 || line.empty()) return 0;
    int cur_u16 = 0;
    std::size_t byte_idx = 0;
    while (byte_idx < line.size() && cur_u16 < u16_col) {
        unsigned char c = static_cast<unsigned char>(line[byte_idx]);
        if (c < 0x80) {
            byte_idx += 1;
            cur_u16 += 1;
        } else if ((c & 0xE0) == 0xC0) {
            byte_idx += 2;
            cur_u16 += 1;
        } else if ((c & 0xF0) == 0xE0) {
            byte_idx += 3;
            cur_u16 += 1;
        } else if ((c & 0xF8) == 0xF0) {
            byte_idx += 4;
            cur_u16 += 2;
        } else {
            byte_idx += 1;
            cur_u16 += 1;
        }
    }
    return static_cast<int>(byte_idx);
}

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto lsp_pos_to_byte_offset(std::string_view doc, int target_line, int target_u16_col)
    -> std::size_t {
    if (target_line < 0) return 0;
    std::size_t pos = 0;
    int cur_line = 0;
    while (pos <= doc.size()) {
        std::size_t nl = doc.find('\n', pos);
        std::size_t line_end = (nl == std::string_view::npos)
            ? doc.size()
            : ((nl > pos && doc[nl - 1] == '\r') ? nl - 1 : nl);
        if (cur_line == target_line) {
            std::string_view ln = doc.substr(pos, line_end - pos);
            int u8_col = utf16_col_to_utf8_col(ln, target_u16_col);
            std::size_t clamped = std::min(static_cast<std::size_t>(std::max(0, u8_col)), ln.size());
            return pos + clamped;
        }
        if (nl == std::string_view::npos) break;
        pos = nl + 1;
        ++cur_line;
    }
    return doc.size();
}

CFIRE_LSP_RPC_INLINE void apply_incremental_edit(std::string& doc, int start_line, int start_col, int end_line, int end_col, std::string_view new_text) {
    if (doc.empty() && start_line == 0 && start_col == 0) {
        doc = std::string(new_text);
        return;
    }

    std::size_t start_byte = lsp_pos_to_byte_offset(doc, start_line, start_col);
    std::size_t end_byte = lsp_pos_to_byte_offset(doc, end_line, end_col);
    if (start_byte > doc.size()) start_byte = doc.size();
    if (end_byte > doc.size()) end_byte = doc.size();
    if (start_byte > end_byte) std::swap(start_byte, end_byte);

    doc.replace(start_byte, end_byte - start_byte, new_text);
}

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto path_to_uri(const std::filesystem::path& p) -> std::string {
    std::error_code ec;
    auto canon = std::filesystem::weakly_canonical(p, ec);
    std::string s = (ec ? p : canon).generic_string();
#if defined(_WIN32)
    // `C:\a\b.cf` is `file:///C:/a/b.cf` (RFC 8089), the form editors send and `uri_to_path` reads back.
    if (s.size() >= 2 && std::isalpha(static_cast<unsigned char>(s[0])) && s[1] == ':') return "file:///" + s;
#endif
    return "file://" + s;
}

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto split_lines(std::string_view text) -> std::vector<std::string> {
    std::vector<std::string> lines;
    std::istringstream iss{std::string(text)};
    std::string l;
    while (std::getline(iss, l)) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        lines.push_back(l);
    }
    return lines;
}

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto extract_leading_doc_comments(const std::vector<std::string>& lines, int one_based_line) -> std::string {
    if (one_based_line <= 1 || lines.empty()) return "";
    int cur_idx = one_based_line - 2;
    std::vector<std::string> comment_lines;
    while (cur_idx >= 0 && static_cast<std::size_t>(cur_idx) < lines.size()) {
        const std::string& raw = lines[static_cast<std::size_t>(cur_idx)];
        std::size_t b = 0;
        while (b < raw.size() && (raw[b] == ' ' || raw[b] == '\t')) ++b;
        if (raw.substr(b).starts_with("//")) {
            std::string ctext = raw.substr(b + 2);
            if (!ctext.empty() && ctext.front() == ' ') ctext = ctext.substr(1);
            comment_lines.push_back(std::move(ctext));
            --cur_idx;
        } else {
            break;
        }
    }
    if (comment_lines.empty()) return "";
    std::reverse(comment_lines.begin(), comment_lines.end());
    std::string out;
    for (const auto& cl : comment_lines) {
        out += cl + "\n";
    }
    return out;
}

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto find_string_interpolation_span_at(std::string_view ln, int character) -> StringInterpolationSpan {
    const int n = static_cast<int>(ln.size());
    int i = 0;
    while (i < n) {
        char c = ln[static_cast<std::size_t>(i)];
        if (c == '/' && i + 1 < n && ln[static_cast<std::size_t>(i + 1)] == '/') {
            break; // line comment
        }
        if (c == '`') {
            int q_open = i++;
            while (i < n && ln[static_cast<std::size_t>(i)] != '`') ++i;
            int q_close = i;
            if (character > q_open && character <= q_close) {
                return StringInterpolationSpan{true, false, q_open + 1, q_close, {}, {}};
            }
            if (i < n) ++i;
            continue;
        }
        if (c == '"') {
            int q_open = i++;
            int interp_depth = 0;
            bool in_inner_quote = false;
            while (i < n) {
                char cur = ln[static_cast<std::size_t>(i)];
                if (cur == '\\') {
                    if (i + 2 < n && ln[static_cast<std::size_t>(i + 1)] == 'u' && ln[static_cast<std::size_t>(i + 2)] == '{') {
                        int j = i + 3;
                        while (j < n && ln[static_cast<std::size_t>(j)] != '}') ++j;
                        i = (j < n) ? (j + 1) : (i + 2);
                        continue;
                    }
                    i += 2;
                    continue;
                }
                if (in_inner_quote) {
                    if (cur == '"') in_inner_quote = false;
                    ++i;
                    continue;
                }
                if (cur == '"') {
                    if (interp_depth > 0) {
                        bool has_closing_quote = false;
                        bool has_closing_brace = false;
                        for (int k = i + 1; k < n; ++k) {
                            if (ln[static_cast<std::size_t>(k)] == '\\' && k + 1 < n) {
                                ++k;
                                continue;
                            }
                            if (!has_closing_quote) {
                                if (ln[static_cast<std::size_t>(k)] == '"') has_closing_quote = true;
                            } else if (ln[static_cast<std::size_t>(k)] == '}') {
                                has_closing_brace = true;
                                break;
                            }
                        }
                        if (has_closing_quote && has_closing_brace) {
                            in_inner_quote = true;
                            ++i;
                            continue;
                        }
                    }
                    break;
                }
                if (cur == '{') {
                    if (i + 1 < n && ln[static_cast<std::size_t>(i + 1)] == '{') {
                        i += 2;
                        continue;
                    }
                    ++interp_depth;
                    ++i;
                    continue;
                }
                if (cur == '}' && interp_depth > 0) {
                    --interp_depth;
                    ++i;
                    continue;
                }
                ++i;
            }
            int q_close = i; // either index of closing '"' or n if unclosed
            if (character > q_open && character <= q_close) {
                std::string_view sv = ln.substr(
                    static_cast<std::size_t>(q_open + 1),
                    static_cast<std::size_t>(q_close - (q_open + 1)));
                auto placeholders = cfire::frontend::scan_interpolation_placeholders(sv);
                for (const auto& ph : placeholders) {
                    int expr_start = q_open + 1 + static_cast<int>(ph.expr_offset);
                    int expr_end = expr_start + static_cast<int>(ph.expr_text.size());
                    if (character >= expr_start && character <= expr_end) {
                        return StringInterpolationSpan{
                            true, true, expr_start, expr_end, ph.expr_text, ph.fmt_spec};
                    }
                }
                // Also support unclosed '{' inside string literal for live completion (e.g. `"val={log.`)
                int open_brace = -1;
                int depth = 0;
                for (int k = q_open + 1; k < q_close; ++k) {
                    if (ln[static_cast<std::size_t>(k)] == '\\' && k + 1 < q_close) {
                        if (k + 2 < q_close && ln[static_cast<std::size_t>(k + 1)] == 'u' && ln[static_cast<std::size_t>(k + 2)] == '{') {
                            int j = k + 3;
                            while (j < q_close && ln[static_cast<std::size_t>(j)] != '}') ++j;
                            if (j < q_close) {
                                k = j;
                                continue;
                            }
                        }
                        ++k;
                        continue;
                    }
                    if (ln[static_cast<std::size_t>(k)] == '{') {
                        if (k + 1 < q_close && ln[static_cast<std::size_t>(k + 1)] == '{') {
                            ++k;
                            continue;
                        }
                        if (depth == 0) open_brace = k;
                        ++depth;
                    } else if (ln[static_cast<std::size_t>(k)] == '}' && depth > 0) {
                        --depth;
                        if (depth == 0) open_brace = -1;
                    }
                }
                if (open_brace >= 0 && character > open_brace && character <= q_close) {
                    return StringInterpolationSpan{
                        true, true, open_brace + 1, q_close,
                        std::string(ln.substr(static_cast<std::size_t>(open_brace + 1), static_cast<std::size_t>(q_close - (open_brace + 1)))),
                        {}};
                }
                return StringInterpolationSpan{true, false, q_open + 1, q_close, {}, {}};
            }
            if (i < n) ++i;
            continue;
        }
        ++i;
    }
    return StringInterpolationSpan{false, false, 0, n, {}, {}};
}

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto symbol_at_position(const std::vector<std::string>& lines, int line, int character) -> CursorSymbol {
    if (line < 0 || static_cast<std::size_t>(line) >= lines.size()) return {};
    const std::string& ln = lines[static_cast<std::size_t>(line)];
    if (ln.empty()) return {};

    auto span = find_string_interpolation_span_at(ln, character);
    if (span.in_string && !span.in_interp_expr) {
        return {};
    }
    const int min_col = span.in_interp_expr ? span.min_col : 0;
    const int max_col = span.in_interp_expr ? span.max_col : static_cast<int>(ln.size());
    if (min_col >= max_col) return {};

    int idx = std::clamp(character, min_col, max_col - 1);
    auto is_ident = [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '@';
    };
    auto is_member_dot = [&](int dot_idx) -> bool {
        if (dot_idx < min_col || dot_idx >= max_col || ln[static_cast<std::size_t>(dot_idx)] != '.') return false;
        if (dot_idx > min_col && ln[static_cast<std::size_t>(dot_idx - 1)] == '.') return false;
        if (dot_idx + 1 < max_col && ln[static_cast<std::size_t>(dot_idx + 1)] == '.') return false;
        return true;
    };

    auto extract_qualifier_before_sep = [&](int sep_idx) -> std::string {
        int qend = sep_idx;
        if (qend <= min_col) return "";
        int p = qend - 1;
        while (p >= min_col) {
            char c = ln[static_cast<std::size_t>(p)];
            if (c == ')' || c == ']') {
                char close_ch = c;
                char open_ch = (c == ')') ? '(' : '[';
                int depth = 1;
                --p;
                bool in_str = false;
                while (p >= min_col && depth > 0) {
                    char sc = ln[static_cast<std::size_t>(p)];
                    if (sc == '"') {
                        int bs = 0;
                        while (p - 1 - bs >= min_col && ln[static_cast<std::size_t>(p - 1 - bs)] == '\\') ++bs;
                        if ((bs & 1) == 0) in_str = !in_str;
                    } else if (!in_str) {
                        if (sc == close_ch) ++depth;
                        else if (sc == open_ch) --depth;
                    }
                    --p;
                }
            } else if (c == '"' && p == qend - 1) {
                --p;
                while (p >= min_col) {
                    if (ln[static_cast<std::size_t>(p)] == '"') {
                        int bs = 0;
                        while (p - 1 - bs >= min_col && ln[static_cast<std::size_t>(p - 1 - bs)] == '\\') ++bs;
                        if ((bs & 1) == 0) {
                            --p;
                            break;
                        }
                    }
                    --p;
                }
                break;
            } else if (c == '`' && p == qend - 1) {
                --p;
                while (p >= min_col && ln[static_cast<std::size_t>(p)] != '`') --p;
                if (p >= min_col) --p;
                break;
            } else if (is_ident(c)) {
                --p;
            } else if (c == '.') {
                if (p > min_col && ln[static_cast<std::size_t>(p - 1)] == '.') {
                    break; // Stop at `..` / `..=` range operators (`0..arr.`).
                }
                --p;
            } else if (p >= min_col + 1 && c == ':' && ln[static_cast<std::size_t>(p - 1)] == ':') {
                p -= 2;
            } else {
                break;
            }
        }
        int qstart = p + 1;
        if (qend > qstart) {
            std::string qual = ln.substr(static_cast<std::size_t>(qstart), static_cast<std::size_t>(qend - qstart));
            if (qual.starts_with("(*") && qual.ends_with(")") && qual.size() > 3) {
                return qual.substr(2, qual.size() - 3);
            }
            if (qual.starts_with("(") && qual.ends_with(")") && qual.size() > 2) {
                std::string inner = qual.substr(1, qual.size() - 2);
                while (!inner.empty() && (inner.front() == ' ' || inner.front() == '\t')) inner.erase(inner.begin());
                if (inner.starts_with("try ")) inner = inner.substr(4);
                while (!inner.empty() && (inner.front() == ' ' || inner.front() == '\t')) inner.erase(inner.begin());
                return inner;
            }
            return qual;
        }
        return "";
    };

    // Check if cursor is immediately after `.` (e.g. `m_out.|`) or `::` (e.g. `cpp::|`) with no following ident
    if (character > min_col && character <= max_col && is_member_dot(character - 1) &&
        (character == max_col || !is_ident(ln[static_cast<std::size_t>(character)]))) {
        return CursorSymbol{extract_qualifier_before_sep(character - 1), "", character, character, false, character - 1};
    }
    if (character >= min_col + 2 && character <= max_col &&
        ln[static_cast<std::size_t>(character - 1)] == ':' && ln[static_cast<std::size_t>(character - 2)] == ':' &&
        (character == max_col || !is_ident(ln[static_cast<std::size_t>(character)]))) {
        return CursorSymbol{extract_qualifier_before_sep(character - 2), "", character, character, true, character - 2};
    }
    if (!is_ident(ln[static_cast<std::size_t>(idx)]) && idx > min_col && is_ident(ln[static_cast<std::size_t>(idx - 1)])) {
        --idx;
    }
    if (!is_ident(ln[static_cast<std::size_t>(idx)])) {
        if (is_member_dot(idx) && idx > min_col) {
            return CursorSymbol{extract_qualifier_before_sep(idx), "", idx + 1, idx + 1, false, idx};
        }
        if (ln[static_cast<std::size_t>(idx)] == ':' && idx > min_col && ln[static_cast<std::size_t>(idx - 1)] == ':') {
            return CursorSymbol{extract_qualifier_before_sep(idx - 1), "", idx + 1, idx + 1, true, idx - 1};
        }
        return {};
    }
    int start = idx;
    while (start > min_col && is_ident(ln[static_cast<std::size_t>(start - 1)])) --start;
    int end = idx;
    while (end < max_col && is_ident(ln[static_cast<std::size_t>(end)])) ++end;
    std::string word = ln.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(end - start));

    std::string qual;
    bool is_dc = false;
    int sep_col = -1;
    if (start > min_col && is_member_dot(start - 1)) {
        sep_col = start - 1;
        qual = extract_qualifier_before_sep(sep_col);
    } else if (start > min_col + 1 && ln[static_cast<std::size_t>(start - 1)] == ':' && ln[static_cast<std::size_t>(start - 2)] == ':') {
        is_dc = true;
        sep_col = start - 2;
        qual = extract_qualifier_before_sep(sep_col);
    }
    return CursorSymbol{qual, word, start, end, is_dc, sep_col};
}

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto find_call_context(const std::vector<std::string>& lines, int line, int character) -> std::optional<CallContext> {
    if (line < 0 || static_cast<std::size_t>(line) >= lines.size()) return std::nullopt;
    const std::string& ln = lines[static_cast<std::size_t>(line)];
    if (ln.empty()) return std::nullopt;
    auto span = find_string_interpolation_span_at(ln, character);
    int min_col = span.in_interp_expr ? span.min_col : 0;
    int max_col = span.in_interp_expr ? span.max_col : static_cast<int>(ln.size());
    if (min_col >= max_col) return std::nullopt;

    int pos = std::clamp(character - 1, min_col, max_col - 1);
    int depth = 0;
    int commas = 0;
    bool in_str = false;
    for (int i = pos; i >= min_col; --i) {
        char c = ln[static_cast<std::size_t>(i)];
        if (c == '"' && (i == min_col || ln[static_cast<std::size_t>(i - 1)] != '\\')) {
            in_str = !in_str;
            continue;
        }
        if (in_str) continue;
        if (c == ')' || c == ']') {
            ++depth;
        } else if (c == ',' && depth == 0) {
            ++commas;
        } else if (c == '(') {
            if (depth > 0) {
                --depth;
            } else {
                // Found opening '(' of the active call! Extract callee before '(' (skipping optional `[TypeArg]`)
                int cend = i;
                if (cend > min_col && ln[static_cast<std::size_t>(cend - 1)] == ']') {
                    int bdepth = 1;
                    cend -= 2;
                    while (cend >= min_col && bdepth > 0) {
                        if (ln[static_cast<std::size_t>(cend)] == ']') ++bdepth;
                        else if (ln[static_cast<std::size_t>(cend)] == '[') --bdepth;
                        --cend;
                    }
                    ++cend;
                }
                auto sym = symbol_at_position(lines, line, cend);
                if (!sym.name.empty()) {
                    return CallContext{sym.qualifier, sym.name, commas};
                }
                return std::nullopt;
            }
        }
    }
    return std::nullopt;
}

CFIRE_LSP_RPC_INLINE auto expr_to_string(const cfire::frontend::ExprPtr& e) -> std::string {
    using namespace cfire::frontend;
    if (!e) return "";
    if (const auto* id = std::get_if<IdentExpr>(&e->node)) return id->name;
    if (const auto* lit = std::get_if<LiteralExpr>(&e->node)) {
        if (lit->kind == TokenKind::String) return "\"" + lit->value + "\"";
        if (lit->kind == TokenKind::RawString) return "`" + lit->value + "`";
        return lit->value;
    }
    if (const auto* un = std::get_if<UnaryExpr>(&e->node)) {
        return un->op + expr_to_string(un->operand);
    }
    if (const auto* bin = std::get_if<BinaryExpr>(&e->node)) {
        return expr_to_string(bin->left) + " " + bin->op + " " + expr_to_string(bin->right);
    }
    if (const auto* mem = std::get_if<MemberExpr>(&e->node)) {
        return expr_to_string(mem->object) + (mem->is_namespace ? "::" : ".") + mem->field;
    }
    if (const auto* idx = std::get_if<IndexExpr>(&e->node)) {
        return expr_to_string(idx->object) + "[" + expr_to_string(idx->index) + "]";
    }
    if (const auto* slc = std::get_if<SliceExpr>(&e->node)) {
        std::string s = expr_to_string(slc->object) + "[";
        if (slc->start) s += expr_to_string(slc->start);
        s += "..";
        if (slc->end) s += expr_to_string(slc->end);
        s += "]";
        return s;
    }
    if (const auto* call = std::get_if<CallExpr>(&e->node)) {
        std::string s = expr_to_string(call->callee);
        if (!call->type_args.empty()) {
            s += "[";
            for (std::size_t i = 0; i < call->type_args.size(); ++i) {
                if (i > 0) s += ", ";
                s += cfire::fmt::format_type_ref(call->type_args[i]);
            }
            s += "]";
        }
        s += "(";
        for (std::size_t i = 0; i < call->args.size(); ++i) {
            if (i > 0) s += ", ";
            if (!call->args[i].intent.empty()) s += call->args[i].intent + " ";
            s += expr_to_string(call->args[i].value);
        }
        s += ")";
        return s;
    }
    if (const auto* sl = std::get_if<StructLitExpr>(&e->node)) {
        return sl->type_name + "{...}";
    }
    if (std::holds_alternative<ArrayLitExpr>(e->node)) {
        return "[...]";
    }
    if (const auto* tr = std::get_if<TryExpr>(&e->node)) {
        return expr_to_string(tr->inner) + "?";
    }
    if (const auto* orex = std::get_if<OrExpr>(&e->node)) {
        return expr_to_string(orex->inner) + " or ...";
    }
    return "";
}

CFIRE_LSP_RPC_INLINE auto interp_expr_arena_cache() -> std::vector<std::shared_ptr<cfire::frontend::AstArena>>& {
    thread_local std::vector<std::shared_ptr<cfire::frontend::AstArena>> cache;
    if (cache.size() > 512) {
        cache.erase(cache.begin(), cache.begin() + 256);
    }
    return cache;
}

CFIRE_LSP_RPC_INLINE void walk_exprs(
    const cfire::frontend::ExprPtr& e,
    const std::function<void(const cfire::frontend::ExprPtr&)>& visitor
) {
    using namespace cfire::frontend;
    if (!e) return;
    visitor(e);
    if (const auto* lit = std::get_if<LiteralExpr>(&e->node)) {
        if (lit->kind == TokenKind::String && lit->value.find('{') != std::string::npos) {
            auto placeholders = scan_interpolation_placeholders(lit->value);
            for (const auto& ph : placeholders) {
                int expr_col = std::max(1, e->col) + 1 + static_cast<int>(ph.expr_offset);
                auto parsed = parse_expr_snippet("<interp>", ph.expr_text, e->line, expr_col);
                if (parsed.has_value() && parsed->expr) {
                    if (parsed->arena) {
                        interp_expr_arena_cache().push_back(parsed->arena);
                    }
                    walk_exprs(parsed->expr, visitor);
                }
            }
        }
    } else if (const auto* un = std::get_if<UnaryExpr>(&e->node)) {
        walk_exprs(un->operand, visitor);
    } else if (const auto* bin = std::get_if<BinaryExpr>(&e->node)) {
        walk_exprs(bin->left, visitor);
        walk_exprs(bin->right, visitor);
    } else if (const auto* mem = std::get_if<MemberExpr>(&e->node)) {
        walk_exprs(mem->object, visitor);
    } else if (const auto* idx = std::get_if<IndexExpr>(&e->node)) {
        walk_exprs(idx->object, visitor);
        walk_exprs(idx->index, visitor);
    } else if (const auto* slc = std::get_if<SliceExpr>(&e->node)) {
        walk_exprs(slc->object, visitor);
        if (slc->start) walk_exprs(slc->start, visitor);
        if (slc->end) walk_exprs(slc->end, visitor);
    } else if (const auto* call = std::get_if<CallExpr>(&e->node)) {
        walk_exprs(call->callee, visitor);
        for (const auto& a : call->args) walk_exprs(a.value, visitor);
        if (call->block_arg) walk_stmt_exprs(call->block_arg->body, visitor);
    } else if (const auto* sl = std::get_if<StructLitExpr>(&e->node)) {
        for (const auto& f : sl->fields) walk_exprs(f.value, visitor);
    } else if (const auto* arr = std::get_if<ArrayLitExpr>(&e->node)) {
        for (const auto& elem : arr->elements) walk_exprs(elem, visitor);
    } else if (const auto* cl = std::get_if<ClosureExpr>(&e->node)) {
        walk_stmt_exprs(cl->body, visitor);
    } else if (const auto* me = std::get_if<MatchExpr>(&e->node)) {
        walk_exprs(me->target, visitor);
        for (const auto& arm : me->arms) {
            if (arm.guard) walk_exprs(arm.guard, visitor);
            walk_stmt_exprs(arm.body, visitor);
        }
    } else if (const auto* tr = std::get_if<TryExpr>(&e->node)) {
        walk_exprs(tr->inner, visitor);
    } else if (const auto* orex = std::get_if<OrExpr>(&e->node)) {
        walk_exprs(orex->inner, visitor);
        walk_stmt_exprs(orex->handler_body, visitor);
        if (orex->fallback_val) walk_exprs(orex->fallback_val, visitor);
    } else if (const auto* tup = std::get_if<TupleExpr>(&e->node)) {
        for (const auto& elem : tup->elements) walk_exprs(elem, visitor);
    }
}

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto expand_tokens_with_interpolations(
    std::string_view path,
    const std::vector<cfire::frontend::Token>& raw_toks
) -> std::vector<cfire::frontend::Token> {
    using namespace cfire::frontend;
    std::vector<Token> out;
    out.reserve(raw_toks.size());
    for (const auto& t : raw_toks) {
        out.push_back(t);
        if (t.kind == TokenKind::String && t.lexeme.find('{') != std::string::npos) {
            auto placeholders = scan_interpolation_placeholders(t.lexeme);
            for (const auto& ph : placeholders) {
                auto sub_toks = tokenize(path, ph.expr_text);
                if (!sub_toks.has_value()) continue;
                int base_col = t.col + 1 + static_cast<int>(ph.expr_offset);
                for (auto sub_t : *sub_toks) {
                    if (sub_t.kind == TokenKind::Eof) continue;
                    if (sub_t.line == 1) {
                        sub_t.col = base_col + (sub_t.col - 1);
                    }
                    sub_t.line = t.line + (sub_t.line - 1);
                    out.push_back(std::move(sub_t));
                }
            }
        }
    }
    return out;
}

CFIRE_LSP_RPC_INLINE void walk_stmt_exprs(
    const std::vector<cfire::frontend::StmtPtr>& stmts,
    const std::function<void(const cfire::frontend::ExprPtr&)>& visitor
) {
    using namespace cfire::frontend;
    for (const auto& s : stmts) {
        if (!s) continue;
        if (const auto* vd = std::get_if<VarDeclStmt>(&s->node)) {
            if (vd->init) walk_exprs(vd->init, visitor);
        } else if (const auto* as = std::get_if<AssignStmt>(&s->node)) {
            if (as->target) walk_exprs(as->target, visitor);
            if (as->value) walk_exprs(as->value, visitor);
        } else if (const auto* das = std::get_if<DestructureAssignStmt>(&s->node)) {
            for (const auto& t : das->targets) walk_exprs(t, visitor);
            if (das->value) walk_exprs(das->value, visitor);
        } else if (const auto* dvs = std::get_if<DestructureVarDeclStmt>(&s->node)) {
            if (dvs->init) walk_exprs(dvs->init, visitor);
        } else if (const auto* def = std::get_if<DeferStmt>(&s->node)) {
            if (def->call) walk_exprs(def->call, visitor);
            walk_stmt_exprs(def->body, visitor);
        } else if (const auto* es = std::get_if<ExprStmt>(&s->node)) {
            if (es->expr) walk_exprs(es->expr, visitor);
        } else if (const auto* rs = std::get_if<ReturnStmt>(&s->node)) {
            if (rs->value) walk_exprs(rs->value, visitor);
        } else if (const auto* ifs = std::get_if<IfStmt>(&s->node)) {
            if (ifs->cond) walk_exprs(ifs->cond, visitor);
            walk_stmt_exprs(ifs->then_body, visitor);
            walk_stmt_exprs(ifs->else_body, visitor);
        } else if (const auto* fs = std::get_if<ForStmt>(&s->node)) {
            if (fs->iter_expr) walk_exprs(fs->iter_expr, visitor);
            if (fs->range_end) walk_exprs(fs->range_end, visitor);
            walk_stmt_exprs(fs->body, visitor);
        } else if (const auto* ms = std::get_if<MatchStmt>(&s->node)) {
            if (ms->target) walk_exprs(ms->target, visitor);
            for (const auto& arm : ms->arms) {
                if (arm.guard) walk_exprs(arm.guard, visitor);
                walk_stmt_exprs(arm.body, visitor);
            }
        } else if (const auto* asc = std::get_if<AsyncScopeStmt>(&s->node)) {
            if (asc->workers) walk_exprs(asc->workers, visitor);
            walk_stmt_exprs(asc->body, visitor);
        }
    }
}

CFIRE_LSP_RPC_INLINE void collect_all_exprs(
    const cfire::frontend::ExprPtr& e,
    std::vector<cfire::frontend::ExprPtr>& out
) {
    walk_exprs(e, [&](const cfire::frontend::ExprPtr& ex) { out.push_back(ex); });
}

CFIRE_LSP_RPC_INLINE void collect_stmt_exprs(
    const std::vector<cfire::frontend::StmtPtr>& stmts,
    std::vector<cfire::frontend::ExprPtr>& out
) {
    walk_stmt_exprs(stmts, [&](const cfire::frontend::ExprPtr& ex) { out.push_back(ex); });
}

CFIRE_LSP_RPC_INLINE int get_block_end_line(
    const std::vector<cfire::frontend::StmtPtr>& body,
    const std::vector<std::string>& lines,
    int start_line
) {
    int max_l = start_line;
    for (const auto& s : body) {
        if (!s) continue;
        max_l = std::max(max_l, s->line);
    }
    int depth = 1;
    for (std::size_t i = (max_l >= 1 ? max_l - 1 : 0); i < lines.size(); ++i) {
        const auto& ln = lines[i];
        for (char c : ln) {
            if (c == '}') {
                --depth;
                if (depth <= 0) {
                    return static_cast<int>(i + 1);
                }
            } else if (c == '{' && static_cast<int>(i + 1) > start_line) {
                ++depth;
            }
        }
    }
    return max_l;
}

CFIRE_LSP_RPC_INLINE void collect_stmt_bindings(
    const std::vector<cfire::frontend::StmtPtr>& stmts,
    const cfire::frontend::FileAST& ast,
    const WorkspaceContext& ws,
    const std::vector<std::string>& lines,
    int max_line,
    std::unordered_map<std::string, LocalBindingInfo>& env,
    std::vector<LocalBindingInfo>& ordered_out,
    const cfire::frontend::TypeTable* sema_types
) {
    using namespace cfire::frontend;
    auto resolve_tid = [&](uint32_t tid_idx, std::string& cf_out, std::string& cpp_out) -> bool {
        if (!sema_types || tid_idx == 0) return false;
        std::string cf_t = format_sema_type_id(tid_idx, sema_types);
        if (cf_t.empty() || cf_t == "void") return false;
        cf_out = std::move(cf_t);
        std::string cpp_t = sema_type_id_to_cpp_preview(tid_idx, sema_types);
        cpp_out = !cpp_t.empty() ? std::move(cpp_t) : cf_type_string_to_cpp(cf_out);
        return true;
    };
    for (const auto& s : stmts) {
        if (!s) continue;
        if (const auto* vd = std::get_if<VarDeclStmt>(&s->node)) {
            if (vd->name != "_" && (max_line <= 0 || vd->line <= max_line)) {
                LocalBindingInfo info;
                info.name = vd->name;
                info.is_mut = vd->is_mut;
                info.decl_line = vd->line;
                if (vd->type) {
                    info.cf_type = cfire::fmt::format_type_ref(*vd->type);
                    info.cpp_type = lower_type_preview(*vd->type);
                    info.inferred_colon_eq = false;
                } else {
                    if (!resolve_tid(vd->resolved_type_id, info.cf_type, info.cpp_type)) {
                        info.cf_type = infer_expr_cf_type(vd->init, ast, ws, env, sema_types);
                        info.cpp_type = cf_type_string_to_cpp(info.cf_type);
                    }
                    info.inferred_colon_eq = true;
                }
                if (vd->line >= 1 && static_cast<std::size_t>(vd->line) <= lines.size()) {
                    const std::string& ln = lines[static_cast<std::size_t>(vd->line - 1)];
                    if (auto p = ln.find(vd->name); p != std::string::npos) {
                        info.name_end_col = static_cast<int>(p + vd->name.size());
                    }
                }
                env[info.name] = info;
                ordered_out.push_back(info);
            }
            if (vd->init) {
                if (const auto* or_ex = std::get_if<OrExpr>(&vd->init->node); or_ex && !or_ex->handler_body.empty()) {
                    int or_end = get_block_end_line(or_ex->handler_body, lines, vd->line);
                    if (max_line <= 0 || (max_line >= vd->line && max_line <= or_end)) {
                        auto or_env = env;
                        if (!or_ex->err_var.empty() && or_ex->err_var != "_") {
                            LocalBindingInfo ei;
                            ei.name = or_ex->err_var;
                            ei.cf_type = "Error";
                            ei.cpp_type = "cfire::Error";
                            ei.decl_line = vd->line;
                            or_env[ei.name] = ei;
                        }
                        collect_stmt_bindings(or_ex->handler_body, ast, ws, lines, max_line, or_env, ordered_out, sema_types);
                        if (max_line > 0) env = std::move(or_env);
                    }
                } else if (const auto* cl = std::get_if<ClosureExpr>(&vd->init->node); cl && !cl->body.empty()) {
                    int cl_end = get_block_end_line(cl->body, lines, vd->line);
                    if (max_line <= 0 || (max_line >= vd->line && max_line <= cl_end)) {
                        auto cl_env = env;
                        for (std::size_t pi = 0; pi < cl->params.size(); ++pi) {
                            const auto& pname = cl->params[pi];
                            if (pname.empty() || pname == "_") continue;
                            LocalBindingInfo pinfo;
                            pinfo.name = pname;
                            pinfo.is_param = true;
                            pinfo.intent = (pi < cl->param_intents.size() && !cl->param_intents[pi].empty()) ? cl->param_intents[pi] : "in";
                            pinfo.is_mut = (pinfo.intent == "mut" || pinfo.intent == "out");
                            pinfo.decl_line = vd->line;
                            if (pi < cl->param_types.size() && cl->param_types[pi]) {
                                pinfo.cf_type = cfire::fmt::format_type_ref(*cl->param_types[pi]);
                                pinfo.cpp_type = lower_type_preview(*cl->param_types[pi], true, pinfo.intent);
                            } else if (!(pi < cl->resolved_param_types.size() &&
                                         resolve_tid(cl->resolved_param_types[pi], pinfo.cf_type, pinfo.cpp_type))) {
                                pinfo.cf_type = "auto";
                                pinfo.cpp_type = "auto";
                            }
                            cl_env[pname] = std::move(pinfo);
                        }
                        collect_stmt_bindings(cl->body, ast, ws, lines, max_line, cl_env, ordered_out, sema_types);
                        if (max_line > 0) env = std::move(cl_env);
                    }
                }
            }
        } else if (const auto* dvs = std::get_if<DestructureVarDeclStmt>(&s->node)) {
            std::vector<std::string> elem_types;
            if (dvs->init) {
                std::string init_type = infer_expr_cf_type(dvs->init, ast, ws, env, sema_types);
                while (init_type.ends_with("!") || init_type.ends_with("?")) init_type.pop_back();
                if (init_type.starts_with("(") && init_type.ends_with(")")) {
                    std::string inner = init_type.substr(1, init_type.size() - 2);
                    std::size_t start = 0;
                    int tdepth = 0;
                    for (std::size_t k = 0; k <= inner.size(); ++k) {
                        if (k < inner.size()) {
                            char c = inner[k];
                            if (c == '(' || c == '[') ++tdepth;
                            else if (c == ')' || c == ']') --tdepth;
                            if (c != ',' || tdepth != 0) continue;
                        }
                        std::string part = inner.substr(start, k - start);
                        start = k + 1;
                        while (!part.empty() && part.front() == ' ') part.erase(part.begin());
                        while (!part.empty() && part.back() == ' ') part.pop_back();
                        elem_types.push_back(std::move(part));
                    }
                }
            }
            for (std::size_t i = 0; i < dvs->bindings.size(); ++i) {
                const auto& [name, is_mut] = dvs->bindings[i];
                if (name != "_" && (max_line <= 0 || dvs->line <= max_line)) {
                    LocalBindingInfo info;
                    info.name = name;
                    info.is_mut = is_mut;
                    info.decl_line = dvs->line;
                    if (!(i < dvs->resolved_binding_types.size() &&
                          resolve_tid(dvs->resolved_binding_types[i], info.cf_type, info.cpp_type))) {
                        info.cf_type = (i < elem_types.size()) ? elem_types[i] : "auto";
                        info.cpp_type = cf_type_string_to_cpp(info.cf_type);
                    }
                    info.inferred_colon_eq = true;
                    if (dvs->line >= 1 && static_cast<std::size_t>(dvs->line) <= lines.size()) {
                        const std::string& ln = lines[static_cast<std::size_t>(dvs->line - 1)];
                        if (auto p = ln.find(name); p != std::string::npos) {
                            info.name_end_col = static_cast<int>(p + name.size());
                        }
                    }
                    env[info.name] = info;
                    ordered_out.push_back(info);
                }
            }
        } else if (const auto* ifs = std::get_if<IfStmt>(&s->node)) {
            int then_start = ifs->line;
            int then_end = get_block_end_line(ifs->then_body, lines, then_start);
            if (max_line <= 0 || (max_line >= then_start && max_line <= then_end)) {
                auto then_env = env;
                if (!ifs->unwrap_var.empty() && ifs->unwrap_var != "_" && ifs->unwrap_init) {
                    LocalBindingInfo ui;
                    ui.name = ifs->unwrap_var;
                    ui.is_mut = ifs->unwrap_mut;
                    ui.decl_line = ifs->line;
                    if (!resolve_tid(ifs->resolved_unwrap_type_id, ui.cf_type, ui.cpp_type)) {
                        std::string u_cf = infer_expr_cf_type(ifs->unwrap_init, ast, ws, env, sema_types);
                        while (u_cf.ends_with("!") || u_cf.ends_with("?")) u_cf.pop_back();
                        ui.cf_type = u_cf;
                        ui.cpp_type = cf_type_string_to_cpp(u_cf);
                    }
                    ui.inferred_colon_eq = true;
                    then_env[ui.name] = ui;
                }
                collect_stmt_bindings(ifs->then_body, ast, ws, lines, max_line, then_env, ordered_out, sema_types);
                if (max_line > 0) env = std::move(then_env);
            }
            int else_start = ifs->then_body.empty() ? ifs->line : then_end;
            int else_end = get_block_end_line(ifs->else_body, lines, else_start);
            if (max_line <= 0 || (max_line >= else_start && max_line <= else_end)) {
                auto else_env = env;
                collect_stmt_bindings(ifs->else_body, ast, ws, lines, max_line, else_env, ordered_out, sema_types);
                if (max_line > 0) env = std::move(else_env);
            }
        } else if (const auto* fs = std::get_if<ForStmt>(&s->node)) {
            int for_start = fs->line;
            int for_end = get_block_end_line(fs->body, lines, for_start);
            if (max_line <= 0 || (max_line >= for_start && max_line <= for_end)) {
                auto for_env = env;
                std::string idx_cf = "usize";
                std::string idx_cpp = "std::size_t";
                std::string item_cf = "auto";
                std::string item_cpp;
                bool item_mut = false;
                if (fs->range_end) {
                    if (!resolve_tid(fs->resolved_index_type_id, item_cf, item_cpp)) {
                        item_cf = "i64";
                        item_cpp = "std::int64_t";
                    }
                } else if (fs->is_unwrap) {
                    item_mut = fs->unwrap_mut;
                    if (!resolve_tid(fs->resolved_unwrap_type_id, item_cf, item_cpp) && fs->iter_expr) {
                        std::string raw_iter = infer_expr_cf_type(fs->iter_expr, ast, ws, env, sema_types);
                        while (raw_iter.ends_with("!") || raw_iter.ends_with("?")) raw_iter.pop_back();
                        if (raw_iter.starts_with("sync.Channel[") && raw_iter.ends_with("]")) {
                            raw_iter = raw_iter.substr(13, raw_iter.size() - 14);
                        } else if (raw_iter.starts_with("Channel[") && raw_iter.ends_with("]")) {
                            raw_iter = raw_iter.substr(8, raw_iter.size() - 9);
                        }
                        item_cf = raw_iter;
                    }
                } else {
                    bool has_idx = resolve_tid(fs->resolved_index_type_id, idx_cf, idx_cpp);
                    bool has_item = resolve_tid(fs->resolved_item_type_id, item_cf, item_cpp);
                    if ((!has_idx || !has_item) && fs->iter_expr) {
                        std::string raw_iter = infer_expr_cf_type(fs->iter_expr, ast, ws, env, sema_types);
                        std::string col_t = strip_wrappers(raw_iter);
                        std::string fallback_item = "auto";
                        if (col_t.starts_with("[]")) {
                            fallback_item = col_t.substr(2);
                        } else if (col_t.starts_with("map[")) {
                            int bdepth = 0;
                            std::size_t rb = std::string::npos;
                            for (std::size_t k = 3; k < col_t.size(); ++k) {
                                if (col_t[k] == '[') ++bdepth;
                                else if (col_t[k] == ']') {
                                    --bdepth;
                                    if (bdepth == 0) {
                                        rb = k;
                                        break;
                                    }
                                }
                            }
                            if (rb != std::string::npos && rb + 1 < col_t.size()) {
                                if (!has_idx) {
                                    idx_cf = col_t.substr(4, rb - 4);
                                    idx_cpp = cf_type_string_to_cpp(idx_cf);
                                }
                                fallback_item = col_t.substr(rb + 1);
                            }
                        } else if (col_t.starts_with("[")) {
                            if (auto rb = col_t.find(']'); rb != std::string::npos && rb + 1 < col_t.size()) {
                                fallback_item = col_t.substr(rb + 1);
                            }
                        } else if (col_t == "str") {
                            fallback_item = "u8";
                        } else if (auto lb = col_t.find('[');
                                   lb != std::string::npos && col_t.ends_with(']') &&
                                   (col_t.starts_with("collections.Set[") || col_t.starts_with("Set[") ||
                                    col_t.starts_with("collections.Deque[") || col_t.starts_with("Deque[") ||
                                    col_t.starts_with("sync.Channel[") || col_t.starts_with("Channel["))) {
                            fallback_item = col_t.substr(lb + 1, col_t.size() - lb - 2);
                        }
                        if (!has_item) item_cf = fallback_item;
                    }
                }
                if (item_cpp.empty()) item_cpp = cf_type_string_to_cpp(item_cf);
                if (!fs->index_var.empty() && fs->index_var != "_") {
                    LocalBindingInfo idx_info;
                    idx_info.name = fs->index_var;
                    idx_info.is_mut = false;
                    idx_info.decl_line = fs->line;
                    idx_info.cf_type = idx_cf;
                    idx_info.cpp_type = idx_cpp;
                    idx_info.inferred_colon_eq = false;
                    for_env[idx_info.name] = idx_info;
                }
                if (!fs->item_var.empty() && fs->item_var != "_") {
                    LocalBindingInfo item_info;
                    item_info.name = fs->item_var;
                    item_info.is_mut = item_mut;
                    item_info.decl_line = fs->line;
                    item_info.cf_type = item_cf;
                    item_info.cpp_type = item_cpp;
                    item_info.inferred_colon_eq = false;
                    for_env[item_info.name] = item_info;
                }
                collect_stmt_bindings(fs->body, ast, ws, lines, max_line, for_env, ordered_out, sema_types);
                if (max_line > 0) env = std::move(for_env);
            }
        } else if (const auto* def = std::get_if<DeferStmt>(&s->node)) {
            int def_start = def->line;
            int def_end = get_block_end_line(def->body, lines, def_start);
            if (max_line <= 0 || (max_line >= def_start && max_line <= def_end)) {
                auto def_env = env;
                collect_stmt_bindings(def->body, ast, ws, lines, max_line, def_env, ordered_out, sema_types);
                if (max_line > 0) env = std::move(def_env);
            }
        } else if (const auto* as = std::get_if<AsyncScopeStmt>(&s->node)) {
            int as_start = as->line;
            int as_end = get_block_end_line(as->body, lines, as_start);
            if (max_line <= 0 || (max_line >= as_start && max_line <= as_end)) {
                auto as_env = env;
                collect_stmt_bindings(as->body, ast, ws, lines, max_line, as_env, ordered_out, sema_types);
                if (max_line > 0) env = std::move(as_env);
            }
            if (!as->result_var.empty() && as->result_var != "_" && (max_line <= 0 || as->line <= max_line)) {
                std::string elem_cf;
                std::string elem_cpp;
                if (resolve_tid(as->resolved_result_elem_type_id, elem_cf, elem_cpp)) {
                    LocalBindingInfo info;
                    info.name = as->result_var;
                    info.is_mut = as->result_mut;
                    info.decl_line = as->line;
                    info.cf_type = "[]" + elem_cf;
                    info.cpp_type = elem_cpp.empty() ? cf_type_string_to_cpp(info.cf_type) : std::format("std::vector<{}>", elem_cpp);
                    info.inferred_colon_eq = true;
                    if (as->line >= 1 && static_cast<std::size_t>(as->line) <= lines.size()) {
                        const std::string& ln = lines[static_cast<std::size_t>(as->line - 1)];
                        if (auto p = ln.find(as->result_var); p != std::string::npos) {
                            info.name_end_col = static_cast<int>(p + as->result_var.size());
                        }
                    }
                    env[info.name] = info;
                    ordered_out.push_back(info);
                }
            }
        } else if (const auto* ms = std::get_if<MatchStmt>(&s->node)) {
            std::unordered_set<std::string> known_enum_variants;
            for (const auto& en : ast.enums) {
                for (const auto& v : en.variants) known_enum_variants.insert(v.name);
            }
            auto find_variant_payload_type = [&](std::string_view enum_prefix, std::string_view vname, std::size_t idx) -> std::string {
                auto scan_enums = [&](const std::vector<EnumDecl>& enums, std::string_view target_enum) -> std::string {
                    for (const auto& en : enums) {
                        if (!target_enum.empty() && target_enum != en.name) continue;
                        for (const auto& v : en.variants) {
                            if (v.name == vname && idx < v.payload_types.size()) {
                                return cfire::fmt::format_type_ref(v.payload_types[idx]);
                            }
                        }
                    }
                    return {};
                };
                if (auto dot = enum_prefix.rfind('.'); dot != std::string_view::npos) {
                    std::string_view pkg_alias = enum_prefix.substr(0, dot);
                    std::string_view ename = enum_prefix.substr(dot + 1);
                    for (const auto& pkg : ws.local_packages) {
                        if (pkg.alias == pkg_alias) {
                            if (auto res = scan_enums(pkg.ast.enums, ename); !res.empty()) return res;
                        }
                    }
                }
                if (auto res = scan_enums(ast.enums, enum_prefix); !res.empty()) return res;
                for (const auto& pkg : ws.local_packages) {
                    for (const auto& en : pkg.ast.enums) {
                        if (!enum_prefix.empty() && enum_prefix != en.name && enum_prefix != pkg.alias + "." + en.name) continue;
                        for (const auto& v : en.variants) {
                            if (v.name == vname && idx < v.payload_types.size()) {
                                return cfire::fmt::format_type_ref(v.payload_types[idx]);
                            }
                        }
                    }
                }
                return "auto";
            };
            std::string target_cf = ms->target ? infer_expr_cf_type(ms->target, ast, ws, env, sema_types) : "auto";
            for (const auto& arm : ms->arms) {
                int arm_start = arm.line;
                int arm_end = get_block_end_line(arm.body, lines, arm_start);
                if (max_line <= 0 || (max_line >= arm_start && max_line <= arm_end)) {
                    auto arm_env = env;
                    const auto pats = arm.patterns.empty()
                        ? std::vector<MatchPattern>{arm.to_pattern()}
                        : arm.patterns;
                    for (const auto& pat : pats) {
                        for (std::size_t bi_idx = 0; bi_idx < pat.bindings.size(); ++bi_idx) {
                            const auto& b = pat.bindings[bi_idx];
                            if (b.empty() || b == "_") continue;
                            LocalBindingInfo bi;
                            bi.name = b;
                            bi.decl_line = arm.line;
                            bi.cf_type = (pat.is_err || pat.variant_name == "Err")
                                ? "Error"
                                : find_variant_payload_type(pat.enum_prefix, pat.variant_name, bi_idx);
                            bi.cpp_type = cf_type_string_to_cpp(bi.cf_type);
                            arm_env[b] = bi;
                        }
                        if (pat.is_slice_pattern) {
                            std::string elem_cf = target_cf.starts_with("[]") ? target_cf.substr(2) : "auto";
                            auto add_slice_var = [&](const std::string& vname, const std::string& vtype) {
                                if (vname.empty() || vname == "_") return;
                                LocalBindingInfo bi;
                                bi.name = vname;
                                bi.decl_line = arm.line;
                                bi.cf_type = vtype;
                                bi.cpp_type = cf_type_string_to_cpp(vtype);
                                arm_env[vname] = bi;
                            };
                            for (const auto& hv : pat.slice_head) add_slice_var(hv, elem_cf);
                            add_slice_var(pat.slice_rest, target_cf);
                            for (const auto& tv : pat.slice_tail) add_slice_var(tv, elem_cf);
                        } else if (pat.enum_prefix.empty() && pat.literal_val.empty() &&
                                   !pat.is_range && !pat.is_some && !pat.is_none &&
                                   !pat.is_ok && !pat.is_err &&
                                   pat.bindings.empty() && !pat.variant_name.empty() &&
                                   pat.variant_name != "_" && !known_enum_variants.contains(pat.variant_name)) {
                            LocalBindingInfo bi;
                            bi.name = pat.variant_name;
                            bi.decl_line = arm.line;
                            bi.cf_type = target_cf;
                            bi.cpp_type = cf_type_string_to_cpp(target_cf);
                            arm_env[pat.variant_name] = bi;
                        }
                    }
                    collect_stmt_bindings(arm.body, ast, ws, lines, max_line, arm_env, ordered_out, sema_types);
                    if (max_line > 0) env = std::move(arm_env);
                }
            }
        } else if (const auto* es = std::get_if<ExprStmt>(&s->node)) {
            if (es->expr) {
                if (const auto* call = std::get_if<CallExpr>(&es->expr->node)) {
                    if (call->block_arg) {
                        int blk_start = es->line;
                        int blk_end = get_block_end_line(call->block_arg->body, lines, blk_start);
                        if (max_line <= 0 || (max_line >= blk_start && max_line <= blk_end)) {
                            auto blk_env = env;
                            for (std::size_t pi = 0; pi < call->block_arg->params.size(); ++pi) {
                                const auto& bp_name = call->block_arg->params[pi];
                                if (bp_name.empty() || bp_name == "_") continue;
                                LocalBindingInfo pinfo;
                                pinfo.name = bp_name;
                                pinfo.is_mut = false;
                                pinfo.is_param = true;
                                pinfo.intent = "in";
                                pinfo.decl_line = es->line;
                                if (!(pi < call->block_arg->resolved_param_types.size() &&
                                      resolve_tid(call->block_arg->resolved_param_types[pi], pinfo.cf_type, pinfo.cpp_type))) {
                                    if (pi < call->type_args.size()) {
                                        pinfo.cf_type = cfire::fmt::format_type_ref(call->type_args[pi]);
                                    } else if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node)) {
                                        if (mem->field == "on" || mem->field == "bind_raw") {
                                            pinfo.cf_type = "str";
                                        } else {
                                            pinfo.cf_type = "auto";
                                        }
                                    } else {
                                        pinfo.cf_type = "auto";
                                    }
                                    pinfo.cpp_type = cf_type_string_to_cpp(pinfo.cf_type);
                                }
                                pinfo.inferred_colon_eq = false;
                                blk_env[pinfo.name] = pinfo;
                            }
                            collect_stmt_bindings(call->block_arg->body, ast, ws, lines, max_line, blk_env, ordered_out, sema_types);
                            if (max_line > 0) env = std::move(blk_env);
                        }
                    }
                } else if (const auto* or_ex = std::get_if<OrExpr>(&es->expr->node); or_ex && !or_ex->handler_body.empty()) {
                    int or_start = es->line;
                    int or_end = get_block_end_line(or_ex->handler_body, lines, or_start);
                    if (max_line <= 0 || (max_line >= or_start && max_line <= or_end)) {
                        auto or_env = env;
                        if (!or_ex->err_var.empty() && or_ex->err_var != "_") {
                            LocalBindingInfo ei;
                            ei.name = or_ex->err_var;
                            ei.cf_type = "Error";
                            ei.cpp_type = "cfire::Error";
                            ei.decl_line = es->line;
                            or_env[ei.name] = ei;
                        }
                        collect_stmt_bindings(or_ex->handler_body, ast, ws, lines, max_line, or_env, ordered_out, sema_types);
                        if (max_line > 0) env = std::move(or_env);
                    }
                }
            }
        }
    }
}

CFIRE_LSP_RPC_INLINE void collect_stmt_bindings(
    const std::vector<cfire::frontend::StmtPtr>& stmts,
    const cfire::frontend::FileAST& ast,
    const WorkspaceContext& ws,
    const std::vector<std::string>& lines,
    std::unordered_map<std::string, LocalBindingInfo>& env,
    std::vector<LocalBindingInfo>& ordered_out,
    const cfire::frontend::TypeTable* sema_types
) {
    collect_stmt_bindings(stmts, ast, ws, lines, 0, env, ordered_out, sema_types);
}

CFIRE_LSP_RPC_INLINE void walk_expr_calls(const cfire::frontend::ExprPtr& expr, const std::function<void(const cfire::frontend::CallExpr&)>& visitor) {
    walk_exprs(expr, [&](const cfire::frontend::ExprPtr& ex) {
        if (const auto* c = std::get_if<cfire::frontend::CallExpr>(&ex->node)) {
            visitor(*c);
        }
    });
}

CFIRE_LSP_RPC_INLINE void walk_stmts_calls(const std::vector<cfire::frontend::StmtPtr>& stmts, const std::function<void(const cfire::frontend::CallExpr&)>& visitor) {
    walk_stmt_exprs(stmts, [&](const cfire::frontend::ExprPtr& ex) {
        if (const auto* c = std::get_if<cfire::frontend::CallExpr>(&ex->node)) {
            visitor(*c);
        }
    });
}

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto build_scope_bindings(
    const cfire::frontend::FileAST& ast,
    const WorkspaceContext& ws,
    const std::vector<std::string>& lines,
    int cursor_zero_based_line,
    const cfire::frontend::TypeTable* sema_types
) -> std::unordered_map<std::string, LocalBindingInfo> {
    std::unordered_map<std::string, LocalBindingInfo> env;
    std::vector<LocalBindingInfo> dummy;
    int one_based = cursor_zero_based_line + 1;

    for (const auto& fn : ast.functions) {
        if (!fn.source_path.empty() && !ast.path.empty() && fn.source_path != ast.path) continue;
        int fn_end = get_block_end_line(fn.body, lines, fn.line);
        if (one_based >= fn.line && one_based <= fn_end) {
            env.clear();
            if (fn.receiver) {
                LocalBindingInfo r;
                r.name = fn.receiver->name;
                r.cf_type = cfire::fmt::format_type_ref(fn.receiver->type);
                r.cpp_type = lower_type_preview(fn.receiver->type, true, fn.receiver->intent);
                r.is_mut = (fn.receiver->intent == "mut");
                r.is_param = true;
                r.intent = fn.receiver->intent;
                r.decl_line = fn.line;
                env[r.name] = r;
            }
            for (const auto& p : fn.params) {
                LocalBindingInfo pi;
                pi.name = p.name;
                pi.cf_type = cfire::fmt::format_type_ref(p.type);
                pi.cpp_type = lower_type_preview(p.type, true, p.intent);
                pi.is_mut = (p.intent == "mut" || p.intent == "out");
                pi.is_param = true;
                pi.intent = p.intent;
                pi.decl_line = p.line;
                env[pi.name] = pi;
            }
            collect_stmt_bindings(fn.body, ast, ws, lines, one_based, env, dummy, sema_types);
            return env;
        }
    }
    for (const auto& t : ast.tests) {
        if (!t.source_path.empty() && !ast.path.empty() && t.source_path != ast.path) continue;
        int t_end = get_block_end_line(t.body, lines, t.line);
        if (one_based >= t.line && one_based <= t_end) {
            env.clear();
            for (const auto& p : t.params) {
                LocalBindingInfo pi;
                pi.name = p.name;
                pi.cf_type = cfire::fmt::format_type_ref(p.type);
                pi.cpp_type = lower_type_preview(p.type, true, p.intent);
                pi.is_mut = false;
                pi.is_param = true;
                pi.intent = p.intent;
                pi.decl_line = p.line;
                env[pi.name] = pi;
            }
            for (const auto& bv : t.bench_variations) {
                LocalBindingInfo bi;
                bi.name = bv.name;
                bi.cf_type = cfire::fmt::format_type_ref(bv.inferred_type);
                bi.cpp_type = lower_type_preview(bv.inferred_type, true, "in");
                bi.is_mut = false;
                bi.is_param = true;
                bi.intent = "in";
                bi.decl_line = bv.line;
                env[bi.name] = bi;
            }
            collect_stmt_bindings(t.body, ast, ws, lines, one_based, env, dummy, sema_types);
            return env;
        }
    }
    return env;
}

CFIRE_LSP_RPC_INLINE void write_rpc_message(std::ostream& out, std::string_view body) {
    out << "Content-Length: " << body.size() << "\r\n\r\n" << body;
    out.flush();
}

} // namespace detail


#endif // !defined(CFIRE_SPLIT_TUS) || defined(CFIRE_LSP_SERVER_IMPL)

} // namespace cfire::lsp

#undef CFIRE_LSP_RPC_INLINE
