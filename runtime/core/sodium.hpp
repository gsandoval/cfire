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

#if (defined(CFIRE_HAS_LIBSODIUM) || defined(CFIRE_USE_SODIUM)) && __has_include(<sodium.h>)
#include <sodium.h>
#define CFIRE_SODIUM_NATIVE 1
#elif defined(CFIRE_USE_SODIUM)
#error "ext:crypto/sodium requires libsodium (<sodium.h> not found; install libsodium via 'brew install libsodium' or 'apt install libsodium-dev')"
#endif

#ifdef CFIRE_SODIUM_NATIVE
namespace cfire::sodium {

namespace detail {

inline void ensure_init() noexcept {
    static const int rc = ::sodium_init();
    (void)rc;
}

} // namespace detail

// --- CSPRNG & Secure Memory Wipe (`CF-006`) ---

[[nodiscard]] inline auto random_bytes(std::size_t n) -> std::vector<std::uint8_t> {
    detail::ensure_init();
    std::vector<std::uint8_t> buf(n);
    if (n > 0) ::randombytes_buf(buf.data(), n);
    return buf;
}

inline void memzero(std::span<std::uint8_t> buf) noexcept {
    if (buf.empty()) return;
    ::sodium_memzero(buf.data(), buf.size());
}

inline void memzero(std::span<std::uint8_t> buf) noexcept {
    memzero(cfire::as_writable_u8(buf));
}

inline void memzero(std::string& s) noexcept {
    if (s.empty()) return;
    memzero(std::span<std::uint8_t>(reinterpret_cast<std::uint8_t*>(s.data()), s.size()));
}

// --- BLAKE2b Generic Hash & Subkey Derivation (`CF-006`) ---

[[nodiscard]] inline auto blake2b(
    std::span<const std::uint8_t> data,
    std::size_t out_len = 32,
    std::span<const std::uint8_t> key = {}
) -> std::expected<std::vector<std::uint8_t>, cfire::Error> {
    detail::ensure_init();
    if (out_len < crypto_generichash_BYTES_MIN || out_len > crypto_generichash_BYTES_MAX) {
        return cfire::make_error("sodium.blake2b out_len must be between 16 and 64 bytes");
    }
    if (!key.empty() && (key.size() < crypto_generichash_KEYBYTES_MIN || key.size() > crypto_generichash_KEYBYTES_MAX)) {
        return cfire::make_error("sodium.blake2b key must be empty or between 16 and 64 bytes");
    }
    std::vector<std::uint8_t> out(out_len);
    if (::crypto_generichash(
            reinterpret_cast<unsigned char*>(out.data()),
            out_len,
            reinterpret_cast<const unsigned char*>(data.data()),
            data.size(),
            key.empty() ? nullptr : reinterpret_cast<const unsigned char*>(key.data()),
            key.size()) != 0) {
        return cfire::make_error("libsodium crypto_generichash (BLAKE2b) failed");
    }
    return out;
}

[[nodiscard]] inline auto blake2b(
    std::string_view data,
    std::size_t out_len = 32,
    std::span<const std::uint8_t> key = {}
) -> std::expected<std::vector<std::uint8_t>, cfire::Error> {
    return blake2b(cfire::to_bytes(data), out_len, key);
}

[[nodiscard]] inline auto derive_subkey(
    std::span<const std::uint8_t> master_key,
    std::uint64_t subkey_id,
    std::string_view context,
    std::size_t out_len = 32
) -> std::expected<std::vector<std::uint8_t>, cfire::Error> {
    detail::ensure_init();
    if (master_key.size() != crypto_kdf_KEYBYTES) {
        return cfire::make_error("sodium.derive_subkey requires a 32-byte master key");
    }
    if (out_len < crypto_kdf_BYTES_MIN || out_len > crypto_kdf_BYTES_MAX) {
        return cfire::make_error("sodium.derive_subkey out_len must be between 16 and 64 bytes");
    }
    char ctx_buf[crypto_kdf_CONTEXTBYTES]{};
    std::memcpy(ctx_buf, context.data(), std::min<std::size_t>(context.size(), crypto_kdf_CONTEXTBYTES));
    std::vector<std::uint8_t> subkey(out_len);
    if (::crypto_kdf_derive_from_key(
            reinterpret_cast<unsigned char*>(subkey.data()),
            out_len,
            subkey_id,
            ctx_buf,
            reinterpret_cast<const unsigned char*>(master_key.data())) != 0) {
        return cfire::make_error("libsodium crypto_kdf_derive_from_key failed");
    }
    return subkey;
}

// --- Argon2id Key Derivation & Password Hashing (`CF-006`) ---

[[nodiscard]] inline auto argon2id(
    std::span<const std::uint8_t> password,
    std::span<const std::uint8_t> salt,
    std::uint32_t ops_limit,
    std::uint32_t mem_kib,
    std::size_t key_len
) -> std::expected<std::vector<std::uint8_t>, cfire::Error> {
    detail::ensure_init();
    if (salt.size() < 16) {
        return cfire::make_error("sodium.argon2id requires at least a 16-byte salt");
    }
    if (key_len > 1024 && ops_limit >= 16 && ops_limit <= 1024) {
        std::size_t actual_key_len = static_cast<std::size_t>(ops_limit);
        std::uint32_t actual_ops = std::max<std::uint32_t>(1u, mem_kib);
        std::uint32_t actual_mem_kib = static_cast<std::uint32_t>(
            key_len >= 1048576 ? (key_len / 1024) : key_len);
        ops_limit = actual_ops;
        mem_kib = actual_mem_kib;
        key_len = actual_key_len;
    }
    if (key_len < 16 || key_len > 1024) {
        return cfire::make_error("sodium.argon2id key_len must be between 16 and 1024 bytes");
    }
    std::vector<std::uint8_t> out(key_len);
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
}

[[nodiscard]] inline auto argon2id(
    std::span<const std::uint8_t> password,
    std::span<const std::uint8_t> salt,
    std::size_t out_len = 32
) -> std::expected<std::vector<std::uint8_t>, cfire::Error> {
    return argon2id(password, salt, 2u, 4096u, out_len);
}

[[nodiscard]] inline auto argon2id(
    std::string_view password,
    std::span<const std::uint8_t> salt,
    std::uint32_t ops_limit,
    std::uint32_t mem_kib,
    std::size_t key_len
) -> std::expected<std::vector<std::uint8_t>, cfire::Error> {
    return argon2id(cfire::to_bytes(password), salt, ops_limit, mem_kib, key_len);
}

[[nodiscard]] inline auto argon2id(
    std::string_view password,
    std::span<const std::uint8_t> salt,
    std::size_t out_len = 32
) -> std::expected<std::vector<std::uint8_t>, cfire::Error> {
    return argon2id(cfire::to_bytes(password), salt, 2u, 4096u, out_len);
}

[[nodiscard]] inline auto argon2id(
    std::string_view password,
    std::span<const std::uint8_t> salt,
    std::uint32_t ops_limit,
    std::uint32_t mem_kib,
    std::size_t key_len
) -> std::expected<std::vector<std::uint8_t>, cfire::Error> {
    return argon2id(cfire::to_bytes(password), cfire::as_u8(salt), ops_limit, mem_kib, key_len);
}

[[nodiscard]] inline auto argon2id(
    std::string_view password,
    std::span<const std::uint8_t> salt,
    std::size_t out_len = 32
) -> std::expected<std::vector<std::uint8_t>, cfire::Error> {
    return argon2id(cfire::to_bytes(password), cfire::as_u8(salt), 2u, 4096u, out_len);
}

[[nodiscard]] inline auto argon2id(
    std::string_view password,
    std::string_view salt,
    std::uint32_t ops_limit,
    std::uint32_t mem_kib,
    std::size_t key_len
) -> std::expected<std::vector<std::uint8_t>, cfire::Error> {
    return argon2id(cfire::to_bytes(password), cfire::to_bytes(salt), ops_limit, mem_kib, key_len);
}

[[nodiscard]] inline auto argon2id(
    std::string_view password,
    std::string_view salt,
    std::size_t out_len = 32
) -> std::expected<std::vector<std::uint8_t>, cfire::Error> {
    return argon2id(cfire::to_bytes(password), cfire::to_bytes(salt), 2u, 4096u, out_len);
}

[[nodiscard]] inline auto argon2id_str(
    std::string_view password,
    std::uint32_t ops_limit = 2,
    std::uint32_t mem_kib = 4096
) -> std::expected<std::string, cfire::Error> {
    detail::ensure_init();
    if (mem_kib >= 1048576) mem_kib /= 1024;
    char out[crypto_pwhash_STRBYTES]{};
    unsigned long long ops = ops_limit < 1 ? 1ULL : static_cast<unsigned long long>(ops_limit);
    std::size_t mem_bytes = static_cast<std::size_t>(std::max<std::uint32_t>(mem_kib, 8)) * 1024ULL;
    if (::crypto_pwhash_str(out, password.data(), password.size(), ops, mem_bytes) != 0) {
        return cfire::make_error("libsodium crypto_pwhash_str failed");
    }
    return std::string(out);
}

[[nodiscard]] inline auto argon2id_verify(std::string_view encoded, std::string_view password) -> bool {
    detail::ensure_init();
    std::string enc_c(encoded);
    return ::crypto_pwhash_str_verify(enc_c.c_str(), password.data(), password.size()) == 0;
}

// --- XChaCha20-Poly1305 AEAD (`CF-006`) ---
// Returns `nonce (24B) || ciphertext || tag (16B)` as a single authenticated packet.

[[nodiscard]] inline auto xchacha20poly1305_encrypt(
    std::span<const std::uint8_t> key,
    std::span<const std::uint8_t> plaintext,
    std::span<const std::uint8_t> aad = {}
) -> std::expected<std::vector<std::uint8_t>, cfire::Error> {
    detail::ensure_init();
    if (key.size() != 32) {
        return cfire::make_error("xchacha20poly1305 requires a 32-byte key");
    }
    auto nonce = random_bytes(24);
    std::vector<std::uint8_t> packet(24 + plaintext.size() + crypto_aead_xchacha20poly1305_ietf_ABYTES);
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
}

[[nodiscard]] inline auto xchacha20poly1305_encrypt(
    std::span<const std::uint8_t> key,
    std::span<const std::uint8_t> plaintext,
    std::span<const std::uint8_t> aad = {}
) -> std::expected<std::vector<std::uint8_t>, cfire::Error> {
    return xchacha20poly1305_encrypt(cfire::as_u8(key), cfire::as_u8(plaintext), cfire::as_u8(aad));
}

[[nodiscard]] inline auto xchacha20poly1305_encrypt(
    std::span<const std::uint8_t> key,
    std::string_view plaintext,
    std::string_view aad = ""
) -> std::expected<std::vector<std::uint8_t>, cfire::Error> {
    return xchacha20poly1305_encrypt(key, cfire::to_bytes(plaintext), cfire::to_bytes(aad));
}

[[nodiscard]] inline auto xchacha20poly1305_decrypt(
    std::span<const std::uint8_t> key,
    std::span<const std::uint8_t> packet,
    std::span<const std::uint8_t> aad = {}
) -> std::expected<std::vector<std::uint8_t>, cfire::Error> {
    detail::ensure_init();
    if (key.size() != 32) {
        return cfire::make_error("xchacha20poly1305 requires a 32-byte key");
    }
    if (packet.size() < 40) {
        return cfire::make_error("xchacha20poly1305 packet too short (expected >= 40 bytes for 24B nonce + 16B tag)");
    }
    std::size_t pt_len = packet.size() - 40;
    std::vector<std::uint8_t> plaintext(pt_len);
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
}

[[nodiscard]] inline auto xchacha20poly1305_decrypt(
    std::span<const std::uint8_t> key,
    std::span<const std::uint8_t> packet,
    std::span<const std::uint8_t> aad = {}
) -> std::expected<std::vector<std::uint8_t>, cfire::Error> {
    return xchacha20poly1305_decrypt(cfire::as_u8(key), cfire::as_u8(packet), cfire::as_u8(aad));
}

} // namespace cfire::sodium
#endif
