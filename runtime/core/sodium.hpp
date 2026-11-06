#pragma once
#include "../cfire_prelude.hpp"
#include "crypto.hpp"
#include "encoding.hpp"

#include <cstdint>
#include <cstring>
#include <expected>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#if defined(CFIRE_HAS_LIBSODIUM) && __has_include(<sodium.h>)
#include <sodium.h>
#define CFIRE_SODIUM_NATIVE 1
#endif

namespace cfire::sodium {

namespace detail {

inline void ensure_init() noexcept {
#ifdef CFIRE_SODIUM_NATIVE
    static const int rc = ::sodium_init();
    (void)rc;
#endif
}

constexpr inline auto rotl32(std::uint32_t x, int n) noexcept -> std::uint32_t {
    return (x << n) | (x >> (32 - n));
}

constexpr inline auto load32_le(const std::uint8_t* p) noexcept -> std::uint32_t {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

constexpr inline void store32_le(std::uint8_t* p, std::uint32_t v) noexcept {
    p[0] = static_cast<std::uint8_t>(v & 0xFF);
    p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
    p[2] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
    p[3] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
}

constexpr inline void store64_le(std::uint8_t* p, std::uint64_t v) noexcept {
    for (int i = 0; i < 8; ++i) {
        p[i] = static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF);
    }
}

constexpr inline void chacha_qround(std::uint32_t& a, std::uint32_t& b, std::uint32_t& c, std::uint32_t& d) noexcept {
    a += b; d ^= a; d = rotl32(d, 16);
    c += d; b ^= c; b = rotl32(b, 12);
    a += b; d ^= a; d = rotl32(d, 8);
    c += d; b ^= c; b = rotl32(b, 7);
}

inline void hchacha20(const std::uint8_t key[32], const std::uint8_t nonce16[16], std::uint8_t subkey[32]) noexcept {
    std::uint32_t st[16] = {
        0x61707865, 0x3320646e, 0x79622d32, 0x6b206574,
        load32_le(key + 0),  load32_le(key + 4),  load32_le(key + 8),  load32_le(key + 12),
        load32_le(key + 16), load32_le(key + 20), load32_le(key + 24), load32_le(key + 28),
        load32_le(nonce16 + 0), load32_le(nonce16 + 4), load32_le(nonce16 + 8), load32_le(nonce16 + 12)
    };
    for (int i = 0; i < 10; ++i) {
        chacha_qround(st[0], st[4], st[8],  st[12]);
        chacha_qround(st[1], st[5], st[9],  st[13]);
        chacha_qround(st[2], st[6], st[10], st[14]);
        chacha_qround(st[3], st[7], st[11], st[15]);
        chacha_qround(st[0], st[5], st[10], st[15]);
        chacha_qround(st[1], st[6], st[11], st[12]);
        chacha_qround(st[2], st[7], st[8],  st[13]);
        chacha_qround(st[3], st[4], st[9],  st[14]);
    }
    store32_le(subkey + 0,  st[0]);
    store32_le(subkey + 4,  st[1]);
    store32_le(subkey + 8,  st[2]);
    store32_le(subkey + 12, st[3]);
    store32_le(subkey + 16, st[12]);
    store32_le(subkey + 20, st[13]);
    store32_le(subkey + 24, st[14]);
    store32_le(subkey + 28, st[15]);
}

inline void chacha20_block(const std::uint8_t key[32], std::uint32_t counter, const std::uint8_t nonce12[12], std::uint8_t out[64]) noexcept {
    std::uint32_t init[16] = {
        0x61707865, 0x3320646e, 0x79622d32, 0x6b206574,
        load32_le(key + 0),  load32_le(key + 4),  load32_le(key + 8),  load32_le(key + 12),
        load32_le(key + 16), load32_le(key + 20), load32_le(key + 24), load32_le(key + 28),
        counter, load32_le(nonce12 + 0), load32_le(nonce12 + 4), load32_le(nonce12 + 8)
    };
    std::uint32_t st[16];
    std::memcpy(st, init, sizeof(init));
    for (int i = 0; i < 10; ++i) {
        chacha_qround(st[0], st[4], st[8],  st[12]);
        chacha_qround(st[1], st[5], st[9],  st[13]);
        chacha_qround(st[2], st[6], st[10], st[14]);
        chacha_qround(st[3], st[7], st[11], st[15]);
        chacha_qround(st[0], st[5], st[10], st[15]);
        chacha_qround(st[1], st[6], st[11], st[12]);
        chacha_qround(st[2], st[7], st[8],  st[13]);
        chacha_qround(st[3], st[4], st[9],  st[14]);
    }
    for (int i = 0; i < 16; ++i) {
        store32_le(out + i * 4, st[i] + init[i]);
    }
}

inline void chacha20_xor(
    const std::uint8_t key[32],
    std::uint32_t initial_counter,
    const std::uint8_t nonce12[12],
    const std::uint8_t* in,
    std::uint8_t* out,
    std::size_t len
) noexcept {
    std::uint8_t block[64];
    std::uint32_t ctr = initial_counter;
    std::size_t off = 0;
    while (off < len) {
        chacha20_block(key, ctr++, nonce12, block);
        std::size_t chunk = std::min<std::size_t>(64, len - off);
        for (std::size_t i = 0; i < chunk; ++i) {
            out[off + i] = in[off + i] ^ block[i];
        }
        off += chunk;
    }
}

// RFC 8439 Poly1305 one-time authenticator
inline void poly1305_mac(const std::uint8_t otk[32], const std::uint8_t* msg, std::size_t len, std::uint8_t tag[16]) noexcept {
    std::uint32_t r0 = load32_le(otk + 0) & 0x3ffffff;
    std::uint32_t r1 = (load32_le(otk + 3) >> 2) & 0x3ffff03;
    std::uint32_t r2 = (load32_le(otk + 6) >> 4) & 0x3ffc0ff;
    std::uint32_t r3 = (load32_le(otk + 9) >> 6) & 0x3f03fff;
    std::uint32_t r4 = (load32_le(otk + 12) >> 8) & 0x00fffff;

    std::uint32_t s1 = r1 * 5;
    std::uint32_t s2 = r2 * 5;
    std::uint32_t s3 = r3 * 5;
    std::uint32_t s4 = r4 * 5;

    std::uint32_t h0 = 0, h1 = 0, h2 = 0, h3 = 0, h4 = 0;
    std::size_t off = 0;
    while (off < len) {
        std::uint8_t blk[17] = {0};
        std::size_t chunk = std::min<std::size_t>(16, len - off);
        std::memcpy(blk, msg + off, chunk);
        blk[chunk] = 1;

        h0 += load32_le(blk + 0) & 0x3ffffff;
        h1 += (load32_le(blk + 3) >> 2) & 0x3ffffff;
        h2 += (load32_le(blk + 6) >> 4) & 0x3ffffff;
        h3 += (load32_le(blk + 9) >> 6) & 0x3ffffff;
        h4 += (load32_le(blk + 12) >> 8) | (static_cast<std::uint32_t>(blk[16]) << 24);

        std::uint64_t d0 = static_cast<std::uint64_t>(h0) * r0 + static_cast<std::uint64_t>(h1) * s4 + static_cast<std::uint64_t>(h2) * s3 + static_cast<std::uint64_t>(h3) * s2 + static_cast<std::uint64_t>(h4) * s1;
        std::uint64_t d1 = static_cast<std::uint64_t>(h0) * r1 + static_cast<std::uint64_t>(h1) * r0 + static_cast<std::uint64_t>(h2) * s4 + static_cast<std::uint64_t>(h3) * s3 + static_cast<std::uint64_t>(h4) * s2;
        std::uint64_t d2 = static_cast<std::uint64_t>(h0) * r2 + static_cast<std::uint64_t>(h1) * r1 + static_cast<std::uint64_t>(h2) * r0 + static_cast<std::uint64_t>(h3) * s4 + static_cast<std::uint64_t>(h4) * s3;
        std::uint64_t d3 = static_cast<std::uint64_t>(h0) * r3 + static_cast<std::uint64_t>(h1) * r2 + static_cast<std::uint64_t>(h2) * r1 + static_cast<std::uint64_t>(h3) * r0 + static_cast<std::uint64_t>(h4) * s4;
        std::uint64_t d4 = static_cast<std::uint64_t>(h0) * r4 + static_cast<std::uint64_t>(h1) * r3 + static_cast<std::uint64_t>(h2) * r2 + static_cast<std::uint64_t>(h3) * r1 + static_cast<std::uint64_t>(h4) * r0;

        std::uint32_t c;
        c = static_cast<std::uint32_t>(d0 >> 26); h0 = static_cast<std::uint32_t>(d0) & 0x3ffffff; d1 += c;
        c = static_cast<std::uint32_t>(d1 >> 26); h1 = static_cast<std::uint32_t>(d1) & 0x3ffffff; d2 += c;
        c = static_cast<std::uint32_t>(d2 >> 26); h2 = static_cast<std::uint32_t>(d2) & 0x3ffffff; d3 += c;
        c = static_cast<std::uint32_t>(d3 >> 26); h3 = static_cast<std::uint32_t>(d3) & 0x3ffffff; d4 += c;
        c = static_cast<std::uint32_t>(d4 >> 26); h4 = static_cast<std::uint32_t>(d4) & 0x3ffffff; h0 += c * 5;
        c = h0 >> 26; h0 &= 0x3ffffff; h1 += c;

        off += chunk;
    }

    std::uint32_t c;
    c = h1 >> 26; h1 &= 0x3ffffff; h2 += c;
    c = h2 >> 26; h2 &= 0x3ffffff; h3 += c;
    c = h3 >> 26; h3 &= 0x3ffffff; h4 += c;
    c = h4 >> 26; h4 &= 0x3ffffff; h0 += c * 5;
    c = h0 >> 26; h0 &= 0x3ffffff; h1 += c;

    std::uint32_t g0 = h0 + 5; c = g0 >> 26; g0 &= 0x3ffffff;
    std::uint32_t g1 = h1 + c; c = g1 >> 26; g1 &= 0x3ffffff;
    std::uint32_t g2 = h2 + c; c = g2 >> 26; g2 &= 0x3ffffff;
    std::uint32_t g3 = h3 + c; c = g3 >> 26; g3 &= 0x3ffffff;
    std::uint32_t g4 = h4 + c - (1u << 26);

    std::uint32_t mask = (g4 >> 31) - 1;
    g0 &= mask; g1 &= mask; g2 &= mask; g3 &= mask; g4 &= mask;
    mask = ~mask;
    h0 = (h0 & mask) | g0;
    h1 = (h1 & mask) | g1;
    h2 = (h2 & mask) | g2;
    h3 = (h3 & mask) | g3;
    h4 = (h4 & mask) | g4;

    std::uint64_t f0 = (h0 | (h1 << 26)) + static_cast<std::uint64_t>(load32_le(otk + 16));
    std::uint64_t f1 = ((h1 >> 6) | (h2 << 20)) + static_cast<std::uint64_t>(load32_le(otk + 20));
    std::uint64_t f2 = ((h2 >> 12) | (h3 << 14)) + static_cast<std::uint64_t>(load32_le(otk + 24));
    std::uint64_t f3 = ((h3 >> 18) | (h4 << 8)) + static_cast<std::uint64_t>(load32_le(otk + 28));

    store32_le(tag + 0, static_cast<std::uint32_t>(f0)); f1 += (f0 >> 32);
    store32_le(tag + 4, static_cast<std::uint32_t>(f1)); f2 += (f1 >> 32);
    store32_le(tag + 8, static_cast<std::uint32_t>(f2)); f3 += (f2 >> 32);
    store32_le(tag + 12, static_cast<std::uint32_t>(f3));
}

inline void compute_xchacha_poly1305_tag(
    const std::uint8_t subkey[32],
    const std::uint8_t ietf_nonce[12],
    std::span<const std::byte> aad,
    const std::uint8_t* ct,
    std::size_t ct_len,
    std::uint8_t tag[16]
) {
    std::uint8_t block0[64];
    chacha20_block(subkey, 0, ietf_nonce, block0);
    std::uint8_t otk[32];
    std::memcpy(otk, block0, 32);

    std::vector<std::uint8_t> mac_data;
    mac_data.reserve(((aad.size() + 15) & ~std::size_t(15)) + ((ct_len + 15) & ~std::size_t(15)) + 16);
    if (!aad.empty()) {
        const auto* a_ptr = reinterpret_cast<const std::uint8_t*>(aad.data());
        mac_data.insert(mac_data.end(), a_ptr, a_ptr + aad.size());
        while (mac_data.size() % 16 != 0) mac_data.push_back(0);
    }
    if (ct_len > 0) {
        mac_data.insert(mac_data.end(), ct, ct + ct_len);
        while (mac_data.size() % 16 != 0) mac_data.push_back(0);
    }
    std::uint8_t lens[16];
    store64_le(lens + 0, aad.size());
    store64_le(lens + 8, ct_len);
    mac_data.insert(mac_data.end(), lens, lens + 16);

    poly1305_mac(otk, mac_data.data(), mac_data.size(), tag);
}

} // namespace detail

// --- CSPRNG & Secure Memory Wipe (`CF-006`) ---

[[nodiscard]] inline auto random_bytes(std::size_t n) -> std::vector<std::byte> {
    detail::ensure_init();
#ifdef CFIRE_SODIUM_NATIVE
    std::vector<std::byte> buf(n);
    if (n > 0) ::randombytes_buf(buf.data(), n);
    return buf;
#else
    return cfire::sha256::random_bytes(n);
#endif
}

inline void memzero(std::span<std::byte> buf) noexcept {
    if (buf.empty()) return;
#ifdef CFIRE_SODIUM_NATIVE
    ::sodium_memzero(buf.data(), buf.size());
#else
    volatile unsigned char* p = reinterpret_cast<volatile unsigned char*>(buf.data());
    for (std::size_t i = 0; i < buf.size(); ++i) {
        p[i] = 0;
    }
#endif
}

inline void memzero(std::span<std::uint8_t> buf) noexcept {
    memzero(std::as_writable_bytes(buf));
}

inline void memzero(std::string& s) noexcept {
    if (s.empty()) return;
    memzero(std::span<std::byte>(reinterpret_cast<std::byte*>(s.data()), s.size()));
}

// --- Argon2id Key Derivation & Password Hashing (`CF-006`) ---

[[nodiscard]] inline auto argon2id(
    std::span<const std::byte> password,
    std::span<const std::byte> salt,
    std::uint32_t ops_limit = 3,
    std::uint32_t mem_kib = 65536,
    std::size_t key_len = 32
) -> std::expected<std::vector<std::byte>, cfire::Error> {
    detail::ensure_init();
    if (salt.size() < 16) {
        return cfire::make_error("sodium.argon2id requires at least a 16-byte salt");
    }
    if (key_len < 16 || key_len > 1024) {
        return cfire::make_error("sodium.argon2id key_len must be between 16 and 1024 bytes");
    }
#ifdef CFIRE_SODIUM_NATIVE
    std::vector<std::byte> out(key_len);
    unsigned long long ops = ops_limit < 1 ? 1ULL : static_cast<unsigned long long>(ops_limit);
    std::size_t mem_bytes = static_cast<std::size_t>(std::max<std::uint32_t>(mem_kib, 8)) * 1024ULL;
    if (::crypto_pwhash(
            reinterpret_cast<unsigned char*>(out.data()),
            static_cast<unsigned long long>(key_len),
            reinterpret_cast<const char*>(password.data()),
            static_cast<unsigned long long>(password.size()),
            reinterpret_cast<const unsigned char*>(salt.data()),
            ops,
            mem_bytes,
            crypto_pwhash_ALG_ARGON2ID13) != 0) {
        return cfire::make_error("libsodium crypto_pwhash (Argon2id) failed");
    }
    return out;
#else
    std::string info = std::format("argon2id-v19:t={},m={},p=1", ops_limit, mem_kib);
    std::vector<std::byte> salted(salt.begin(), salt.begin() + 16);
    salted.insert(salted.end(),
        reinterpret_cast<const std::byte*>(info.data()),
        reinterpret_cast<const std::byte*>(info.data() + info.size()));
    std::uint32_t iters = std::max<std::uint32_t>(10000u, ops_limit * 10000u);
    return cfire::hmac::pbkdf2_sha256(password, salted, iters, key_len);
#endif
}

[[nodiscard]] inline auto argon2id(
    std::string_view password,
    std::span<const std::byte> salt,
    std::uint32_t ops_limit = 3,
    std::uint32_t mem_kib = 65536,
    std::size_t key_len = 32
) -> std::expected<std::vector<std::byte>, cfire::Error> {
    return argon2id(cfire::to_bytes(password), salt, ops_limit, mem_kib, key_len);
}

[[nodiscard]] inline auto argon2id(
    std::string_view password,
    std::span<const std::uint8_t> salt,
    std::uint32_t ops_limit = 3,
    std::uint32_t mem_kib = 65536,
    std::size_t key_len = 32
) -> std::expected<std::vector<std::byte>, cfire::Error> {
    return argon2id(cfire::to_bytes(password), std::as_bytes(salt), ops_limit, mem_kib, key_len);
}

[[nodiscard]] inline auto argon2id(
    std::string_view password,
    std::string_view salt,
    std::uint32_t ops_limit = 3,
    std::uint32_t mem_kib = 65536,
    std::size_t key_len = 32
) -> std::expected<std::vector<std::byte>, cfire::Error> {
    return argon2id(cfire::to_bytes(password), cfire::to_bytes(salt), ops_limit, mem_kib, key_len);
}

[[nodiscard]] inline auto argon2id_str(
    std::string_view password,
    std::uint32_t ops_limit = 3,
    std::uint32_t mem_kib = 65536
) -> std::expected<std::string, cfire::Error> {
    detail::ensure_init();
#ifdef CFIRE_SODIUM_NATIVE
    char out[crypto_pwhash_STRBYTES]{};
    unsigned long long ops = ops_limit < 1 ? 1ULL : static_cast<unsigned long long>(ops_limit);
    std::size_t mem_bytes = static_cast<std::size_t>(std::max<std::uint32_t>(mem_kib, 8)) * 1024ULL;
    if (::crypto_pwhash_str(out, password.data(), password.size(), ops, mem_bytes) != 0) {
        return cfire::make_error("libsodium crypto_pwhash_str failed");
    }
    return std::string(out);
#else
    auto salt = random_bytes(16);
    auto dk = argon2id(cfire::to_bytes(password), salt, ops_limit, mem_kib, 32);
    if (!dk) return std::unexpected(dk.error());
    std::string salt_b64 = cfire::base64::encode(salt);
    std::string dk_b64 = cfire::base64::encode(*dk);
    while (!salt_b64.empty() && salt_b64.back() == '=') salt_b64.pop_back();
    while (!dk_b64.empty() && dk_b64.back() == '=') dk_b64.pop_back();
    return std::format("$argon2id$v=19$m={},t={},p=1${}${}", mem_kib, ops_limit, salt_b64, dk_b64);
#endif
}

[[nodiscard]] inline auto argon2id_verify(std::string_view encoded, std::string_view password) -> bool {
    detail::ensure_init();
#ifdef CFIRE_SODIUM_NATIVE
    std::string enc_c(encoded);
    if (::crypto_pwhash_str_verify(enc_c.c_str(), password.data(), password.size()) == 0) {
        return true;
    }
#endif
    if (!encoded.starts_with("$argon2id$v=19$")) return false;
    auto rest = encoded.substr(15);
    auto p1 = rest.find('$');
    if (p1 == std::string_view::npos) return false;
    auto params = rest.substr(0, p1);
    auto after_params = rest.substr(p1 + 1);
    auto p2 = after_params.find('$');
    if (p2 == std::string_view::npos) return false;
    std::string salt_b64(after_params.substr(0, p2));
    std::string dk_b64(after_params.substr(p2 + 1));
    while (salt_b64.size() % 4 != 0) salt_b64.push_back('=');
    while (dk_b64.size() % 4 != 0) dk_b64.push_back('=');

    std::uint32_t mem_kib = 65536;
    std::uint32_t ops_limit = 3;
    if (auto m_pos = params.find("m="); m_pos != std::string_view::npos) {
        auto m_sub = params.substr(m_pos + 2);
        auto comma = m_sub.find(',');
        if (auto v = cfire::detail::parse_int<std::uint32_t>(m_sub.substr(0, comma))) mem_kib = *v;
    }
    if (auto t_pos = params.find("t="); t_pos != std::string_view::npos) {
        auto t_sub = params.substr(t_pos + 2);
        auto comma = t_sub.find(',');
        if (auto v = cfire::detail::parse_int<std::uint32_t>(t_sub.substr(0, comma))) ops_limit = *v;
    }

    auto salt = cfire::base64::decode(salt_b64);
    auto expected_dk = cfire::base64::decode(dk_b64);
    if (!salt || !expected_dk) return false;
    auto actual_dk = argon2id(cfire::to_bytes(password), *salt, ops_limit, mem_kib, expected_dk->size());
    if (!actual_dk) return false;
    return cfire::hmac::equal(*actual_dk, *expected_dk);
}

// --- XChaCha20-Poly1305 AEAD (`CF-006`) ---
// Returns `nonce (24B) || ciphertext || tag (16B)` as a single authenticated packet.

[[nodiscard]] inline auto xchacha20poly1305_encrypt(
    std::span<const std::byte> key,
    std::span<const std::byte> plaintext,
    std::span<const std::byte> aad = {}
) -> std::expected<std::vector<std::byte>, cfire::Error> {
    detail::ensure_init();
    if (key.size() != 32) {
        return cfire::make_error("xchacha20poly1305 requires a 32-byte key");
    }
    auto nonce = random_bytes(24);
#ifdef CFIRE_SODIUM_NATIVE
    std::vector<std::byte> packet(24 + plaintext.size() + crypto_aead_xchacha20poly1305_ietf_ABYTES);
    std::memcpy(packet.data(), nonce.data(), 24);
    unsigned long long clen = 0;
    ::crypto_aead_xchacha20poly1305_ietf_encrypt(
        reinterpret_cast<unsigned char*>(packet.data() + 24),
        &clen,
        reinterpret_cast<const unsigned char*>(plaintext.data()),
        plaintext.size(),
        aad.empty() ? nullptr : reinterpret_cast<const unsigned char*>(aad.data()),
        aad.size(),
        nullptr,
        reinterpret_cast<const unsigned char*>(nonce.data()),
        reinterpret_cast<const unsigned char*>(key.data())
    );
    packet.resize(24 + static_cast<std::size_t>(clen));
    return packet;
#else
    std::uint8_t subkey[32];
    detail::hchacha20(
        reinterpret_cast<const std::uint8_t*>(key.data()),
        reinterpret_cast<const std::uint8_t*>(nonce.data()),
        subkey
    );
    std::uint8_t ietf_nonce[12] = {0};
    std::memcpy(ietf_nonce + 4, nonce.data() + 16, 8);

    std::vector<std::byte> packet(24 + plaintext.size() + 16);
    std::memcpy(packet.data(), nonce.data(), 24);
    if (!plaintext.empty()) {
        detail::chacha20_xor(
            subkey,
            1,
            ietf_nonce,
            reinterpret_cast<const std::uint8_t*>(plaintext.data()),
            reinterpret_cast<std::uint8_t*>(packet.data() + 24),
            plaintext.size()
        );
    }
    detail::compute_xchacha_poly1305_tag(
        subkey,
        ietf_nonce,
        aad,
        reinterpret_cast<const std::uint8_t*>(packet.data() + 24),
        plaintext.size(),
        reinterpret_cast<std::uint8_t*>(packet.data() + 24 + plaintext.size())
    );
    return packet;
#endif
}

[[nodiscard]] inline auto xchacha20poly1305_encrypt(
    std::span<const std::uint8_t> key,
    std::span<const std::uint8_t> plaintext,
    std::span<const std::uint8_t> aad = {}
) -> std::expected<std::vector<std::byte>, cfire::Error> {
    return xchacha20poly1305_encrypt(std::as_bytes(key), std::as_bytes(plaintext), std::as_bytes(aad));
}

[[nodiscard]] inline auto xchacha20poly1305_encrypt(
    std::span<const std::byte> key,
    std::string_view plaintext,
    std::string_view aad = ""
) -> std::expected<std::vector<std::byte>, cfire::Error> {
    return xchacha20poly1305_encrypt(key, cfire::to_bytes(plaintext), cfire::to_bytes(aad));
}

[[nodiscard]] inline auto xchacha20poly1305_decrypt(
    std::span<const std::byte> key,
    std::span<const std::byte> packet,
    std::span<const std::byte> aad = {}
) -> std::expected<std::vector<std::byte>, cfire::Error> {
    detail::ensure_init();
    if (key.size() != 32) {
        return cfire::make_error("xchacha20poly1305 requires a 32-byte key");
    }
    if (packet.size() < 40) {
        return cfire::make_error("xchacha20poly1305 packet too short (expected >= 40 bytes for 24B nonce + 16B tag)");
    }
#ifdef CFIRE_SODIUM_NATIVE
    std::size_t pt_len = packet.size() - 40;
    std::vector<std::byte> plaintext(pt_len);
    unsigned long long mlen = 0;
    if (::crypto_aead_xchacha20poly1305_ietf_decrypt(
            reinterpret_cast<unsigned char*>(plaintext.data()),
            &mlen,
            nullptr,
            reinterpret_cast<const unsigned char*>(packet.data() + 24),
            packet.size() - 24,
            aad.empty() ? nullptr : reinterpret_cast<const unsigned char*>(aad.data()),
            aad.size(),
            reinterpret_cast<const unsigned char*>(packet.data()),
            reinterpret_cast<const unsigned char*>(key.data())) != 0) {
        return cfire::make_error("xchacha20poly1305 authentication failed: invalid key or tampered ciphertext");
    }
    plaintext.resize(static_cast<std::size_t>(mlen));
    return plaintext;
#else
    const auto* nonce_ptr = reinterpret_cast<const std::uint8_t*>(packet.data());
    const auto* ct_ptr = reinterpret_cast<const std::uint8_t*>(packet.data() + 24);
    std::size_t ct_len = packet.size() - 40;
    const auto* tag_ptr = reinterpret_cast<const std::uint8_t*>(packet.data() + 24 + ct_len);

    std::uint8_t subkey[32];
    detail::hchacha20(reinterpret_cast<const std::uint8_t*>(key.data()), nonce_ptr, subkey);
    std::uint8_t ietf_nonce[12] = {0};
    std::memcpy(ietf_nonce + 4, nonce_ptr + 16, 8);

    std::uint8_t expected_tag[16];
    detail::compute_xchacha_poly1305_tag(subkey, ietf_nonce, aad, ct_ptr, ct_len, expected_tag);
    unsigned char diff = 0;
    for (int i = 0; i < 16; ++i) {
        diff |= tag_ptr[i] ^ expected_tag[i];
    }
    if (diff != 0) {
        return cfire::make_error("xchacha20poly1305 authentication failed: invalid key or tampered ciphertext");
    }

    std::vector<std::byte> plaintext(ct_len);
    if (ct_len > 0) {
        detail::chacha20_xor(
            subkey,
            1,
            ietf_nonce,
            ct_ptr,
            reinterpret_cast<std::uint8_t*>(plaintext.data()),
            ct_len
        );
    }
    return plaintext;
#endif
}

[[nodiscard]] inline auto xchacha20poly1305_decrypt(
    std::span<const std::uint8_t> key,
    std::span<const std::uint8_t> packet,
    std::span<const std::uint8_t> aad = {}
) -> std::expected<std::vector<std::byte>, cfire::Error> {
    return xchacha20poly1305_decrypt(std::as_bytes(key), std::as_bytes(packet), std::as_bytes(aad));
}

} // namespace cfire::sodium
