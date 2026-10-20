#pragma once
#include "../cfire_prelude.hpp"

#include <string>
#include <string_view>
#include <vector>
#include <span>

#include "str.generated.hpp"

namespace cfire::str {

// String manipulation routines powered by pure cfire implementation (lib/str.cf)

[[nodiscard]] inline auto trim(std::string_view sv) -> std::string {
    return ::trim(std::string(sv));
}

[[nodiscard]] inline auto split(std::string_view sv, std::string_view sep) -> std::vector<std::string> {
    return ::split(std::string(sv), std::string(sep));
}

[[nodiscard]] inline auto join(std::span<const std::string> parts, std::string_view sep) -> std::string {
    std::vector<std::string> vec(parts.begin(), parts.end());
    return ::join(vec, std::string(sep));
}

[[nodiscard]] inline auto to_upper(std::string_view sv) -> std::string {
    return ::to_upper(std::string(sv));
}

[[nodiscard]] inline auto to_lower(std::string_view sv) -> std::string {
    return ::to_lower(std::string(sv));
}

[[nodiscard]] inline auto replace_all(std::string_view sv, std::string_view from, std::string_view to) -> std::string {
    return ::replace_all(std::string(sv), std::string(from), std::string(to));
}

[[nodiscard]] inline auto replace(std::string_view sv, std::string_view from, std::string_view to) -> std::string {
    return ::replace(std::string(sv), std::string(from), std::string(to));
}

[[nodiscard]] inline auto starts_with(std::string_view sv, std::string_view prefix) noexcept -> bool {
    return ::starts_with(std::string(sv), std::string(prefix));
}

[[nodiscard]] inline auto ends_with(std::string_view sv, std::string_view suffix) noexcept -> bool {
    return ::ends_with(std::string(sv), std::string(suffix));
}

[[nodiscard]] inline auto contains(std::string_view sv, std::string_view needle) noexcept -> bool {
    return ::contains(std::string(sv), std::string(needle));
}

} // namespace cfire::str
