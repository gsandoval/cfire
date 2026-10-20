#pragma once
#include "../cfire_prelude.hpp"
#include "fs.hpp"

#include <span>
#include <vector>
#include <string>
#include <string_view>
#include <expected>
#include <filesystem>
#include <cstdint>
#include <format>
#include <sstream>

namespace cfire::base64 {

inline constexpr std::string_view kStdChars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
inline constexpr std::string_view kUrlChars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

[[nodiscard]] inline auto encode(std::span<const std::byte> data, bool url_safe = false) -> std::string {
    std::string_view tbl = url_safe ? kUrlChars : kStdChars;
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    std::size_t i = 0;
    while (i < data.size()) {
        std::size_t remaining = data.size() - i;
        std::uint32_t b0 = static_cast<std::uint8_t>(data[i]);
        std::uint32_t b1 = (remaining > 1) ? static_cast<std::uint8_t>(data[i + 1]) : 0;
        std::uint32_t b2 = (remaining > 2) ? static_cast<std::uint8_t>(data[i + 2]) : 0;
        std::uint32_t triple = (b0 << 16) | (b1 << 8) | b2;

        out.push_back(tbl[(triple >> 18) & 0x3F]);
        out.push_back(tbl[(triple >> 12) & 0x3F]);
        if (remaining > 1) {
            out.push_back(tbl[(triple >> 6) & 0x3F]);
        } else if (!url_safe) {
            out.push_back('=');
        }

        if (remaining > 2) {
            out.push_back(tbl[triple & 0x3F]);
        } else if (!url_safe) {
            out.push_back('=');
        }

        i += (remaining >= 3) ? 3 : remaining;
    }
    return out;
}

[[nodiscard]] inline auto encode_str(std::string_view s, bool url_safe = false) -> std::string {
    return encode(std::span<const std::byte>(reinterpret_cast<const std::byte*>(s.data()), s.size()), url_safe);
}

[[nodiscard]] inline auto decode(std::string_view s) -> std::expected<std::vector<std::byte>, cfire::Error> {
    auto decode_char = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+' || c == '-') return 62;
        if (c == '/' || c == '_') return 63;
        if (c == '=') return -1;
        return -2; // invalid
    };

    std::vector<std::byte> out;
    out.reserve((s.size() * 3) / 4);

    std::vector<int> quad;
    quad.reserve(4);

    for (char c : s) {
        if (std::isspace(static_cast<unsigned char>(c))) continue;
        int val = decode_char(c);
        if (val == -2) {
            return cfire::make_error(std::format("invalid base64 character '{}'", c));
        }
        quad.push_back(val);
        if (quad.size() == 4) {
            int b0 = quad[0], b1 = quad[1], b2 = quad[2], b3 = quad[3];
            if (b0 < 0 || b1 < 0) {
                return cfire::make_error("malformed base64 padding");
            }
            std::uint32_t triple = (static_cast<std::uint32_t>(b0) << 18) |
                                   (static_cast<std::uint32_t>(b1) << 12) |
                                   (static_cast<std::uint32_t>(b2 < 0 ? 0 : b2) << 6) |
                                   (static_cast<std::uint32_t>(b3 < 0 ? 0 : b3));

            out.push_back(static_cast<std::byte>((triple >> 16) & 0xFF));
            if (b2 != -1) out.push_back(static_cast<std::byte>((triple >> 8) & 0xFF));
            if (b3 != -1) out.push_back(static_cast<std::byte>(triple & 0xFF));
            quad.clear();
        }
    }

    if (!quad.empty()) {
        if (quad.size() == 1) {
            return cfire::make_error("truncated base64 input");
        }
        int b0 = quad[0];
        int b1 = quad[1];
        if (b0 < 0 || b1 < 0) return cfire::make_error("malformed base64");
        int b2 = (quad.size() > 2) ? quad[2] : -1;
        std::uint32_t triple = (static_cast<std::uint32_t>(b0) << 18) |
                               (static_cast<std::uint32_t>(b1) << 12) |
                               (static_cast<std::uint32_t>(b2 < 0 ? 0 : b2) << 6);
        out.push_back(static_cast<std::byte>((triple >> 16) & 0xFF));
        if (b2 != -1) out.push_back(static_cast<std::byte>((triple >> 8) & 0xFF));
    }

    return out;
}

[[nodiscard]] inline auto decode_str(std::string_view s) -> std::expected<std::string, cfire::Error> {
    auto b = decode(s);
    if (!b) return std::unexpected(b.error());
    return std::string(reinterpret_cast<const char*>(b->data()), b->size());
}

} // namespace cfire::base64

// --- `core:encoding/hex` (Hexadecimal Encoding/Decoding) ---
namespace cfire::hex {

[[nodiscard]] inline auto encode(std::span<const std::byte> data) -> std::string {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(data.size() * 2);
    for (std::byte b : data) {
        auto val = static_cast<std::uint8_t>(b);
        out.push_back(digits[(val >> 4) & 0x0F]);
        out.push_back(digits[val & 0x0F]);
    }
    return out;
}

[[nodiscard]] inline auto encode_str(std::string_view s) -> std::string {
    return encode(std::span<const std::byte>(reinterpret_cast<const std::byte*>(s.data()), s.size()));
}

[[nodiscard]] inline auto decode(std::string_view s) -> std::expected<std::vector<std::byte>, cfire::Error> {
    auto hex_val = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };

    if (s.size() % 2 != 0) {
        return cfire::make_error(std::format("odd hex string length ({})", s.size()));
    }
    std::vector<std::byte> out;
    out.reserve(s.size() / 2);
    for (std::size_t i = 0; i < s.size(); i += 2) {
        int hi = hex_val(s[i]);
        int lo = hex_val(s[i + 1]);
        if (hi < 0 || lo < 0) {
            return cfire::make_error(std::format("invalid hex character at offset {}", (hi < 0 ? i : i + 1)));
        }
        out.push_back(static_cast<std::byte>((hi << 4) | lo));
    }
    return out;
}

[[nodiscard]] inline auto decode_str(std::string_view s) -> std::expected<std::string, cfire::Error> {
    auto b = decode(s);
    if (!b) return std::unexpected(b.error());
    return std::string(reinterpret_cast<const char*>(b->data()), b->size());
}

} // namespace cfire::hex

// --- `core:encoding/csv` (RFC 4180 CSV Parsing & Stringification) ---
namespace cfire::csv {

[[nodiscard]] inline auto parse(std::string_view s) -> std::vector<std::vector<std::string>> {
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> current_row;
    std::string current_field;
    bool in_quotes = false;
    std::size_t i = 0;

    while (i < s.size()) {
        char c = s[i];
        if (in_quotes) {
            if (c == '"') {
                if (i + 1 < s.size() && s[i + 1] == '"') {
                    current_field.push_back('"');
                    i += 2;
                } else {
                    in_quotes = false;
                    ++i;
                }
            } else {
                current_field.push_back(c);
                ++i;
            }
        } else {
            if (c == '"') {
                in_quotes = true;
                ++i;
            } else if (c == ',') {
                current_row.push_back(std::move(current_field));
                current_field.clear();
                ++i;
            } else if (c == '\r') {
                if (i + 1 < s.size() && s[i + 1] == '\n') ++i;
                current_row.push_back(std::move(current_field));
                current_field.clear();
                rows.push_back(std::move(current_row));
                current_row.clear();
                ++i;
            } else if (c == '\n') {
                current_row.push_back(std::move(current_field));
                current_field.clear();
                rows.push_back(std::move(current_row));
                current_row.clear();
                ++i;
            } else {
                current_field.push_back(c);
                ++i;
            }
        }
    }

    if (!current_field.empty() || !current_row.empty()) {
        current_row.push_back(std::move(current_field));
        rows.push_back(std::move(current_row));
    }

    return rows;
}

[[nodiscard]] inline auto stringify(const std::vector<std::vector<std::string>>& rows) -> std::string {
    std::string out;
    for (std::size_t r = 0; r < rows.size(); ++r) {
        const auto& row = rows[r];
        for (std::size_t c = 0; c < row.size(); ++c) {
            if (c > 0) out.push_back(',');
            const auto& cell = row[c];
            bool needs_quote = cell.find_first_of(",\"\r\n") != std::string::npos;
            if (needs_quote) {
                out.push_back('"');
                for (char ch : cell) {
                    if (ch == '"') out.push_back('"');
                    out.push_back(ch);
                }
                out.push_back('"');
            } else {
                out += cell;
            }
        }
        out += "\r\n";
    }
    return out;
}

[[nodiscard]] inline auto read_all(std::string_view path)
    -> std::expected<std::vector<std::vector<std::string>>, cfire::Error>
{
    auto content = cfire::fs::read_text(path);
    if (!content) return std::unexpected(content.error());
    return parse(*content);
}

[[nodiscard]] inline auto write_all(std::string_view path, const std::vector<std::vector<std::string>>& rows)
    -> std::expected<void, cfire::Error>
{
    std::string data = stringify(rows);
    return cfire::fs::write_text(path, data);
}

} // namespace cfire::csv

// --- `core:net` (POSIX BSD TCP Sockets + Loopback Client/Server) ---

