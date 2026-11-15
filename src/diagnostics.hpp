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
    // structured facts recorded where the problem is detected (an identifier, a keyword, a
    // replacement token, ...). Fix-it builders read these instead of re-parsing the message text.
    std::unordered_map<std::string, std::string> facts{};
};

using Diagnostic = StructuredDiagnostic;

// every compiler diagnostic is constructed here at the point of detection. `col == 0` means
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

// Structured fact recorded by the diagnostic's producer ("" when absent).
[[nodiscard]] inline auto fact_of(const StructuredDiagnostic& d, std::string_view key) -> std::string {
    if (auto it = d.facts.find(std::string(key)); it != d.facts.end()) return it->second;
    return "";
}

#if defined(CFIRE_SPLIT_TUS)
#define CFIRE_DIAG_INLINE
#else
#define CFIRE_DIAG_INLINE inline
#endif

[[nodiscard]] CFIRE_DIAG_INLINE auto parse_clang_diagnostic_line(
    std::string_view err_str,
    std::string_view default_file = "",
    std::string_view default_severity = "error",
    std::string_view default_code = "cfire/ERROR"
) -> StructuredDiagnostic;

[[nodiscard]] CFIRE_DIAG_INLINE auto split_lines(std::string_view text) -> std::vector<std::string>;

CFIRE_DIAG_INLINE void synthesize_fixits_for_diagnostic(
    StructuredDiagnostic& d,
    const cfire::frontend::FileAST* ast,
    const std::vector<std::string>& lines,
    const std::vector<const cfire::frontend::EnumDecl*>& extra_enums = {}
);

[[nodiscard]] CFIRE_DIAG_INLINE auto serialize_diagnostics_json(
    std::string_view file,
    bool success,
    const std::vector<StructuredDiagnostic>& diags
) -> std::string;

[[nodiscard]] CFIRE_DIAG_INLINE auto serialize_diagnostics_json(
    const std::vector<StructuredDiagnostic>& diags
) -> std::string;

CFIRE_DIAG_INLINE auto detect_missing_stdlib_imports(
    const cfire::frontend::FileAST& ast,
    const std::vector<std::string>& lines
) -> std::vector<FixItEdit>;

CFIRE_DIAG_INLINE auto apply_fixits(
    std::string_view original_content,
    std::vector<FixItEdit> edits
) -> std::pair<std::string, std::vector<FixItEdit>>;

[[nodiscard]] CFIRE_DIAG_INLINE auto serialize_fix_result_json(
    std::string_view file,
    bool success,
    std::size_t applied_count,
    const std::vector<FixItEdit>& edits
) -> std::string;

[[nodiscard]] CFIRE_DIAG_INLINE auto humanize_cpp_compiler_output(std::string_view raw_out) -> std::string;

#undef CFIRE_DIAG_INLINE

} // namespace cfire::diag

#if !defined(CFIRE_SPLIT_TUS)
#include "diagnostics_registry_impl.hpp"
#endif
