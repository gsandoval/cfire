#pragma once
#include "../cfire_prelude.hpp"

#include <span>
#include <vector>
#include <expected>
#include <cstdint>
#include <cstring>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
#include <compression.h>
#endif
#endif

#ifdef CFIRE_USE_ZSTD
#if defined(PLATFORM_IOS) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
namespace cfire::zstd {
[[nodiscard]] inline auto compress(std::span<const std::byte> src, std::int32_t level = 3)
    -> std::expected<std::vector<std::byte>, cfire::Error>
{
    (void)level;
    std::uint64_t orig_len = src.size();
    std::size_t cap = src.size() + 64 + (src.size() / 8);
    std::vector<std::byte> out(12 + cap);
    const char magic[4] = {'C', 'F', 'Z', '1'};
    std::memcpy(out.data(), magic, 4);
    std::memcpy(out.data() + 4, &orig_len, 8);
    if (orig_len == 0) {
        out.resize(12);
        return out;
    }
    std::size_t written = compression_encode_buffer(
        reinterpret_cast<uint8_t*>(out.data() + 12), cap,
        reinterpret_cast<const uint8_t*>(src.data()), src.size(),
        nullptr, COMPRESSION_LZFSE);
    if (written == 0) {
        std::memcpy(out.data() + 12, src.data(), src.size());
        out[3] = std::byte{'0'};
        out.resize(12 + src.size());
        return out;
    }
    out.resize(12 + written);
    return out;
}

[[nodiscard]] inline auto decompress(
    std::span<const std::byte> src,
    std::size_t max_output_bytes = 64 * 1024 * 1024
) -> std::expected<std::vector<std::byte>, cfire::Error>
{
    if (src.size() < 12) {
        return cfire::make_error("zstd invalid or truncated frame header");
    }
    char magic[4]{};
    std::memcpy(magic, src.data(), 4);
    if (magic[0] != 'C' || magic[1] != 'F' || magic[2] != 'Z' || (magic[3] != '1' && magic[3] != '0')) {
        return cfire::make_error("zstd unknown or invalid frame content size");
    }
    std::uint64_t content_size = 0;
    std::memcpy(&content_size, src.data() + 4, 8);
    if (content_size > max_output_bytes) {
        return cfire::make_error(std::format("zstd frame content size {} exceeds maximum allowed limit {}", content_size, max_output_bytes));
    }
    std::vector<std::byte> out(static_cast<std::size_t>(content_size));
    if (content_size == 0) return out;
    if (magic[3] == '0') {
        if (src.size() - 12 != content_size) return cfire::make_error("zstd raw frame size mismatch");
        std::memcpy(out.data(), src.data() + 12, static_cast<std::size_t>(content_size));
        return out;
    }
    std::size_t decoded = compression_decode_buffer(
        reinterpret_cast<uint8_t*>(out.data()), out.size(),
        reinterpret_cast<const uint8_t*>(src.data() + 12), src.size() - 12,
        nullptr, COMPRESSION_LZFSE);
    if (decoded != out.size()) {
        return cfire::make_error("zstd decompress error");
    }
    return out;
}

[[nodiscard]] inline auto compress(std::string_view src, std::int32_t level = 3)
    -> std::expected<std::vector<std::byte>, cfire::Error>
{
    return compress(std::as_bytes(std::span<const char>(src.data(), src.size())), level);
}
} // namespace cfire::zstd
#elif defined(PLATFORM_WEB)
namespace cfire::zstd {
[[nodiscard]] inline auto compress(std::span<const std::byte> src, std::int32_t = 3)
    -> std::expected<std::vector<std::byte>, cfire::Error>
{
    return std::vector<std::byte>(src.begin(), src.end());
}
[[nodiscard]] inline auto decompress(std::span<const std::byte> src, std::size_t = 64 * 1024 * 1024)
    -> std::expected<std::vector<std::byte>, cfire::Error>
{
    return std::vector<std::byte>(src.begin(), src.end());
}
} // namespace cfire::zstd
#else
#include <zstd.h>
namespace cfire::zstd {
[[nodiscard]] inline auto compress(std::span<const std::byte> src, std::int32_t level = 3)
    -> std::expected<std::vector<std::byte>, cfire::Error>
{
    std::size_t bound = ZSTD_compressBound(src.size());
    std::vector<std::byte> out(bound);
    std::size_t written = ZSTD_compress(out.data(), bound, src.data(), src.size(), level);
    if (ZSTD_isError(written)) {
        return cfire::make_error(std::format("zstd compress error: {}", ZSTD_getErrorName(written)));
    }
    out.resize(written);
    return out;
}

[[nodiscard]] inline auto decompress(
    std::span<const std::byte> src,
    std::size_t max_output_bytes = 64 * 1024 * 1024
) -> std::expected<std::vector<std::byte>, cfire::Error>
{
    unsigned long long content_size = ZSTD_getFrameContentSize(src.data(), src.size());
    if (content_size == ZSTD_CONTENTSIZE_ERROR || content_size == ZSTD_CONTENTSIZE_UNKNOWN) {
        return cfire::make_error("zstd unknown or invalid frame content size");
    }
    if (content_size > max_output_bytes) {
        return cfire::make_error(std::format("zstd frame content size {} exceeds maximum allowed limit {}", content_size, max_output_bytes));
    }
    std::vector<std::byte> out(static_cast<std::size_t>(content_size));
    std::size_t decoded = ZSTD_decompress(out.data(), out.size(), src.data(), src.size());
    if (ZSTD_isError(decoded)) {
        return cfire::make_error(std::format("zstd decompress error: {}", ZSTD_getErrorName(decoded)));
    }
    return out;
}

[[nodiscard]] inline auto compress(std::string_view src, std::int32_t level = 3)
    -> std::expected<std::vector<std::byte>, cfire::Error>
{
    return compress(std::as_bytes(std::span<const char>(src.data(), src.size())), level);
}
} // namespace cfire::zstd
#endif // PLATFORM_IOS / PLATFORM_WEB
#endif // CFIRE_USE_ZSTD

