#pragma once
// runtime/core/ai/doc_pdf.hpp — Native Apple PDFKit, Linux pdftotext & Portable PDF Content Stream Parser

#include "doc_zip_deflate.hpp"

namespace cfire::ai::detail {

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

} // namespace cfire::ai::detail
