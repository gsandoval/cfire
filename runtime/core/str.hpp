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

// --- Substring Search & Index Primitives (STDLIB-P1-04) ---
[[nodiscard]] inline auto index_of(std::string_view sv, std::string_view substr, std::size_t start_pos = 0) noexcept -> std::int64_t {
    auto pos = sv.find(substr, start_pos);
    return (pos == std::string_view::npos) ? -1 : static_cast<std::int64_t>(pos);
}

[[nodiscard]] inline auto last_index_of(std::string_view sv, std::string_view substr) noexcept -> std::int64_t {
    auto pos = sv.rfind(substr);
    return (pos == std::string_view::npos) ? -1 : static_cast<std::int64_t>(pos);
}

[[nodiscard]] inline auto rfind(std::string_view sv, std::string_view substr, std::size_t start_pos = std::string_view::npos) noexcept -> std::int64_t {
    auto pos = sv.rfind(substr, start_pos);
    return (pos == std::string_view::npos) ? -1 : static_cast<std::int64_t>(pos);
}

// --- String & Slice Raw Pointer Interop for C FFI (LANG-P1-02) ---
[[nodiscard]] inline auto bytes(std::string_view sv) noexcept -> std::span<const std::byte> {
    return cfire::to_bytes(sv);
}

[[nodiscard]] inline auto byte_ptr(const std::string& s) noexcept -> const char* {
    return s.data();
}
[[nodiscard]] inline auto byte_ptr(std::string& s) noexcept -> char* {
    return s.data();
}
[[nodiscard]] inline auto byte_ptr(std::string_view sv) noexcept -> const char* {
    return sv.data();
}
[[nodiscard]] inline auto byte_ptr(const char* s) noexcept -> const char* {
    return s;
}

[[nodiscard]] inline auto byte_ptr_at(const std::string& s, std::int64_t offset) noexcept -> const char* {
    if (offset < 0) return s.data();
    if (static_cast<std::size_t>(offset) >= s.size()) return s.data() + s.size();
    return s.data() + offset;
}
[[nodiscard]] inline auto byte_ptr_at(std::string& s, std::int64_t offset) noexcept -> char* {
    if (offset < 0) return s.data();
    if (static_cast<std::size_t>(offset) >= s.size()) return s.data() + s.size();
    return s.data() + offset;
}
[[nodiscard]] inline auto byte_ptr_at(std::string_view sv, std::int64_t offset) noexcept -> const char* {
    if (offset < 0) return sv.data();
    if (static_cast<std::size_t>(offset) >= sv.size()) return sv.data() + sv.size();
    return sv.data() + offset;
}

template <typename T>
[[nodiscard]] inline auto byte_ptr(const std::vector<T>& vec) noexcept -> const T* {
    return vec.data();
}
template <typename T>
[[nodiscard]] inline auto byte_ptr(std::vector<T>& vec) noexcept -> T* {
    return vec.data();
}
template <typename T>
[[nodiscard]] inline auto byte_ptr(std::span<T> sp) noexcept -> T* {
    return sp.data();
}
template <typename T>
[[nodiscard]] inline auto byte_ptr(std::span<const T> sp) noexcept -> const T* {
    return sp.data();
}

template <typename T>
[[nodiscard]] inline auto byte_ptr_at(const std::vector<T>& vec, std::int64_t offset) noexcept -> const T* {
    if (offset < 0) return vec.data();
    if (static_cast<std::size_t>(offset) >= vec.size()) return vec.data() + vec.size();
    return vec.data() + offset;
}
template <typename T>
[[nodiscard]] inline auto byte_ptr_at(std::vector<T>& vec, std::int64_t offset) noexcept -> T* {
    if (offset < 0) return vec.data();
    if (static_cast<std::size_t>(offset) >= vec.size()) return vec.data() + vec.size();
    return vec.data() + offset;
}
template <typename T>
[[nodiscard]] inline auto byte_ptr_at(std::span<T> sp, std::int64_t offset) noexcept -> T* {
    if (offset < 0) return sp.data();
    if (static_cast<std::size_t>(offset) >= sp.size()) return sp.data() + sp.size();
    return sp.data() + offset;
}
template <typename T>
[[nodiscard]] inline auto byte_ptr_at(std::span<const T> sp, std::int64_t offset) noexcept -> const T* {
    if (offset < 0) return sp.data();
    if (static_cast<std::size_t>(offset) >= sp.size()) return sp.data() + sp.size();
    return sp.data() + offset;
}

} // namespace cfire::str

