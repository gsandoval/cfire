#pragma once
#include "../cfire_prelude.hpp"

#include <string>
#include <string_view>
#include <vector>
#include <cstdint>

namespace cfire::unicode {

inline auto decode_rune(std::string_view s, std::size_t& i) -> std::pair<std::uint32_t, std::size_t> {
    if (i >= s.size()) return {0, 0};
    unsigned char b0 = static_cast<unsigned char>(s[i]);
    if (b0 < 0x80) {
        ++i;
        return {b0, 1};
    }
    if ((b0 & 0xE0) == 0xC0) {
        if (i + 1 < s.size()) {
            unsigned char b1 = static_cast<unsigned char>(s[i + 1]);
            if ((b1 & 0xC0) == 0x80) {
                std::uint32_t cp = ((b0 & 0x1F) << 6) | (b1 & 0x3F);
                if (cp >= 0x80) { // Reject overlong
                    i += 2;
                    return {cp, 2};
                }
            }
        }
    } else if ((b0 & 0xF0) == 0xE0) {
        if (i + 2 < s.size()) {
            unsigned char b1 = static_cast<unsigned char>(s[i + 1]);
            unsigned char b2 = static_cast<unsigned char>(s[i + 2]);
            if ((b1 & 0xC0) == 0x80 && (b2 & 0xC0) == 0x80) {
                std::uint32_t cp = ((b0 & 0x0F) << 12) | ((b1 & 0x3F) << 6) | (b2 & 0x3F);
                // Reject overlong and surrogates (0xD800 - 0xDFFF)
                if (cp >= 0x800 && (cp < 0xD800 || cp > 0xDFFF)) {
                    i += 3;
                    return {cp, 3};
                }
            }
        }
    } else if ((b0 & 0xF8) == 0xF0) {
        if (i + 3 < s.size()) {
            unsigned char b1 = static_cast<unsigned char>(s[i + 1]);
            unsigned char b2 = static_cast<unsigned char>(s[i + 2]);
            unsigned char b3 = static_cast<unsigned char>(s[i + 3]);
            if ((b1 & 0xC0) == 0x80 && (b2 & 0xC0) == 0x80 && (b3 & 0xC0) == 0x80) {
                std::uint32_t cp = ((b0 & 0x07) << 18) | ((b1 & 0x3F) << 12) | ((b2 & 0x3F) << 6) | (b3 & 0x3F);
                if (cp >= 0x10000 && cp <= 0x10FFFF) {
                    i += 4;
                    return {cp, 4};
                }
            }
        }
    }
    // Invalid sequence: advance 1 byte and return Unicode replacement character U+FFFD
    ++i;
    return {0xFFFD, 1};
}

[[nodiscard]] inline auto is_valid_utf8(std::string_view s) -> bool {
    std::size_t i = 0;
    while (i < s.size()) {
        std::size_t prev = i;
        auto [cp, len] = decode_rune(s, i);
        if (cp == 0xFFFD && (i - prev != 3 || s.substr(prev, 3) != "\xEF\xBF\xBD")) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline auto rune_count(std::string_view s) -> std::size_t {
    std::size_t count = 0;
    std::size_t i = 0;
    while (i < s.size()) {
        decode_rune(s, i);
        ++count;
    }
    return count;
}

[[nodiscard]] inline auto runes(std::string_view s) -> std::vector<std::uint32_t> {
    std::vector<std::uint32_t> r;
    std::size_t i = 0;
    while (i < s.size()) {
        auto [cp, _] = decode_rune(s, i);
        r.push_back(cp);
    }
    return r;
}

[[nodiscard]] inline auto encode_rune(std::uint32_t cp) -> std::string {
    std::string out;
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
    } else {
        out += "\xEF\xBF\xBD";
    }
    return out;
}

[[nodiscard]] inline auto to_lower(std::string_view s) -> std::string {
    std::string out;
    out.reserve(s.size());
    std::size_t i = 0;
    while (i < s.size()) {
        auto [cp, len] = decode_rune(s, i);
        if (cp >= 'A' && cp <= 'Z') {
            cp += ('a' - 'A');
        } else if (cp >= 0x0410 && cp <= 0x042F) { // Cyrillic Capital A-Ya
            cp += 0x20;
        } else if (cp >= 0x0391 && cp <= 0x03A9 && cp != 0x03A2) { // Greek Capital
            cp += 0x20;
        }
        out += encode_rune(cp);
    }
    return out;
}

[[nodiscard]] inline auto to_upper(std::string_view s) -> std::string {
    std::string out;
    out.reserve(s.size());
    std::size_t i = 0;
    while (i < s.size()) {
        auto [cp, len] = decode_rune(s, i);
        if (cp >= 'a' && cp <= 'z') {
            cp -= ('a' - 'A');
        } else if (cp >= 0x0430 && cp <= 0x044F) { // Cyrillic Small a-ya
            cp -= 0x20;
        } else if (cp >= 0x03B1 && cp <= 0x03C9) { // Greek Small
            cp -= 0x20;
        }
        out += encode_rune(cp);
    }
    return out;
}

} // namespace cfire::unicode

