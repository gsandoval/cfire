#include "../../runtime/core/net/http.hpp"
#include <cstdint>
#include <cstddef>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0 || size > 64 * 1024) return 0;
    std::string_view raw(reinterpret_cast<const char*>(data), size);
    auto res = cfire::http::parse_request(raw);
    (void)res;
    return 0;
}
