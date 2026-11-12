// Corpus replay driver (CF-141): links against any fuzz target without libFuzzer, so committed corpora
// run as regression tests on every toolchain (Apple clang ships without libFuzzer).
// Usage: <binary> <file-or-dir>...   Exit status is non-zero only if the target crashes.
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static void replay(const std::filesystem::path& p, std::size_t& count) {
    std::ifstream in(p, std::ios::binary);
    std::vector<char> buf((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    (void)LLVMFuzzerTestOneInput(reinterpret_cast<const uint8_t*>(buf.data()), buf.size());
    ++count;
}

int main(int argc, char** argv) {
    std::size_t count = 0;
    for (int i = 1; i < argc; ++i) {
        std::filesystem::path p(argv[i]);
        if (std::filesystem::is_directory(p)) {
            for (const auto& e : std::filesystem::recursive_directory_iterator(p)) {
                if (e.is_regular_file()) replay(e.path(), count);
            }
        } else if (std::filesystem::exists(p)) {
            replay(p, count);
        }
    }
    std::printf("replayed %zu input(s) without crashing\n", count);
    return 0;
}
