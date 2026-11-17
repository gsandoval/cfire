#pragma once
// src/lexer.hpp — Tokenizer for cfire (.cf)

#include "lexer_types.hpp"
#include <cctype>
#include <expected>
#include <format>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cfire::frontend {

// lexer and parser errors are structured at the point of detection. `text` is the
// rendered `file:line:col: message` form for callers that only print.
struct SourceError {
    std::string file{};
    int line = 1;
    int col = 1;
    std::string message{};
    std::string code{"cfire/SYNTAX"};
    std::unordered_map<std::string, std::string> facts{};

    SourceError() = default;
    SourceError(std::string f, int l, int c, std::string msg, std::string cd = "cfire/SYNTAX",
                std::unordered_map<std::string, std::string> fts = {})
        : file(std::move(f)), line(l), col(c), message(std::move(msg)), code(std::move(cd)), facts(std::move(fts)) {}
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

#if defined(CFIRE_SPLIT_TUS)
#define CFIRE_PARSER_INLINE
#else
#define CFIRE_PARSER_INLINE inline
#endif

[[nodiscard]] CFIRE_PARSER_INLINE auto tokenize(
    std::string_view file,
    std::string_view src,
    std::vector<CommentSpan>* comments = nullptr
) -> std::expected<std::vector<Token>, SourceError>;

#undef CFIRE_PARSER_INLINE

} // namespace cfire::frontend

#if !defined(CFIRE_SPLIT_TUS)
#include "lexer_impl.hpp"
#endif
