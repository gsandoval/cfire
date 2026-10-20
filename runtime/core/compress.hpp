#pragma once
#include "../cfire_prelude.hpp"

#include <span>
#include <vector>
#include <expected>
#include <cstdint>

#ifdef CFIRE_USE_ZSTD
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

[[nodiscard]] inline auto decompress(std::span<const std::byte> src)
    -> std::expected<std::vector<std::byte>, cfire::Error>
{
    unsigned long long content_size = ZSTD_getFrameContentSize(src.data(), src.size());
    if (content_size == ZSTD_CONTENTSIZE_ERROR || content_size == ZSTD_CONTENTSIZE_UNKNOWN) {
        return cfire::make_error("zstd unknown or invalid frame content size");
    }
    std::vector<std::byte> out(static_cast<std::size_t>(content_size));
    std::size_t decoded = ZSTD_decompress(out.data(), out.size(), src.data(), src.size());
    if (ZSTD_isError(decoded)) {
        return cfire::make_error(std::format("zstd decompress error: {}", ZSTD_getErrorName(decoded)));
    }
    return out;
}
} // namespace cfire::zstd
#endif

