#pragma once
// tests/unit/test_deflate_bounds.hpp —: truncated or garbage DEFLATE input stops at the end of the input.

#include "../test_harness.hpp"
#include "core/ai/doc_zip_deflate.hpp"

#include <random>

void test_deflate_stops_at_end_of_input([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_deflate_stops_at_end_of_input");
    namespace d = cfire::ai::detail;
    const std::string fox = "the quick brown fox jumps over the lazy dog, the quick brown fox";
    const std::string fox3 = fox + fox + fox;
    // `fox3` as raw DEFLATE (fixed Huffman codes, with back-references), from zlib at level 9.
    const std::vector<std::uint8_t> fox3_deflate = {
        0x2b, 0xc9, 0x48, 0x55, 0x28, 0x2c, 0xcd, 0x4c, 0xce, 0x56, 0x48, 0x2a, 0xca, 0x2f, 0xcf, 0x53, 0x48, 0xcb,
        0xaf, 0x50, 0xc8, 0x2a, 0xcd, 0x2d, 0x28, 0x56, 0xc8, 0x2f, 0x4b, 0x2d, 0x52, 0x28, 0x01, 0x4a, 0xe7, 0x24,
        0x56, 0x55, 0x2a, 0xa4, 0xe4, 0xa7, 0xeb, 0x80, 0x79, 0x68, 0x8a, 0x4b, 0x06, 0x58, 0x3f, 0x00};
    auto as_str = [](const std::vector<std::uint8_t>& v) { return std::string(v.begin(), v.end()); };

    // 1. A complete stream still decodes.
    require(as_str(d::inflate_raw(fox3_deflate)) == fox3, "a complete fixed-Huffman stream decodes");

    // 2. Every truncation decodes to a prefix of the text, never to more than the text.
    bool prefixes = true;
    for (std::size_t n = 1; n < fox3_deflate.size(); ++n) {
        auto out = as_str(d::inflate_raw(std::span(fox3_deflate.data(), n)));
        if (!fox3.starts_with(out)) {
            prefixes = false;
            std::println("    truncated to {} bytes gave {} bytes that are not a prefix", n, out.size());
            break;
        }
    }
    require(prefixes, "a truncated stream decodes to a prefix of its text");

    // 3. Random bytes with a dynamic-block header used to read zero bits past the end, which can decode as
    //    literals: 91 of these 300 inputs inflated to the 64 MiB cap. Now the output stays near the input size.
    std::mt19937 rng(1);
    std::size_t largest = 0;
    for (int i = 0; i < 300; ++i) {
        std::vector<std::uint8_t> in(64 + rng() % 512);
        for (auto& b : in) b = static_cast<std::uint8_t>(rng());
        in[0] = static_cast<std::uint8_t>((in[0] & ~0x7) | 0x5);  // BFINAL=1, BTYPE=2 (dynamic)
        largest = std::max(largest, d::inflate_raw(in).size());
        largest = std::max(largest, d::inflate_zlib_or_raw(in).size());
    }
    require(largest < 64 * 1024, std::format("garbage does not inflate to the cap (largest {} bytes)", largest));

    // 4. A stored block: LEN must match its complement NLEN, and a short block keeps what is there.
    std::vector<std::uint8_t> stored = {0x01, 0x05, 0x00, 0xfa, 0xff, 'h', 'e', 'l', 'l', 'o'};
    require(as_str(d::inflate_raw(stored)) == "hello", "a stored block decodes");
    stored[3] = 0x00;
    require(d::inflate_raw(stored).empty(), "a stored block whose NLEN is not ~LEN is rejected");

    // 5. Zip entries with a data descriptor take their size from the central directory (indexed once).
    {
        std::vector<std::uint8_t> zip;
        std::vector<std::uint8_t> cd;
        auto add = [&](std::string_view name, std::string_view body) {
            const auto offset = static_cast<std::uint32_t>(zip.size());
            d::append_u32_le(zip, 0x04034b50u);
            d::append_u16_le(zip, 20);
            d::append_u16_le(zip, 0x08);  // sizes follow in a data descriptor
            d::append_u16_le(zip, 0);     // stored
            for (int k = 0; k < 16; ++k) zip.push_back(0);
            d::append_u16_le(zip, static_cast<std::uint16_t>(name.size()));
            d::append_u16_le(zip, 0);
            zip.insert(zip.end(), name.begin(), name.end());
            zip.insert(zip.end(), body.begin(), body.end());
            d::append_u32_le(cd, 0x02014b50u);
            for (int k = 0; k < 16; ++k) cd.push_back(0);
            d::append_u32_le(cd, static_cast<std::uint32_t>(body.size()));  // compressed size, offset 20
            d::append_u32_le(cd, static_cast<std::uint32_t>(body.size()));
            d::append_u16_le(cd, static_cast<std::uint16_t>(name.size()));  // offset 28
            for (int k = 0; k < 12; ++k) cd.push_back(0);
            d::append_u32_le(cd, offset);
            cd.insert(cd.end(), name.begin(), name.end());
        };
        add("a.txt", "alpha");
        add("b.txt", "bravo!");
        zip.insert(zip.end(), cd.begin(), cd.end());
        auto entries = d::extract_zip_entries(zip);
        require(entries.size() == 2 && entries["a.txt"] == "alpha" && entries["b.txt"] == "bravo!",
            std::format("data-descriptor zip entries are sized from the central directory ({} entries)", entries.size()));
    }
}
