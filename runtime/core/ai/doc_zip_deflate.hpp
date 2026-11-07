#pragma once
// runtime/core/ai/doc_zip_deflate.hpp — Zero-Dependency DEFLATE, ZLIB, ZIP & XML Helpers

#include "vectors.hpp"

namespace cfire::ai::detail {

// --- Zero-Dependency RFC 1951 (DEFLATE) / RFC 1950 (ZLIB) & ZIP Engine ---

struct BitStream {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
    std::size_t byte_pos = 0;
    std::uint32_t bit_buf = 0;
    int bit_count = 0;

    void fill(int need) noexcept {
        while (bit_count < need && byte_pos < size) {
            bit_buf |= static_cast<std::uint32_t>(data[byte_pos++]) << bit_count;
            bit_count += 8;
        }
    }

    [[nodiscard]] auto read_bits(int n) noexcept -> std::uint32_t {
        if (n == 0) return 0;
        fill(n);
        std::uint32_t val = bit_buf & ((1u << n) - 1u);
        bit_buf >>= n;
        bit_count -= n;
        return val;
    }

    void align_byte() noexcept {
        int drop = bit_count & 7;
        bit_buf >>= drop;
        bit_count -= drop;
    }
};

struct HuffTree {
    std::array<std::uint16_t, 16> counts{};
    std::array<std::uint16_t, 288> symbols{};

    void build(std::span<const std::uint8_t> lengths) noexcept {
        counts.fill(0);
        symbols.fill(0);
        for (std::uint8_t len : lengths) {
            if (len > 0 && len < 16) counts[len]++;
        }
        std::array<std::uint16_t, 16> offs{};
        std::uint16_t acc = 0;
        for (std::size_t i = 1; i < 16; ++i) {
            offs[i] = acc;
            acc += counts[i];
        }
        for (std::size_t sym = 0; sym < lengths.size(); ++sym) {
            std::uint8_t len = lengths[sym];
            if (len > 0 && len < 16) {
                symbols[offs[len]++] = static_cast<std::uint16_t>(sym);
            }
        }
    }

    [[nodiscard]] auto decode(BitStream& bs) const noexcept -> int {
        int code = 0;
        int first = 0;
        int index = 0;
        for (int len = 1; len < 16; ++len) {
            code |= static_cast<int>(bs.read_bits(1));
            int count = counts[static_cast<std::size_t>(len)];
            if (code - count < first) {
                return symbols[static_cast<std::size_t>(index + (code - first))];
            }
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        return -1;
    }
};

[[nodiscard]] inline auto inflate_raw(std::span<const std::byte> src, std::size_t max_out = 64 * 1024 * 1024) -> std::vector<std::byte> {
    std::vector<std::byte> out;
    if (src.empty()) return out;
    BitStream bs{reinterpret_cast<const std::uint8_t*>(src.data()), src.size(), 0, 0, 0};

    static constexpr std::uint16_t len_base[29] = {
        3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258
    };
    static constexpr std::uint8_t len_extra[29] = {
        0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0
    };
    static constexpr std::uint16_t dist_base[30] = {
        1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
    };
    static constexpr std::uint8_t dist_extra[30] = {
        0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
    };
    static constexpr std::uint8_t clen_order[19] = {
        16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
    };

    std::uint32_t bfinal = 0;
    while (!bfinal && (bs.byte_pos < bs.size || bs.bit_count > 0)) {
        bfinal = bs.read_bits(1);
        std::uint32_t btype = bs.read_bits(2);
        if (btype == 0) {
            bs.align_byte();
            std::uint32_t len = bs.read_bits(16);
            std::uint32_t nlen = bs.read_bits(16);
            (void)nlen;
            if (out.size() + len > max_out) break;
            for (std::uint32_t i = 0; i < len && bs.byte_pos < bs.size; ++i) {
                out.push_back(static_cast<std::byte>(bs.data[bs.byte_pos++]));
            }
        } else if (btype == 1 || btype == 2) {
            HuffTree lit_tree{};
            HuffTree dist_tree{};
            if (btype == 1) {
                std::array<std::uint8_t, 288> lit_lens{};
                for (std::size_t i = 0; i <= 143; ++i) lit_lens[i] = 8;
                for (std::size_t i = 144; i <= 255; ++i) lit_lens[i] = 9;
                for (std::size_t i = 256; i <= 279; ++i) lit_lens[i] = 7;
                for (std::size_t i = 280; i <= 287; ++i) lit_lens[i] = 8;
                std::array<std::uint8_t, 30> dist_lens{};
                dist_lens.fill(5);
                lit_tree.build(lit_lens);
                dist_tree.build(dist_lens);
            } else {
                std::uint32_t hlit = bs.read_bits(5) + 257;
                std::uint32_t hdist = bs.read_bits(5) + 1;
                std::uint32_t hclen = bs.read_bits(4) + 4;
                if (hlit > 286 || hdist > 30) break;
                std::array<std::uint8_t, 19> clen_lens{};
                for (std::uint32_t i = 0; i < hclen; ++i) {
                    clen_lens[clen_order[i]] = static_cast<std::uint8_t>(bs.read_bits(3));
                }
                HuffTree clen_tree{};
                clen_tree.build(clen_lens);
                std::vector<std::uint8_t> all_lens(hlit + hdist, 0);
                std::size_t idx = 0;
                while (idx < all_lens.size()) {
                    int sym = clen_tree.decode(bs);
                    if (sym < 0) break;
                    if (sym < 16) {
                        all_lens[idx++] = static_cast<std::uint8_t>(sym);
                    } else if (sym == 16) {
                        std::uint32_t rep = bs.read_bits(2) + 3;
                        std::uint8_t prev = idx > 0 ? all_lens[idx - 1] : 0;
                        while (rep-- > 0 && idx < all_lens.size()) all_lens[idx++] = prev;
                    } else if (sym == 17) {
                        std::uint32_t rep = bs.read_bits(3) + 3;
                        while (rep-- > 0 && idx < all_lens.size()) all_lens[idx++] = 0;
                    } else if (sym == 18) {
                        std::uint32_t rep = bs.read_bits(7) + 11;
                        while (rep-- > 0 && idx < all_lens.size()) all_lens[idx++] = 0;
                    }
                }
                lit_tree.build(std::span<const std::uint8_t>(all_lens.data(), hlit));
                dist_tree.build(std::span<const std::uint8_t>(all_lens.data() + hlit, hdist));
            }

            while (true) {
                int sym = lit_tree.decode(bs);
                if (sym < 0 || sym == 256) break;
                if (sym < 256) {
                    if (out.size() >= max_out) break;
                    out.push_back(static_cast<std::byte>(sym));
                } else {
                    int l_idx = sym - 257;
                    if (l_idx < 0 || l_idx >= 29) break;
                    std::size_t length = len_base[l_idx] + bs.read_bits(len_extra[l_idx]);
                    int d_idx = dist_tree.decode(bs);
                    if (d_idx < 0 || d_idx >= 30) break;
                    std::size_t dist = dist_base[d_idx] + bs.read_bits(dist_extra[d_idx]);
                    if (dist == 0 || dist > out.size() || out.size() + length > max_out) break;
                    for (std::size_t k = 0; k < length; ++k) {
                        out.push_back(out[out.size() - dist]);
                    }
                }
            }
        } else {
            break;
        }
    }
    return out;
}

[[nodiscard]] inline auto inflate_zlib_or_raw(std::span<const std::byte> src) -> std::vector<std::byte> {
    if (src.size() >= 2) {
        auto b0 = static_cast<std::uint8_t>(src[0]);
        auto b1 = static_cast<std::uint8_t>(src[1]);
        if ((b0 & 0x0F) == 8 && ((static_cast<std::uint32_t>(b0) * 256u + b1) % 31u == 0)) {
            std::size_t hdr = (b1 & 0x20) ? 6 : 2;
            if (src.size() > hdr) {
                auto dec = inflate_raw(src.subspan(hdr));
                if (!dec.empty()) return dec;
            }
        }
    }
    return inflate_raw(src);
}

[[nodiscard]] inline auto read_u16_le(const std::byte* p) noexcept -> std::uint16_t {
    return static_cast<std::uint16_t>(static_cast<std::uint8_t>(p[0])) |
           (static_cast<std::uint16_t>(static_cast<std::uint8_t>(p[1])) << 8);
}

[[nodiscard]] inline auto read_u32_le(const std::byte* p) noexcept -> std::uint32_t {
    return static_cast<std::uint32_t>(static_cast<std::uint8_t>(p[0])) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(p[1])) << 8) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(p[2])) << 16) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(p[3])) << 24);
}

inline void append_u16_le(std::vector<std::byte>& buf, std::uint16_t v) {
    buf.push_back(static_cast<std::byte>(v & 0xFF));
    buf.push_back(static_cast<std::byte>((v >> 8) & 0xFF));
}

inline void append_u32_le(std::vector<std::byte>& buf, std::uint32_t v) {
    buf.push_back(static_cast<std::byte>(v & 0xFF));
    buf.push_back(static_cast<std::byte>((v >> 8) & 0xFF));
    buf.push_back(static_cast<std::byte>((v >> 16) & 0xFF));
    buf.push_back(static_cast<std::byte>((v >> 24) & 0xFF));
}

[[nodiscard]] inline auto crc32_bytes(std::string_view s) noexcept -> std::uint32_t {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (unsigned char c : s) {
        crc ^= c;
        for (int k = 0; k < 8; ++k) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

[[nodiscard]] inline auto extract_zip_entries(std::span<const std::byte> zip_bytes) -> std::unordered_map<std::string, std::string> {
    std::unordered_map<std::string, std::string> files;
    std::size_t pos = 0;
    while (pos + 30 <= zip_bytes.size()) {
        std::uint32_t sig = read_u32_le(zip_bytes.data() + pos);
        if (sig != 0x04034b50u) {
            // Try advancing or stopping if Central Directory reached
            if (sig == 0x02014b50u || sig == 0x06054b50u) break;
            ++pos;
            continue;
        }
        std::uint16_t flags = read_u16_le(zip_bytes.data() + pos + 6);
        std::uint16_t method = read_u16_le(zip_bytes.data() + pos + 8);
        std::uint32_t comp_size = read_u32_le(zip_bytes.data() + pos + 18);
        std::uint16_t name_len = read_u16_le(zip_bytes.data() + pos + 26);
        std::uint16_t extra_len = read_u16_le(zip_bytes.data() + pos + 28);
        std::size_t data_offset = pos + 30 + name_len + extra_len;
        if (data_offset > zip_bytes.size()) break;

        std::string name(reinterpret_cast<const char*>(zip_bytes.data() + pos + 30), name_len);

        // If bit 3 (data descriptor) is set and comp_size == 0, scan Central Directory for actual size
        if ((flags & 0x08u) != 0 && comp_size == 0) {
            // Look up in central directory at the end of the file
            for (std::size_t cd = data_offset; cd + 46 <= zip_bytes.size(); ++cd) {
                if (read_u32_le(zip_bytes.data() + cd) == 0x02014b50u) {
                    std::uint32_t cd_comp = read_u32_le(zip_bytes.data() + cd + 20);
                    std::uint16_t cd_nlen = read_u16_le(zip_bytes.data() + cd + 28);
                    if (cd + 46 + cd_nlen <= zip_bytes.size()) {
                        std::string_view cd_name(reinterpret_cast<const char*>(zip_bytes.data() + cd + 46), cd_nlen);
                        if (cd_name == name) {
                            comp_size = cd_comp;
                            break;
                        }
                    }
                }
            }
        }

        if (data_offset + comp_size > zip_bytes.size()) break;
        std::span<const std::byte> payload = zip_bytes.subspan(data_offset, comp_size);
        if (method == 0) {
            files[name] = std::string(reinterpret_cast<const char*>(payload.data()), payload.size());
        } else if (method == 8) {
            auto dec = inflate_raw(payload);
            files[name] = std::string(reinterpret_cast<const char*>(dec.data()), dec.size());
        }
        pos = data_offset + comp_size;
    }
    return files;
}

[[nodiscard]] inline auto xml_unescape(std::string_view s) -> std::string {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '&') {
            auto semi = s.find(';', i + 1);
            if (semi != std::string_view::npos && semi - i <= 10) {
                std::string_view ent = s.substr(i + 1, semi - i - 1);
                if (ent == "amp") { out.push_back('&'); i = semi; continue; }
                if (ent == "lt") { out.push_back('<'); i = semi; continue; }
                if (ent == "gt") { out.push_back('>'); i = semi; continue; }
                if (ent == "quot") { out.push_back('"'); i = semi; continue; }
                if (ent == "apos" || ent == "#39" || ent == "#x27") { out.push_back('\''); i = semi; continue; }
                if (ent == "nbsp" || ent == "#160") { out.push_back(' '); i = semi; continue; }
            }
        }
        out.push_back(s[i]);
    }
    return out;
}

[[nodiscard]] inline auto trim_ws(std::string_view sv) -> std::string {
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) sv.remove_prefix(1);
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back()))) sv.remove_suffix(1);
    return std::string(sv);
}

[[nodiscard]] inline auto extract_xml_text_runs(std::string_view xml, std::string_view tag_name) -> std::string {
    std::string out;
    std::string open_prefix = "<" + std::string(tag_name);
    std::string close_tag = "</" + std::string(tag_name) + ">";
    std::size_t pos = 0;
    while ((pos = xml.find(open_prefix, pos)) != std::string_view::npos) {
        std::size_t after = pos + open_prefix.size();
        if (after < xml.size() && xml[after] != '>' && !std::isspace(static_cast<unsigned char>(xml[after])) && xml[after] != '/') {
            pos = after;
            continue;
        }
        auto gt = xml.find('>', after);
        if (gt == std::string_view::npos) break;
        if (xml[gt - 1] == '/') {
            pos = gt + 1;
            continue;
        }
        auto end = xml.find(close_tag, gt + 1);
        if (end == std::string_view::npos) break;
        out += xml_unescape(xml.substr(gt + 1, end - (gt + 1)));
        pos = end + close_tag.size();
    }
    return out;
}

} // namespace cfire::ai::detail
