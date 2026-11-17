#pragma once
// src/build/cache_paths.hpp — Unified cache root, config paths, atomic writes, JSON helpers & SHA-256

#include "../../runtime/cfire_prelude.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <string>
#include <string_view>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <process.h>
#include <stdlib.h>
#else
#include <unistd.h>
#endif

namespace cfire::build {

inline std::filesystem::path user_home_dir() {
#if defined(_WIN32)
    if (const char* up = std::getenv("USERPROFILE"); up && *up) {
        return std::filesystem::path(up);
    }
    const char* hd = std::getenv("HOMEDRIVE");
    const char* hp = std::getenv("HOMEPATH");
    if (hd && *hd && hp && *hp) {
        return std::filesystem::path(std::string(hd) + std::string(hp));
    }
#endif
    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::filesystem::path(home);
    }
    return {};
}

inline std::string current_user_tag() {
#if defined(_WIN32)
    if (const char* u = std::getenv("USERNAME"); u && *u) {
        std::string sanitized;
        for (char c : std::string_view(u)) {
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '_' || c == '-') {
                sanitized.push_back(c);
            }
        }
        if (!sanitized.empty()) return sanitized;
    }
    return "user";
#else
    return std::to_string(static_cast<unsigned long long>(::getuid()));
#endif
}

inline uint64_t current_pid() {
#if defined(_WIN32)
    return static_cast<uint64_t>(::_getpid());
#else
    return static_cast<uint64_t>(::getpid());
#endif
}

inline void set_process_env(const char* name, const std::string& value) {
    if (!name || !*name) return;
#if defined(_WIN32)
    ::_putenv_s(name, value.c_str());
#else
    ::setenv(name, value.c_str(), 1);
#endif
}

inline void unset_process_env(const char* name) {
    if (!name || !*name) return;
#if defined(_WIN32)
    ::_putenv_s(name, "");
#else
    ::unsetenv(name);
#endif
}

inline bool is_dir_writable(const std::filesystem::path& dir) {
    if (dir.empty()) return false;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) return false;
    static std::atomic<uint64_t> probe_seq{0};
    uint64_t seq = probe_seq.fetch_add(1, std::memory_order_relaxed);
    auto probe = dir / std::format(".write_probe_{}_{}", current_pid(), seq);
    {
        std::ofstream ofs(probe, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!ofs.is_open()) return false;
        ofs << "1";
        ofs.flush();
        if (!ofs) {
            ofs.close();
            std::filesystem::remove(probe, ec);
            return false;
        }
    }
    std::filesystem::remove(probe, ec);
    return true;
}

[[nodiscard]] inline std::filesystem::path resolve_configured_cache_root() {
    return cfire::detail::resolve_configured_cache_root();
}

inline std::filesystem::path user_cache_root_dir() {
    return cfire::detail::user_cache_dir("");
}

inline std::filesystem::path cache_subdir(std::string_view name) {
    return cfire::detail::user_cache_dir(name);
}

[[nodiscard]] inline auto user_libs_cache_dir() -> std::filesystem::path {
    return cache_subdir("libs");
}

// Header AST cache inside a build.cf workspace when present, or the user cache root otherwise.
[[nodiscard]] inline auto header_inspect_cache_dir(const std::filesystem::path& start_dir) -> std::filesystem::path {
    std::error_code ec;
    std::filesystem::path base = start_dir.empty() ? std::filesystem::path(".") : start_dir;
    auto cur = std::filesystem::weakly_canonical(base, ec);
    if (ec || cur.empty()) cur = std::filesystem::absolute(base, ec);
    if (std::filesystem::is_regular_file(cur, ec)) cur = cur.parent_path();
    for (int depth = 0; depth < 8 && !cur.empty(); ++depth) {
        if (std::filesystem::exists(cur / "build.cf", ec)) {
            auto proj_cache = cur / ".cfire" / "cache";
            std::filesystem::create_directories(proj_cache, ec);
            if (!ec && std::filesystem::is_directory(proj_cache, ec)) {
                return proj_cache;
            }
            break;
        }
        if (cur == cur.parent_path()) break;
        cur = cur.parent_path();
    }
    return cache_subdir("headers");
}

inline std::filesystem::path config_file_path() {
    if (const char* override_cfg = std::getenv("CFIRE_CONFIG_PATH");
        override_cfg && *override_cfg) {
        return std::filesystem::path(override_cfg);
    }
#if !defined(_WIN32)
    if (const char* xdg_cfg = std::getenv("XDG_CONFIG_HOME");
        xdg_cfg && *xdg_cfg) {
        return std::filesystem::path(xdg_cfg) / "cfire" / "config.json";
    }
#endif
    if (auto home = user_home_dir(); !home.empty()) {
        return home / ".cfire" / "config.json";
    }
    static cfire::detail::SecureTempDir fallback_cfg("cfire_cfg");
    return fallback_cfg.file("config.json");
}

inline bool read_text_file(const std::filesystem::path& path, std::string& out) {
    std::ifstream ifs(path, std::ios::in | std::ios::binary);
    if (!ifs.is_open()) return false;
    out.assign((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    return !ifs.bad();
}

inline bool atomic_write_text_file(
    const std::filesystem::path& target,
    std::string_view content,
    std::filesystem::perms perms = std::filesystem::perms::none
) {
    if (target.empty()) return false;
    std::error_code ec;
    if (target.has_parent_path()) {
        std::filesystem::create_directories(target.parent_path(), ec);
        if (ec) return false;
    }
    static std::atomic<uint64_t> tmp_seq{0};
    uint64_t seq = tmp_seq.fetch_add(1, std::memory_order_relaxed);
    auto tmp = std::filesystem::path(
        target.string() + std::format(".tmp.{}.{}", current_pid(), seq)
    );
    {
        std::ofstream ofs(tmp, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!ofs.is_open()) return false;
        if (!content.empty()) {
            ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
        }
        ofs.flush();
        if (!ofs) {
            ofs.close();
            std::filesystem::remove(tmp, ec);
            return false;
        }
    }
    if (perms != std::filesystem::perms::none) {
        std::filesystem::permissions(tmp, perms, std::filesystem::perm_options::replace, ec);
        ec.clear();
    }
    std::filesystem::rename(tmp, target, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

inline std::string json_escape_str(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    out += std::format("\\u{:04x}", static_cast<unsigned int>(c));
                } else {
                    out.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    return out;
}

inline void skip_json_ws(std::string_view src, size_t& pos) {
    while (pos < src.size()) {
        char c = src[pos];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            ++pos;
        } else {
            break;
        }
    }
}

inline bool parse_json_string_literal(std::string_view src, size_t& pos, std::string& out) {
    out.clear();
    if (pos >= src.size() || src[pos] != '"') return false;
    ++pos;
    while (pos < src.size()) {
        char c = src[pos++];
        if (c == '"') return true;
        if (c == '\\') {
            if (pos >= src.size()) return false;
            char esc = src[pos++];
            switch (esc) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    if (pos + 4 > src.size()) return false;
                    uint32_t cp = 0;
                    for (int i = 0; i < 4; ++i) {
                        char h = src[pos++];
                        cp <<= 4;
                        if (h >= '0' && h <= '9') cp |= static_cast<uint32_t>(h - '0');
                        else if (h >= 'a' && h <= 'f') cp |= static_cast<uint32_t>(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') cp |= static_cast<uint32_t>(h - 'A' + 10);
                        else return false;
                    }
                    if (cp < 0x80) {
                        out.push_back(static_cast<char>(cp));
                    } else if (cp < 0x800) {
                        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                    } else {
                        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                    }
                    break;
                }
                default:
                    out.push_back(esc);
                    break;
            }
        } else {
            out.push_back(c);
        }
    }
    return false;
}

inline bool skip_json_value(std::string_view src, size_t& pos) {
    skip_json_ws(src, pos);
    if (pos >= src.size()) return false;
    char c = src[pos];
    if (c == '"') {
        std::string tmp;
        return parse_json_string_literal(src, pos, tmp);
    }
    if (c == '{' || c == '[') {
        int depth = 0;
        bool in_str = false;
        while (pos < src.size()) {
            char ch = src[pos++];
            if (in_str) {
                if (ch == '\\' && pos < src.size()) {
                    ++pos;
                } else if (ch == '"') {
                    in_str = false;
                }
            } else {
                if (ch == '"') in_str = true;
                else if (ch == '{' || ch == '[') ++depth;
                else if (ch == '}' || ch == ']') {
                    --depth;
                    if (depth == 0) return true;
                }
            }
        }
        return false;
    }
    while (pos < src.size()) {
        char ch = src[pos];
        if (ch == ',' || ch == '}' || ch == ']' || ch == ' ' || ch == '\t' ||
            ch == '\r' || ch == '\n') {
            return true;
        }
        ++pos;
    }
    return true;
}

inline bool parse_flat_string_map_json(
    std::string_view src,
    std::map<std::string, std::string>& out
) {
    out.clear();
    size_t pos = 0;
    skip_json_ws(src, pos);
    if (pos >= src.size()) return true;
    if (src[pos] != '{') return false;
    ++pos;
    skip_json_ws(src, pos);
    if (pos < src.size() && src[pos] == '}') {
        ++pos;
        return true;
    }
    while (pos < src.size()) {
        skip_json_ws(src, pos);
        std::string key;
        if (!parse_json_string_literal(src, pos, key)) return false;
        skip_json_ws(src, pos);
        if (pos >= src.size() || src[pos] != ':') return false;
        ++pos;
        skip_json_ws(src, pos);
        if (pos < src.size() && src[pos] == '"') {
            std::string val;
            if (!parse_json_string_literal(src, pos, val)) return false;
            out[std::move(key)] = std::move(val);
        } else {
            if (!skip_json_value(src, pos)) return false;
        }
        skip_json_ws(src, pos);
        if (pos >= src.size()) return false;
        if (src[pos] == '}') {
            ++pos;
            return true;
        }
        if (src[pos] != ',') return false;
        ++pos;
    }
    return false;
}

inline std::string serialize_flat_string_map_json(const std::map<std::string, std::string>& m) {
    if (m.empty()) return "{}\n";
    std::string out = "{\n";
    size_t idx = 0;
    for (const auto& [k, v] : m) {
        out += "  \"";
        out += json_escape_str(k);
        out += "\": \"";
        out += json_escape_str(v);
        out += '"';
        if (++idx < m.size()) out.push_back(',');
        out.push_back('\n');
    }
    out += "}\n";
    return out;
}

namespace detail {

inline constexpr std::array<uint32_t, 64> kSha256K = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

inline uint32_t rotr32(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32u - n));
}

struct Sha256State {
    uint32_t h[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
    };
    uint8_t buf[64]{};
    size_t buf_len = 0;
    uint64_t total_bits = 0;

    void compress(const uint8_t* block) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) |
                   (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
                   (static_cast<uint32_t>(block[i * 4 + 3]));
        }
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
            uint32_t ch = (e & f) ^ ((~e) & g);
            uint32_t temp1 = hh + S1 + ch + kSha256K[static_cast<size_t>(i)] + w[i];
            uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t temp2 = S0 + maj;
            hh = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    void update(const uint8_t* data, size_t len) {
        total_bits += static_cast<uint64_t>(len) * 8ULL;
        size_t pos = 0;
        if (buf_len > 0) {
            size_t copy_n = std::min(64 - buf_len, len);
            std::memcpy(buf + buf_len, data, copy_n);
            buf_len += copy_n;
            pos += copy_n;
            if (buf_len == 64) {
                compress(buf);
                buf_len = 0;
            }
        }
        while (pos + 64 <= len) {
            compress(data + pos);
            pos += 64;
        }
        if (pos < len) {
            buf_len = len - pos;
            std::memcpy(buf, data + pos, buf_len);
        }
    }

    std::string final_hex() {
        buf[buf_len++] = 0x80;
        if (buf_len > 56) {
            while (buf_len < 64) buf[buf_len++] = 0;
            compress(buf);
            buf_len = 0;
        }
        while (buf_len < 56) buf[buf_len++] = 0;
        for (int i = 0; i < 8; ++i) {
            buf[56 + i] = static_cast<uint8_t>((total_bits >> ((7 - i) * 8)) & 0xFFu);
        }
        compress(buf);

        static constexpr char kHex[] = "0123456789abcdef";
        std::string out;
        out.reserve(64);
        for (int i = 0; i < 8; ++i) {
            for (int shift = 28; shift >= 0; shift -= 4) {
                out.push_back(kHex[(h[i] >> shift) & 0xFu]);
            }
        }
        return out;
    }
};

} // namespace detail

inline std::string sha256_bytes_hex(const uint8_t* data, size_t len) {
    detail::Sha256State st;
    if (data && len > 0) st.update(data, len);
    return st.final_hex();
}

inline std::string sha256_hex(std::string_view data) {
    return sha256_bytes_hex(reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

inline std::string sha256_file_hex(const std::filesystem::path& p, bool* ok) {
    std::ifstream ifs(p, std::ios::in | std::ios::binary);
    if (!ifs.is_open()) {
        if (ok) *ok = false;
        return {};
    }
    detail::Sha256State st;
    std::array<char, 65536> buf{};
    while (ifs.good()) {
        ifs.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        std::streamsize got = ifs.gcount();
        if (got > 0) {
            st.update(reinterpret_cast<const uint8_t*>(buf.data()), static_cast<size_t>(got));
        }
    }
    if (ifs.bad()) {
        if (ok) *ok = false;
        return {};
    }
    if (ok) *ok = true;
    return st.final_hex();
}

[[nodiscard]] inline auto sha256_file_hex(const std::filesystem::path& path)
    -> std::expected<std::string, std::string>
{
    std::ifstream ifs(path, std::ios::in | std::ios::binary);
    if (!ifs.is_open()) {
        return std::unexpected(std::format("failed to open '{}' for SHA-256 hashing", path.string()));
    }
    detail::Sha256State st;
    std::array<char, 65536> buf{};
    while (ifs.good()) {
        ifs.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        std::streamsize got = ifs.gcount();
        if (got > 0) {
            st.update(reinterpret_cast<const uint8_t*>(buf.data()), static_cast<size_t>(got));
        }
    }
    if (ifs.bad()) {
        return std::unexpected(std::format("I/O error while reading '{}' for SHA-256 hashing", path.string()));
    }
    return st.final_hex();
}

inline bool file_stat_fingerprint(
    const std::filesystem::path& p,
    int64_t& mtime_ns,
    uint64_t& file_size
) {
    std::error_code ec;
    if (!std::filesystem::exists(p, ec) || ec) return false;
    auto ft = std::filesystem::last_write_time(p, ec);
    if (ec) return false;
    auto sz = std::filesystem::file_size(p, ec);
    if (ec) sz = 0;
    mtime_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        ft.time_since_epoch()
    ).count();
    file_size = static_cast<uint64_t>(sz);
    return true;
}

inline std::string file_fingerprint_string(const std::filesystem::path& p) {
    std::error_code ec;
    auto canon = std::filesystem::canonical(p, ec);
    const auto& resolved = (!ec && !canon.empty()) ? canon : p;
    int64_t mtime_ns = 0;
    uint64_t file_size = 0;
    if (!file_stat_fingerprint(resolved, mtime_ns, file_size)) {
        return resolved.string();
    }
    return std::format("{}|{}|{}", resolved.string(), mtime_ns, file_size);
}

} // namespace cfire::build
