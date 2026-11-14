#pragma once
// src/parser.hpp — Recursive-Descent Parser in C++23 for cfire (.cf)

#include "ast.hpp"
#include "cfire_prelude.hpp"
#include <algorithm>
#include <ranges>
#include <sstream>
#include <expected>
#include <format>
#include <string>
#include <vector>

#include <regex>

namespace cfire::frontend {

struct ParseResult {
    FileAST ast;
    std::vector<std::string> errors;              // rendered text of `source_errors`
    std::vector<SourceError> source_errors{};     // Q24 (CF-149): structured, as detected

    [[nodiscard]] bool ok() const noexcept { return errors.empty(); }
};

[[nodiscard]] inline auto make_parse_result(FileAST ast, std::vector<SourceError> errs) -> ParseResult {
    ParseResult r;
    r.ast = std::move(ast);
    for (const auto& e : errs) r.errors.push_back(e.text());
    r.source_errors = std::move(errs);
    return r;
}

[[nodiscard]] inline auto is_make_ident(const ExprPtr& callee) -> bool {
    if (!callee) return false;
    if (const auto* id = std::get_if<IdentExpr>(&callee->node)) {
        return id->name == "make";
    }
    return false;
}

struct InterpPlaceholder {
    std::size_t start_pos = 0;   // index of '{' in string
    std::size_t end_pos = 0;     // index of '}' in string (inclusive)
    std::size_t expr_offset = 0; // index of first char of expr_text in string
    std::string expr_text{};     // trimmed expression inside '{...}' (before optional ':fmt_spec')
    std::string fmt_spec{};      // optional std::format specifier (without leading ':')
};

#if defined(CFIRE_SPLIT_TUS)
#define CFIRE_PARSER_INLINE
#else
#define CFIRE_PARSER_INLINE inline
#endif

[[nodiscard]] CFIRE_PARSER_INLINE auto parse_source_resilient(std::string_view path, std::string_view src) -> ParseResult;
[[nodiscard]] CFIRE_PARSER_INLINE auto parse_source(std::string_view path, std::string_view src) -> std::expected<FileAST, SourceError>;
CFIRE_PARSER_INLINE void append_doctests(std::string_view path, std::string_view src, FileAST& ast);
[[nodiscard]] CFIRE_PARSER_INLINE auto parse_expr_snippet(std::string_view path, std::string_view src, int base_line = 1, int base_col = 1) -> std::expected<ExprSnippetResult, SourceError>;
[[nodiscard]] CFIRE_PARSER_INLINE auto is_valid_std_format_spec(std::string_view spec) noexcept -> bool;
[[nodiscard]] CFIRE_PARSER_INLINE auto scan_interpolation_placeholders(std::string_view sv) -> std::vector<InterpPlaceholder>;

#undef CFIRE_PARSER_INLINE

} // namespace cfire::frontend

#if !defined(CFIRE_SPLIT_TUS)
#include "parser_impl.inc"
#endif
