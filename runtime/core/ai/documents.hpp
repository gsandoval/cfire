#pragma once
// runtime/core/ai/documents.hpp — Zero-Dependency DEFLATE, ZIP, PDF, Office & EPUB Document Parsers

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

inline void parse_docx_xml(std::string_view xml, Document& doc) {
    doc.format = "docx";
    std::string full_md;
    std::string current_sec_title = "Document";
    std::string current_sec_body;
    std::int32_t sec_idx = 1;

    std::size_t pos = 0;
    while (pos < xml.size()) {
        auto next_p = xml.find("<w:p", pos);
        auto next_tbl = xml.find("<w:tbl", pos);
        if (next_p == std::string_view::npos && next_tbl == std::string_view::npos) break;

        if (next_tbl != std::string_view::npos && (next_p == std::string_view::npos || next_tbl < next_p)) {
            auto end_tbl = xml.find("</w:tbl>", next_tbl);
            if (end_tbl == std::string_view::npos) break;
            std::string_view tbl_xml = xml.substr(next_tbl, end_tbl - next_tbl);
            std::size_t tr_pos = 0;
            bool first_row = true;
            while ((tr_pos = tbl_xml.find("<w:tr", tr_pos)) != std::string_view::npos) {
                auto end_tr = tbl_xml.find("</w:tr>", tr_pos);
                if (end_tr == std::string_view::npos) break;
                std::string_view tr_xml = tbl_xml.substr(tr_pos, end_tr - tr_pos);
                std::vector<std::string> cells;
                std::size_t tc_pos = 0;
                while ((tc_pos = tr_xml.find("<w:tc", tc_pos)) != std::string_view::npos) {
                    auto end_tc = tr_xml.find("</w:tc>", tc_pos);
                    if (end_tc == std::string_view::npos) break;
                    cells.push_back(trim_ws(extract_xml_text_runs(tr_xml.substr(tc_pos, end_tc - tc_pos), "w:t")));
                    tc_pos = end_tc + 7;
                }
                if (!cells.empty()) {
                    std::string row_line = "| ";
                    for (const auto& c : cells) row_line += c + " | ";
                    row_line.pop_back();
                    full_md += row_line + "\n";
                    current_sec_body += row_line + "\n";
                    if (first_row) {
                        std::string sep_line = "|";
                        for (std::size_t k = 0; k < cells.size(); ++k) sep_line += " --- |";
                        full_md += sep_line + "\n";
                        current_sec_body += sep_line + "\n";
                        first_row = false;
                    }
                }
                tr_pos = end_tr + 7;
            }
            full_md += "\n";
            current_sec_body += "\n";
            pos = end_tbl + 8;
            continue;
        }

        // Check <w:p> or <w:p ...>
        std::size_t after_p = next_p + 4;
        if (after_p < xml.size() && xml[after_p] != '>' && !std::isspace(static_cast<unsigned char>(xml[after_p])) && xml[after_p] != '/') {
            pos = after_p;
            continue;
        }
        auto end_p = xml.find("</w:p>", next_p);
        if (end_p == std::string_view::npos) break;
        std::string_view p_xml = xml.substr(next_p, end_p - next_p);
        std::string text = trim_ws(extract_xml_text_runs(p_xml, "w:t"));
        if (!text.empty()) {
            int heading_level = 0;
            auto st_pos = p_xml.find("w:pStyle");
            if (st_pos != std::string_view::npos) {
                auto val_pos = p_xml.find("w:val=\"", st_pos);
                if (val_pos != std::string_view::npos) {
                    val_pos += 7;
                    auto q_end = p_xml.find('"', val_pos);
                    if (q_end != std::string_view::npos) {
                        std::string_view style_name = p_xml.substr(val_pos, q_end - val_pos);
                        if (style_name.starts_with("Heading") || style_name.starts_with("heading")) {
                            char last = style_name.back();
                            if (last >= '1' && last <= '6') heading_level = last - '0';
                            else heading_level = 1;
                        } else if (style_name == "Title") {
                            heading_level = 1;
                        } else if (style_name == "Subtitle") {
                            heading_level = 2;
                        }
                    }
                }
            }

            if (heading_level > 0) {
                if (!trim_ws(current_sec_body).empty()) {
                    doc.sections.push_back(DocumentSection{
                        .index = sec_idx++,
                        .title = current_sec_title,
                        .text = trim_ws(current_sec_body)
                    });
                    current_sec_body.clear();
                }
                current_sec_title = text;
                std::string hashes(static_cast<std::size_t>(heading_level), '#');
                std::string line = hashes + " " + text + "\n\n";
                full_md += line;
                current_sec_body += line;
            } else {
                full_md += text + "\n\n";
                current_sec_body += text + "\n\n";
            }
        }
        pos = end_p + 6;
    }

    if (!trim_ws(current_sec_body).empty()) {
        doc.sections.push_back(DocumentSection{
            .index = sec_idx,
            .title = current_sec_title,
            .text = trim_ws(current_sec_body)
        });
    }
    doc.text = trim_ws(full_md);
}

inline void parse_pptx_zip(const std::unordered_map<std::string, std::string>& entries, Document& doc) {
    doc.format = "pptx";
    struct SlideItem {
        int num = 0;
        std::string xml;
        std::string notes_xml;
    };
    std::vector<SlideItem> slides;
    for (const auto& [name, content] : entries) {
        if (name.starts_with("ppt/slides/slide") && name.ends_with(".xml")) {
            std::string_view num_sv(name);
            num_sv.remove_prefix(16); // "ppt/slides/slide"
            num_sv.remove_suffix(4);  // ".xml"
            int slide_num = 0;
            for (char c : num_sv) {
                if (std::isdigit(static_cast<unsigned char>(c))) slide_num = slide_num * 10 + (c - '0');
            }
            if (slide_num > 0) {
                std::string notes_key = std::format("ppt/notesSlides/notesSlide{}.xml", slide_num);
                std::string notes = entries.contains(notes_key) ? entries.at(notes_key) : "";
                slides.push_back(SlideItem{slide_num, content, std::move(notes)});
            }
        }
    }
    std::sort(slides.begin(), slides.end(), [](const SlideItem& a, const SlideItem& b) { return a.num < b.num; });

    std::string full_md;
    for (const auto& sl : slides) {
        std::string slide_title;
        std::vector<std::string> body_lines;

        // Parse <p:sp> shapes to distinguish title placeholder from body text
        std::size_t sp_pos = 0;
        while ((sp_pos = sl.xml.find("<p:sp", sp_pos)) != std::string::npos) {
            auto end_sp = sl.xml.find("</p:sp>", sp_pos);
            if (end_sp == std::string::npos) break;
            std::string_view sp_xml(sl.xml.data() + sp_pos, end_sp - sp_pos);
            bool is_title = (sp_xml.find("type=\"title\"") != std::string_view::npos) ||
                            (sp_xml.find("type=\"ctrTitle\"") != std::string_view::npos);

            std::size_t p_pos = 0;
            while ((p_pos = sp_xml.find("<a:p", p_pos)) != std::string_view::npos) {
                auto end_p = sp_xml.find("</a:p>", p_pos);
                if (end_p == std::string_view::npos) break;
                std::string para = trim_ws(extract_xml_text_runs(sp_xml.substr(p_pos, end_p - p_pos), "a:t"));
                if (!para.empty()) {
                    if (is_title && slide_title.empty()) slide_title = para;
                    else body_lines.push_back(para);
                }
                p_pos = end_p + 6;
            }
            sp_pos = end_sp + 7;
        }

        // Fallback if <a:t> wasn't inside <p:sp>
        if (slide_title.empty() && body_lines.empty()) {
            std::string raw = trim_ws(extract_xml_text_runs(sl.xml, "a:t"));
            if (!raw.empty()) body_lines.push_back(raw);
        }
        if (slide_title.empty()) {
            if (!body_lines.empty()) {
                slide_title = body_lines.front();
                body_lines.erase(body_lines.begin());
            } else {
                slide_title = std::format("Slide {}", sl.num);
            }
        }

        std::string sec_md = std::format("## Slide {}: {}\n", sl.num, slide_title);
        for (const auto& b : body_lines) {
            sec_md += "- " + b + "\n";
        }
        if (!sl.notes_xml.empty()) {
            std::string notes_txt = trim_ws(extract_xml_text_runs(sl.notes_xml, "a:t"));
            if (!notes_txt.empty()) {
                sec_md += "Notes: " + notes_txt + "\n";
            }
        }

        doc.sections.push_back(DocumentSection{
            .index = sl.num,
            .title = slide_title,
            .text = trim_ws(sec_md)
        });
        full_md += sec_md + "\n";
    }
    doc.text = trim_ws(full_md);
}

inline void parse_xlsx_zip(const std::unordered_map<std::string, std::string>& entries, Document& doc) {
    doc.format = "xlsx";
    std::vector<std::string> shared_strings;
    if (auto it = entries.find("xl/sharedStrings.xml"); it != entries.end()) {
        std::string_view ss_xml = it->second;
        std::size_t si_pos = 0;
        while ((si_pos = ss_xml.find("<si", si_pos)) != std::string_view::npos) {
            auto end_si = ss_xml.find("</si>", si_pos);
            if (end_si == std::string_view::npos) break;
            shared_strings.push_back(extract_xml_text_runs(ss_xml.substr(si_pos, end_si - si_pos), "t"));
            si_pos = end_si + 5;
        }
    }

    struct SheetItem {
        int num = 0;
        std::string xml;
    };
    std::vector<SheetItem> sheets;
    for (const auto& [name, content] : entries) {
        if (name.starts_with("xl/worksheets/sheet") && name.ends_with(".xml")) {
            std::string_view num_sv(name);
            num_sv.remove_prefix(19);
            num_sv.remove_suffix(4);
            int num = 0;
            for (char c : num_sv) {
                if (std::isdigit(static_cast<unsigned char>(c))) num = num * 10 + (c - '0');
            }
            if (num > 0) sheets.push_back(SheetItem{num, content});
        }
    }
    std::sort(sheets.begin(), sheets.end(), [](const SheetItem& a, const SheetItem& b) { return a.num < b.num; });

    std::string full_md;
    for (const auto& sh : sheets) {
        std::string sheet_title = std::format("Sheet {}", sh.num);
        std::string sheet_md = std::format("## {}\n", sheet_title);
        std::string_view xml = sh.xml;
        std::size_t row_pos = 0;
        bool first_row = true;
        while ((row_pos = xml.find("<row", row_pos)) != std::string_view::npos) {
            auto end_row = xml.find("</row>", row_pos);
            if (end_row == std::string_view::npos) break;
            std::string_view row_xml = xml.substr(row_pos, end_row - row_pos);
            std::vector<std::string> cells;
            std::size_t c_pos = 0;
            while ((c_pos = row_xml.find("<c", c_pos)) != std::string_view::npos) {
                if (c_pos + 2 < row_xml.size() && row_xml[c_pos + 2] != ' ' && row_xml[c_pos + 2] != '>') {
                    c_pos += 2;
                    continue;
                }
                auto gt = row_xml.find('>', c_pos);
                if (gt == std::string_view::npos) break;
                if (row_xml[gt - 1] == '/') {
                    cells.emplace_back("");
                    c_pos = gt + 1;
                    continue;
                }
                auto end_c = row_xml.find("</c>", gt);
                if (end_c == std::string_view::npos) break;
                std::string_view c_tag = row_xml.substr(c_pos, gt - c_pos);
                std::string_view c_body = row_xml.substr(gt + 1, end_c - (gt + 1));
                bool is_shared = c_tag.find("t=\"s\"") != std::string_view::npos;
                bool is_inline = c_tag.find("t=\"inlineStr\"") != std::string_view::npos;
                std::string val;
                if (is_inline) {
                    val = extract_xml_text_runs(c_body, "t");
                } else {
                    val = extract_xml_text_runs(c_body, "v");
                    if (is_shared && !val.empty()) {
                        std::size_t idx = static_cast<std::size_t>(std::strtoul(val.c_str(), nullptr, 10));
                        if (idx < shared_strings.size()) val = shared_strings[idx];
                    }
                }
                cells.push_back(trim_ws(val));
                c_pos = end_c + 4;
            }
            if (!cells.empty()) {
                std::string line = "| ";
                for (const auto& c : cells) line += c + " | ";
                line.pop_back();
                sheet_md += line + "\n";
                if (first_row) {
                    std::string sep = "|";
                    for (std::size_t k = 0; k < cells.size(); ++k) sep += " --- |";
                    sheet_md += sep + "\n";
                    first_row = false;
                }
            }
            row_pos = end_row + 6;
        }
        doc.sections.push_back(DocumentSection{
            .index = sh.num,
            .title = sheet_title,
            .text = trim_ws(sheet_md)
        });
        full_md += sheet_md + "\n";
    }
    doc.text = trim_ws(full_md);
}

[[nodiscard]] inline auto extract_pdf_apple(std::span<const std::byte> pdf_bytes, Document& doc) -> bool {
#if defined(__APPLE__)
    const auto& rt = apple_nl_runtime();
    if (!rt.get_class || !rt.sel_register || !rt.msg_send) return false;
    void* nsDataCls = rt.get_class("NSData");
    void* pdfDocCls = rt.get_class("PDFDocument");
    if (!nsDataCls || !pdfDocCls) return false;

    void* selDataWithBytes = rt.sel_register("dataWithBytes:length:");
    void* selAlloc = rt.sel_register("alloc");
    void* selInitWithData = rt.sel_register("initWithData:");
    void* selPageCount = rt.sel_register("pageCount");
    void* selPageAtIndex = rt.sel_register("pageAtIndex:");
    void* selString = rt.sel_register("string");
    void* selUTF8 = rt.sel_register("UTF8String");

    using FnDataBytes = void*(*)(void*, void*, const void*, unsigned long);
    using FnAlloc = void*(*)(void*, void*);
    using FnInitData = void*(*)(void*, void*, void*);
    using FnPageCount = unsigned long(*)(void*, void*);
    using FnPageAt = void*(*)(void*, void*, unsigned long);
    using FnString = void*(*)(void*, void*);
    using FnUTF8 = const char*(*)(void*, void*);

    void* nsData = reinterpret_cast<FnDataBytes>(rt.msg_send)(nsDataCls, selDataWithBytes, pdf_bytes.data(), pdf_bytes.size());
    if (!nsData) return false;
    void* rawDoc = reinterpret_cast<FnAlloc>(rt.msg_send)(pdfDocCls, selAlloc);
    if (!rawDoc) return false;
    void* pdfDoc = reinterpret_cast<FnInitData>(rt.msg_send)(rawDoc, selInitWithData, nsData);
    if (!pdfDoc) return false;

    unsigned long count = reinterpret_cast<FnPageCount>(rt.msg_send)(pdfDoc, selPageCount);
    if (count == 0) return false;

    std::string full_md;
    for (unsigned long i = 0; i < count; ++i) {
        void* page = reinterpret_cast<FnPageAt>(rt.msg_send)(pdfDoc, selPageAtIndex, i);
        if (!page) continue;
        void* nsStr = reinterpret_cast<FnString>(rt.msg_send)(page, selString);
        std::string page_txt;
        if (nsStr) {
            const char* cstr = reinterpret_cast<FnUTF8>(rt.msg_send)(nsStr, selUTF8);
            if (cstr) page_txt = trim_ws(cstr);
        }
        if (!page_txt.empty()) {
            std::string title = std::format("Page {}", i + 1);
            doc.sections.push_back(DocumentSection{
                .index = static_cast<std::int32_t>(i + 1),
                .title = title,
                .text = page_txt
            });
            if (!full_md.empty()) full_md += "\n\n";
            full_md += std::format("## {}\n{}", title, page_txt);
        }
    }
    if (!doc.sections.empty()) {
        doc.text = trim_ws(full_md);
        return true;
    }
#else
    (void)pdf_bytes;
    (void)doc;
#endif
    return false;
}

inline void encode_utf8_codepoint(std::uint32_t cp, std::string& out) {
    if (cp <= 0x7Fu) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FFu) {
        out.push_back(static_cast<char>(0xC0u | ((cp >> 6) & 0x1Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else if (cp <= 0xFFFFu) {
        out.push_back(static_cast<char>(0xE0u | ((cp >> 12) & 0x0Fu)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else if (cp <= 0x10FFFFu) {
        out.push_back(static_cast<char>(0xF0u | ((cp >> 18) & 0x07u)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    }
}

[[nodiscard]] inline auto hex_nibble(char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

[[nodiscard]] inline auto parse_hex_uint(std::string_view hex) -> std::uint32_t {
    std::uint32_t val = 0;
    for (char c : hex) {
        int n = hex_nibble(c);
        if (n >= 0) val = (val << 4) | static_cast<std::uint32_t>(n);
    }
    return val;
}

[[nodiscard]] inline auto decode_hex_utf16be_or_bytes(std::string_view hex) -> std::string {
    std::vector<std::uint8_t> bytes;
    int hi = -1;
    for (char c : hex) {
        int n = hex_nibble(c);
        if (n < 0) continue;
        if (hi < 0) {
            hi = n;
        } else {
            bytes.push_back(static_cast<std::uint8_t>((hi << 4) | n));
            hi = -1;
        }
    }
    if (hi >= 0) bytes.push_back(static_cast<std::uint8_t>(hi << 4));
    if (bytes.empty()) return "";

    std::string out;
    if (bytes.size() >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF) {
        for (std::size_t i = 2; i + 1 < bytes.size(); i += 2) {
            std::uint32_t cp = (static_cast<std::uint32_t>(bytes[i]) << 8) | bytes[i + 1];
            if (cp >= 0xD800u && cp <= 0xDBFFu && i + 3 < bytes.size()) {
                std::uint32_t low = (static_cast<std::uint32_t>(bytes[i + 2]) << 8) | bytes[i + 3];
                if (low >= 0xDC00u && low <= 0xDFFFu) {
                    cp = 0x10000u + (((cp - 0xD800u) << 10) | (low - 0xDC00u));
                    i += 2;
                }
            }
            encode_utf8_codepoint(cp, out);
        }
        return out;
    }
    if (bytes.size() % 2 == 0) {
        bool has_high_zero = false;
        for (std::size_t i = 0; i < bytes.size(); i += 2) {
            if (bytes[i] == 0 && bytes[i + 1] >= 0x20) { has_high_zero = true; break; }
        }
        if (has_high_zero) {
            for (std::size_t i = 0; i + 1 < bytes.size(); i += 2) {
                std::uint32_t cp = (static_cast<std::uint32_t>(bytes[i]) << 8) | bytes[i + 1];
                encode_utf8_codepoint(cp, out);
            }
            return out;
        }
    }
    for (std::uint8_t b : bytes) {
        if (b >= 0x20 || b == '\n' || b == '\r' || b == '\t') out.push_back(static_cast<char>(b));
    }
    return out;
}

inline void parse_pdf_cmap(std::string_view stream_text, std::unordered_map<std::uint32_t, std::string>& cmap) {
    auto next_hex_token = [&](std::size_t& p, std::size_t limit) -> std::optional<std::string_view> {
        auto lt = stream_text.find('<', p);
        if (lt == std::string_view::npos || lt >= limit) return std::nullopt;
        auto gt = stream_text.find('>', lt + 1);
        if (gt == std::string_view::npos || gt > limit) return std::nullopt;
        p = gt + 1;
        return stream_text.substr(lt + 1, gt - lt - 1);
    };

    std::size_t pos = 0;
    while ((pos = stream_text.find("beginbfchar", pos)) != std::string_view::npos) {
        pos += 11;
        auto end_pos = stream_text.find("endbfchar", pos);
        if (end_pos == std::string_view::npos) break;
        std::size_t scan = pos;
        while (scan < end_pos) {
            auto src_hex = next_hex_token(scan, end_pos);
            if (!src_hex) break;
            auto dst_hex = next_hex_token(scan, end_pos);
            if (!dst_hex) break;
            std::uint32_t cid = parse_hex_uint(*src_hex);
            std::uint32_t cp = parse_hex_uint(*dst_hex);
            std::string utf8;
            if ( dst_hex->size() <= 4 && cp > 0 ) {
                encode_utf8_codepoint(cp, utf8);
            } else {
                utf8 = decode_hex_utf16be_or_bytes(*dst_hex);
            }
            if (!utf8.empty()) cmap[cid] = std::move(utf8);
        }
        pos = end_pos + 9;
    }

    pos = 0;
    while ((pos = stream_text.find("beginbfrange", pos)) != std::string_view::npos) {
        pos += 12;
        auto end_pos = stream_text.find("endbfrange", pos);
        if (end_pos == std::string_view::npos) break;
        std::size_t scan = pos;
        while (scan < end_pos) {
            auto start_hex = next_hex_token(scan, end_pos);
            if (!start_hex) break;
            auto last_hex = next_hex_token(scan, end_pos);
            if (!last_hex) break;
            auto dst_hex = next_hex_token(scan, end_pos);
            if (!dst_hex) break;
            std::uint32_t cid_start = parse_hex_uint(*start_hex);
            std::uint32_t cid_end = parse_hex_uint(*last_hex);
            std::uint32_t cp_start = parse_hex_uint(*dst_hex);
            if (cid_end >= cid_start && (cid_end - cid_start) < 2048) {
                for (std::uint32_t c = cid_start; c <= cid_end; ++c) {
                    std::string utf8;
                    encode_utf8_codepoint(cp_start + (c - cid_start), utf8);
                    cmap[c] = std::move(utf8);
                }
            }
        }
        pos = end_pos + 10;
    }
}

[[nodiscard]] inline auto decode_pdf_hex_string(
    std::string_view hex,
    const std::unordered_map<std::uint32_t, std::string>& cmap
) -> std::string {
    std::string clean;
    clean.reserve(hex.size());
    for (char c : hex) {
        if (hex_nibble(c) >= 0) clean.push_back(c);
    }
    if (clean.empty()) return "";
    if (clean.starts_with("FEFF") || clean.starts_with("feff")) {
        return decode_hex_utf16be_or_bytes(clean);
    }
    if (!cmap.empty()) {
        std::string out;
        if (clean.size() % 4 == 0) {
            bool all_mapped = true;
            for (std::size_t i = 0; i < clean.size(); i += 4) {
                std::uint32_t cid = parse_hex_uint(std::string_view(clean).substr(i, 4));
                if (auto it = cmap.find(cid); it != cmap.end()) {
                    out += it->second;
                } else {
                    all_mapped = false;
                    break;
                }
            }
            if (all_mapped && !out.empty()) return out;
        }
        out.clear();
        if (clean.size() % 2 == 0) {
            bool any_mapped = false;
            for (std::size_t i = 0; i < clean.size(); i += 2) {
                std::uint32_t cid = parse_hex_uint(std::string_view(clean).substr(i, 2));
                if (auto it = cmap.find(cid); it != cmap.end()) {
                    out += it->second;
                    any_mapped = true;
                } else if (cid >= 0x20 && cid <= 0x7E) {
                    out.push_back(static_cast<char>(cid));
                }
            }
            if (any_mapped && !out.empty()) return out;
        }
    }
    return decode_hex_utf16be_or_bytes(clean);
}

[[nodiscard]] inline auto decode_pdf_string_literal(
    std::string_view lit,
    const std::unordered_map<std::uint32_t, std::string>& cmap = {}
) -> std::string {
    std::string raw;
    raw.reserve(lit.size());
    for (std::size_t i = 0; i < lit.size(); ++i) {
        if (lit[i] == '\\' && i + 1 < lit.size()) {
            char next = lit[++i];
            if (next == 'n') raw.push_back('\n');
            else if (next == 'r') raw.push_back('\r');
            else if (next == 't') raw.push_back('\t');
            else if (next == '(' || next == ')' || next == '\\') raw.push_back(next);
            else if (next >= '0' && next <= '7') {
                int oct = next - '0';
                if (i + 1 < lit.size() && lit[i + 1] >= '0' && lit[i + 1] <= '7') oct = oct * 8 + (lit[++i] - '0');
                if (i + 1 < lit.size() && lit[i + 1] >= '0' && lit[i + 1] <= '7') oct = oct * 8 + (lit[++i] - '0');
                raw.push_back(static_cast<char>(oct));
            } else {
                raw.push_back(next);
            }
        } else {
            raw.push_back(lit[i]);
        }
    }
    if (!cmap.empty()) {
        std::string mapped;
        bool used_cmap = false;
        for (unsigned char c : raw) {
            if (auto it = cmap.find(static_cast<std::uint32_t>(c)); it != cmap.end()) {
                mapped += it->second;
                used_cmap = true;
            } else {
                mapped.push_back(static_cast<char>(c));
            }
        }
        if (used_cmap) return mapped;
    }
    return raw;
}

[[nodiscard]] inline auto parse_pdf_content_stream(
    std::string_view stream_text,
    const std::unordered_map<std::uint32_t, std::string>& cmap = {}
) -> std::string {
    if (stream_text.find("beginbfchar") != std::string_view::npos ||
        stream_text.find("beginbfrange") != std::string_view::npos) {
        return "";
    }
    std::string page_out;
    std::size_t pos = 0;
    while (pos < stream_text.size()) {
        auto bt = stream_text.find("BT", pos);
        if (bt == std::string_view::npos) break;
        if (bt > 0 && std::isalnum(static_cast<unsigned char>(stream_text[bt - 1]))) {
            pos = bt + 2;
            continue;
        }
        auto et = stream_text.find("ET", bt + 2);
        if (et == std::string_view::npos) et = stream_text.size();
        std::string_view blk = stream_text.substr(bt + 2, et - (bt + 2));

        std::string blk_text;
        bool in_tj_array = false;
        std::size_t i = 0;
        while (i < blk.size()) {
            if (blk[i] == '[') {
                in_tj_array = true;
                ++i;
            } else if (blk[i] == ']') {
                in_tj_array = false;
                ++i;
            } else if (blk[i] == '(') {
                int depth = 1;
                std::size_t start = ++i;
                while (i < blk.size() && depth > 0) {
                    if (blk[i] == '\\') { i += 2; continue; }
                    if (blk[i] == '(') ++depth;
                    else if (blk[i] == ')') --depth;
                    if (depth > 0) ++i;
                }
                blk_text += decode_pdf_string_literal(blk.substr(start, i - start), cmap);
                if (i < blk.size()) ++i;
            } else if (blk[i] == '<' && (i + 1 >= blk.size() || blk[i + 1] != '<')) {
                auto gt = blk.find('>', i + 1);
                if (gt == std::string_view::npos) break;
                blk_text += decode_pdf_hex_string(blk.substr(i + 1, gt - i - 1), cmap);
                i = gt + 1;
            } else if (in_tj_array && (blk[i] == '-' || std::isdigit(static_cast<unsigned char>(blk[i])))) {
                std::size_t num_start = i;
                if (blk[i] == '-') ++i;
                while (i < blk.size() && (std::isdigit(static_cast<unsigned char>(blk[i])) || blk[i] == '.')) ++i;
                double kern = std::strtod(std::string(blk.substr(num_start, i - num_start)).c_str(), nullptr);
                if (kern <= -120.0 && !blk_text.empty() && blk_text.back() != ' ' && blk_text.back() != '\n') {
                    blk_text.push_back(' ');
                }
            } else if (blk.substr(i).starts_with("Td") || blk.substr(i).starts_with("TD") || blk.substr(i).starts_with("T*")) {
                if (!blk_text.empty() && blk_text.back() != '\n') blk_text.push_back('\n');
                i += 2;
            } else {
                ++i;
            }
        }
        blk_text = trim_ws(blk_text);
        if (!blk_text.empty()) {
            if (!page_out.empty()) page_out += "\n";
            page_out += blk_text;
        }
        pos = et + 2;
    }
    return page_out;
}

[[nodiscard]] inline auto extract_pdf_linux(std::span<const std::byte> pdf_bytes, Document& doc) -> bool {
#if defined(__linux__)
    static int has_pdftotext = -1;
    if (has_pdftotext < 0) {
        has_pdftotext = (std::system("which pdftotext >/dev/null 2>&1") == 0) ? 1 : 0;
    }
    if (has_pdftotext != 1) return false;

    static std::uint64_t seq = 0;
    std::string tmp_path = std::format("/tmp/cfire_pdf_{}_{}.pdf", static_cast<long>(::getpid()), ++seq);
    if (!cfire::fs::write_bytes(tmp_path, pdf_bytes)) return false;

    std::string cmd = std::format("pdftotext -layout '{}' - 2>/dev/null", tmp_path);
    std::string full_out;
    if (FILE* pipe = ::popen(cmd.c_str(), "r")) {
        char buf[4096];
        while (std::size_t n = std::fread(buf, 1, sizeof(buf), pipe)) {
            full_out.append(buf, n);
        }
        ::pclose(pipe);
    }
    (void)cfire::fs::remove(tmp_path);

    if (trim_ws(full_out).empty()) return false;
    std::string full_md;
    std::size_t start = 0;
    std::int32_t page_idx = 1;
    while (start < full_out.size()) {
        auto ff = full_out.find('\f', start);
        if (ff == std::string::npos) ff = full_out.size();
        std::string page_txt = trim_ws(std::string_view(full_out).substr(start, ff - start));
        if (!page_txt.empty()) {
            std::string title = std::format("Page {}", page_idx);
            doc.sections.push_back(DocumentSection{.index = page_idx++, .title = title, .text = page_txt});
            if (!full_md.empty()) full_md += "\n\n";
            full_md += std::format("## {}\n{}", title, page_txt);
        }
        start = ff + 1;
    }
    doc.text = trim_ws(full_md);
    return !doc.text.empty();
#else
    (void)pdf_bytes;
    (void)doc;
    return false;
#endif
}

inline void parse_pdf_portable(std::span<const std::byte> pdf_bytes, Document& doc) {
    doc.format = "pdf";
    if (extract_pdf_apple(pdf_bytes, doc)) return;
    if (extract_pdf_linux(pdf_bytes, doc)) return;

    std::string_view raw(reinterpret_cast<const char*>(pdf_bytes.data()), pdf_bytes.size());
    std::vector<std::string> decoded_streams;
    std::unordered_map<std::uint32_t, std::string> cmap;

    std::size_t pos = 0;
    while ((pos = raw.find("stream", pos)) != std::string_view::npos) {
        if (pos > 0 && raw[pos - 1] == 'd') { // "endstream"
            pos += 6;
            continue;
        }
        std::size_t dict_start = (pos > 256) ? (pos - 256) : 0;
        std::string_view dict_window = raw.substr(dict_start, pos - dict_start);
        bool is_flate = dict_window.find("/FlateDecode") != std::string_view::npos;

        std::size_t data_start = pos + 6;
        if (data_start < raw.size() && raw[data_start] == '\r') ++data_start;
        if (data_start < raw.size() && raw[data_start] == '\n') ++data_start;

        auto end_s = raw.find("endstream", data_start);
        if (end_s == std::string_view::npos) break;
        std::size_t data_end = end_s;
        while (data_end > data_start && (raw[data_end - 1] == '\n' || raw[data_end - 1] == '\r')) --data_end;

        std::string decoded_stream;
        if (is_flate && data_end > data_start) {
            auto dec = inflate_zlib_or_raw(pdf_bytes.subspan(data_start, data_end - data_start));
            decoded_stream.assign(reinterpret_cast<const char*>(dec.data()), dec.size());
        } else if (data_end > data_start) {
            decoded_stream.assign(raw.data() + data_start, data_end - data_start);
        }

        if (decoded_stream.find("beginbfchar") != std::string::npos ||
            decoded_stream.find("beginbfrange") != std::string::npos) {
            parse_pdf_cmap(decoded_stream, cmap);
        } else if (!decoded_stream.empty()) {
            decoded_streams.push_back(std::move(decoded_stream));
        }
        pos = end_s + 9;
    }

    std::int32_t page_num = 1;
    std::string full_md;
    for (const auto& stream_str : decoded_streams) {
        std::string page_txt = parse_pdf_content_stream(stream_str, cmap);
        if (!page_txt.empty()) {
            std::string title = std::format("Page {}", page_num);
            doc.sections.push_back(DocumentSection{
                .index = page_num++,
                .title = title,
                .text = page_txt
            });
            if (!full_md.empty()) full_md += "\n\n";
            full_md += std::format("## {}\n{}", title, page_txt);
        }
    }
    doc.text = trim_ws(full_md);
}

inline void parse_html_document(std::string_view html, Document& doc) {
    doc.format = "html";
    std::string cleaned;
    cleaned.reserve(html.size());

    // Strip <script>, <style>, and <!-- comments -->
    std::size_t i = 0;
    while (i < html.size()) {
        if (html.substr(i).starts_with("<!--")) {
            auto end_c = html.find("-->", i + 4);
            i = (end_c == std::string_view::npos) ? html.size() : (end_c + 3);
            continue;
        }
        if (html[i] == '<') {
            auto gt = html.find('>', i + 1);
            if (gt == std::string_view::npos) break;
            std::string tag_raw(html.substr(i + 1, gt - i - 1));
            std::string tag_lower;
            for (unsigned char c : tag_raw) {
                if (std::isspace(c) || c == '/') {
                    if (!tag_lower.empty()) break;
                    if (c == '/' && tag_lower.empty()) tag_lower.push_back('/');
                } else {
                    tag_lower.push_back(static_cast<char>(std::tolower(c)));
                }
            }
            if (tag_lower == "script" || tag_lower == "style") {
                std::string close_needle = "</" + tag_lower;
                std::size_t scan = gt + 1;
                while (scan < html.size()) {
                    if (html[scan] == '<' && scan + close_needle.size() <= html.size()) {
                        bool match = true;
                        for (std::size_t k = 0; k < close_needle.size(); ++k) {
                            if (std::tolower(static_cast<unsigned char>(html[scan + k])) != close_needle[k]) {
                                match = false;
                                break;
                            }
                        }
                        if (match) {
                            auto close_gt = html.find('>', scan);
                            scan = (close_gt == std::string_view::npos) ? html.size() : (close_gt + 1);
                            break;
                        }
                    }
                    ++scan;
                }
                i = scan;
                continue;
            }
            if (tag_lower.size() == 2 && tag_lower[0] == 'h' && tag_lower[1] >= '1' && tag_lower[1] <= '6') {
                cleaned += "\n\n" + std::string(static_cast<std::size_t>(tag_lower[1] - '0'), '#') + " ";
            } else if (tag_lower.size() == 3 && tag_lower[0] == '/' && tag_lower[1] == 'h' && tag_lower[2] >= '1' && tag_lower[2] <= '6') {
                cleaned += "\n\n";
            } else if (tag_lower == "li") {
                cleaned += "\n- ";
            } else if (tag_lower == "p" || tag_lower == "/p" || tag_lower == "br" || tag_lower == "div" || tag_lower == "/div" || tag_lower == "/tr") {
                cleaned += "\n";
            } else if (tag_lower == "td" || tag_lower == "th") {
                cleaned += " | ";
            }
            i = gt + 1;
            continue;
        }
        cleaned.push_back(html[i++]);
    }

    std::string unescaped = xml_unescape(cleaned);
    // Split into sections by Markdown headings
    std::string full_md;
    std::string cur_title = "Document";
    std::string cur_body;
    std::int32_t sec_idx = 1;
    std::size_t line_start = 0;
    while (line_start < unescaped.size()) {
        auto nl = unescaped.find('\n', line_start);
        if (nl == std::string::npos) nl = unescaped.size();
        std::string line = trim_ws(std::string_view(unescaped).substr(line_start, nl - line_start));
        line_start = nl + 1;
        if (line.empty()) continue;
        if (line.starts_with("#")) {
            if (!trim_ws(cur_body).empty()) {
                doc.sections.push_back(DocumentSection{.index = sec_idx++, .title = cur_title, .text = trim_ws(cur_body)});
                cur_body.clear();
            }
            std::size_t h_end = 0;
            while (h_end < line.size() && line[h_end] == '#') ++h_end;
            cur_title = trim_ws(std::string_view(line).substr(h_end));
        }
        cur_body += line + "\n";
        full_md += line + "\n";
    }
    if (!trim_ws(cur_body).empty()) {
        doc.sections.push_back(DocumentSection{.index = sec_idx, .title = cur_title, .text = trim_ws(cur_body)});
    }
    doc.text = trim_ws(full_md);
}

inline void parse_markdown_sections(std::string_view md, Document& doc) {
    doc.text = trim_ws(md);
    std::string cur_title = "Section 1";
    std::string cur_body;
    std::int32_t sec_idx = 1;
    std::size_t pos = 0;
    while (pos < md.size()) {
        auto nl = md.find('\n', pos);
        if (nl == std::string_view::npos) nl = md.size();
        std::string_view line = md.substr(pos, nl - pos);
        pos = nl + 1;
        std::string trimmed = trim_ws(line);
        if (trimmed.starts_with("#")) {
            if (!trim_ws(cur_body).empty()) {
                doc.sections.push_back(DocumentSection{.index = sec_idx++, .title = cur_title, .text = trim_ws(cur_body)});
                cur_body.clear();
            }
            std::size_t h = 0;
            while (h < trimmed.size() && trimmed[h] == '#') ++h;
            cur_title = trim_ws(std::string_view(trimmed).substr(h));
            continue;
        }
        cur_body.append(line.data(), line.size());
        cur_body.push_back('\n');
    }
    if (!trim_ws(cur_body).empty()) {
        doc.sections.push_back(DocumentSection{.index = sec_idx, .title = cur_title, .text = trim_ws(cur_body)});
    } else if (doc.sections.empty() && !cur_title.empty() && cur_title != "Section 1") {
        doc.sections.push_back(DocumentSection{.index = sec_idx, .title = cur_title, .text = cur_title});
    }
}

[[nodiscard]] inline auto ocr_apple_vision(std::span<const std::byte> img_bytes) -> std::string {
#if defined(__APPLE__)
    const auto& rt = apple_nl_runtime();
    if (!rt.get_class || !rt.sel_register || !rt.msg_send) return "";
    void* nsDataCls = rt.get_class("NSData");
    void* nsDictCls = rt.get_class("NSDictionary");
    void* nsArrayCls = rt.get_class("NSArray");
    void* vnHandlerCls = rt.get_class("VNImageRequestHandler");
    void* vnRecReqCls = rt.get_class("VNRecognizeTextRequest");
    if (!nsDataCls || !nsDictCls || !nsArrayCls || !vnHandlerCls || !vnRecReqCls) return "";

    void* selDataWithBytes = rt.sel_register("dataWithBytes:length:");
    void* selDict = rt.sel_register("dictionary");
    void* selAlloc = rt.sel_register("alloc");
    void* selInit = rt.sel_register("init");
    void* selInitDataOpts = rt.sel_register("initWithData:options:");
    void* selArrayWithObj = rt.sel_register("arrayWithObject:");
    void* selPerform = rt.sel_register("performRequests:error:");
    void* selResults = rt.sel_register("results");
    void* selCount = rt.sel_register("count");
    void* selObjAt = rt.sel_register("objectAtIndex:");
    void* selTopCand = rt.sel_register("topCandidates:");
    void* selFirstObj = rt.sel_register("firstObject");
    void* selString = rt.sel_register("string");
    void* selUTF8 = rt.sel_register("UTF8String");

    using FnDataBytes = void*(*)(void*, void*, const void*, unsigned long);
    using FnZero = void*(*)(void*, void*);
    using FnInitDataOpts = void*(*)(void*, void*, void*, void*);
    using FnArrObj = void*(*)(void*, void*, void*);
    using FnPerform = signed char(*)(void*, void*, void*, void**);
    using FnCount = unsigned long(*)(void*, void*);
    using FnObjAt = void*(*)(void*, void*, unsigned long);
    using FnUTF8 = const char*(*)(void*, void*);

    void* nsData = reinterpret_cast<FnDataBytes>(rt.msg_send)(nsDataCls, selDataWithBytes, img_bytes.data(), img_bytes.size());
    void* emptyDict = reinterpret_cast<FnZero>(rt.msg_send)(nsDictCls, selDict);
    if (!nsData || !emptyDict) return "";

    void* rawHandler = reinterpret_cast<FnZero>(rt.msg_send)(vnHandlerCls, selAlloc);
    void* handler = rawHandler ? reinterpret_cast<FnInitDataOpts>(rt.msg_send)(rawHandler, selInitDataOpts, nsData, emptyDict) : nullptr;
    void* rawReq = reinterpret_cast<FnZero>(rt.msg_send)(vnRecReqCls, selAlloc);
    void* req = rawReq ? reinterpret_cast<FnZero>(rt.msg_send)(rawReq, selInit) : nullptr;
    if (!handler || !req) return "";

    void* reqArray = reinterpret_cast<FnArrObj>(rt.msg_send)(nsArrayCls, selArrayWithObj, req);
    void* err = nullptr;
    signed char ok = reinterpret_cast<FnPerform>(rt.msg_send)(handler, selPerform, reqArray, &err);
    if (!ok) return "";

    void* results = reinterpret_cast<FnZero>(rt.msg_send)(req, selResults);
    if (!results) return "";
    unsigned long n = reinterpret_cast<FnCount>(rt.msg_send)(results, selCount);
    std::string out;
    for (unsigned long i = 0; i < n; ++i) {
        void* obs = reinterpret_cast<FnObjAt>(rt.msg_send)(results, selObjAt, i);
        if (!obs) continue;
        void* cands = reinterpret_cast<FnObjAt>(rt.msg_send)(obs, selTopCand, 1UL);
        if (!cands) continue;
        void* top = reinterpret_cast<FnZero>(rt.msg_send)(cands, selFirstObj);
        if (!top) continue;
        void* nsStr = reinterpret_cast<FnZero>(rt.msg_send)(top, selString);
        if (!nsStr) continue;
        const char* utf8 = reinterpret_cast<FnUTF8>(rt.msg_send)(nsStr, selUTF8);
        if (utf8 && *utf8) {
            if (!out.empty()) out += "\n";
            out += utf8;
        }
    }
    return out;
#else
    (void)img_bytes;
    return "";
#endif
}

[[nodiscard]] inline auto extract_image_text_metadata(std::span<const std::byte> bytes) -> std::string {
    std::string out;
    // 1. Check PNG tEXt / iTXt chunks
    if (bytes.size() > 8 && static_cast<std::uint8_t>(bytes[0]) == 0x89 && static_cast<std::uint8_t>(bytes[1]) == 'P' &&
        static_cast<std::uint8_t>(bytes[2]) == 'N' && static_cast<std::uint8_t>(bytes[3]) == 'G') {
        std::size_t pos = 8;
        while (pos + 12 <= bytes.size()) {
            std::uint32_t len = (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[pos])) << 24) |
                                (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[pos + 1])) << 16) |
                                (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[pos + 2])) << 8) |
                                static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[pos + 3]));
            std::string_view type(reinterpret_cast<const char*>(bytes.data() + pos + 4), 4);
            if (pos + 12 + len > bytes.size()) break;
            if (type == "tEXt" || type == "iTXt") {
                std::string_view chunk(reinterpret_cast<const char*>(bytes.data() + pos + 8), len);
                auto nul = chunk.find('\0');
                std::string_view val = (nul != std::string_view::npos) ? chunk.substr(nul + 1) : chunk;
                while (!val.empty() && val.front() == '\0') val.remove_prefix(1);
                if (!val.empty()) {
                    if (!out.empty()) out += "\n";
                    out.append(val.data(), val.size());
                }
            }
            pos += 12 + len;
        }
    }
    // 2. Check JPEG COM (0xFF 0xFE) comment segments
    if (bytes.size() > 4 && static_cast<std::uint8_t>(bytes[0]) == 0xFF && static_cast<std::uint8_t>(bytes[1]) == 0xD8) {
        std::size_t pos = 2;
        while (pos + 4 <= bytes.size() && static_cast<std::uint8_t>(bytes[pos]) == 0xFF) {
            auto marker = static_cast<std::uint8_t>(bytes[pos + 1]);
            if (marker == 0xDA || marker == 0xD9) break;
            std::uint16_t seg_len = static_cast<std::uint16_t>((static_cast<std::uint8_t>(bytes[pos + 2]) << 8) |
                                                               static_cast<std::uint8_t>(bytes[pos + 3]));
            if (seg_len < 2 || pos + 2 + seg_len > bytes.size()) break;
            if (marker == 0xFE && seg_len > 2) {
                std::string_view com(reinterpret_cast<const char*>(bytes.data() + pos + 4), seg_len - 2);
                if (!com.empty()) {
                    if (!out.empty()) out += "\n";
                    out.append(com.data(), com.size());
                }
            }
            pos += 2 + seg_len;
        }
    }
    return out;
}

inline void parse_odf_zip(
    const std::unordered_map<std::string, std::string>& entries,
    std::string_view mime_type,
    Document& doc
) {
    if (mime_type.find("presentation") != std::string::npos) doc.format = "odp";
    else if (mime_type.find("spreadsheet") != std::string::npos) doc.format = "ods";
    else doc.format = "odt";

    auto it = entries.find("content.xml");
    if (it == entries.end()) return;
    std::string_view xml = it->second;

    if (doc.format == "odp") {
        std::size_t pos = 0;
        std::int32_t slide_idx = 1;
        std::string full_md;
        while ((pos = xml.find("<draw:page", pos)) != std::string_view::npos) {
            auto gt = xml.find('>', pos);
            if (gt == std::string_view::npos) break;
            if (xml[gt - 1] == '/') { pos = gt + 1; continue; }
            auto end_page = xml.find("</draw:page>", gt + 1);
            if (end_page == std::string_view::npos) break;
            std::string_view page_xml = xml.substr(gt + 1, end_page - (gt + 1));

            std::string slide_txt;
            std::size_t p_pos = 0;
            while ((p_pos = page_xml.find("<text:p", p_pos)) != std::string_view::npos) {
                auto p_gt = page_xml.find('>', p_pos);
                if (p_gt == std::string_view::npos) break;
                if (page_xml[p_gt - 1] == '/') { p_pos = p_gt + 1; continue; }
                auto p_end = page_xml.find("</text:p>", p_gt + 1);
                if (p_end == std::string_view::npos) break;
                std::string_view inner = page_xml.substr(p_gt + 1, p_end - (p_gt + 1));
                std::string line;
                std::size_t k = 0;
                while (k < inner.size()) {
                    if (inner[k] == '<') {
                        auto t_end = inner.find('>', k);
                        if (t_end == std::string_view::npos) break;
                        std::string_view tag = inner.substr(k + 1, t_end - k - 1);
                        if (tag.starts_with("text:s")) line.push_back(' ');
                        else if (tag.starts_with("text:tab")) line.push_back('\t');
                        else if (tag.starts_with("text:line-break")) line.push_back('\n');
                        k = t_end + 1;
                    } else {
                        line.push_back(inner[k++]);
                    }
                }
                line = trim_ws(xml_unescape(line));
                if (!line.empty()) {
                    if (!slide_txt.empty()) slide_txt += "\n";
                    slide_txt += line;
                }
                p_pos = p_end + 9;
            }
            if (!slide_txt.empty()) {
                std::string title = std::format("Slide {}", slide_idx);
                doc.sections.push_back(DocumentSection{.index = slide_idx++, .title = title, .text = slide_txt});
                if (!full_md.empty()) full_md += "\n\n";
                full_md += std::format("## {}\n{}", title, slide_txt);
            }
            pos = end_page + 12;
        }
        doc.text = trim_ws(full_md);
        return;
    }

    if (doc.format == "ods") {
        std::size_t pos = 0;
        std::int32_t sheet_idx = 1;
        std::string full_md;
        while ((pos = xml.find("<table:table", pos)) != std::string_view::npos) {
            if (xml.substr(pos).starts_with("<table:table-")) { pos += 13; continue; }
            auto gt = xml.find('>', pos);
            if (gt == std::string_view::npos) break;
            auto end_tbl = xml.find("</table:table>", gt + 1);
            if (end_tbl == std::string_view::npos) break;
            std::string_view tbl_xml = xml.substr(gt + 1, end_tbl - (gt + 1));

            std::string sheet_md;
            std::size_t r_pos = 0;
            std::size_t row_idx = 0;
            while ((r_pos = tbl_xml.find("<table:table-row", r_pos)) != std::string_view::npos) {
                auto r_gt = tbl_xml.find('>', r_pos);
                if (r_gt == std::string_view::npos) break;
                if (tbl_xml[r_gt - 1] == '/') { r_pos = r_gt + 1; continue; }
                auto r_end = tbl_xml.find("</table:table-row>", r_gt + 1);
                if (r_end == std::string_view::npos) break;
                std::string_view row_xml = tbl_xml.substr(r_gt + 1, r_end - (r_gt + 1));

                std::vector<std::string> cells;
                std::size_t c_pos = 0;
                while ((c_pos = row_xml.find("<table:table-cell", c_pos)) != std::string_view::npos) {
                    auto c_gt = row_xml.find('>', c_pos);
                    if (c_gt == std::string_view::npos) break;
                    if (row_xml[c_gt - 1] == '/') { cells.emplace_back(""); c_pos = c_gt + 1; continue; }
                    auto c_end = row_xml.find("</table:table-cell>", c_gt + 1);
                    if (c_end == std::string_view::npos) break;
                    std::string_view cell_xml = row_xml.substr(c_gt + 1, c_end - (c_gt + 1));
                    std::string cell_txt;
                    std::size_t k = 0;
                    while (k < cell_xml.size()) {
                        if (cell_xml[k] == '<') {
                            auto t_end = cell_xml.find('>', k);
                            if (t_end == std::string_view::npos) break;
                            k = t_end + 1;
                        } else {
                            cell_txt.push_back(cell_xml[k++]);
                        }
                    }
                    cells.push_back(trim_ws(xml_unescape(cell_txt)));
                    c_pos = c_end + 19;
                }
                if (!cells.empty()) {
                    sheet_md += "| ";
                    for (const auto& c : cells) sheet_md += c + " | ";
                    sheet_md.pop_back();
                    sheet_md += "\n";
                    if (row_idx == 0) {
                        sheet_md += "|";
                        for (std::size_t k = 0; k < cells.size(); ++k) sheet_md += " --- |";
                        sheet_md += "\n";
                    }
                    ++row_idx;
                }
                r_pos = r_end + 18;
            }
            sheet_md = trim_ws(sheet_md);
            if (!sheet_md.empty()) {
                std::string title = std::format("Sheet {}", sheet_idx);
                doc.sections.push_back(DocumentSection{.index = sheet_idx++, .title = title, .text = sheet_md});
                if (!full_md.empty()) full_md += "\n\n";
                full_md += std::format("## {}\n{}", title, sheet_md);
            }
            pos = end_tbl + 14;
        }
        doc.text = trim_ws(full_md);
        return;
    }

    // Default: OpenDocument Text (.odt)
    std::string full_md;
    std::string current_title = "Document";
    std::string current_body;
    std::int32_t sec_idx = 1;
    std::size_t pos = 0;
    while (pos < xml.size()) {
        auto next_h = xml.find("<text:h", pos);
        auto next_p = xml.find("<text:p", pos);
        std::size_t tag_pos = std::min(next_h, next_p);
        if (tag_pos == std::string_view::npos) break;
        bool is_heading = (tag_pos == next_h);
        std::string close_tag = is_heading ? "</text:h>" : "</text:p>";

        auto gt = xml.find('>', tag_pos);
        if (gt == std::string_view::npos) break;
        if (xml[gt - 1] == '/') { pos = gt + 1; continue; }
        auto end_el = xml.find(close_tag, gt + 1);
        if (end_el == std::string_view::npos) break;
        std::string_view inner = xml.substr(gt + 1, end_el - (gt + 1));

        std::string para;
        std::size_t k = 0;
        while (k < inner.size()) {
            if (inner[k] == '<') {
                auto t_end = inner.find('>', k);
                if (t_end == std::string_view::npos) break;
                std::string_view tag = inner.substr(k + 1, t_end - k - 1);
                if (tag.starts_with("text:s")) para.push_back(' ');
                else if (tag.starts_with("text:tab")) para.push_back('\t');
                else if (tag.starts_with("text:line-break")) para.push_back('\n');
                k = t_end + 1;
            } else {
                para.push_back(inner[k++]);
            }
        }
        para = trim_ws(xml_unescape(para));
        if (!para.empty()) {
            if (is_heading) {
                if (!trim_ws(current_body).empty()) {
                    doc.sections.push_back(DocumentSection{.index = sec_idx++, .title = current_title, .text = trim_ws(current_body)});
                    current_body.clear();
                }
                current_title = para;
                std::string h_line = "## " + para;
                current_body += h_line + "\n\n";
                full_md += h_line + "\n\n";
            } else {
                current_body += para + "\n\n";
                full_md += para + "\n\n";
            }
        }
        pos = end_el + close_tag.size();
    }
    if (!trim_ws(current_body).empty()) {
        doc.sections.push_back(DocumentSection{.index = sec_idx, .title = current_title, .text = trim_ws(current_body)});
    }
    doc.text = trim_ws(full_md);
}

inline void parse_epub_zip(const std::unordered_map<std::string, std::string>& entries, Document& doc) {
    doc.format = "epub";
    std::vector<std::string> chapter_keys;
    for (const auto& [k, _] : entries) {
        if (k.ends_with(".xhtml") || k.ends_with(".html") || k.ends_with(".htm")) {
            if (k.find("nav") == std::string::npos && k.find("toc") == std::string::npos) {
                chapter_keys.push_back(k);
            }
        }
    }
    std::sort(chapter_keys.begin(), chapter_keys.end());
    std::string full_md;
    std::int32_t ch_idx = 1;
    for (const auto& k : chapter_keys) {
        Document ch_doc;
        parse_html_document(entries.at(k), ch_doc);
        if (!ch_doc.text.empty()) {
            std::string title = (!ch_doc.sections.empty() && ch_doc.sections.front().title != "Document")
                                ? ch_doc.sections.front().title
                                : std::format("Chapter {}", ch_idx);
            doc.sections.push_back(DocumentSection{.index = ch_idx++, .title = title, .text = ch_doc.text});
            if (!full_md.empty()) full_md += "\n\n";
            full_md += ch_doc.text;
        }
    }
    doc.text = trim_ws(full_md);
}

} // namespace cfire::ai::detail
