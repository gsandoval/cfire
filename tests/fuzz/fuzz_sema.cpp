// Parse + resilient Sema on arbitrary input (CF-141). Parse-only fuzzing missed CF-140's crash.
#include "../../src/sema.hpp"
#include <cstdint>
#include <cstddef>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0 || size > 64 * 1024) return 0;
    std::string_view src(reinterpret_cast<const char*>(data), size);
    auto parsed = cfire::frontend::parse_source_resilient("fuzz.cf", src);
    auto checked = cfire::frontend::analyze_resilient(std::move(parsed.ast));
    (void)checked;
    return 0;
}
