#pragma once
#include "../cfire_prelude.hpp"

#include <regex>
#include <string>
#include <string_view>
#include <vector>
#include <expected>
#include <format>

namespace cfire::regex {

class Regex {
public:
    Regex() = default;
    explicit Regex(std::string pattern)
        : pattern_(std::move(pattern)), re_(pattern_, std::regex::ECMAScript) {}

    [[nodiscard]] auto is_match(std::string_view text) const -> bool {
        return std::regex_search(text.begin(), text.end(), re_);
    }

    [[nodiscard]] auto find(std::string_view text) const -> std::optional<std::string> {
        std::match_results<std::string_view::const_iterator> match;
        if (std::regex_search(text.begin(), text.end(), match, re_)) {
            return std::string(match[0].first, match[0].second);
        }
        return std::nullopt;
    }

    [[nodiscard]] auto find_all(std::string_view text) const -> std::vector<std::string> {
        std::vector<std::string> results;
        auto words_begin = std::regex_iterator<std::string_view::const_iterator>(text.begin(), text.end(), re_);
        auto words_end = std::regex_iterator<std::string_view::const_iterator>();
        for (auto it = words_begin; it != words_end; ++it) {
            results.push_back(it->str());
        }
        return results;
    }

    [[nodiscard]] auto replace_all(std::string_view text, std::string_view repl) const -> std::string {
        std::string s(text);
        return std::regex_replace(s, re_, std::string(repl));
    }

    [[nodiscard]] auto pattern() const -> const std::string& { return pattern_; }

private:
    std::string pattern_{};
    std::regex re_{};
};

[[nodiscard]] inline auto compile(std::string_view pattern) -> std::expected<Regex, cfire::Error> {
    try {
        return Regex(std::string(pattern));
    } catch (const std::regex_error& e) {
        return cfire::make_error(std::format("invalid regex pattern '{}': {}", pattern, e.what()));
    }
}

[[nodiscard]] inline auto is_match(std::string_view pattern, std::string_view text) -> std::expected<bool, cfire::Error> {
    auto r = compile(pattern);
    if (!r) return std::unexpected(r.error());
    return r->is_match(text);
}

[[nodiscard]] inline auto replace_all(std::string_view pattern, std::string_view text, std::string_view repl)
    -> std::expected<std::string, cfire::Error>
{
    auto r = compile(pattern);
    if (!r) return std::unexpected(r.error());
    return r->replace_all(text, repl);
}

} // namespace cfire::regex

