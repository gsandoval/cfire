#pragma once
#include "../cfire_prelude.hpp"

#include <string>
#include <string_view>
#include <vector>
#include <cctype>
#include <algorithm>

namespace cfire::str {

[[nodiscard]] inline auto trim(std::string_view sv) -> std::string {
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) sv.remove_prefix(1);
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back()))) sv.remove_suffix(1);
    return std::string(sv);
}

[[nodiscard]] inline auto split(std::string_view sv, std::string_view sep) -> std::vector<std::string> {
    std::vector<std::string> parts;
    if (sep.empty()) {
        parts.emplace_back(sv);
        return parts;
    }
    std::size_t start = 0;
    for (;;) {
        std::size_t pos = sv.find(sep, start);
        if (pos == std::string_view::npos) {
            parts.emplace_back(sv.substr(start));
            break;
        }
        parts.emplace_back(sv.substr(start, pos - start));
        start = pos + sep.size();
    }
    return parts;
}

[[nodiscard]] inline auto join(std::span<const std::string> parts, std::string_view sep) -> std::string {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) out += sep;
        out += parts[i];
    }
    return out;
}

[[nodiscard]] inline auto to_upper(std::string_view sv) -> std::string {
    std::string out(sv);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return out;
}

[[nodiscard]] inline auto to_lower(std::string_view sv) -> std::string {
    std::string out(sv);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

[[nodiscard]] inline auto replace_all(std::string_view sv, std::string_view from, std::string_view to) -> std::string {
    if (from.empty()) return std::string(sv);
    std::string out;
    std::size_t start = 0;
    for (;;) {
        std::size_t pos = sv.find(from, start);
        if (pos == std::string_view::npos) {
            out.append(sv.substr(start));
            break;
        }
        out.append(sv.substr(start, pos - start));
        out.append(to);
        start = pos + from.size();
    }
    return out;
}

[[nodiscard]] inline auto starts_with(std::string_view sv, std::string_view prefix) noexcept -> bool {
    return sv.starts_with(prefix);
}

[[nodiscard]] inline auto ends_with(std::string_view sv, std::string_view suffix) noexcept -> bool {
    return sv.ends_with(suffix);
}

[[nodiscard]] inline auto contains(std::string_view sv, std::string_view needle) noexcept -> bool {
    return sv.find(needle) != std::string_view::npos;
}

[[nodiscard]] inline auto replace(std::string_view sv, std::string_view from, std::string_view to) -> std::string {
    return replace_all(sv, from, to);
}

} // namespace cfire::str

