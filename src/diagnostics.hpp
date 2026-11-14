#pragma once
// src/diagnostics.hpp — Structured Diagnostics, 1-Based Source Locations & Auto-Fix Synthesizer

#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <format>
#include <cctype>
#include <algorithm>
#include <filesystem>
#include <unordered_set>
#include <unordered_map>
#include "cfire_prelude.hpp"
#include "ast.hpp"
#include "docgen/catalog_types.hpp"

namespace cfire::diag {

struct FixItEdit {
    std::string title{};
    int start_line = 1; // 1-based
    int start_col = 1;  // 1-based
    int end_line = 1;   // 1-based
    int end_col = 1;    // 1-based
    std::string replacement{};
    bool is_preferred = true;
};

struct StructuredDiagnostic {
    std::string file{};
    int line = 1;       // 1-based
    int col = 1;        // 1-based
    int end_line = 1;   // 1-based
    int end_col = 1;    // 1-based
    std::string severity{}; // "error" | "warning" | "info"
    std::string code{};     // e.g. "cfire/MUT-CALLSITE"
    std::string message{};  // clean message without redundant "<file>:<line>:<col>: " prefix
    std::string source{"cfire"};
    std::vector<FixItEdit> fixits{};
    // Q24 (CF-149): structured facts recorded where the problem is detected (an identifier, a keyword, a
    // replacement token, ...). Fix-it builders read these instead of re-parsing the message text.
    std::unordered_map<std::string, std::string> facts{};
};

using Diagnostic = StructuredDiagnostic;

// Q24 (CF-149): every compiler diagnostic is constructed here at the point of detection. `col == 0` means
// "only the line is known": resolve_diagnostic_range() then spans the statement on that line.
[[nodiscard]] inline auto diag(std::string_view file, int line, int col, std::string_view code, std::string message,
                               std::unordered_map<std::string, std::string> facts = {}) -> StructuredDiagnostic {
    StructuredDiagnostic d;
    d.file = std::string(file);
    d.line = line > 0 ? line : 1;
    d.col = col;
    d.end_line = d.line;
    d.end_col = col;
    d.severity = "error";
    d.code = code.empty() ? std::string("cfire/SEMA-ERROR") : std::string(code);
    d.message = std::move(message);
    d.facts = std::move(facts);
    return d;
}

// Structured lexer/parser error -> diagnostic (field copy, no text parsing).
[[nodiscard]] inline auto from_source_error(const cfire::frontend::SourceError& e) -> StructuredDiagnostic {
    auto d = diag(e.file, e.line, e.col, e.code, e.message);
    d.severity = "error";
    return d;
}

// Canonical single-line text form: `file:line:col: [warning: ]message (code)` (the code appears exactly once).
[[nodiscard]] inline auto render_diagnostic(const StructuredDiagnostic& d) -> std::string {
    std::string_view msg = d.message;
    bool has_code = !d.code.empty() && msg.ends_with("(" + d.code + ")");
    return std::format("{}:{}:{}: {}{}{}", d.file, d.line, d.col > 0 ? d.col : 1,
        d.severity == "warning" ? "warning: " : (d.severity == "info" ? "note: " : ""),
        msg, has_code || d.code.empty() ? std::string() : std::format(" ({})", d.code));
}

// Exact source range for `d` given the file's lines: the token starting at `col` (identifier, number,
// string literal, or operator run), or the whole statement on the line when only the line is known.
inline void resolve_diagnostic_range(StructuredDiagnostic& d, const std::vector<std::string>& lines) {
    if (d.line < 1 || static_cast<std::size_t>(d.line - 1) >= lines.size()) {
        if (d.col < 1) d.col = 1;
        if (d.end_col < d.col) d.end_col = d.col;
        return;
    }
    const std::string& ln = lines[static_cast<std::size_t>(d.line - 1)];
    auto is_ident = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
    if (d.col < 1 || static_cast<std::size_t>(d.col - 1) >= ln.size()) {
        std::size_t b = ln.find_first_not_of(" \t");
        std::size_t e = ln.find_last_not_of(" \t\r");
        if (b == std::string::npos) { d.col = 1; d.end_col = 1; return; }
        if (auto cmt = ln.find("//", b); cmt != std::string::npos && cmt > b) e = ln.find_last_not_of(" \t", cmt - 1);
        d.col = static_cast<int>(b) + 1;
        d.end_col = static_cast<int>(e) + 2;
        d.end_line = d.line;
        return;
    }
    std::size_t i = static_cast<std::size_t>(d.col - 1);
    std::size_t j = i;
    if (ln[i] == '"') {
        j = i + 1;
        while (j < ln.size() && ln[j] != '"') j += (ln[j] == '\\' ? 2 : 1);
        j = std::min(ln.size(), j + 1);
    } else if (is_ident(ln[i])) {
        while (j < ln.size() && (is_ident(ln[j]) || ln[j] == '.')) ++j;
        while (j > i + 1 && ln[j - 1] == '.') --j;
    } else {
        while (j < ln.size() && std::string_view("+-*/%=<>!&|^~?:").find(ln[j]) != std::string_view::npos) ++j;
        if (j == i) j = i + 1;
    }
    d.end_line = d.line;
    d.end_col = static_cast<int>(j) + 1;
}

[[nodiscard]] inline auto json_escape(std::string_view s) -> std::string {
    return cfire::detail::json_escape(s);
}

// The only text-to-diagnostic parser left (Q24): it reads *clang's* output for generated C++, which is
// external text. cfire's own lexer, parser and Sema diagnostics are structured from the start.
[[nodiscard]] inline auto parse_clang_diagnostic_line(
    std::string_view err_str,
    std::string_view default_file = "",
    std::string_view default_severity = "error",
    std::string_view default_code = "cfire/ERROR"
) -> StructuredDiagnostic {
    StructuredDiagnostic d;
    d.file = std::string(default_file);
    d.severity = std::string(default_severity);
    d.code = std::string(default_code);
    d.line = 1;
    d.col = 1;
    d.end_line = 1;
    d.end_col = 1;
    d.source = "cfire";

    std::size_t c1 = std::string_view::npos;
    if (!default_file.empty() && err_str.starts_with(default_file)) {
        c1 = err_str.find(':', default_file.size());
    } else {
        c1 = err_str.find(':');
    }

    if (c1 != std::string_view::npos && c1 > 0) {
        auto c2 = err_str.find(':', c1 + 1);
        if (c2 != std::string_view::npos) {
            auto c3 = err_str.find(':', c2 + 1);
            if (c3 != std::string_view::npos) {
                auto parsed_line = cfire::detail::parse_int<int>(err_str.substr(c1 + 1, c2 - c1 - 1));
                auto parsed_col = cfire::detail::parse_int<int>(err_str.substr(c2 + 1, c3 - c2 - 1));
                if (parsed_line && *parsed_line > 0 && parsed_col && *parsed_col > 0) {
                    d.file = std::string(err_str.substr(0, c1));
                    d.line = *parsed_line;
                    d.col = *parsed_col;
                    d.end_line = *parsed_line;
                    d.end_col = *parsed_col + 1;

                    std::string_view rest = err_str.substr(c3 + 1);
                    while (!rest.empty() && std::isspace(static_cast<unsigned char>(rest.front()))) {
                        rest.remove_prefix(1);
                    }
                    if (rest.starts_with("warning: ")) {
                        d.severity = "warning";
                        rest.remove_prefix(9);
                    } else if (rest.starts_with("error: ")) {
                        d.severity = "error";
                        rest.remove_prefix(7);
                    }
                    d.message = std::string(rest);
                } else {
                    d.message = std::string(err_str);
                }
            } else {
                d.message = std::string(err_str);
            }
        } else {
            d.message = std::string(err_str);
        }
    } else {
        d.message = std::string(err_str);
    }

    // Extract diagnostic code suffix (e.g. `(cfire/MUT-CALLSITE)`)
    if (auto p = d.message.find("cfire/"); p != std::string::npos) {
        std::size_t e = p + 6;
        while (e < d.message.size() && (std::isalnum(static_cast<unsigned char>(d.message[e])) || d.message[e] == '-')) {
            ++e;
        }
        d.code = d.message.substr(p, e - p);
    }

    return d;
}

[[nodiscard]] inline auto split_lines(std::string_view text) -> std::vector<std::string> {
    std::vector<std::string> lines;
    std::string cur;
    for (char c : text) {
        if (c == '\n') {
            lines.push_back(std::move(cur));
            cur.clear();
        } else if (c != '\r') {
            cur += c;
        }
    }
    lines.push_back(std::move(cur));
    return lines;
}

// Structured fact recorded by the diagnostic's producer ("" when absent).
[[nodiscard]] inline auto fact_of(const StructuredDiagnostic& d, std::string_view key) -> std::string {
    if (auto it = d.facts.find(std::string(key)); it != d.facts.end()) return it->second;
    return "";
}

// W4 (Q16) rewrite helpers. They work on one source line and give up (no fix-it) on anything they cannot
// parse with certainty, such as a call whose arguments continue on the next line.
namespace rewrite_detail {
[[nodiscard]] inline auto trim_ws(std::string_view v) -> std::string_view {
    while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.remove_prefix(1);
    while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) v.remove_suffix(1);
    return v;
}
// Splits `ln[open+1 ..]` into top-level comma-separated arguments up to the matching ')'. Returns the index of
// that ')' or npos. String literals and nested brackets are skipped.
[[nodiscard]] inline auto split_call_args(const std::string& ln, std::size_t open, std::vector<std::string>& out) -> std::size_t {
    int depth = 0;
    std::size_t arg_start = open + 1;
    for (std::size_t i = open + 1; i < ln.size(); ++i) {
        char c = ln[i];
        if (c == '"') {
            for (++i; i < ln.size() && ln[i] != '"'; ++i) if (ln[i] == '\\') ++i;
            continue;
        }
        if (c == '(' || c == '[' || c == '{') ++depth;
        else if (c == ')' || c == ']' || c == '}') {
            if (depth == 0) {
                if (c != ')') return std::string::npos;
                std::string_view last = trim_ws(std::string_view(ln).substr(arg_start, i - arg_start));
                if (!last.empty() || !out.empty()) out.emplace_back(last);
                return i;
            }
            --depth;
        } else if (c == ',' && depth == 0) {
            out.emplace_back(trim_ws(std::string_view(ln).substr(arg_start, i - arg_start)));
            arg_start = i + 1;
        }
    }
    return std::string::npos;
}
// A receiver that can take `.method()` without parentheses: identifiers, field chains, calls, indexing and literals.
[[nodiscard]] inline auto is_postfix_operand(std::string_view r) -> bool {
    if (r.empty()) return false;
    if (r.front() == '"' && r.back() == '"') return true;
    int depth = 0;
    for (char c : r) {
        if (c == '(' || c == '[' || c == '{') { ++depth; continue; }
        if (c == ')' || c == ']' || c == '}') { --depth; continue; }
        if (depth > 0) continue;
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.')) return false;
    }
    // A bare integer literal would lex `3.abs()` as a float; parenthesize it.
    return !std::isdigit(static_cast<unsigned char>(r.front()));
}
// Start of the postfix receiver expression that ends right before `end` (scanning back over a.b(c)[d]).
[[nodiscard]] inline auto receiver_start(const std::string& ln, std::size_t end) -> std::size_t {
    std::size_t i = end;
    while (i > 0) {
        char c = ln[i - 1];
        if (c == ')' || c == ']') {
            char open = c == ')' ? '(' : '[';
            int depth = 0;
            std::size_t j = i;
            while (j > 0) {
                char d = ln[j - 1];
                if (d == c) ++depth;
                else if (d == open && --depth == 0) break;
                --j;
            }
            if (j == 0) return std::string::npos;
            i = j - 1;
            continue;
        }
        if (c == '"') {
            std::size_t j = i - 1;
            while (j > 0 && ln[j - 1] != '"') --j;
            if (j == 0) return std::string::npos;
            i = j - 1;
            continue;
        }
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.') { --i; continue; }
        break;
    }
    return i < end ? i : std::string::npos;
}
} // namespace rewrite_detail

// Q24 (CF-149): fix-it builders, keyed by diagnostic code. They use the producer's structured facts plus the
// source lines; they never parse the message text.
inline void synthesize_fixits_for_diagnostic(
    StructuredDiagnostic& d,
    const cfire::frontend::FileAST* ast,
    const std::vector<std::string>& lines,
    const std::vector<const cfire::frontend::EnumDecl*>& extra_enums = {}
) {
    if (d.code == "cfire/MUT-CALLSITE") {
        FixItEdit edit;
        edit.title = "Insert 'mut ' at call site (cfire/MUT-CALLSITE)";
        edit.start_line = d.line;
        edit.start_col = d.col;
        edit.end_line = d.line;
        edit.end_col = d.col;
        edit.replacement = "mut ";
        d.fixits.push_back(std::move(edit));
        return;
    }

    if (d.code == "cfire/OUT-CALLSITE") {
        FixItEdit edit;
        edit.title = "Insert 'out ' at call site (cfire/OUT-CALLSITE)";
        edit.start_line = d.line;
        edit.start_col = d.col;
        edit.end_line = d.line;
        edit.end_col = d.col;
        edit.replacement = "out ";
        d.fixits.push_back(std::move(edit));
        return;
    }

    if (d.code == "cfire/MOVE-CALLSITE") {
        FixItEdit edit;
        edit.title = "Insert 'move ' at call site (cfire/MOVE-CALLSITE)";
        edit.start_line = d.line;
        edit.start_col = d.col;
        edit.end_line = d.line;
        edit.end_col = d.col;
        edit.replacement = "move ";
        d.fixits.push_back(std::move(edit));
        return;
    }

    if (d.code == "cfire/REDUNDANT-TRY") {
        if (d.line >= 1 && static_cast<std::size_t>(d.line - 1) < lines.size()) {
            const std::string& cur_ln = lines[static_cast<std::size_t>(d.line - 1)];
            std::size_t search_from = (d.col > 1 && static_cast<std::size_t>(d.col - 1) < cur_ln.size())
                ? static_cast<std::size_t>(d.col - 1)
                : 0;
            if (auto pos = cur_ln.find("try ", search_from); pos != std::string::npos) {
                FixItEdit edit;
                edit.title = "Remove redundant 'try ' (cfire/REDUNDANT-TRY)";
                edit.start_line = d.line;
                edit.start_col = static_cast<int>(pos) + 1;
                edit.end_line = d.line;
                edit.end_col = static_cast<int>(pos) + 5;
                edit.replacement = "";
                edit.is_preferred = true;
                d.fixits.push_back(std::move(edit));
            }
        }
        return;
    }

    if (d.code == "cfire/REDUNDANT-CALLSITE-INTENT") {
        if (d.line >= 1 && static_cast<std::size_t>(d.line - 1) < lines.size()) {
            const std::string& cur_ln = lines[static_cast<std::size_t>(d.line - 1)];
            std::size_t search_from = (d.col > 1 && static_cast<std::size_t>(d.col - 1) < cur_ln.size())
                ? static_cast<std::size_t>(d.col - 1)
                : 0;
            std::string kw = (fact_of(d, "keyword").empty() ? std::string("mut") : fact_of(d, "keyword")) + " ";
            if (auto pos = cur_ln.find(kw, search_from); pos != std::string::npos) {
                FixItEdit edit;
                edit.title = std::format("Remove redundant '{}' at call site (cfire/REDUNDANT-CALLSITE-INTENT)", kw);
                edit.start_line = d.line;
                edit.start_col = static_cast<int>(pos) + 1;
                edit.end_line = d.line;
                edit.end_col = static_cast<int>(pos + kw.size()) + 1;
                edit.replacement = "";
                edit.is_preferred = true;
                d.fixits.push_back(std::move(edit));
            }
        }
        return;
    }

    if (d.code == "cfire/UNHANDLED-RESULT") {
        if (d.line > 0 && d.col > 0) {
            FixItEdit edit;
            edit.title = "Add 'try' before fallible call (cfire/UNHANDLED-RESULT)";
            edit.start_line = d.line;
            edit.start_col = d.col;
            edit.end_line = d.line;
            edit.end_col = d.col;
            edit.replacement = "try ";
            edit.is_preferred = true;
            d.fixits.push_back(std::move(edit));
        }
        return;
    }

    if (d.code == "cfire/PREFER-ENUM") {
        std::string pfx;
        pfx = fact_of(d, "prefix");
        FixItEdit edit;
        edit.title = pfx.empty()
            ? "Convert constants to enum (cfire/PREFER-ENUM)"
            : std::format("Convert '{}_*' constants to enum (cfire/PREFER-ENUM)", pfx);
        edit.start_line = d.line;
        edit.start_col = 1;
        edit.end_line = d.line;
        edit.end_col = 1;
        std::string enum_name = pfx;
        if (!enum_name.empty()) {
            for (char& c : enum_name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            enum_name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(enum_name[0])));
        } else {
            enum_name = "State";
        }
        edit.replacement = std::format("// Suggested refactoring:\n// type {} enum {{\n//     ...\n// }}\n", enum_name);
        edit.is_preferred = true;
        d.fixits.push_back(std::move(edit));
        return;
    }

    if (d.code == "cfire/MISSING-IMPORT") {
        std::string mod_path;
        mod_path = fact_of(d, "module");
        if (!mod_path.empty()) {
            int insert_line = 1;
            for (std::size_t i = 0; i < lines.size(); ++i) {
                std::string_view l = lines[i];
                while (!l.empty() && std::isspace(static_cast<unsigned char>(l.front()))) l.remove_prefix(1);
                if (l.starts_with("package ")) {
                    insert_line = static_cast<int>(i) + 2;
                    break;
                }
            }
            FixItEdit edit;
            edit.title = std::format("Import missing module '{}' (cfire/MISSING-IMPORT)", mod_path);
            edit.start_line = insert_line;
            edit.start_col = 1;
            edit.end_line = insert_line;
            edit.end_col = 1;
            edit.replacement = std::format("import \"{}\"\n", mod_path);
            edit.is_preferred = true;
            d.fixits.push_back(std::move(edit));
        }
        return;
    }

    if (d.code == "cfire/INVALID-IMPORT") {
        std::string old_imp;
        std::string new_imp;
        old_imp = fact_of(d, "from");
        new_imp = fact_of(d, "to");
        if (!old_imp.empty() && !new_imp.empty() && d.line >= 1 && static_cast<std::size_t>(d.line - 1) < lines.size()) {
            const std::string& cur_ln = lines[static_cast<std::size_t>(d.line - 1)];
            std::string target_str = "\"" + old_imp + "\"";
            if (auto pos = cur_ln.find(target_str); pos != std::string::npos) {
                FixItEdit edit;
                edit.title = std::format("Replace '{}' with '{}' (cfire/INVALID-IMPORT)", old_imp, new_imp);
                edit.start_line = d.line;
                edit.start_col = static_cast<int>(pos) + 1;
                edit.end_line = d.line;
                edit.end_col = static_cast<int>(pos + target_str.size()) + 1;
                edit.replacement = "\"" + new_imp + "\"";
                edit.is_preferred = true;
                d.fixits.push_back(std::move(edit));
                return;
            }
        }
    }

    if (d.code == "cfire/UNKNOWN-METHOD" && d.line >= 1 && static_cast<std::size_t>(d.line - 1) < lines.size()) {
        // `.push_back(` -> `.push(` when the producer knows the canonical replacement (CF-143).
        std::string method = fact_of(d, "method");
        std::string repl = fact_of(d, "replacement");
        if (!method.empty() && !repl.empty()) {
            std::string from = "." + method + "(";
            std::string to = "." + repl + "(";
            const std::string& cur_ln = lines[static_cast<std::size_t>(d.line - 1)];
            std::size_t from_col = d.col > 0 ? static_cast<std::size_t>(d.col - 1) : 0;
            auto pos = cur_ln.find(from, from_col);
            if (pos == std::string::npos) pos = cur_ln.find(from);
            if (pos != std::string::npos) {
                FixItEdit edit;
                edit.title = std::format("Replace '{}' with '{}' (cfire/UNKNOWN-METHOD)", from, to);
                edit.start_line = d.line;
                edit.start_col = static_cast<int>(pos) + 1;
                edit.end_line = d.line;
                edit.end_col = static_cast<int>(pos + from.size()) + 1;
                edit.replacement = to;
                edit.is_preferred = true;
                d.fixits.push_back(std::move(edit));
            }
        }
        return;
    }

    // W4: `cfire/RETIRED-API` marks the 0.16 breaking-change migration (Q16); its rewrites share the
    // DEPRECATED-API builders below, so `cfire fix --only cfire/RETIRED-API` applies exactly the migration.
    if ((d.code == "cfire/DEPRECATED-API" || d.code == "cfire/RETIRED-API") && d.line >= 1 && static_cast<std::size_t>(d.line - 1) < lines.size()) {
        const std::string& ln = lines[static_cast<std::size_t>(d.line - 1)];
        const std::string kind = fact_of(d, "rewrite");
        if (kind == "call_to_method") {
            // `str.trim(s)` -> `s.trim()`, `sort.slice(mut xs)` -> `xs.sort()`, `math.clamp_i64(v, 0, 9)` -> `v.clamp(0, 9)`.
            std::string from = fact_of(d, "from") + "(";
            std::size_t from_col = d.col > 0 ? static_cast<std::size_t>(d.col - 1) : 0;
            std::size_t pos = ln.find(from, from_col);
            if (pos == std::string::npos) pos = ln.find(from);
            std::vector<std::string> args;
            std::size_t close = pos == std::string::npos ? pos : rewrite_detail::split_call_args(ln, pos + from.size() - 1, args);
            if (close != std::string::npos && !args.empty()) {
                std::string recv = args[0];
                for (std::string_view pfx : {"mut ", "&mut ", "&"}) {
                    if (recv.starts_with(pfx)) { recv = recv.substr(pfx.size()); break; }
                }
                if (!rewrite_detail::is_postfix_operand(recv)) recv = "(" + recv + ")";
                std::string rest;
                for (std::size_t i = 1; i < args.size(); ++i) rest += (i > 1 ? ", " : "") + args[i];
                FixItEdit edit;
                edit.replacement = std::format("{}.{}({})", recv, fact_of(d, "method"), rest);
                edit.title = std::format("Rewrite as '{}' ({})", edit.replacement, d.code);
                edit.start_line = d.line;
                edit.start_col = static_cast<int>(pos) + 1;
                edit.end_line = d.line;
                edit.end_col = static_cast<int>(close) + 2;
                d.fixits.push_back(std::move(edit));
            }
            return;
        }
        if (kind == "make_bracket" || kind == "ctor_to_make") {
            // `make[own[P]](v)` -> `make(own[P], v)`; `http.make_router()` -> `make(http.Router)`;
            // `sync.make_channel[i32](8)` -> `make(sync.Channel[i32], 8)`.
            const bool bracket = kind == "make_bracket";
            std::string head = bracket ? std::string("make[") : fact_of(d, "from");
            std::size_t from_col = d.col > 0 ? static_cast<std::size_t>(d.col - 1) : 0;
            std::size_t pos = ln.find(head, from_col);
            if (pos == std::string::npos) pos = ln.find(head);
            if (pos == std::string::npos) return;
            std::size_t i = pos + head.size();
            std::string type_text = bracket ? std::string() : fact_of(d, "to_type");
            if (bracket || (i < ln.size() && ln[i] == '[')) {
                std::size_t open = bracket ? i - 1 : i;
                int depth = 0;
                std::size_t close = std::string::npos;
                for (std::size_t k = open; k < ln.size(); ++k) {
                    if (ln[k] == '[') ++depth;
                    else if (ln[k] == ']' && --depth == 0) { close = k; break; }
                }
                if (close == std::string::npos) return;
                std::string inner = ln.substr(open + 1, close - open - 1);
                if (bracket) type_text = inner;
                else if (type_text.find('[') == std::string::npos) type_text += "[" + inner + "]";
                i = close + 1;
            }
            if (i >= ln.size() || ln[i] != '(') return;
            std::vector<std::string> args;
            std::size_t close = rewrite_detail::split_call_args(ln, i, args);
            if (close == std::string::npos || type_text.empty()) return;
            std::string rep = "make(" + type_text;
            for (const auto& a : args) rep += ", " + a;
            rep += ")";
            FixItEdit edit;
            edit.replacement = rep;
            edit.title = std::format("Rewrite as '{}' ({})", rep, d.code);
            edit.start_line = d.line;
            edit.start_col = static_cast<int>(pos) + 1;
            edit.end_line = d.line;
            edit.end_col = static_cast<int>(close) + 2;
            d.fixits.push_back(std::move(edit));
            return;
        }
        if (kind == "tls_server") {
            std::string from = fact_of(d, "from") + "(";
            std::size_t from_col = d.col > 0 ? static_cast<std::size_t>(d.col - 1) : 0;
            std::size_t pos = ln.find(from, from_col);
            if (pos == std::string::npos) pos = ln.find(from);
            std::vector<std::string> args;
            std::size_t close = pos == std::string::npos ? pos : rewrite_detail::split_call_args(ln, pos + from.size() - 1, args);
            if (close == std::string::npos || args.size() != 3) return;
            const std::string ns = fact_of(d, "ns");
            const bool pem = !fact_of(d, "pem").empty();
            FixItEdit edit;
            edit.replacement = std::format("make({0}.Server, {1}, tls: {0}.TlsConfig{{{2}: {3}, {4}: {5}}})", ns, args[0],
                pem ? "cert_pem" : "cert_file", args[1], pem ? "key_pem" : "key_file", args[2]);
            edit.title = std::format("Rewrite as '{}' ({})", edit.replacement, d.code);
            edit.start_line = d.line;
            edit.start_col = static_cast<int>(pos) + 1;
            edit.end_line = d.line;
            edit.end_col = static_cast<int>(close) + 2;
            d.fixits.push_back(std::move(edit));
            return;
        }
        if (kind == "swap_to_fn" || kind == "append_verify_off") {
            std::string from = fact_of(d, "from") + "(";
            std::size_t from_col = d.col > 0 ? static_cast<std::size_t>(d.col - 1) : 0;
            std::size_t pos = ln.find(from, from_col);
            if (pos == std::string::npos) pos = ln.find(from);
            std::vector<std::string> args;
            std::size_t close = pos == std::string::npos ? pos : rewrite_detail::split_call_args(ln, pos + from.size() - 1, args);
            if (close == std::string::npos) return;
            std::string inner;
            if (kind == "swap_to_fn") {
                if (args.empty() || args.size() > 2) return;
                inner = (args.size() == 2 ? args[1] : std::string("0")) + ", " + args[0];
            } else {
                for (const auto& a : args) inner += a + ", ";
                inner += "verify_peer: false";
            }
            FixItEdit edit;
            edit.replacement = std::format("{}({})", fact_of(d, "to"), inner);
            edit.title = std::format("Rewrite as '{}' ({})", edit.replacement, d.code);
            edit.start_line = d.line;
            edit.start_col = static_cast<int>(pos) + 1;
            edit.end_line = d.line;
            edit.end_col = static_cast<int>(close) + 2;
            d.fixits.push_back(std::move(edit));
            return;
        }
        if (kind == "len_call") {
            // `hosts.len` -> `hosts.len()`; skip occurrences already followed by `(` or an identifier char.
            std::size_t from_col = d.col > 0 ? static_cast<std::size_t>(d.col - 1) : 0;
            for (std::size_t pos = ln.find(".len", from_col); pos != std::string::npos; pos = ln.find(".len", pos + 1)) {
                std::size_t after = pos + 4;
                if (after < ln.size() && (ln[after] == '(' || std::isalnum(static_cast<unsigned char>(ln[after])) || ln[after] == '_')) continue;
                FixItEdit edit;
                edit.title = "Call '.len()' (cfire/RETIRED-API)";
                edit.start_line = d.line;
                edit.start_col = static_cast<int>(after) + 1;
                edit.end_line = d.line;
                edit.end_col = static_cast<int>(after) + 1;
                edit.replacement = "()";
                d.fixits.push_back(std::move(edit));
                break;
            }
            return;
        }
        if (kind == "delete_line") {
            // A retired `import "core:sort"` line: remove it together with its newline.
            if (ln.find("\"" + fact_of(d, "from") + "\"") != std::string::npos) {
                FixItEdit edit;
                edit.title = std::format("Remove retired import '{}' (cfire/RETIRED-API)", fact_of(d, "from"));
                edit.start_line = d.line;
                edit.start_col = 1;
                edit.end_line = d.line + 1;
                edit.end_col = 1;
                d.fixits.push_back(std::move(edit));
            }
            return;
        }
        if (kind == "method_to_assoc") {
            // `s.parse_i32()` -> `i32.parse(s)`.
            std::string needle = "." + fact_of(d, "method") + "()";
            std::size_t from_col = d.col > 0 ? static_cast<std::size_t>(d.col - 1) : 0;
            std::size_t dot = ln.find(needle, from_col);
            if (dot == std::string::npos) dot = ln.find(needle);
            std::size_t start = dot == std::string::npos ? dot : rewrite_detail::receiver_start(ln, dot);
            if (start != std::string::npos) {
                FixItEdit edit;
                edit.replacement = std::format("{}({})", fact_of(d, "to"), ln.substr(start, dot - start));
                edit.title = std::format("Rewrite as '{}' ({})", edit.replacement, d.code);
                edit.start_line = d.line;
                edit.start_col = static_cast<int>(start) + 1;
                edit.end_line = d.line;
                edit.end_col = static_cast<int>(dot + needle.size()) + 1;
                d.fixits.push_back(std::move(edit));
            }
            return;
        }
    }

    if (d.code == "cfire/DEPRECATED-API" || d.code == "cfire/RETIRED-API") {
        if (d.line >= 1 && static_cast<std::size_t>(d.line - 1) < lines.size()) {
            const std::string& cur_ln = lines[static_cast<std::size_t>(d.line - 1)];
            auto try_replace_token = [&](std::string_view from_tok, std::string_view to_tok) -> bool {
                if (auto pos = cur_ln.find(from_tok); pos != std::string::npos) {
                    FixItEdit edit;
                    edit.title = std::format("Replace '{}' with '{}' ({})", from_tok, to_tok, d.code);
                    edit.start_line = d.line;
                    edit.start_col = static_cast<int>(pos) + 1;
                    edit.end_line = d.line;
                    edit.end_col = static_cast<int>(pos + from_tok.size()) + 1;
                    edit.replacement = std::string(to_tok);
                    edit.is_preferred = true;
                    d.fixits.push_back(std::move(edit));
                    return true;
                }
                return false;
            };
            if (std::string from = fact_of(d, "from"), to = fact_of(d, "to"); !from.empty() && !to.empty()) {
                if (try_replace_token(from, to)) return;
            }
        }
        return;
    }

    if (d.code == "cfire/DEPRECATED-DISCARD") {
        if (d.line >= 1 && static_cast<std::size_t>(d.line - 1) < lines.size()) {
            const std::string& cur_ln = lines[static_cast<std::size_t>(d.line - 1)];
            if (auto pos = cur_ln.find(":="); pos != std::string::npos) {
                FixItEdit edit;
                edit.title = "Replace ':=' with '=' for discard (cfire/DEPRECATED-DISCARD)";
                edit.start_line = d.line;
                edit.start_col = static_cast<int>(pos) + 1;
                edit.end_line = d.line;
                edit.end_col = static_cast<int>(pos + 2) + 1;
                edit.replacement = "=";
                edit.is_preferred = true;
                d.fixits.push_back(std::move(edit));
                return;
            }
        }
        return;
    }

    if (d.code == "cfire/DEPRECATED-IN-PARAM") {
        if (d.line >= 1 && static_cast<std::size_t>(d.line - 1) < lines.size()) {
            const std::string& cur_ln = lines[static_cast<std::size_t>(d.line - 1)];
            if (auto pos = cur_ln.find("in "); pos != std::string::npos) {
                FixItEdit edit;
                edit.title = "Remove redundant 'in' parameter intent (cfire/DEPRECATED-IN-PARAM)";
                edit.start_line = d.line;
                edit.start_col = static_cast<int>(pos) + 1;
                edit.end_line = d.line;
                edit.end_col = static_cast<int>(pos + 3) + 1;
                edit.replacement = "";
                edit.is_preferred = true;
                d.fixits.push_back(std::move(edit));
                return;
            }
        }
        return;
    }

    if (d.code == "cfire/DEPRECATED-INTENT-POS") {
        if (d.line >= 1 && static_cast<std::size_t>(d.line - 1) < lines.size()) {
            const std::string& cur_ln = lines[static_cast<std::size_t>(d.line - 1)];
            if (auto mpos = cur_ln.find(": mut "); mpos != std::string::npos) {
                auto id_end = cur_ln.rfind(' ', mpos);
                auto id_start = (id_end == std::string::npos) ? 0 : id_end + 1;
                if (id_end == std::string::npos) {
                    auto p_start = cur_ln.rfind('(', mpos);
                    id_start = (p_start == std::string::npos) ? 0 : p_start + 1;
                }
                while (id_start < mpos && (cur_ln[id_start] == ' ' || cur_ln[id_start] == '(' || cur_ln[id_start] == ',')) ++id_start;
                std::string pname = cur_ln.substr(id_start, mpos - id_start);
                FixItEdit edit;
                edit.title = std::format("Move 'mut' before parameter name: 'mut {}:' (cfire/DEPRECATED-INTENT-POS)", pname);
                edit.start_line = d.line;
                edit.start_col = static_cast<int>(id_start) + 1;
                edit.end_line = d.line;
                edit.end_col = static_cast<int>(mpos + 6) + 1;
                edit.replacement = "mut " + pname + ": ";
                edit.is_preferred = true;
                d.fixits.push_back(std::move(edit));
                return;
            }
        }
        return;
    }

    // Q53 (CF-189): `name: &mut T` -> `mut name: T`, `name: &T` / `-> &T` -> drop the `&`.
    if (d.code == "cfire/REFERENCE-TYPE") {
        if (d.line >= 1 && static_cast<std::size_t>(d.line - 1) < lines.size()) {
            const std::string& ln = lines[static_cast<std::size_t>(d.line - 1)];
            const std::size_t from = (d.col >= 1 && static_cast<std::size_t>(d.col - 1) < ln.size()) ? static_cast<std::size_t>(d.col - 1) : 0;
            FixItEdit edit;
            edit.start_line = edit.end_line = d.line;
            edit.is_preferred = true;
            if (fact_of(d, "kind") == "mut") {
                const auto amp = ln.find(": &mut ", from);
                if (amp == std::string::npos) return;
                std::size_t id_start = amp;
                while (id_start > 0 && (std::isalnum(static_cast<unsigned char>(ln[id_start - 1])) || ln[id_start - 1] == '_')) --id_start;
                const std::string name = ln.substr(id_start, amp - id_start);
                if (name.empty()) return;
                edit.title = std::format("Write 'mut {}: T' (cfire/REFERENCE-TYPE)", name);
                edit.start_col = static_cast<int>(id_start) + 1;
                edit.end_col = static_cast<int>(amp + 7) + 1;
                edit.replacement = "mut " + name + ": ";
            } else {
                std::size_t amp = ln.find('&', from);
                while (amp != std::string::npos && amp + 1 < ln.size() && ln[amp + 1] == '&') amp = ln.find('&', amp + 2);
                if (amp == std::string::npos) return;
                edit.title = "Remove '&': the type is already a view (cfire/REFERENCE-TYPE)";
                edit.start_col = static_cast<int>(amp) + 1;
                edit.end_col = static_cast<int>(amp + 1) + 1;
                edit.replacement = "";
            }
            d.fixits.push_back(std::move(edit));
        }
        return;
    }

    if (d.code == "cfire/DEPRECATED-SEPARATOR") {
        if (d.line >= 1 && static_cast<std::size_t>(d.line - 1) < lines.size()) {
            const std::string& cur_ln = lines[static_cast<std::size_t>(d.line - 1)];
            if (fact_of(d, "kind") == "enum-variant") {
                std::size_t col_idx = (d.col >= 1 && static_cast<std::size_t>(d.col - 1) < cur_ln.size()) ? static_cast<std::size_t>(d.col - 1) : 0;
                auto dot_pos = cur_ln.rfind('.', col_idx);
                if (dot_pos == std::string::npos) dot_pos = cur_ln.find('.', col_idx);
                if (dot_pos != std::string::npos) {
                    FixItEdit edit;
                    edit.title = "Replace '.' with '::' for enum variant (cfire/DEPRECATED-SEPARATOR)";
                    edit.start_line = d.line;
                    edit.start_col = static_cast<int>(dot_pos) + 1;
                    edit.end_line = d.line;
                    edit.end_col = static_cast<int>(dot_pos + 1) + 1;
                    edit.replacement = "::";
                    edit.is_preferred = true;
                    d.fixits.push_back(std::move(edit));
                    return;
                }
            } else if (fact_of(d, "kind") == "qualifier") {
                // Replace '::' with '.' on import qualifier
                std::size_t col_idx = (d.col >= 1 && static_cast<std::size_t>(d.col - 1) < cur_ln.size()) ? static_cast<std::size_t>(d.col - 1) : 0;
                auto dcol_pos = cur_ln.find("::", col_idx);
                if (dcol_pos == std::string::npos && col_idx > 0) dcol_pos = cur_ln.rfind("::", col_idx);
                if (dcol_pos != std::string::npos) {
                    FixItEdit edit;
                    edit.title = "Replace '::' with '.' on import qualifier (cfire/DEPRECATED-SEPARATOR)";
                    edit.start_line = d.line;
                    edit.start_col = static_cast<int>(dcol_pos) + 1;
                    edit.end_line = d.line;
                    edit.end_col = static_cast<int>(dcol_pos + 2) + 1;
                    edit.replacement = ".";
                    edit.is_preferred = true;
                    d.fixits.push_back(std::move(edit));
                    return;
                }
            }
        }
        return;
    }

    // CF-196: wrap an implicitly narrowed value in its checked conversion (`f(n)` -> `f(i32(n))`). The value runs
    // from the diagnostic column to the next `,` `)` `]` `}` `;` at depth 0 or the end of the line; a `mut`
    // argument cannot be converted, so it gets no fix-it.
    if (d.code == "cfire/TYPE-MISMATCH" && fact_of(d, "kind") == "narrowing" && d.line >= 1 && static_cast<std::size_t>(d.line - 1) < lines.size()) {
        const std::string& ln = lines[static_cast<std::size_t>(d.line - 1)];
        const std::string to = std::string(fact_of(d, "to"));
        if (to.empty() || d.col < 1 || static_cast<std::size_t>(d.col - 1) >= ln.size()) return;
        const std::size_t start = static_cast<std::size_t>(d.col - 1);
        if (start >= 4 && ln.compare(start - 4, 4, "mut ") == 0) return;
        int depth = 0;
        std::size_t end = start;
        for (; end < ln.size(); ++end) {
            const char c = ln[end];
            if (c == '"') {
                for (++end; end < ln.size() && ln[end] != '"'; ++end) if (ln[end] == '\\') ++end;
                continue;
            }
            if (c == '(' || c == '[' || c == '{') ++depth;
            else if (c == ')' || c == ']' || c == '}') { if (depth == 0) break; --depth; }
            else if ((c == ',' || c == ';') && depth == 0) break;
        }
        while (end > start && std::isspace(static_cast<unsigned char>(ln[end - 1]))) --end;
        if (end <= start || depth != 0) return;
        const std::string value = ln.substr(start, end - start);
        FixItEdit edit;
        edit.title = std::format("Convert with '{}(...)', which panics if the value does not fit (cfire/TYPE-MISMATCH)", to);
        edit.start_line = edit.end_line = d.line;
        edit.start_col = d.col;
        edit.end_col = static_cast<int>(end) + 1;
        edit.replacement = std::format("{}({})", to, value);
        edit.is_preferred = true;
        d.fixits.push_back(std::move(edit));
        return;
    }

    if (d.code == "cfire/REDUNDANT-DEREF") {
        if (d.line >= 1 && static_cast<std::size_t>(d.line - 1) < lines.size()) {
            const std::string& cur_ln = lines[static_cast<std::size_t>(d.line - 1)];
            if (auto pos = cur_ln.find("(*"); pos != std::string::npos) {
                if (auto end_p = cur_ln.find(").", pos); end_p != std::string::npos) {
                    std::string id = cur_ln.substr(pos + 2, end_p - (pos + 2));
                    FixItEdit edit;
                    edit.title = std::format("Replace '(*{}).' with '{}.' (cfire/REDUNDANT-DEREF)", id, id);
                    edit.start_line = d.line;
                    edit.start_col = static_cast<int>(pos) + 1;
                    edit.end_line = d.line;
                    edit.end_col = static_cast<int>(end_p + 2) + 1;
                    edit.replacement = id + ".";
                    edit.is_preferred = true;
                    d.fixits.push_back(std::move(edit));
                    return;
                }
            }
        }
        return;
    }

    if (d.code == "cfire/NULL-DEREF") {
        std::string ptr_name;
        ptr_name = fact_of(d, "ptr");
        if (!ptr_name.empty() && d.line >= 1 && static_cast<std::size_t>(d.line - 1) < lines.size()) {
            const std::string& cur_ln = lines[static_cast<std::size_t>(d.line - 1)];
            std::string indent;
            for (char c : cur_ln) {
                if (c == ' ' || c == '\t') indent += c;
                else break;
            }
            FixItEdit edit;
            edit.title = std::format("Insert 'assert({} != none)' guard before dereference (cfire/NULL-DEREF)", ptr_name);
            edit.start_line = d.line;
            edit.start_col = 1;
            edit.end_line = d.line;
            edit.end_col = 1;
            edit.replacement = std::format("{}assert({} != none)\n", indent, ptr_name);
            d.fixits.push_back(std::move(edit));
            return;
        }
    }

    if (d.code == "cfire/IMMUTABLE-MUT-ARG" || d.code == "cfire/MUT-RECEIVER") {
        std::string var_name;
        var_name = fact_of(d, "var");
        if (!var_name.empty()) {
            // Find declaration of var_name up to (d.line - 1) in lines
            int target_line = -1;
            int target_col = -1;
            for (int ln = std::min(d.line - 2, static_cast<int>(lines.size()) - 1); ln >= 0; --ln) {
                const auto& cur_ln = lines[static_cast<std::size_t>(ln)];
                auto pos = cur_ln.find(var_name);
                if (pos != std::string::npos) {
                    bool left_ok = (pos == 0) || (!std::isalnum(static_cast<unsigned char>(cur_ln[pos - 1])) && cur_ln[pos - 1] != '_');
                    std::size_t after = pos + var_name.size();
                    bool right_ok = (after >= cur_ln.size()) || (!std::isalnum(static_cast<unsigned char>(cur_ln[after])) && cur_ln[after] != '_');
                    if (left_ok && right_ok) {
                        // Ensure it's a declaration: followed by `:=` or `:` or `=`:
                        std::string_view tail = std::string_view(cur_ln).substr(after);
                        while (!tail.empty() && std::isspace(static_cast<unsigned char>(tail.front()))) tail.remove_prefix(1);
                        if (tail.starts_with(":=") || tail.starts_with(":") || tail.starts_with("=")) {
                            std::string_view head = std::string_view(cur_ln).substr(0, pos);
                            while (!head.empty() && std::isspace(static_cast<unsigned char>(head.back()))) head.remove_suffix(1);
                            if (!head.ends_with("mut")) {
                                target_line = ln + 1; // 1-based
                                target_col = static_cast<int>(pos) + 1; // 1-based
                                break;
                            }
                        }
                    }
                }
            }
            if (target_line > 0 && target_col > 0) {
                FixItEdit edit;
                edit.title = std::format("Declare '{}' as mutable ('mut {}')", var_name, var_name);
                edit.start_line = target_line;
                edit.start_col = target_col;
                edit.end_line = target_line;
                edit.end_col = target_col;
                edit.replacement = "mut ";
                d.fixits.push_back(std::move(edit));
            }
        }
        return;
    }

    if (d.code == "cfire/NON-EXHAUSTIVE-MATCH") {
        std::string enum_name;
        std::string fallback_variant;
        enum_name = fact_of(d, "enum");
        fallback_variant = fact_of(d, "missing");

        const cfire::frontend::EnumDecl* target_enum = nullptr;
        if (!enum_name.empty()) {
            if (ast != nullptr) {
                for (const auto& en : ast->enums) {
                    if (en.name == enum_name) {
                        target_enum = &en;
                        break;
                    }
                }
            }
            if (!target_enum) {
                for (const auto* en : extra_enums) {
                    if (en && en->name == enum_name) {
                        target_enum = en;
                        break;
                    }
                }
            }
        }

        int start_0 = std::max(0, d.line - 1);
        std::string base_indent;
        if (static_cast<std::size_t>(start_0) < lines.size()) {
            const std::string& match_ln = lines[static_cast<std::size_t>(start_0)];
            for (char c : match_ln) {
                if (c == ' ' || c == '\t') base_indent += c;
                else break;
            }
        }
        std::string arm_indent = base_indent + "    ";
        std::string body_indent = arm_indent + "    ";

        int match_close_line = -1; // 0-based
        int match_close_col = 0;   // 0-based
        int brace_depth = 0;
        bool found_open_brace = false;

        for (int ln = start_0; ln < static_cast<int>(lines.size()); ++ln) {
            const std::string& cur_ln = lines[static_cast<std::size_t>(ln)];
            for (int col = 0; col < static_cast<int>(cur_ln.size()); ++col) {
                char c = cur_ln[static_cast<std::size_t>(col)];
                if (c == '{') {
                    found_open_brace = true;
                    ++brace_depth;
                } else if (c == '}') {
                    if (found_open_brace) {
                        --brace_depth;
                        if (brace_depth == 0) {
                            match_close_line = ln;
                            match_close_col = col;
                            break;
                        }
                    }
                }
            }
            if (match_close_line != -1) break;
        }

        if (match_close_line == -1) {
            match_close_line = start_0 + 1;
            match_close_col = 0;
        }

        std::unordered_set<std::string> covered;
        if (ast != nullptr) {
            const cfire::frontend::MatchStmt* target_match = nullptr;
            auto find_match = [&](auto self, const std::vector<cfire::frontend::StmtPtr>& stmts) -> void {
                if (target_match) return;
                for (const auto& s : stmts) {
                    if (!s) continue;
                    if (const auto* ms = std::get_if<cfire::frontend::MatchStmt>(&s->node)) {
                        if (ms->line - 1 == start_0 || (ms->line - 1 <= start_0 && start_0 <= match_close_line)) {
                            target_match = ms;
                            return;
                        }
                    }
                    if (const auto* ifs = std::get_if<cfire::frontend::IfStmt>(&s->node)) {
                        self(self, ifs->then_body);
                        self(self, ifs->else_body);
                    } else if (const auto* fs = std::get_if<cfire::frontend::ForStmt>(&s->node)) {
                        self(self, fs->body);
                    } else if (const auto* ms2 = std::get_if<cfire::frontend::MatchStmt>(&s->node)) {
                        for (const auto& arm : ms2->arms) self(self, arm.body);
                    }
                }
            };
            for (const auto& fn : ast->functions) find_match(find_match, fn.body);
            for (const auto& t : ast->tests) find_match(find_match, t.body);
            if (target_match) {
                for (const auto& arm : target_match->arms) {
                    covered.insert(arm.variant_name);
                }
            }
        }

        std::string arms_text;
        if (target_enum) {
            for (const auto& v : target_enum->variants) {
                if (!covered.contains(v.name)) {
                    arms_text += arm_indent + v.name;
                    if (!v.payload_types.empty()) {
                        arms_text += "(";
                        for (std::size_t i = 0; i < v.payload_types.size(); ++i) {
                            if (i > 0) arms_text += ", ";
                            arms_text += std::format("v{}", i);
                        }
                        arms_text += ")";
                    }
                    arms_text += " => {\n" + body_indent + "\n" + arm_indent + "}\n";
                }
            }
        } else if (!fallback_variant.empty()) {
            arms_text = arm_indent + fallback_variant + " => {\n" + body_indent + "\n" + arm_indent + "}\n";
        }

        int edit_ln_0 = match_close_line;
        int edit_col_0 = 0;
        if (match_close_col > 0 && static_cast<std::size_t>(match_close_line) < lines.size()) {
            const std::string& cl_ln = lines[static_cast<std::size_t>(match_close_line)];
            std::size_t first_non_ws = cl_ln.find_first_not_of(" \t");
            if (first_non_ws != std::string::npos && static_cast<int>(first_non_ws) == match_close_col) {
                edit_col_0 = 0;
            } else {
                edit_col_0 = match_close_col;
                arms_text = "\n" + arms_text + base_indent;
            }
        }

        if (!arms_text.empty()) {
            FixItEdit edit;
            edit.title = std::format("Fill missing match arms for '{}' (cfire/NON-EXHAUSTIVE-MATCH)",
                enum_name.empty() ? "enum" : enum_name);
            edit.start_line = edit_ln_0 + 1;
            edit.start_col = edit_col_0 + 1;
            edit.end_line = edit_ln_0 + 1;
            edit.end_col = edit_col_0 + 1;
            edit.replacement = arms_text;
            edit.is_preferred = true;
            d.fixits.push_back(std::move(edit));
        }

        std::string wildcard_text = arm_indent + "_ => {\n" + body_indent + "\n" + arm_indent + "}\n";
        if (edit_col_0 > 0) {
            wildcard_text = "\n" + wildcard_text + base_indent;
        }
        FixItEdit wc_edit;
        wc_edit.title = "Add wildcard pattern '_ => { ... }' (cfire/NON-EXHAUSTIVE-MATCH)";
        wc_edit.start_line = edit_ln_0 + 1;
        wc_edit.start_col = edit_col_0 + 1;
        wc_edit.end_line = edit_ln_0 + 1;
        wc_edit.end_col = edit_col_0 + 1;
        wc_edit.replacement = wildcard_text;
        wc_edit.is_preferred = false;
        d.fixits.push_back(std::move(wc_edit));
    }
}

[[nodiscard]] inline auto serialize_diagnostics_json(
    std::string_view file,
    bool success,
    const std::vector<StructuredDiagnostic>& diags
) -> std::string {
    std::string json;
    json.reserve(256 + diags.size() * 128);
    json += std::format("{{\"success\": {}, \"file\": \"{}\", \"diagnostics\": [",
        success ? "true" : "false", json_escape(file));

    for (std::size_t i = 0; i < diags.size(); ++i) {
        if (i > 0) json += ", ";
        const auto& d = diags[i];
        json += std::format(
            "{{\"file\": \"{}\", \"line\": {}, \"col\": {}, \"end_line\": {}, \"end_col\": {}, \"severity\": \"{}\", \"code\": \"{}\", \"source\": \"{}\", \"message\": \"{}\", \"fixits\": [",
            json_escape(d.file), d.line, d.col, d.end_line, d.end_col, json_escape(d.severity), json_escape(d.code), json_escape(d.source), json_escape(d.message));

        for (std::size_t j = 0; j < d.fixits.size(); ++j) {
            if (j > 0) json += ", ";
            const auto& fix = d.fixits[j];
            json += std::format(
                "{{\"title\": \"{}\", \"start_line\": {}, \"start_col\": {}, \"end_line\": {}, \"end_col\": {}, \"replacement\": \"{}\"}}",
                json_escape(fix.title), fix.start_line, fix.start_col, fix.end_line, fix.end_col, json_escape(fix.replacement));
        }
        json += "]}";
    }
    json += "]}";
    return json;
}

[[nodiscard]] inline auto serialize_diagnostics_json(
    const std::vector<StructuredDiagnostic>& diags
) -> std::string {
    bool ok = true;
    for (const auto& d : diags) {
        if (d.severity == "error") { ok = false; break; }
    }
    return serialize_diagnostics_json("", ok, diags);
}

inline auto detect_missing_stdlib_imports(
    const cfire::frontend::FileAST& ast,
    const std::vector<std::string>& lines
) -> std::vector<FixItEdit> {
    std::vector<FixItEdit> edits;
    auto catalog = cfire::doc::stdlib_catalog();

    std::unordered_set<std::string> existing_imports;
    for (const auto& imp : ast.imports) {
        existing_imports.insert(imp.path);
    }

    int insert_line = 1;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        std::string_view l = lines[i];
        while (!l.empty() && std::isspace(static_cast<unsigned char>(l.front()))) l.remove_prefix(1);
        if (l.starts_with("package ")) {
            insert_line = static_cast<int>(i) + 2; // line after package declaration
            break;
        }
    }

    // Only code counts: `"build.CC archive"` in a string or `// see fs.read_text` in a comment is not a use.
    std::vector<std::string> code_lines;
    code_lines.reserve(lines.size());
    for (const auto& raw : lines) {
        std::string code;
        char quote = 0;
        for (std::size_t i = 0; i < raw.size(); ++i) {
            char c = raw[i];
            if (quote) {
                if (c == '\\' && quote == '"') { ++i; continue; }
                if (c == quote) quote = 0;
                code.push_back(' ');
                continue;
            }
            if (c == '"' || c == '`') { quote = c; code.push_back(' '); continue; }
            if (c == '/' && i + 1 < raw.size() && raw[i + 1] == '/') break;
            code.push_back(c);
        }
        code_lines.push_back(std::move(code));
    }

    // A module alias that the file binds as a parameter, receiver or local (`fn add(log: AuditLog)`) is not a module use.
    std::unordered_set<std::string> bound_names;
    for (const auto& fn : ast.functions) {
        if (fn.receiver) bound_names.insert(fn.receiver->name);
        for (const auto& p : fn.params) bound_names.insert(p.name);
    }
    for (const auto& line : code_lines) {
        for (std::size_t p = line.find(":="); p != std::string::npos; p = line.find(":=", p + 2)) {
            std::size_t e = p;
            while (e > 0 && line[e - 1] == ' ') --e;
            std::size_t b0 = e;
            while (b0 > 0 && (std::isalnum(static_cast<unsigned char>(line[b0 - 1])) || line[b0 - 1] == '_')) --b0;
            if (b0 < e) bound_names.insert(line.substr(b0, e - b0));
        }
    }

    std::unordered_set<std::string> needed_modules;
    for (const auto& mod : catalog) {
        if (existing_imports.contains(mod.import_path)) continue;
        if (bound_names.contains(mod.ns_alias)) continue;
        std::string pattern = mod.ns_alias + ".";
        for (const auto& line : code_lines) {
            auto pos = line.find(pattern);
            if (pos != std::string::npos) {
                bool left_ok = (pos == 0) || (!std::isalnum(static_cast<unsigned char>(line[pos - 1])) && line[pos - 1] != '_');
                if (left_ok) {
                    needed_modules.insert(mod.import_path);
                    break;
                }
            }
        }
    }

    std::vector<std::string> sorted_needed(needed_modules.begin(), needed_modules.end());
    std::sort(sorted_needed.begin(), sorted_needed.end());

    std::string imports_text;
    for (const auto& imp : sorted_needed) {
        imports_text += std::format("import \"{}\"\n", imp);
    }

    if (!imports_text.empty()) {
        FixItEdit edit;
        edit.title = "Import missing standard library module(s)";
        edit.start_line = insert_line;
        edit.start_col = 1;
        edit.end_line = insert_line;
        edit.end_col = 1;
        edit.replacement = imports_text;
        edits.push_back(std::move(edit));
    }

    return edits;
}

inline auto apply_fixits(
    std::string_view original_content,
    std::vector<FixItEdit> edits
) -> std::pair<std::string, std::vector<FixItEdit>> {
    if (edits.empty()) {
        return {std::string(original_content), {}};
    }

    std::vector<std::size_t> line_offsets;
    line_offsets.push_back(0);
    for (std::size_t i = 0; i < original_content.size(); ++i) {
        if (original_content[i] == '\n') {
            line_offsets.push_back(i + 1);
        }
    }

    struct OffsetEdit {
        FixItEdit edit;
        std::size_t start_byte = 0;
        std::size_t end_byte = 0;
    };

    std::vector<OffsetEdit> offset_edits;
    for (auto& e : edits) {
        if (e.start_line <= 0 || static_cast<std::size_t>(e.start_line - 1) >= line_offsets.size()) continue;
        if (e.end_line <= 0 || static_cast<std::size_t>(e.end_line - 1) >= line_offsets.size()) continue;

        std::size_t s_line_start = line_offsets[static_cast<std::size_t>(e.start_line - 1)];
        std::size_t e_line_start = line_offsets[static_cast<std::size_t>(e.end_line - 1)];

        std::size_t s_byte = s_line_start + static_cast<std::size_t>(std::max(0, e.start_col - 1));
        std::size_t e_byte = e_line_start + static_cast<std::size_t>(std::max(0, e.end_col - 1));

        s_byte = std::min(s_byte, original_content.size());
        e_byte = std::min(e_byte, original_content.size());

        if (s_byte <= e_byte) {
            offset_edits.push_back(OffsetEdit{std::move(e), s_byte, e_byte});
        }
    }

    std::sort(offset_edits.begin(), offset_edits.end(), [](const OffsetEdit& a, const OffsetEdit& b) {
        if (a.start_byte != b.start_byte) return a.start_byte > b.start_byte;
        return a.end_byte > b.end_byte;
    });

    std::string result(original_content);
    std::vector<FixItEdit> applied_edits;
    std::size_t last_start = result.size() + 1;

    for (const auto& oe : offset_edits) {
        if (!applied_edits.empty()) {
            const auto& prev = applied_edits.back();
            if (prev.start_line == oe.edit.start_line &&
                prev.start_col == oe.edit.start_col &&
                prev.end_line == oe.edit.end_line &&
                prev.end_col == oe.edit.end_col &&
                prev.replacement == oe.edit.replacement) {
                continue;
            }
        }
        if (oe.end_byte <= last_start && oe.start_byte <= oe.end_byte && oe.end_byte <= result.size()) {
            result.replace(oe.start_byte, oe.end_byte - oe.start_byte, oe.edit.replacement);
            last_start = oe.start_byte;
            applied_edits.push_back(oe.edit);
        }
    }

    return {std::move(result), std::move(applied_edits)};
}

[[nodiscard]] inline auto serialize_fix_result_json(
    std::string_view file,
    bool success,
    std::size_t applied_count,
    const std::vector<FixItEdit>& edits
) -> std::string {
    std::string json;
    json += std::format("{{\"success\": {}, \"file\": \"{}\", \"applied_fixes\": {}, \"edits\": [",
        success ? "true" : "false", json_escape(file), applied_count);
    for (std::size_t i = 0; i < edits.size(); ++i) {
        if (i > 0) json += ", ";
        const auto& fix = edits[i];
        json += std::format(
            "{{\"title\": \"{}\", \"start_line\": {}, \"start_col\": {}, \"end_line\": {}, \"end_col\": {}, \"replacement\": \"{}\"}}",
            json_escape(fix.title), fix.start_line, fix.start_col, fix.end_line, fix.end_col, json_escape(fix.replacement));
    }
    json += "]}";
    return json;
}

[[nodiscard]] inline auto humanize_cpp_compiler_output(std::string_view raw_out) -> std::string {
    std::string s(raw_out);
    auto replace_all = [](std::string& str, std::string_view from, std::string_view to) {
        if (from.empty()) return;
        std::size_t pos = 0;
        while ((pos = str.find(from, pos)) != std::string::npos) {
            str.replace(pos, from.size(), to);
            pos += to.size();
        }
    };

    replace_all(s, "std::__1::", "std::");
    replace_all(s, "std::__cxx11::", "std::");
    replace_all(s, "std::basic_string<char, std::char_traits<char>, std::allocator<char>>", "std::string");
    replace_all(s, "std::basic_string<char>", "std::string");
    replace_all(s, "std::basic_string_view<char, std::char_traits<char>>", "std::string_view");
    replace_all(s, "std::basic_string_view<char>", "std::string_view");
    replace_all(s, "std::string_view", "&str");
    replace_all(s, "std::string", "str");
    replace_all(s, "std::int64_t", "i64");
    replace_all(s, "std::int32_t", "i32");
    replace_all(s, "std::int16_t", "i16");
    replace_all(s, "std::int8_t", "i8");
    replace_all(s, "std::uint64_t", "u64");
    replace_all(s, "std::uint32_t", "u32");
    replace_all(s, "std::uint16_t", "u16");
    replace_all(s, "std::uint8_t", "u8");
    replace_all(s, "std::size_t", "usize");
    replace_all(s, "cfire::fs::File", "fs.File");
    replace_all(s, "cfire::io::File", "fs.File");
    replace_all(s, "cfire::net::TcpConn", "net.TcpConn");
    replace_all(s, "cfire::net::TcpStream", "net.TcpConn");
    replace_all(s, "cfire::net::TcpListener", "net.TcpListener");
    replace_all(s, "std::unexpected<cfire::Error>", "error(...)");
    replace_all(s, "std::unexpected<Error>", "error(...)");
    replace_all(s, "cfire::make_error", "error(...)");
    replace_all(s, "cfire::Error", "Error");
    replace_all(s, "cfire::unwrap_or", "'or' fallback");
    replace_all(s, "cfire::unwrap_val", "'try' unwrap");
    replace_all(s, "cfire::try_unwrap", "'try' unwrap");
    replace_all(s, "cfire::slice", "slice '[a..b]'");
    replace_all(s, "cfire::index_at", "index '[i]'");
    replace_all(s, "no member named 'size' in", "no member '.len' in");
    replace_all(s, "does not provide a subscript operator", "does not support indexing '[i]'");

    // Strip Clang ` (aka '...')` type alias annotations
    {
        std::size_t apos = 0;
        while ((apos = s.find(" (aka '", apos)) != std::string::npos) {
            auto end_pos = s.find("')", apos + 7);
            if (end_pos != std::string::npos && end_pos - apos < 160) {
                s.erase(apos, (end_pos + 2) - apos);
            } else {
                apos += 7;
            }
        }
    }

    // Rewrite `.cfire/gen/<stem>.cpp` paths back to `<stem>.cf`
    {
        std::size_t gpos = 0;
        while ((gpos = s.find(".cfire/gen/", gpos)) != std::string::npos) {
            auto end_pos = s.find(".cpp:", gpos);
            if (end_pos != std::string::npos && end_pos - gpos < 128) {
                std::string stem = s.substr(gpos + 11, end_pos - (gpos + 11));
                if (stem.ends_with("_test")) stem.erase(stem.size() - 5);
                s.replace(gpos, (end_pos + 4) - gpos, stem + ".cf");
            } else {
                gpos += 11;
            }
        }
    }

    // Iteratively rewrite C++ container templates from innermost to outermost
    for (int pass = 0; pass < 6; ++pass) {
        bool changed = false;
        auto rewrite_unary_template = [&](std::string_view prefix, std::string_view open_rep, std::string_view close_rep) {
            std::size_t pos = 0;
            while ((pos = s.find(prefix, pos)) != std::string::npos) {
                std::size_t inner_start = pos + prefix.size();
                int depth = 1;
                std::size_t i = inner_start;
                std::size_t comma_pos = std::string::npos;
                while (i < s.size() && depth > 0) {
                    if (s[i] == '<') ++depth;
                    else if (s[i] == '>') --depth;
                    else if (s[i] == ',' && depth == 1 && comma_pos == std::string::npos) {
                        comma_pos = i;
                    }
                    if (depth > 0) ++i;
                }
                if (i < s.size() && depth == 0) {
                    std::size_t inner_end = (comma_pos != std::string::npos) ? comma_pos : i;
                    std::string inner = s.substr(inner_start, inner_end - inner_start);
                    while (!inner.empty() && inner.back() == ' ') inner.pop_back();
                    std::string rep = std::string(open_rep) + inner + std::string(close_rep);
                    s.replace(pos, i - pos + 1, rep);
                    changed = true;
                } else {
                    pos += prefix.size();
                }
            }
        };
        rewrite_unary_template("std::vector<", "[]", "");
        rewrite_unary_template("std::span<const ", "&[]", "");
        rewrite_unary_template("std::span<", "&mut []", "");
        rewrite_unary_template("std::optional<", "", "?");
        rewrite_unary_template("std::expected<", "", "!");
        rewrite_unary_template("std::unique_ptr<", "own[", "]");
        rewrite_unary_template("std::shared_ptr<", "shared[", "]");
        if (!changed) break;
    }

    // Collapse C++ runtime header template instantiation errors onto the originating `.cf:line:col` call site
    auto is_runtime_hdr_line = [](std::string_view ln) -> bool {
        return ln.contains("cfire_prelude.hpp:") ||
               ln.contains("cfire_core_stdlib.hpp:") ||
               ln.contains("cfire_test_runtime.hpp:") ||
               ln.contains("/runtime/core/");
    };
    if (s.contains("cfire_prelude.hpp:") || s.contains("cfire_core_stdlib.hpp:") ||
        s.contains("cfire_test_runtime.hpp:") || s.contains("/runtime/core/")) {
        auto raw_lines = split_lines(s);
        std::vector<std::string> out_lines;
        std::vector<bool> skip_line(raw_lines.size(), false);
        for (std::size_t idx = 0; idx < raw_lines.size(); ++idx) {
            if (skip_line[idx]) continue;
            const std::string& ln = raw_lines[idx];
            if (ln.starts_with("In file included from ") &&
                (ln.contains("<built-in>") || ln.contains("cfire_prelude.hpp") || ln.contains("/runtime/"))) {
                continue;
            }
            if (is_runtime_hdr_line(ln) && ln.contains(": error: ")) {
                auto err_pos = ln.find(": error: ");
                std::string err_msg = ln.substr(err_pos + 9);
                std::string cf_loc_prefix;
                for (std::size_t j = idx + 1; j < raw_lines.size() && j <= idx + 12; ++j) {
                    const std::string& nln = raw_lines[j];
                    if (auto note_pos = nln.find(": note: in instantiation of "); note_pos != std::string::npos) {
                        std::string_view prefix_sv(nln.data(), note_pos);
                        if (prefix_sv.contains(".cf:")) {
                            cf_loc_prefix = std::string(prefix_sv);
                            skip_line[j] = true;
                            // Also skip snippet lines immediately following the note or runtime error
                            for (std::size_t k = idx + 1; k < raw_lines.size(); ++k) {
                                if (k == j) continue;
                                std::string_view kl = raw_lines[k];
                                while (!kl.empty() && kl.front() == ' ') kl.remove_prefix(1);
                                if (kl.starts_with("|") || (!kl.empty() && std::isdigit(static_cast<unsigned char>(kl.front())) && raw_lines[k].contains(" | "))) {
                                    skip_line[k] = true;
                                } else if (is_runtime_hdr_line(raw_lines[k]) && raw_lines[k].contains(": note:")) {
                                    skip_line[k] = true;
                                } else if (k > j) {
                                    break;
                                }
                            }
                            break;
                        }
                    }
                }
                if (!cf_loc_prefix.empty()) {
                    if (!err_msg.contains("cfire/")) {
                        err_msg += " (cfire/TYPE-ERROR)";
                    }
                    out_lines.push_back(std::format("{}: error: {}", cf_loc_prefix, err_msg));
                    continue;
                }
            }
            out_lines.push_back(ln);
        }
        std::string rebuilt;
        for (std::size_t i = 0; i < out_lines.size(); ++i) {
            if (i > 0) rebuilt += '\n';
            rebuilt += out_lines[i];
        }
        s = std::move(rebuilt);
    }

    return s;
}

} // namespace cfire::diag
