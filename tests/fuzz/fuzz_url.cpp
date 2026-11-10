#include "../../runtime/core/net/url.hpp"
#include <cstdint>
#include <cstddef>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0 || size > 64 * 1024) return 0;
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    auto u = cfire::url::parse(sv);
    (void)u;
    auto dec = cfire::url::decode(sv);
    (void)dec;
    return 0;
}
