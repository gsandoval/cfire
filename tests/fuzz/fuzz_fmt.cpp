// Formatter invariants (CF-141): formatting never crashes, is idempotent, and its output re-parses.
#include "../../src/formatter.hpp"
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0 || size > 64 * 1024) return 0;
    std::string_view src(reinterpret_cast<const char*>(data), size);
    auto once = cfire::fmt::format_source(src, "fuzz.cf");
    if (!once) return 0;
    if (!cfire::frontend::parse_source("fuzz.cf", *once)) std::abort();  // formatted output must re-parse
    auto twice = cfire::fmt::format_source(*once, "fuzz.cf");
    if (!twice || *twice != *once) std::abort();                       // formatting must be idempotent
    return 0;
}
