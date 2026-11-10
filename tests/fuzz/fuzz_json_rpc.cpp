#include "../../src/lsp/json_rpc.hpp"
#include <cstdint>
#include <cstddef>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0 || size > 64 * 1024) return 0;
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    auto s = cfire::lsp::detail::extract_json_string(sv, "method");
    (void)s;
    auto id = cfire::lsp::detail::extract_json_id(sv);
    (void)id;
    auto i = cfire::lsp::detail::extract_json_int_after(sv, "line");
    (void)i;
    auto b = cfire::lsp::detail::extract_json_bool(sv, "apply");
    (void)b;
    return 0;
}
