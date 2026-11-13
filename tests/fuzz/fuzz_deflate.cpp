#include "../../runtime/core/ai/doc_zip_deflate.hpp"
#include <cstdint>
#include <cstddef>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0 || size > 64 * 1024) return 0;
    auto sp = std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(data), size);
    auto raw = cfire::ai::detail::inflate_raw(sp, 1024 * 1024);
    (void)raw;
    auto zlib = cfire::ai::detail::inflate_zlib_or_raw(sp);
    (void)zlib;
    auto entries = cfire::ai::detail::extract_zip_entries(sp);
    (void)entries;
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    auto unesc = cfire::ai::detail::xml_unescape(sv);
    (void)unesc;
    auto runs = cfire::ai::detail::extract_xml_text_runs(sv, "t");
    (void)runs;
    return 0;
}
