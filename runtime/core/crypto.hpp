#pragma once
#include "../cfire_prelude.hpp"

#include <span>
#include <vector>
#include <string>
#include <string_view>
#include <expected>
#include <filesystem>
#include <fstream>
#include <format>
#include <cstring>
#include <cstdint>

#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
#include <CommonCrypto/CommonHMAC.h>
#endif

namespace cfire::sha256 {

namespace detail {
    inline constexpr std::uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
    };

    constexpr inline auto rotr(std::uint32_t x, std::uint32_t n) noexcept -> std::uint32_t {
        return (x >> n) | (x << (32 - n));
    }

    inline void transform(std::uint32_t state[8], const std::uint8_t block[64]) noexcept {
        std::uint32_t w[64];
        for (std::size_t i = 0; i < 16; ++i) {
            w[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
                   (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
                   (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
                   (static_cast<std::uint32_t>(block[i * 4 + 3]));
        }
        for (std::size_t i = 16; i < 64; ++i) {
            std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        std::uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
        std::uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

        for (std::size_t i = 0; i < 64; ++i) {
            std::uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            std::uint32_t ch = (e & f) ^ ((~e) & g);
            std::uint32_t temp1 = h + S1 + ch + K[i] + w[i];
            std::uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            std::uint32_t temp2 = S0 + maj;

            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        state[0] += a; state[1] += b; state[2] += c; state[3] += d;
        state[4] += e; state[5] += f; state[6] += g; state[7] += h;
    }
} // namespace detail

[[nodiscard]] inline auto hash_bytes(std::span<const std::byte> data) -> std::vector<std::byte> {
#ifdef __APPLE__
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(data.data(), static_cast<CC_LONG>(data.size()), digest);
    std::vector<std::byte> out(CC_SHA256_DIGEST_LENGTH);
    std::memcpy(out.data(), digest, CC_SHA256_DIGEST_LENGTH);
    return out;
#else
    std::uint32_t state[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };
    std::uint64_t total_bits = data.size() * 8;
    std::size_t offset = 0;
    while (offset + 64 <= data.size()) {
        detail::transform(state, reinterpret_cast<const std::uint8_t*>(data.data() + offset));
        offset += 64;
    }

    std::uint8_t block[64] = {0};
    std::size_t rem = data.size() - offset;
    if (rem > 0) {
        std::memcpy(block, data.data() + offset, rem);
    }
    block[rem] = 0x80;
    if (rem >= 56) {
        detail::transform(state, block);
        std::memset(block, 0, 64);
    }
    for (std::size_t i = 0; i < 8; ++i) {
        block[63 - i] = static_cast<std::uint8_t>((total_bits >> (i * 8)) & 0xFF);
    }
    detail::transform(state, block);

    std::vector<std::byte> out(32);
    for (std::size_t i = 0; i < 8; ++i) {
        out[i * 4]     = static_cast<std::byte>((state[i] >> 24) & 0xFF);
        out[i * 4 + 1] = static_cast<std::byte>((state[i] >> 16) & 0xFF);
        out[i * 4 + 2] = static_cast<std::byte>((state[i] >> 8) & 0xFF);
        out[i * 4 + 3] = static_cast<std::byte>(state[i] & 0xFF);
    }
    return out;
#endif
}

[[nodiscard]] inline auto hash_hex(std::span<const std::byte> data) -> std::string {
    auto b = hash_bytes(data);
    std::string hex;
    hex.reserve(64);
    for (std::byte byte_val : b) {
        hex += std::format("{:02x}", static_cast<unsigned char>(byte_val));
    }
    return hex;
}

[[nodiscard]] inline auto hash_hex(std::string_view sv) -> std::string {
    return hash_hex(std::span<const std::byte>(reinterpret_cast<const std::byte*>(sv.data()), sv.size()));
}

[[nodiscard]] inline auto hash_file_hex(const std::filesystem::path& file_path) -> std::expected<std::string, std::string> {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file_path, ec)) {
        return std::unexpected(std::format("path '{}' is not a regular file", file_path.string()));
    }
    std::ifstream ifs(file_path, std::ios::binary);
    if (!ifs) {
        return std::unexpected(std::format("failed to open file '{}' for hashing", file_path.string()));
    }
#ifdef __APPLE__
    CC_SHA256_CTX ctx;
    CC_SHA256_Init(&ctx);
    char buf[65536];
    while (ifs.good()) {
        ifs.read(buf, sizeof(buf));
        auto bytes_read = ifs.gcount();
        if (bytes_read > 0) {
            CC_SHA256_Update(&ctx, buf, static_cast<CC_LONG>(bytes_read));
        }
    }
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256_Final(digest, &ctx);
    std::string hex;
    hex.reserve(64);
    for (unsigned char b : digest) {
        hex += std::format("{:02x}", b);
    }
    return hex;
#else
    std::vector<std::byte> all_data((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    return hash_hex(std::span<const std::byte>(all_data.data(), all_data.size()));
#endif
}

} // namespace cfire::sha256

namespace cfire::hmac {

[[nodiscard]] inline auto sha256_bytes(std::span<const std::byte> key, std::span<const std::byte> data) -> std::vector<std::byte> {
#ifdef __APPLE__
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CCHmac(kCCHmacAlgSHA256, key.data(), key.size(), data.data(), data.size(), digest);
    std::vector<std::byte> out(CC_SHA256_DIGEST_LENGTH);
    std::memcpy(out.data(), digest, CC_SHA256_DIGEST_LENGTH);
    return out;
#else
    std::uint8_t k0[64] = {0};
    if (key.size() > 64) {
        auto hk = cfire::sha256::hash_bytes(key);
        std::memcpy(k0, hk.data(), hk.size());
    } else {
        std::memcpy(k0, key.data(), key.size());
    }

    std::uint8_t ipad[64], opad[64];
    for (std::size_t i = 0; i < 64; ++i) {
        ipad[i] = k0[i] ^ 0x36;
        opad[i] = k0[i] ^ 0x5c;
    }

    std::vector<std::byte> inner_data;
    inner_data.reserve(64 + data.size());
    for (std::uint8_t b : ipad) inner_data.push_back(static_cast<std::byte>(b));
    inner_data.insert(inner_data.end(), data.begin(), data.end());
    auto inner_hash = cfire::sha256::hash_bytes(inner_data);

    std::vector<std::byte> outer_data;
    outer_data.reserve(64 + inner_hash.size());
    for (std::uint8_t b : opad) outer_data.push_back(static_cast<std::byte>(b));
    outer_data.insert(outer_data.end(), inner_hash.begin(), inner_hash.end());
    return cfire::sha256::hash_bytes(outer_data);
#endif
}

[[nodiscard]] inline auto sha256_hex(std::span<const std::byte> key, std::span<const std::byte> data) -> std::string {
    auto b = sha256_bytes(key, data);
    std::string hex;
    hex.reserve(64);
    for (std::byte byte_val : b) {
        hex += std::format("{:02x}", static_cast<unsigned char>(byte_val));
    }
    return hex;
}

[[nodiscard]] inline auto sha256_hex(std::string_view key, std::string_view data) -> std::string {
    return sha256_hex(
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(key.data()), key.size()),
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(data.data()), data.size())
    );
}

[[nodiscard]] inline auto equal(std::span<const std::byte> a, std::span<const std::byte> b) -> bool {
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        diff |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
    }
    return diff == 0;
}

[[nodiscard]] inline auto equal(std::string_view a, std::string_view b) -> bool {
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        diff |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
    }
    return diff == 0;
}

} // namespace cfire::hmac

// Backward compatibility alias for blake3
namespace cfire::blake3 {
[[nodiscard]] inline auto hash_hex(std::span<const std::byte> data) -> std::string {
    return cfire::sha256::hash_hex(data);
}
[[nodiscard]] inline auto hash_hex(std::string_view sv) -> std::string {
    return cfire::sha256::hash_hex(sv);
}
} // namespace cfire::blake3

// --- `core:crypto/aes` (AES-256 Symmetric Encryption, CTR and GCM AEAD) ---
namespace cfire::aes {

struct GcmResult {
    std::vector<std::byte> ciphertext;
    std::vector<std::byte> tag;
};

namespace detail {

inline constexpr std::uint8_t kSBox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16
};

inline constexpr std::uint32_t kRcon[10] = {
    0x01000000, 0x02000000, 0x04000000, 0x08000000, 0x10000000,
    0x20000000, 0x40000000, 0x80000000, 0x1B000000, 0x36000000
};

inline void aes256_key_expansion(const std::uint8_t key[32], std::uint32_t w[60]) {
    for (int i = 0; i < 8; ++i) {
        w[i] = (static_cast<std::uint32_t>(key[4 * i]) << 24) |
               (static_cast<std::uint32_t>(key[4 * i + 1]) << 16) |
               (static_cast<std::uint32_t>(key[4 * i + 2]) << 8) |
               (static_cast<std::uint32_t>(key[4 * i + 3]));
    }
    for (int i = 8; i < 60; ++i) {
        std::uint32_t temp = w[i - 1];
        if (i % 8 == 0) {
            temp = (temp << 8) | (temp >> 24);
            temp = (static_cast<std::uint32_t>(kSBox[(temp >> 24) & 0xFF]) << 24) |
                   (static_cast<std::uint32_t>(kSBox[(temp >> 16) & 0xFF]) << 16) |
                   (static_cast<std::uint32_t>(kSBox[(temp >> 8) & 0xFF]) << 8) |
                   (static_cast<std::uint32_t>(kSBox[temp & 0xFF]));
            temp ^= kRcon[(i / 8) - 1];
        } else if (i % 8 == 4) {
            temp = (static_cast<std::uint32_t>(kSBox[(temp >> 24) & 0xFF]) << 24) |
                   (static_cast<std::uint32_t>(kSBox[(temp >> 16) & 0xFF]) << 16) |
                   (static_cast<std::uint32_t>(kSBox[(temp >> 8) & 0xFF]) << 8) |
                   (static_cast<std::uint32_t>(kSBox[temp & 0xFF]));
        }
        w[i] = w[i - 8] ^ temp;
    }
}

inline auto gmul(std::uint8_t a, std::uint8_t b) -> std::uint8_t {
    std::uint8_t p = 0;
    for (int counter = 0; counter < 8; ++counter) {
        if (b & 1) p ^= a;
        std::uint8_t hi_bit_set = a & 0x80;
        a <<= 1;
        if (hi_bit_set) a ^= 0x1b;
        b >>= 1;
    }
    return p;
}

inline void aes256_encrypt_block(const std::uint32_t w[60], const std::uint8_t in[16], std::uint8_t out[16]) {
    std::uint8_t state[4][4];
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            state[r][c] = in[r + 4 * c];
        }
    }
    for (int c = 0; c < 4; ++c) {
        std::uint32_t word = w[c];
        state[0][c] ^= (word >> 24) & 0xFF;
        state[1][c] ^= (word >> 16) & 0xFF;
        state[2][c] ^= (word >> 8) & 0xFF;
        state[3][c] ^= word & 0xFF;
    }
    for (int round = 1; round <= 14; ++round) {
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) {
                state[r][c] = kSBox[state[r][c]];
            }
        }
        std::uint8_t t;
        t = state[1][0]; state[1][0] = state[1][1]; state[1][1] = state[1][2]; state[1][2] = state[1][3]; state[1][3] = t;
        t = state[2][0]; state[2][0] = state[2][2]; state[2][2] = t;
        t = state[2][1]; state[2][1] = state[2][3]; state[2][3] = t;
        t = state[3][3]; state[3][3] = state[3][2]; state[3][2] = state[3][1]; state[3][1] = state[3][0]; state[3][0] = t;

        if (round < 14) {
            for (int c = 0; c < 4; ++c) {
                std::uint8_t a0 = state[0][c], a1 = state[1][c], a2 = state[2][c], a3 = state[3][c];
                state[0][c] = gmul(2, a0) ^ gmul(3, a1) ^ a2 ^ a3;
                state[1][c] = a0 ^ gmul(2, a1) ^ gmul(3, a2) ^ a3;
                state[2][c] = a0 ^ a1 ^ gmul(2, a2) ^ gmul(3, a3);
                state[3][c] = gmul(3, a0) ^ a1 ^ a2 ^ gmul(2, a3);
            }
        }
        for (int c = 0; c < 4; ++c) {
            std::uint32_t word = w[round * 4 + c];
            state[0][c] ^= (word >> 24) & 0xFF;
            state[1][c] ^= (word >> 16) & 0xFF;
            state[2][c] ^= (word >> 8) & 0xFF;
            state[3][c] ^= word & 0xFF;
        }
    }
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            out[r + 4 * c] = state[r][c];
        }
    }
}

inline void inc_counter(std::uint8_t counter[16]) {
    for (int i = 15; i >= 12; --i) {
        if (++counter[i] != 0) break;
    }
}

inline void ghash_mult(std::uint8_t x[16], const std::uint8_t y[16]) {
    std::uint8_t v[16];
    std::memcpy(v, y, 16);
    std::uint8_t z[16] = {0};

    for (int i = 0; i < 128; ++i) {
        if ((x[i / 8] >> (7 - (i % 8))) & 1) {
            for (int j = 0; j < 16; ++j) z[j] ^= v[j];
        }
        bool lsb = v[15] & 1;
        for (int j = 15; j > 0; --j) {
            v[j] = (v[j] >> 1) | ((v[j - 1] & 1) << 7);
        }
        v[0] >>= 1;
        if (lsb) {
            v[0] ^= 0xe1;
        }
    }
    std::memcpy(x, z, 16);
}

inline void compute_ghash_tag(
    const std::uint8_t h[16],
    const std::uint8_t j0[16],
    const std::uint32_t w[60],
    const std::uint8_t* aad, std::size_t aad_len,
    const std::uint8_t* ct, std::size_t ct_len,
    std::uint8_t tag[16]
) {
    std::uint8_t s[16] = {0};
    if (aad && aad_len > 0) {
        for (std::size_t i = 0; i < aad_len; i += 16) {
            std::uint8_t blk[16] = {0};
            std::size_t n = std::min<std::size_t>(16, aad_len - i);
            std::memcpy(blk, aad + i, n);
            for (int k = 0; k < 16; ++k) s[k] ^= blk[k];
            ghash_mult(s, h);
        }
    }
    if (ct && ct_len > 0) {
        for (std::size_t i = 0; i < ct_len; i += 16) {
            std::uint8_t blk[16] = {0};
            std::size_t n = std::min<std::size_t>(16, ct_len - i);
            std::memcpy(blk, ct + i, n);
            for (int k = 0; k < 16; ++k) s[k] ^= blk[k];
            ghash_mult(s, h);
        }
    }
    std::uint8_t len_blk[16] = {0};
    std::uint64_t aad_bits = static_cast<std::uint64_t>(aad_len) * 8;
    std::uint64_t ct_bits = static_cast<std::uint64_t>(ct_len) * 8;
    for (int k = 0; k < 8; ++k) len_blk[7 - k] = static_cast<std::uint8_t>((aad_bits >> (k * 8)) & 0xFF);
    for (int k = 0; k < 8; ++k) len_blk[15 - k] = static_cast<std::uint8_t>((ct_bits >> (k * 8)) & 0xFF);
    for (int k = 0; k < 16; ++k) s[k] ^= len_blk[k];
    ghash_mult(s, h);

    std::uint8_t ej0[16];
    aes256_encrypt_block(w, j0, ej0);
    for (int i = 0; i < 16; ++i) tag[i] = s[i] ^ ej0[i];
}

} // namespace detail

[[nodiscard]] inline auto encrypt_ctr(
    std::span<const std::byte> key,
    std::span<const std::byte> iv,
    std::span<const std::byte> plaintext
) -> std::expected<std::vector<std::byte>, cfire::Error> {
    if (key.size() != 32) {
        return cfire::make_error("AES-256 requires a 32-byte (256-bit) key");
    }
    if (iv.size() != 16) {
        return cfire::make_error("AES-CTR requires a 16-byte initialization vector");
    }
    std::uint32_t w[60];
    detail::aes256_key_expansion(reinterpret_cast<const std::uint8_t*>(key.data()), w);

    std::uint8_t ctr[16];
    std::memcpy(ctr, iv.data(), 16);

    std::vector<std::byte> out(plaintext.size());
    const auto* pt = reinterpret_cast<const std::uint8_t*>(plaintext.data());
    auto* ct = reinterpret_cast<std::uint8_t*>(out.data());

    for (std::size_t i = 0; i < plaintext.size(); i += 16) {
        std::uint8_t ks[16];
        detail::aes256_encrypt_block(w, ctr, ks);
        detail::inc_counter(ctr);
        std::size_t n = std::min<std::size_t>(16, plaintext.size() - i);
        for (std::size_t j = 0; j < n; ++j) {
            ct[i + j] = pt[i + j] ^ ks[j];
        }
    }
    return out;
}

[[nodiscard]] inline auto decrypt_ctr(
    std::span<const std::byte> key,
    std::span<const std::byte> iv,
    std::span<const std::byte> ciphertext
) -> std::expected<std::vector<std::byte>, cfire::Error> {
    return encrypt_ctr(key, iv, ciphertext);
}

[[nodiscard]] inline auto encrypt_ctr(
    std::string_view key,
    std::string_view iv,
    std::string_view plaintext
) -> std::expected<std::vector<std::byte>, cfire::Error> {
    return encrypt_ctr(
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(key.data()), key.size()),
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(iv.data()), iv.size()),
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(plaintext.data()), plaintext.size())
    );
}

[[nodiscard]] inline auto decrypt_ctr(
    std::string_view key,
    std::string_view iv,
    std::span<const std::byte> ciphertext
) -> std::expected<std::vector<std::byte>, cfire::Error> {
    return decrypt_ctr(
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(key.data()), key.size()),
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(iv.data()), iv.size()),
        ciphertext
    );
}

[[nodiscard]] inline auto encrypt_gcm(
    std::span<const std::byte> key,
    std::span<const std::byte> nonce,
    std::span<const std::byte> plaintext,
    std::span<const std::byte> aad = {}
) -> std::expected<GcmResult, cfire::Error> {
    if (key.size() != 32) {
        return cfire::make_error("AES-256 requires a 32-byte (256-bit) key");
    }
    if (nonce.size() != 12) {
        return cfire::make_error("AES-GCM requires a 12-byte (96-bit) nonce");
    }

    std::uint32_t w[60];
    detail::aes256_key_expansion(reinterpret_cast<const std::uint8_t*>(key.data()), w);

    std::uint8_t h[16] = {0};
    std::uint8_t zero[16] = {0};
    detail::aes256_encrypt_block(w, zero, h);

    std::uint8_t j0[16] = {0};
    std::memcpy(j0, nonce.data(), 12);
    j0[15] = 1;

    std::uint8_t ctr[16];
    std::memcpy(ctr, j0, 16);
    detail::inc_counter(ctr);

    GcmResult res;
    res.ciphertext.resize(plaintext.size());
    const auto* pt = reinterpret_cast<const std::uint8_t*>(plaintext.data());
    auto* ct = reinterpret_cast<std::uint8_t*>(res.ciphertext.data());

    for (std::size_t i = 0; i < plaintext.size(); i += 16) {
        std::uint8_t ks[16];
        detail::aes256_encrypt_block(w, ctr, ks);
        detail::inc_counter(ctr);
        std::size_t n = std::min<std::size_t>(16, plaintext.size() - i);
        for (std::size_t j = 0; j < n; ++j) {
            ct[i + j] = pt[i + j] ^ ks[j];
        }
    }

    res.tag.resize(16);
    detail::compute_ghash_tag(
        h, j0, w,
        reinterpret_cast<const std::uint8_t*>(aad.data()), aad.size(),
        ct, res.ciphertext.size(),
        reinterpret_cast<std::uint8_t*>(res.tag.data())
    );

    return res;
}

[[nodiscard]] inline auto decrypt_gcm(
    std::span<const std::byte> key,
    std::span<const std::byte> nonce,
    std::span<const std::byte> ciphertext,
    std::span<const std::byte> tag,
    std::span<const std::byte> aad = {}
) -> std::expected<std::vector<std::byte>, cfire::Error> {
    if (key.size() != 32) {
        return cfire::make_error("AES-256 requires a 32-byte (256-bit) key");
    }
    if (nonce.size() != 12) {
        return cfire::make_error("AES-GCM requires a 12-byte (96-bit) nonce");
    }
    if (tag.size() != 16) {
        return cfire::make_error("AES-GCM authentication tag must be 16 bytes");
    }

    std::uint32_t w[60];
    detail::aes256_key_expansion(reinterpret_cast<const std::uint8_t*>(key.data()), w);

    std::uint8_t h[16] = {0};
    std::uint8_t zero[16] = {0};
    detail::aes256_encrypt_block(w, zero, h);

    std::uint8_t j0[16] = {0};
    std::memcpy(j0, nonce.data(), 12);
    j0[15] = 1;

    std::uint8_t expected_tag[16];
    detail::compute_ghash_tag(
        h, j0, w,
        reinterpret_cast<const std::uint8_t*>(aad.data()), aad.size(),
        reinterpret_cast<const std::uint8_t*>(ciphertext.data()), ciphertext.size(),
        expected_tag
    );

    unsigned char diff = 0;
    const auto* actual_tag = reinterpret_cast<const std::uint8_t*>(tag.data());
    for (int i = 0; i < 16; ++i) {
        diff |= actual_tag[i] ^ expected_tag[i];
    }
    if (diff != 0) {
        return cfire::make_error("AES-GCM authentication failure: tag mismatch or corrupted ciphertext");
    }

    std::uint8_t ctr[16];
    std::memcpy(ctr, j0, 16);
    detail::inc_counter(ctr);

    std::vector<std::byte> plaintext(ciphertext.size());
    const auto* ct = reinterpret_cast<const std::uint8_t*>(ciphertext.data());
    auto* pt = reinterpret_cast<std::uint8_t*>(plaintext.data());

    for (std::size_t i = 0; i < ciphertext.size(); i += 16) {
        std::uint8_t ks[16];
        detail::aes256_encrypt_block(w, ctr, ks);
        detail::inc_counter(ctr);
        std::size_t n = std::min<std::size_t>(16, ciphertext.size() - i);
        for (std::size_t j = 0; j < n; ++j) {
            pt[i + j] = ct[i + j] ^ ks[j];
        }
    }

    return plaintext;
}

[[nodiscard]] inline auto encrypt_gcm(
    std::string_view key,
    std::string_view nonce,
    std::string_view plaintext,
    std::string_view aad = ""
) -> std::expected<GcmResult, cfire::Error> {
    return encrypt_gcm(
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(key.data()), key.size()),
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(nonce.data()), nonce.size()),
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(plaintext.data()), plaintext.size()),
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(aad.data()), aad.size())
    );
}

[[nodiscard]] inline auto decrypt_gcm(
    std::string_view key,
    std::string_view nonce,
    std::span<const std::byte> ciphertext,
    std::span<const std::byte> tag,
    std::string_view aad = ""
) -> std::expected<std::vector<std::byte>, cfire::Error> {
    return decrypt_gcm(
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(key.data()), key.size()),
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(nonce.data()), nonce.size()),
        ciphertext,
        tag,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(aad.data()), aad.size())
    );
}

[[nodiscard]] inline auto decrypt_gcm_str(
    std::string_view key,
    std::string_view nonce,
    std::span<const std::byte> ciphertext,
    std::span<const std::byte> tag,
    std::string_view aad = ""
) -> std::expected<std::string, cfire::Error> {
    auto res = decrypt_gcm(key, nonce, ciphertext, tag, aad);
    if (!res) return std::unexpected(res.error());
    return std::string(reinterpret_cast<const char*>(res->data()), res->size());
}

} // namespace cfire::aes

// --- `core:encoding/base64` (RFC 4648 Base64 & URL-Safe Encoding/Decoding) ---

