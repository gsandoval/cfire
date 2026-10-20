#pragma once
#include "../cfire_prelude.hpp"
#include "fs.hpp"

#include <span>
#include <vector>
#include <string>
#include <string_view>
#include <expected>
#include <cstdint>

#include "encoding.generated.hpp"

// --- `core:encoding/base64` (RFC 4648 Base64 Encoding & Decoding powered by lib/encoding.cf) ---
namespace cfire::base64 {

[[nodiscard]] inline auto encode_str(std::string_view s, bool url_safe = false) -> std::string {
    return ::base64_encode(std::string(s), url_safe);
}

[[nodiscard]] inline auto encode(std::span<const std::byte> data, bool url_safe = false) -> std::string {
    std::string s(reinterpret_cast<const char*>(data.data()), data.size());
    return ::base64_encode(s, url_safe);
}

[[nodiscard]] inline auto decode_str(std::string_view s) -> std::expected<std::string, cfire::Error> {
    return ::base64_decode(std::string(s));
}

[[nodiscard]] inline auto decode(std::string_view s) -> std::expected<std::vector<std::byte>, cfire::Error> {
    auto res = ::base64_decode(std::string(s));
    if (!res) return std::unexpected(res.error());
    std::vector<std::byte> out(res->size());
    std::memcpy(out.data(), res->data(), res->size());
    return out;
}

} // namespace cfire::base64

// --- `core:encoding/hex` (Hexadecimal Encoding & Decoding powered by lib/encoding.cf) ---
namespace cfire::hex {

[[nodiscard]] inline auto encode_str(std::string_view s) -> std::string {
    return ::hex_encode(std::string(s));
}

[[nodiscard]] inline auto encode(std::span<const std::byte> data) -> std::string {
    std::string s(reinterpret_cast<const char*>(data.data()), data.size());
    return ::hex_encode(s);
}

[[nodiscard]] inline auto decode_str(std::string_view s) -> std::expected<std::string, cfire::Error> {
    return ::hex_decode(std::string(s));
}

[[nodiscard]] inline auto decode(std::string_view s) -> std::expected<std::vector<std::byte>, cfire::Error> {
    auto res = ::hex_decode(std::string(s));
    if (!res) return std::unexpected(res.error());
    std::vector<std::byte> out(res->size());
    std::memcpy(out.data(), res->data(), res->size());
    return out;
}

} // namespace cfire::hex

// --- `core:encoding/csv` (RFC 4180 CSV Parsing & Stringification powered by lib/encoding.cf) ---
namespace cfire::csv {

[[nodiscard]] inline auto parse(std::string_view s) -> std::vector<std::vector<std::string>> {
    return ::csv_parse(std::string(s));
}

[[nodiscard]] inline auto stringify(const std::vector<std::vector<std::string>>& rows) -> std::string {
    return ::csv_stringify(rows);
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
