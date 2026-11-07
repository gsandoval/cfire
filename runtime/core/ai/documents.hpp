#pragma once
// runtime/core/ai/documents.hpp — Zero-Dependency DEFLATE, ZIP, PDF, Office & EPUB Document Parsers

#include "doc_zip_deflate.hpp"
#include "doc_pdf.hpp"
#include "doc_office.hpp"

namespace cfire::ai::detail {

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

} // namespace cfire::ai::detail
