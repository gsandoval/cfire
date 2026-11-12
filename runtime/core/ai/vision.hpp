#pragma once
// runtime/core/ai/vision.hpp — Image Decoders, Visual Embeddings, OCR & Multimodal Document Loaders

#include "documents.hpp"

namespace cfire::ai {
namespace detail {


struct DecodedImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba{};
};

[[nodiscard]] inline auto paeth_predictor(int a, int b, int c) noexcept -> std::uint8_t {
    int p = a + b - c;
    int pa = std::abs(p - a);
    int pb = std::abs(p - b);
    int pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return static_cast<std::uint8_t>(a);
    if (pb <= pc) return static_cast<std::uint8_t>(b);
    return static_cast<std::uint8_t>(c);
}

[[nodiscard]] inline auto decode_image_pixels(std::span<const std::byte> bytes) -> DecodedImage {
    DecodedImage img;
    // 1. PNG decoder (IHDR + IDAT inflate + scanline unfiltering)
    if (bytes.size() >= 33 && static_cast<std::uint8_t>(bytes[0]) == 0x89 &&
        static_cast<std::uint8_t>(bytes[1]) == 'P' && static_cast<std::uint8_t>(bytes[2]) == 'N' &&
        static_cast<std::uint8_t>(bytes[3]) == 'G') {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint8_t bit_depth = 0;
        std::uint8_t color_type = 0;
        std::uint8_t interlace = 0;
        std::vector<std::byte> idat;

        std::size_t pos = 8;
        while (pos + 12 <= bytes.size()) {
            std::uint32_t len = (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[pos])) << 24) |
                                (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[pos + 1])) << 16) |
                                (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[pos + 2])) << 8) |
                                static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[pos + 3]));
            std::string_view type(reinterpret_cast<const char*>(bytes.data() + pos + 4), 4);
            if (pos + 12 + len > bytes.size()) break;
            const std::byte* chunk_data = bytes.data() + pos + 8;
            if (type == "IHDR" && len >= 13) {
                width = (static_cast<std::uint32_t>(static_cast<std::uint8_t>(chunk_data[0])) << 24) |
                        (static_cast<std::uint32_t>(static_cast<std::uint8_t>(chunk_data[1])) << 16) |
                        (static_cast<std::uint32_t>(static_cast<std::uint8_t>(chunk_data[2])) << 8) |
                        static_cast<std::uint32_t>(static_cast<std::uint8_t>(chunk_data[3]));
                height = (static_cast<std::uint32_t>(static_cast<std::uint8_t>(chunk_data[4])) << 24) |
                         (static_cast<std::uint32_t>(static_cast<std::uint8_t>(chunk_data[5])) << 16) |
                         (static_cast<std::uint32_t>(static_cast<std::uint8_t>(chunk_data[6])) << 8) |
                         static_cast<std::uint32_t>(static_cast<std::uint8_t>(chunk_data[7]));
                bit_depth = static_cast<std::uint8_t>(chunk_data[8]);
                color_type = static_cast<std::uint8_t>(chunk_data[9]);
                interlace = static_cast<std::uint8_t>(chunk_data[12]);
            } else if (type == "IDAT") {
                idat.insert(idat.end(), chunk_data, chunk_data + len);
            } else if (type == "IEND") {
                break;
            }
            pos += 12 + len;
        }

        if (width > 0 && height > 0 && width <= 8192 && height <= 8192 && bit_depth == 8 && interlace == 0 && !idat.empty()) {
            std::size_t bpp = (color_type == 6) ? 4 : (color_type == 2) ? 3 : (color_type == 4) ? 2 : (color_type == 0) ? 1 : 0;
            if (bpp > 0) {
                auto raw = inflate_zlib_or_raw(idat);
                std::size_t stride = static_cast<std::size_t>(width) * bpp;
                if (raw.size() >= static_cast<std::size_t>(height) * (1 + stride)) {
                    std::vector<std::uint8_t> recon(static_cast<std::size_t>(height) * stride, 0);
                    for (std::uint32_t y = 0; y < height; ++y) {
                        std::uint8_t filter = static_cast<std::uint8_t>(raw[static_cast<std::size_t>(y) * (1 + stride)]);
                        const std::byte* src_row = raw.data() + static_cast<std::size_t>(y) * (1 + stride) + 1;
                        std::uint8_t* dst_row = recon.data() + static_cast<std::size_t>(y) * stride;
                        const std::uint8_t* prev_row = (y > 0) ? (recon.data() + static_cast<std::size_t>(y - 1) * stride) : nullptr;

                        for (std::size_t x = 0; x < stride; ++x) {
                            std::uint8_t val = static_cast<std::uint8_t>(src_row[x]);
                            std::uint8_t a = (x >= bpp) ? dst_row[x - bpp] : 0;
                            std::uint8_t b = prev_row ? prev_row[x] : 0;
                            std::uint8_t c = (prev_row && x >= bpp) ? prev_row[x - bpp] : 0;
                            switch (filter) {
                                case 0: dst_row[x] = val; break;
                                case 1: dst_row[x] = static_cast<std::uint8_t>(val + a); break;
                                case 2: dst_row[x] = static_cast<std::uint8_t>(val + b); break;
                                case 3: dst_row[x] = static_cast<std::uint8_t>(val + ((static_cast<int>(a) + static_cast<int>(b)) >> 1)); break;
                                case 4: dst_row[x] = static_cast<std::uint8_t>(val + paeth_predictor(a, b, c)); break;
                                default: dst_row[x] = val; break;
                            }
                        }
                    }
                    img.width = width;
                    img.height = height;
                    img.rgba.resize(static_cast<std::size_t>(width) * height * 4);
                    for (std::size_t i = 0; i < static_cast<std::size_t>(width) * height; ++i) {
                        if (bpp == 4) {
                            img.rgba[i * 4 + 0] = recon[i * 4 + 0];
                            img.rgba[i * 4 + 1] = recon[i * 4 + 1];
                            img.rgba[i * 4 + 2] = recon[i * 4 + 2];
                            img.rgba[i * 4 + 3] = recon[i * 4 + 3];
                        } else if (bpp == 3) {
                            img.rgba[i * 4 + 0] = recon[i * 3 + 0];
                            img.rgba[i * 4 + 1] = recon[i * 3 + 1];
                            img.rgba[i * 4 + 2] = recon[i * 3 + 2];
                            img.rgba[i * 4 + 3] = 255;
                        } else if (bpp == 2) {
                            img.rgba[i * 4 + 0] = recon[i * 2];
                            img.rgba[i * 4 + 1] = recon[i * 2];
                            img.rgba[i * 4 + 2] = recon[i * 2];
                            img.rgba[i * 4 + 3] = recon[i * 2 + 1];
                        } else {
                            img.rgba[i * 4 + 0] = recon[i];
                            img.rgba[i * 4 + 1] = recon[i];
                            img.rgba[i * 4 + 2] = recon[i];
                            img.rgba[i * 4 + 3] = 255;
                        }
                    }
                    return img;
                }
            }
        }
    }

    // 2. BMP decoder (24-bit or 32-bit uncompressed)
    if (bytes.size() >= 54 && static_cast<std::uint8_t>(bytes[0]) == 'B' && static_cast<std::uint8_t>(bytes[1]) == 'M') {
        std::uint32_t data_off = read_u32_le(bytes.data() + 10);
        auto w = static_cast<std::int32_t>(read_u32_le(bytes.data() + 18));
        auto h = static_cast<std::int32_t>(read_u32_le(bytes.data() + 22));
        std::uint16_t bpp = read_u16_le(bytes.data() + 28);
        bool flip = (h > 0);
        std::uint32_t abs_h = static_cast<std::uint32_t>(std::abs(h));
        if (w > 0 && abs_h > 0 && w <= 4096 && abs_h <= 4096 && (bpp == 24 || bpp == 32)) {
            std::size_t row_stride = ((static_cast<std::size_t>(w) * (bpp / 8)) + 3) & ~std::size_t(3);
            if (data_off + row_stride * abs_h <= bytes.size()) {
                img.width = static_cast<std::uint32_t>(w);
                img.height = abs_h;
                img.rgba.resize(static_cast<std::size_t>(w) * abs_h * 4);
                for (std::uint32_t y = 0; y < abs_h; ++y) {
                    std::uint32_t src_y = flip ? (abs_h - 1 - y) : y;
                    const std::byte* row = bytes.data() + data_off + src_y * row_stride;
                    for (std::int32_t x = 0; x < w; ++x) {
                        std::size_t dst_idx = (static_cast<std::size_t>(y) * w + x) * 4;
                        std::size_t src_idx = static_cast<std::size_t>(x) * (bpp / 8);
                        img.rgba[dst_idx + 0] = static_cast<std::uint8_t>(row[src_idx + 2]);
                        img.rgba[dst_idx + 1] = static_cast<std::uint8_t>(row[src_idx + 1]);
                        img.rgba[dst_idx + 2] = static_cast<std::uint8_t>(row[src_idx + 0]);
                        img.rgba[dst_idx + 3] = (bpp == 32) ? static_cast<std::uint8_t>(row[src_idx + 3]) : 255;
                    }
                }
                return img;
            }
        }
    }

    // 3. Netpbm P6 (RGB) / P5 (Grayscale) binary decoder (also used by Linux djpeg bridge)
    auto decode_ppm_pgm = [](std::span<const std::byte> b) -> DecodedImage {
        DecodedImage out;
        if (b.size() < 11 || static_cast<char>(b[0]) != 'P') return out;
        char magic = static_cast<char>(b[1]);
        if (magic != '6' && magic != '5') return out;
        std::size_t idx = 2;
        auto skip_ws_and_comments = [&]() {
            while (idx < b.size()) {
                char c = static_cast<char>(b[idx]);
                if (c == '#') {
                    while (idx < b.size() && static_cast<char>(b[idx]) != '\n') ++idx;
                } else if (std::isspace(static_cast<unsigned char>(c))) {
                    ++idx;
                } else {
                    break;
                }
            }
        };
        auto read_int = [&]() -> int {
            skip_ws_and_comments();
            int val = 0;
            bool found = false;
            while (idx < b.size() && std::isdigit(static_cast<unsigned char>(b[idx]))) {
                val = val * 10 + (static_cast<char>(b[idx]) - '0');
                found = true;
                ++idx;
            }
            return found ? val : -1;
        };
        int w = read_int();
        int h = read_int();
        int maxv = read_int();
        if (w <= 0 || h <= 0 || w > 8192 || h > 8192 || maxv <= 0 || maxv > 255 || idx >= b.size()) return out;
        ++idx; // single whitespace byte after maxval
        std::size_t ch = (magic == '6') ? 3u : 1u;
        std::size_t needed = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * ch;
        if (idx + needed > b.size()) return out;
        out.width = static_cast<std::uint32_t>(w);
        out.height = static_cast<std::uint32_t>(h);
        out.rgba.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
        for (std::size_t p = 0; p < static_cast<std::size_t>(w) * static_cast<std::size_t>(h); ++p) {
            if (ch == 3) {
                out.rgba[p * 4 + 0] = static_cast<std::uint8_t>(b[idx + p * 3 + 0]);
                out.rgba[p * 4 + 1] = static_cast<std::uint8_t>(b[idx + p * 3 + 1]);
                out.rgba[p * 4 + 2] = static_cast<std::uint8_t>(b[idx + p * 3 + 2]);
            } else {
                std::uint8_t g = static_cast<std::uint8_t>(b[idx + p]);
                out.rgba[p * 4 + 0] = g;
                out.rgba[p * 4 + 1] = g;
                out.rgba[p * 4 + 2] = g;
            }
            out.rgba[p * 4 + 3] = 255;
        }
        return out;
    };

    if (auto ppm = decode_ppm_pgm(bytes); ppm.width > 0) {
        return ppm;
    }

#if defined(__APPLE__)
    // 4. Apple ImageIO + CoreGraphics hardware-accelerated decoder (JPEG, WebP, HEIC, TIFF, GIF)
    {
        (void)::dlopen("/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation", RTLD_LAZY | RTLD_GLOBAL);
        (void)::dlopen("/System/Library/Frameworks/CoreGraphics.framework/CoreGraphics", RTLD_LAZY | RTLD_GLOBAL);
        (void)::dlopen("/System/Library/Frameworks/ImageIO.framework/ImageIO", RTLD_LAZY | RTLD_GLOBAL);

        using CFDataCreateFn = void*(*)(void*, const std::uint8_t*, long);
        using CFReleaseFn = void(*)(void*);
        using CGImgSrcCreateFn = void*(*)(void*, void*);
        using CGImgSrcCreateImgFn = void*(*)(void*, std::size_t, void*);
        using CGImgGetDimFn = std::size_t(*)(void*);
        using CGColorSpaceCreateFn = void*(*)();
        using CGBitmapCtxCreateFn = void*(*)(void*, std::size_t, std::size_t, std::size_t, std::size_t, void*, std::uint32_t);
        using CGContextDrawImgFn = void(*)(void*, struct CGRect_cfire, void*);

        struct CGRect_cfire { double x; double y; double w; double h; };

        auto fn_data = reinterpret_cast<CFDataCreateFn>(::dlsym(RTLD_DEFAULT, "CFDataCreate"));
        auto fn_rel = reinterpret_cast<CFReleaseFn>(::dlsym(RTLD_DEFAULT, "CFRelease"));
        auto fn_src = reinterpret_cast<CGImgSrcCreateFn>(::dlsym(RTLD_DEFAULT, "CGImageSourceCreateWithData"));
        auto fn_img = reinterpret_cast<CGImgSrcCreateImgFn>(::dlsym(RTLD_DEFAULT, "CGImageSourceCreateImageAtIndex"));
        auto fn_w = reinterpret_cast<CGImgGetDimFn>(::dlsym(RTLD_DEFAULT, "CGImageGetWidth"));
        auto fn_h = reinterpret_cast<CGImgGetDimFn>(::dlsym(RTLD_DEFAULT, "CGImageGetHeight"));
        auto fn_cs = reinterpret_cast<CGColorSpaceCreateFn>(::dlsym(RTLD_DEFAULT, "CGColorSpaceCreateDeviceRGB"));
        auto fn_ctx = reinterpret_cast<CGBitmapCtxCreateFn>(::dlsym(RTLD_DEFAULT, "CGBitmapContextCreate"));
        auto fn_draw = reinterpret_cast<CGContextDrawImgFn>(::dlsym(RTLD_DEFAULT, "CGContextDrawImage"));

        if (fn_data && fn_rel && fn_src && fn_img && fn_w && fn_h && fn_cs && fn_ctx && fn_draw) {
            void* cf_data = fn_data(nullptr, reinterpret_cast<const std::uint8_t*>(bytes.data()), static_cast<long>(bytes.size()));
            if (cf_data) {
                void* img_src = fn_src(cf_data, nullptr);
                if (img_src) {
                    void* cg_img = fn_img(img_src, 0, nullptr);
                    if (cg_img) {
                        std::size_t w = fn_w(cg_img);
                        std::size_t h = fn_h(cg_img);
                        if (w > 0 && h > 0 && w <= 8192 && h <= 8192) {
                            img.width = static_cast<std::uint32_t>(w);
                            img.height = static_cast<std::uint32_t>(h);
                            img.rgba.assign(w * h * 4, 0);
                            void* cs = fn_cs();
                            // kCGImageAlphaPremultipliedLast (1) | kCGBitmapByteOrder32Big (1 << 12 = 4096) -> 16385
                            void* ctx = fn_ctx(img.rgba.data(), w, h, 8, w * 4, cs, 16385u);
                            if (ctx) {
                                fn_draw(ctx, CGRect_cfire{0.0, 0.0, static_cast<double>(w), static_cast<double>(h)}, cg_img);
                                fn_rel(ctx);
                            }
                            if (cs) fn_rel(cs);
                        }
                        fn_rel(cg_img);
                    }
                    fn_rel(img_src);
                }
                fn_rel(cf_data);
                if (img.width > 0 && img.height > 0) return img;
            }
        }
    }
#elif defined(__linux__)
    // 4. Linux JPEG decoder bridge via djpeg if installed
    if (bytes.size() > 4 && static_cast<std::uint8_t>(bytes[0]) == 0xFF && static_cast<std::uint8_t>(bytes[1]) == 0xD8) {
        static int has_djpeg = -1;
        if (has_djpeg < 0) {
            has_djpeg = (std::system("which djpeg >/dev/null 2>&1") == 0) ? 1 : 0;
        }
        if (has_djpeg == 1) {
            cfire::detail::SecureTempDir tmp_dir("cfire_jpg");  // private 0700 dir, removed on scope exit (CF-153)
            std::string tmp_jpg = tmp_dir.file("in.jpg").string();
            if (tmp_dir.ok() && cfire::fs::write_bytes(tmp_jpg, bytes)) {
                std::string cmd = std::format("djpeg -pnm '{}' 2>/dev/null", tmp_jpg);
                std::vector<std::byte> pnm_bytes;
                if (FILE* pipe = ::popen(cmd.c_str(), "r")) {
                    std::byte buf[4096];
                    while (std::size_t n = std::fread(buf, 1, sizeof(buf), pipe)) {
                        pnm_bytes.insert(pnm_bytes.end(), buf, buf + n);
                    }
                    ::pclose(pipe);
                }
                (void)cfire::fs::remove(tmp_jpg);
                if (auto ppm = decode_ppm_pgm(pnm_bytes); ppm.width > 0) {
                    return ppm;
                }
            }
        }
    }
#endif
    return img;
}

[[nodiscard]] inline auto ocr_linux(std::span<const std::byte> img_bytes) -> std::string {
#if defined(__linux__)
    // Tier 1: Zero-compile-time-dependency dynamic dlopen of libtesseract C-API
    struct TessRuntime {
        void* handle = nullptr;
        void* (*create)() = nullptr;
        int (*init3)(void*, const char*, const char*) = nullptr;
        void (*set_image)(void*, const unsigned char*, int, int, int, int) = nullptr;
        char* (*get_utf8)(void*) = nullptr;
        void (*delete_text)(const char*) = nullptr;
        void (*destroy)(void*) = nullptr;
    };
    static const TessRuntime tess = [] {
        TessRuntime rt{};
        const char* libs[] = {"libtesseract.so.5", "libtesseract.so.4", "libtesseract.so"};
        for (const char* lib : libs) {
            rt.handle = ::dlopen(lib, RTLD_LAZY);
            if (rt.handle) break;
        }
        if (rt.handle) {
            rt.create = reinterpret_cast<decltype(rt.create)>(::dlsym(rt.handle, "TessBaseAPICreate"));
            rt.init3 = reinterpret_cast<decltype(rt.init3)>(::dlsym(rt.handle, "TessBaseAPIInit3"));
            rt.set_image = reinterpret_cast<decltype(rt.set_image)>(::dlsym(rt.handle, "TessBaseAPISetImage"));
            rt.get_utf8 = reinterpret_cast<decltype(rt.get_utf8)>(::dlsym(rt.handle, "TessBaseAPIGetUTF8Text"));
            rt.delete_text = reinterpret_cast<decltype(rt.delete_text)>(::dlsym(rt.handle, "TessDeleteText"));
            rt.destroy = reinterpret_cast<decltype(rt.destroy)>(::dlsym(rt.handle, "TessBaseAPIDelete"));
        }
        return rt;
    }();

    if (tess.create && tess.init3 && tess.set_image && tess.get_utf8 && tess.delete_text && tess.destroy) {
        auto decoded = decode_image_pixels(img_bytes);
        if (decoded.width > 0 && decoded.height > 0) {
            void* api = tess.create();
            if (api && tess.init3(api, nullptr, "eng") == 0) {
                tess.set_image(api, decoded.rgba.data(), static_cast<int>(decoded.width),
                               static_cast<int>(decoded.height), 4, static_cast<int>(decoded.width * 4));
                char* raw_txt = tess.get_utf8(api);
                std::string res = raw_txt ? trim_ws(raw_txt) : "";
                if (raw_txt) tess.delete_text(raw_txt);
                tess.destroy(api);
                if (!res.empty()) return res;
            } else if (api) {
                tess.destroy(api);
            }
        }
    }

    // Tier 2: Subprocess fallback to `tesseract` CLI if installed
    static int has_tesseract_cli = -1;
    if (has_tesseract_cli < 0) {
        has_tesseract_cli = (std::system("which tesseract >/dev/null 2>&1") == 0) ? 1 : 0;
    }
    if (has_tesseract_cli == 1) {
        cfire::detail::SecureTempDir tmp_dir("cfire_ocr");  // private 0700 dir, removed on scope exit (CF-153)
        std::string tmp_path = tmp_dir.file("in.img").string();
        if (tmp_dir.ok() && cfire::fs::write_bytes(tmp_path, img_bytes)) {
            std::string cmd = std::format("tesseract '{}' stdout --oem 1 -l eng 2>/dev/null", tmp_path);
            std::string out;
            if (FILE* pipe = ::popen(cmd.c_str(), "r")) {
                char buf[4096];
                while (std::size_t n = std::fread(buf, 1, sizeof(buf), pipe)) out.append(buf, n);
                ::pclose(pipe);
            }
            (void)cfire::fs::remove(tmp_path);
            out = trim_ws(out);
            if (!out.empty()) return out;
        }
    }
#else
    (void)img_bytes;
#endif
    // Tier 3: Optional local Vision LLM OCR fallback when CFIRE_OCR_MODEL is set
    std::string ocr_model = cfire::os::getenv("CFIRE_OCR_MODEL", "");
    if (!ocr_model.empty()) {
        std::string b64 = cfire::base64::encode(img_bytes);
        std::string body = std::format(
            R"({{"model":"{}","messages":[{{"role":"user","content":[{{"type":"text","text":"Extract all visible text from this image verbatim."}},{{"type":"image_url","image_url":{{"url":"data:image/png;base64,{}"}}}}]}}]}})",
            cfire::json::escape_string(ocr_model), b64);
        if (auto res = cfire::http::post("http://127.0.0.1:11434/v1/chat/completions", body, "application/json");
            res && res->status_code == 200) {
            if (auto dyn = cfire::json::parse_dynamic(res->body); dyn && dyn->is_object()) {
                if (auto ch = dyn->get("choices"); ch && ch->is_array() && !ch->as_array().empty()) {
                    if (auto msg = ch->as_array()[0].get("message"); msg && msg->is_object()) {
                        if (auto cnt = msg->get("content"); cnt && cnt->is_string()) {
                            return trim_ws(cnt->as_str());
                        }
                    }
                }
            }
        }
    }
    return "";
}

[[nodiscard]] inline auto embed_image_local(std::span<const std::byte> bytes, std::size_t dims = 384) -> std::vector<float> {
    std::vector<float> vec(dims, 0.0f);
    if (bytes.empty()) return vec;

    // 1. Pixel-true spatial color moments, Sobel HOG edge orientation histograms & 8x8 dHash
    auto decoded = decode_image_pixels(bytes);
    if (decoded.width > 0 && decoded.height > 0) {
        const std::uint32_t W = decoded.width;
        const std::uint32_t H = decoded.height;
        auto luma_at = [&](std::uint32_t x, std::uint32_t y) -> float {
            std::size_t idx = (static_cast<std::size_t>(y) * W + x) * 4;
            return 0.299f * decoded.rgba[idx] + 0.587f * decoded.rgba[idx + 1] + 0.114f * decoded.rgba[idx + 2];
        };

        // 1a. 4x4 spatial grid color & luminance moments
        for (std::uint32_t gy = 0; gy < 4; ++gy) {
            for (std::uint32_t gx = 0; gx < 4; ++gx) {
                std::uint32_t x0 = (gx * W) / 4;
                std::uint32_t x1 = std::max(x0 + 1, ((gx + 1) * W) / 4);
                std::uint32_t y0 = (gy * H) / 4;
                std::uint32_t y1 = std::max(y0 + 1, ((gy + 1) * H) / 4);
                double sum_r = 0, sum_g = 0, sum_b = 0, sum_l = 0;
                std::size_t count = 0;
                for (std::uint32_t y = y0; y < y1 && y < H; ++y) {
                    for (std::uint32_t x = x0; x < x1 && x < W; ++x) {
                        std::size_t idx = (static_cast<std::size_t>(y) * W + x) * 4;
                        sum_r += decoded.rgba[idx];
                        sum_g += decoded.rgba[idx + 1];
                        sum_b += decoded.rgba[idx + 2];
                        sum_l += luma_at(x, y);
                        ++count;
                    }
                }
                if (count > 0) {
                    int qr = static_cast<int>((sum_r / count) / 32.0);
                    int qg = static_cast<int>((sum_g / count) / 32.0);
                    int qb = static_cast<int>((sum_b / count) / 32.0);
                    int ql = static_cast<int>((sum_l / count) / 32.0);
                    accumulate_feature(vec, std::format("cell:{},{}:rgb:{},{},{}", gx, gy, qr, qg, qb), 3.0f);
                    accumulate_feature(vec, std::format("cell:{},{}:luma:{}", gx, gy, ql), 2.5f);
                }
            }
        }

        // 1b. 2x2 spatial grid x 8-bin Sobel edge orientation histogram (HOG)
        if (W >= 3 && H >= 3) {
            for (std::uint32_t qy = 0; qy < 2; ++qy) {
                for (std::uint32_t qx = 0; qx < 2; ++qx) {
                    float bins[8] = {0};
                    std::uint32_t x0 = std::max(1u, (qx * W) / 2);
                    std::uint32_t x1 = std::min(W - 1, ((qx + 1) * W) / 2);
                    std::uint32_t y0 = std::max(1u, (qy * H) / 2);
                    std::uint32_t y1 = std::min(H - 1, ((qy + 1) * H) / 2);
                    for (std::uint32_t y = y0; y < y1; ++y) {
                        for (std::uint32_t x = x0; x < x1; ++x) {
                            float gx_val = (luma_at(x + 1, y - 1) + 2.0f * luma_at(x + 1, y) + luma_at(x + 1, y + 1)) -
                                           (luma_at(x - 1, y - 1) + 2.0f * luma_at(x - 1, y) + luma_at(x - 1, y + 1));
                            float gy_val = (luma_at(x - 1, y + 1) + 2.0f * luma_at(x, y + 1) + luma_at(x + 1, y + 1)) -
                                           (luma_at(x - 1, y - 1) + 2.0f * luma_at(x, y - 1) + luma_at(x + 1, y - 1));
                            float mag = std::sqrt(gx_val * gx_val + gy_val * gy_val);
                            if (mag > 16.0f) {
                                float angle = std::atan2(gy_val, gx_val) + 3.14159265f; // [0, 2pi]
                                int bin = static_cast<int>((angle / (2.0f * 3.14159265f)) * 8.0f) & 7;
                                bins[bin] += mag;
                            }
                        }
                    }
                    int dom_bin = 0;
                    float max_mag = 0.0f;
                    for (int b = 0; b < 8; ++b) {
                        if (bins[b] > max_mag) { max_mag = bins[b]; dom_bin = b; }
                    }
                    if (max_mag > 0.0f) {
                        accumulate_feature(vec, std::format("hog:{},{}:dom:{}", qx, qy, dom_bin), 4.0f);
                    }
                }
            }
        }

        // 1c. 8x8 perceptual dHash horizontal gradient bits
        for (std::uint32_t dy = 0; dy < 8; ++dy) {
            for (std::uint32_t dx = 0; dx < 8; ++dx) {
                std::uint32_t sx0 = (dx * W) / 9;
                std::uint32_t sx1 = std::min(W - 1, ((dx + 1) * W) / 9);
                std::uint32_t sy = std::min(H - 1, (dy * H) / 8);
                int bit = (luma_at(sx0, sy) < luma_at(sx1, sy)) ? 1 : 0;
                accumulate_feature(vec, std::format("dhash:{},{}:{}", dx, dy, bit), 1.2f);
            }
        }
    }

    // 2. Structural header & multi-scale byte shingle projections (fallback for undecoded formats like WebP/JPEG)
    if (bytes.size() >= 24 && static_cast<std::uint8_t>(bytes[0]) == 0x89 && static_cast<std::uint8_t>(bytes[1]) == 'P') {
        std::uint32_t w = (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[16])) << 24) |
                          (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[17])) << 16) |
                          (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[18])) << 8) |
                          static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[19]));
        std::uint32_t h = (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[20])) << 24) |
                          (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[21])) << 16) |
                          (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[22])) << 8) |
                          static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[23]));
        accumulate_feature(vec, std::format("png:{}x{}", w, h), 2.0f);
    }

    if (decoded.width == 0) {
        std::size_t n = bytes.size();
        std::size_t step = std::max<std::size_t>(1, n / 512);
        for (std::size_t i = 0; i + 4 <= n; i += step) {
            std::string_view shingle(reinterpret_cast<const char*>(bytes.data() + i), 4);
            accumulate_feature(vec, shingle, 1.0f);
            std::uint8_t qbucket = static_cast<std::uint8_t>(bytes[i]) >> 4;
            std::size_t region = (i * 8) / n;
            char region_tok[4] = {'R', static_cast<char>('0' + region), static_cast<char>('A' + qbucket), '\0'};
            accumulate_feature(vec, std::string_view(region_tok, 3), 0.75f);
        }
    }

    // 3. Fuse any OCR or embedded text semantics into the same vector space
    std::string ocr_txt = ocr_apple_vision(bytes);
    if (ocr_txt.empty()) ocr_txt = ocr_linux(bytes);
    std::string meta_txt = extract_image_text_metadata(bytes);
    std::string combined_txt = ocr_txt;
    if (!meta_txt.empty()) {
        if (!combined_txt.empty()) combined_txt += "\n";
        combined_txt += meta_txt;
    }
    if (!combined_txt.empty()) {
        auto txt_vec = embed_trigram_hash(combined_txt, dims);
        for (std::size_t i = 0; i < dims && i < txt_vec.size(); ++i) {
            vec[i] += txt_vec[i] * 2.0f;
        }
    }

    return normalize(vec);
}

} // namespace detail

// --- Public Multimodal, Document Ingestion, OCR & ZIP Helpers (`core:ai`) ---

[[nodiscard]] inline auto create_zip(const std::vector<ZipEntry>& entries) -> std::vector<std::byte> {
    std::vector<std::byte> out;
    struct CdRecord {
        std::string name;
        std::uint32_t crc;
        std::uint32_t size;
        std::uint32_t offset;
    };
    std::vector<CdRecord> cd_records;

    for (const auto& e : entries) {
        std::uint32_t offset = static_cast<std::uint32_t>(out.size());
        std::uint32_t crc = detail::crc32_bytes(e.content);
        std::uint32_t sz = static_cast<std::uint32_t>(e.content.size());

        detail::append_u32_le(out, 0x04034b50u); // Local file header signature
        detail::append_u16_le(out, 20);          // Version needed
        detail::append_u16_le(out, 0);           // Flags
        detail::append_u16_le(out, 0);           // Method 0 = STORE
        detail::append_u16_le(out, 0);           // Mod time
        detail::append_u16_le(out, 0);           // Mod date
        detail::append_u32_le(out, crc);
        detail::append_u32_le(out, sz);
        detail::append_u32_le(out, sz);
        detail::append_u16_le(out, static_cast<std::uint16_t>(e.name.size()));
        detail::append_u16_le(out, 0);           // Extra len
        for (char c : e.name) out.push_back(static_cast<std::byte>(c));
        for (char c : e.content) out.push_back(static_cast<std::byte>(c));

        cd_records.push_back(CdRecord{e.name, crc, sz, offset});
    }

    std::uint32_t cd_start = static_cast<std::uint32_t>(out.size());
    for (const auto& r : cd_records) {
        detail::append_u32_le(out, 0x02014b50u); // Central directory file header
        detail::append_u16_le(out, 20);
        detail::append_u16_le(out, 20);
        detail::append_u16_le(out, 0);
        detail::append_u16_le(out, 0);
        detail::append_u16_le(out, 0);
        detail::append_u16_le(out, 0);
        detail::append_u32_le(out, r.crc);
        detail::append_u32_le(out, r.size);
        detail::append_u32_le(out, r.size);
        detail::append_u16_le(out, static_cast<std::uint16_t>(r.name.size()));
        detail::append_u16_le(out, 0);
        detail::append_u16_le(out, 0);
        detail::append_u16_le(out, 0);
        detail::append_u16_le(out, 0);
        detail::append_u32_le(out, 0);
        detail::append_u32_le(out, r.offset);
        for (char c : r.name) out.push_back(static_cast<std::byte>(c));
    }
    std::uint32_t cd_size = static_cast<std::uint32_t>(out.size()) - cd_start;

    detail::append_u32_le(out, 0x06054b50u); // End of central directory
    detail::append_u16_le(out, 0);
    detail::append_u16_le(out, 0);
    detail::append_u16_le(out, static_cast<std::uint16_t>(cd_records.size()));
    detail::append_u16_le(out, static_cast<std::uint16_t>(cd_records.size()));
    detail::append_u32_le(out, cd_size);
    detail::append_u32_le(out, cd_start);
    detail::append_u16_le(out, 0);
    return out;
}

[[nodiscard]] inline auto detect_mime_type(std::string_view path_or_name, std::span<const std::byte> bytes = {}) -> std::string {
    if (bytes.size() >= 5 && std::memcmp(bytes.data(), "%PDF-", 5) == 0) return "application/pdf";
    if (bytes.size() >= 8 && static_cast<std::uint8_t>(bytes[0]) == 0x89 && std::memcmp(bytes.data() + 1, "PNG", 3) == 0) return "image/png";
    if (bytes.size() >= 3 && static_cast<std::uint8_t>(bytes[0]) == 0xFF && static_cast<std::uint8_t>(bytes[1]) == 0xD8 && static_cast<std::uint8_t>(bytes[2]) == 0xFF) return "image/jpeg";
    if (bytes.size() >= 6 && (std::memcmp(bytes.data(), "GIF87a", 6) == 0 || std::memcmp(bytes.data(), "GIF89a", 6) == 0)) return "image/gif";
    if (bytes.size() >= 12 && std::memcmp(bytes.data(), "RIFF", 4) == 0 && std::memcmp(bytes.data() + 8, "WEBP", 4) == 0) return "image/webp";
    if (bytes.size() >= 12 && std::memcmp(bytes.data(), "RIFF", 4) == 0 && std::memcmp(bytes.data() + 8, "WAVE", 4) == 0) return "audio/wav";
    if (bytes.size() >= 2 && static_cast<std::uint8_t>(bytes[0]) == 'B' && static_cast<std::uint8_t>(bytes[1]) == 'M') return "image/bmp";

    std::string lower;
    lower.reserve(path_or_name.size());
    for (unsigned char c : path_or_name) lower.push_back(static_cast<char>(std::tolower(c)));

    if (lower.ends_with(".pdf")) return "application/pdf";
    if (lower.ends_with(".docx")) return "application/vnd.openxmlformats-officedocument.wordprocessingml.document";
    if (lower.ends_with(".pptx")) return "application/vnd.openxmlformats-officedocument.presentationml.presentation";
    if (lower.ends_with(".xlsx")) return "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet";
    if (lower.ends_with(".odt")) return "application/vnd.oasis.opendocument.text";
    if (lower.ends_with(".odp")) return "application/vnd.oasis.opendocument.presentation";
    if (lower.ends_with(".ods")) return "application/vnd.oasis.opendocument.spreadsheet";
    if (lower.ends_with(".epub")) return "application/epub+zip";
    if (lower.ends_with(".png")) return "image/png";
    if (lower.ends_with(".bmp")) return "image/bmp";
    if (lower.ends_with(".jpg") || lower.ends_with(".jpeg")) return "image/jpeg";
    if (lower.ends_with(".webp")) return "image/webp";
    if (lower.ends_with(".gif")) return "image/gif";
    if (lower.ends_with(".svg")) return "image/svg+xml";
    if (lower.ends_with(".wav")) return "audio/wav";
    if (lower.ends_with(".mp3")) return "audio/mpeg";
    if (lower.ends_with(".html") || lower.ends_with(".htm")) return "text/html";
    if (lower.ends_with(".md") || lower.ends_with(".markdown")) return "text/markdown";
    if (lower.ends_with(".csv")) return "text/csv";
    if (lower.ends_with(".json")) return "application/json";

    if (bytes.size() >= 4 && detail::read_u32_le(bytes.data()) == 0x04034b50u) {
        auto entries = detail::extract_zip_entries(bytes);
        if (entries.contains("mimetype")) {
            std::string mt = detail::trim_ws(entries.at("mimetype"));
            if (!mt.empty()) return mt;
        }
        if (entries.contains("word/document.xml")) return "application/vnd.openxmlformats-officedocument.wordprocessingml.document";
        if (entries.contains("ppt/presentation.xml") || entries.contains("ppt/slides/slide1.xml")) return "application/vnd.openxmlformats-officedocument.presentationml.presentation";
        if (entries.contains("xl/workbook.xml") || entries.contains("xl/worksheets/sheet1.xml")) return "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet";
        if (entries.contains("META-INF/container.xml")) return "application/epub+zip";
        if (entries.contains("content.xml")) return "application/vnd.oasis.opendocument.text";
        return "application/zip";
    }
    return "text/plain";
}

[[nodiscard]] inline auto ocr_bytes(std::span<const std::byte> data) -> std::expected<std::string, cfire::Error> {
    if (data.empty()) return cfire::make_error("cannot run OCR on empty byte buffer");
    std::string vis = detail::ocr_apple_vision(data);
    if (vis.empty()) vis = detail::ocr_linux(data);
    std::string meta = detail::extract_image_text_metadata(data);
    if (!vis.empty() && !meta.empty() && vis.find(meta) == std::string::npos) {
        return vis + "\n" + meta;
    }
    if (!vis.empty()) return vis;
    return meta;
}

[[nodiscard]] inline auto ocr_bytes(std::span<const std::uint8_t> data) -> std::expected<std::string, cfire::Error> {
    return ocr_bytes(std::as_bytes(data));
}

[[nodiscard]] inline auto ocr(const std::filesystem::path& path) -> std::expected<std::string, cfire::Error> {
    auto bytes = cfire::fs::read_bytes(path);
    if (!bytes) return std::unexpected(bytes.error());
    return ocr_bytes(*bytes);
}

[[nodiscard]] inline auto parse_document(std::span<const std::byte> bytes, std::string_view filename = "document.txt")
    -> std::expected<Document, cfire::Error>
{
    Document doc;
    doc.path = std::string(filename);
    doc.mime_type = detect_mime_type(filename, bytes);

    if (doc.mime_type == "application/pdf") {
        detail::parse_pdf_portable(bytes, doc);
        return doc;
    }

    if (doc.mime_type.find("wordprocessingml") != std::string::npos ||
        doc.mime_type.find("presentationml") != std::string::npos ||
        doc.mime_type.find("spreadsheetml") != std::string::npos ||
        doc.mime_type.find("opendocument") != std::string::npos ||
        doc.mime_type == "application/epub+zip" ||
        doc.mime_type == "application/zip") {
        auto entries = detail::extract_zip_entries(bytes);
        if (entries.contains("word/document.xml")) {
            detail::parse_docx_xml(entries.at("word/document.xml"), doc);
            return doc;
        }
        if (doc.mime_type.find("presentationml") != std::string::npos || entries.contains("ppt/slides/slide1.xml")) {
            detail::parse_pptx_zip(entries, doc);
            return doc;
        }
        if (doc.mime_type.find("spreadsheetml") != std::string::npos || entries.contains("xl/worksheets/sheet1.xml")) {
            detail::parse_xlsx_zip(entries, doc);
            return doc;
        }
        if (doc.mime_type.find("opendocument") != std::string::npos || entries.contains("content.xml")) {
            detail::parse_odf_zip(entries, doc.mime_type, doc);
            return doc;
        }
        if (doc.mime_type == "application/epub+zip" || entries.contains("META-INF/container.xml")) {
            detail::parse_epub_zip(entries, doc);
            return doc;
        }
    }

    if (doc.mime_type.starts_with("image/")) {
        doc.format = "image";
        auto ocr_res = ocr_bytes(bytes);
        doc.text = ocr_res ? *ocr_res : "";
        doc.sections.push_back(DocumentSection{
            .index = 1,
            .title = std::filesystem::path(filename).filename().string(),
            .text = doc.text
        });
        return doc;
    }

    std::string_view raw_text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (doc.mime_type == "text/html") {
        detail::parse_html_document(raw_text, doc);
        return doc;
    }

    if (doc.mime_type == "text/csv") {
        doc.format = "csv";
        auto rows = cfire::csv::parse(raw_text);
        std::string md;
        for (std::size_t r = 0; r < rows.size(); ++r) {
            md += "| ";
            for (const auto& cell : rows[r]) md += cell + " | ";
            if (!rows[r].empty()) md.pop_back();
            md += "\n";
            if (r == 0 && !rows[r].empty()) {
                md += "|";
                for (std::size_t c = 0; c < rows[r].size(); ++c) md += " --- |";
                md += "\n";
            }
        }
        doc.text = detail::trim_ws(md);
        doc.sections.push_back(DocumentSection{.index = 1, .title = "CSV Table", .text = doc.text});
        return doc;
    }

    doc.format = (doc.mime_type == "text/markdown") ? "markdown" : "text";
    detail::parse_markdown_sections(raw_text, doc);
    return doc;
}

[[nodiscard]] inline auto parse_document(std::span<const std::uint8_t> bytes, std::string_view filename = "document.txt")
    -> std::expected<Document, cfire::Error>
{
    return parse_document(std::as_bytes(bytes), filename);
}

[[nodiscard]] inline auto parse_document(std::string_view content, std::string_view filename = "document.md")
    -> std::expected<Document, cfire::Error>
{
    return parse_document(std::as_bytes(std::span<const char>(content.data(), content.size())), filename);
}

[[nodiscard]] inline auto load_document(const std::filesystem::path& path)
    -> std::expected<Document, cfire::Error>
{
    auto bytes = cfire::fs::read_bytes(path);
    if (!bytes) return std::unexpected(bytes.error());
    auto doc = parse_document(*bytes, path.string());
    if (!doc) return std::unexpected(doc.error());
    doc->path = path.string();
    return doc;
}

[[nodiscard]] inline auto chunk_document(
    const Document& doc,
    std::int32_t max_chars = 512,
    std::int32_t overlap = 64
) -> std::vector<DocumentSection> {
    std::vector<DocumentSection> out;
    if (doc.sections.empty()) {
        for (auto& ch : chunk_text(doc.text, max_chars, overlap)) {
            out.push_back(DocumentSection{.index = 1, .title = doc.path, .text = std::move(ch)});
        }
        return out;
    }
    for (const auto& sec : doc.sections) {
        auto pieces = chunk_text(sec.text, max_chars, overlap);
        if (pieces.empty() && !sec.text.empty()) {
            out.push_back(sec);
        } else {
            for (auto& p : pieces) {
                out.push_back(DocumentSection{
                    .index = sec.index,
                    .title = sec.title,
                    .text = std::move(p)
                });
            }
        }
    }
    return out;
}

[[nodiscard]] inline auto make_attachment(
    std::string_view filename,
    std::span<const std::byte> data,
    std::string_view mime_type = ""
) -> Attachment {
    Attachment att;
    att.filename = std::filesystem::path(filename).filename().string();
    att.mime_type = mime_type.empty() ? detect_mime_type(filename, data) : std::string(mime_type);
    att.data.assign(data.begin(), data.end());
    if (!att.mime_type.starts_with("image/") && !att.mime_type.starts_with("audio/")) {
        if (auto doc = parse_document(data, filename); doc && !doc->text.empty()) {
            att.extracted_text = doc->text;
        }
    }
    return att;
}

[[nodiscard]] inline auto load_attachment(const std::filesystem::path& path)
    -> std::expected<Attachment, cfire::Error>
{
    auto bytes = cfire::fs::read_bytes(path);
    if (!bytes) return std::unexpected(bytes.error());
    return make_attachment(path.string(), *bytes);
}

[[nodiscard]] inline auto user_with_file(std::string content, const std::filesystem::path& path)
    -> std::expected<Message, cfire::Error>
{
    auto att = load_attachment(path);
    if (!att) return std::unexpected(att.error());
    return user_with_attachment(std::move(content), std::move(*att));
}

[[nodiscard]] inline auto embed_image_bytes(
    std::string_view model,
    std::span<const std::byte> bytes,
    std::string_view mime_type = "image/png"
) -> std::expected<std::vector<float>, cfire::Error> {
    if (bytes.empty()) return cfire::make_error("cannot embed empty image buffer");
    if (model.empty() || model == "local" || model == "apple" || model == "native") {
        return detail::embed_image_local(bytes, 384);
    }
    // Cloud multimodal embedding fallback (e.g. Gemini embedContent with inlineData)
    std::string key = cfire::os::getenv("GEMINI_API_KEY", "");
    std::string url = std::format("https://generativelanguage.googleapis.com/v1beta/models/{}:embedContent", model);
    std::unordered_map<std::string, std::string> headers;
    headers["x-goog-api-key"] = key;
    std::string b64 = cfire::base64::encode(bytes);
    std::string body = std::format(
        R"({{"model":"models/{}","content":{{"parts":[{{"inlineData":{{"mimeType":"{}","data":"{}"}}}}]}}}})",
        cfire::json::escape_string(model),
        cfire::json::escape_string(mime_type),
        b64);
    auto res = cfire::http::post(url, body, "application/json", headers);
    if (!res) return std::unexpected(res.error());
    if (res->status_code >= 400) {
        return cfire::make_error(std::format("AI embed_image HTTP {} error: {}", res->status_code, res->body));
    }
    auto dyn = cfire::json::parse_dynamic(res->body);
    if (dyn && dyn->is_object()) {
        if (auto emb_obj = dyn->get("embedding"); emb_obj && emb_obj->is_object()) {
            if (auto vals = emb_obj->get("values"); vals && vals->is_array()) {
                std::vector<float> vec;
                vec.reserve(vals->as_array().size());
                for (const auto& v : vals->as_array()) vec.push_back(static_cast<float>(v.as_float()));
                return vec;
            }
        }
    }
    return cfire::make_error("image embedding vector not found in provider response");
}

[[nodiscard]] inline auto embed_image(
    std::string_view model,
    const std::filesystem::path& path
) -> std::expected<std::vector<float>, cfire::Error> {
    auto bytes = cfire::fs::read_bytes(path);
    if (!bytes) return std::unexpected(bytes.error());
    std::string mime = detect_mime_type(path.string(), *bytes);
    return embed_image_bytes(model, *bytes, mime);
}

[[nodiscard]] inline auto detect_language(std::string_view text) -> std::string {
    if (text.empty()) return "und";
#if defined(__APPLE__)
    const auto& rt = detail::apple_nl_runtime();
    if (rt.get_class && rt.sel_register && rt.msg_send) {
        void* nsStringCls = rt.get_class("NSString");
        void* nlRecCls = rt.get_class("NLLanguageRecognizer");
        if (nsStringCls && nlRecCls) {
            void* selWithUTF8 = rt.sel_register("stringWithUTF8String:");
            void* selDomLang = rt.sel_register("dominantLanguageForString:");
            void* selUTF8 = rt.sel_register("UTF8String");

            using FnStrUTF8 = void*(*)(void*, void*, const char*);
            using FnDomLang = void*(*)(void*, void*, void*);
            using FnGetUTF8 = const char*(*)(void*, void*);

            std::string s(text);
            void* nsText = reinterpret_cast<FnStrUTF8>(rt.msg_send)(nsStringCls, selWithUTF8, s.c_str());
            if (nsText) {
                void* dom = reinterpret_cast<FnDomLang>(rt.msg_send)(nlRecCls, selDomLang, nsText);
                if (dom) {
                    const char* cstr = reinterpret_cast<FnGetUTF8>(rt.msg_send)(dom, selUTF8);
                    if (cstr && *cstr) return std::string(cstr);
                }
            }
        }
    }
#endif
    // Portable UTF-8 script & stopword language classifier for Linux & WebAssembly
    std::size_t kana = 0, hangul = 0, cjk = 0, cyrillic = 0, arabic = 0, devanagari = 0;
    for (std::size_t i = 0; i < text.size();) {
        auto b0 = static_cast<unsigned char>(text[i]);
        std::uint32_t cp = 0;
        std::size_t step = 1;
        if (b0 < 0x80) {
            cp = b0;
        } else if ((b0 & 0xE0) == 0xC0 && i + 1 < text.size()) {
            cp = ((b0 & 0x1Fu) << 6) | (static_cast<unsigned char>(text[i + 1]) & 0x3Fu);
            step = 2;
        } else if ((b0 & 0xF0) == 0xE0 && i + 2 < text.size()) {
            cp = ((b0 & 0x0Fu) << 12) | ((static_cast<unsigned char>(text[i + 1]) & 0x3Fu) << 6) |
                 (static_cast<unsigned char>(text[i + 2]) & 0x3Fu);
            step = 3;
        } else if ((b0 & 0xF8) == 0xF0 && i + 3 < text.size()) {
            cp = ((b0 & 0x07u) << 18) | ((static_cast<unsigned char>(text[i + 1]) & 0x3Fu) << 12) |
                 ((static_cast<unsigned char>(text[i + 2]) & 0x3Fu) << 6) |
                 (static_cast<unsigned char>(text[i + 3]) & 0x3Fu);
            step = 4;
        }
        i += step;
        if ((cp >= 0x3040 && cp <= 0x309F) || (cp >= 0x30A0 && cp <= 0x30FF)) ++kana;
        else if ((cp >= 0xAC00 && cp <= 0xD7AF) || (cp >= 0x1100 && cp <= 0x11FF)) ++hangul;
        else if (cp >= 0x4E00 && cp <= 0x9FFF) ++cjk;
        else if (cp >= 0x0400 && cp <= 0x04FF) ++cyrillic;
        else if (cp >= 0x0600 && cp <= 0x06FF) ++arabic;
        else if (cp >= 0x0900 && cp <= 0x097F) ++devanagari;
    }
    if (kana > 0) return "ja";
    if (hangul > 0) return "ko";
    if (cjk > 0) return "zh-Hans";
    if (cyrillic > 0) return "ru";
    if (arabic > 0) return "ar";
    if (devanagari > 0) return "hi";

    std::string padded = " ";
    for (unsigned char c : text) {
        if (std::isalpha(c)) padded.push_back(static_cast<char>(std::tolower(c)));
        else if (c >= 0x80) padded.push_back(static_cast<char>(c));
        else padded.push_back(' ');
    }
    padded.push_back(' ');

    auto score_words = [&](std::initializer_list<std::string_view> words) -> int {
        int s = 0;
        for (auto w : words) {
            if (padded.find(w) != std::string::npos) ++s;
        }
        return s;
    };
    int es = score_words({" el ", " la ", " los ", " las ", " una ", " con ", " para ", " por ", " que ", " del ", " hola ", " mundo "});
    int fr = score_words({" le ", " la ", " les ", " des ", " une ", " avec ", " dans ", " pour ", " est ", " pas ", " bonjour ", " monde "});
    int de = score_words({" der ", " die ", " das ", " und ", " ist ", " nicht ", " mit ", " auf ", " ein ", " eine ", " hallo ", " welt "});
    int pt = score_words({" os ", " as ", " uma ", " com ", " para ", " nao ", " por ", " dos ", " das ", " ola "});
    int it = score_words({" il ", " lo ", " gli ", " una ", " con ", " per ", " che ", " non ", " del ", " ciao ", " mondo "});
    int en = score_words({" the ", " and ", " is ", " in ", " to ", " of ", " for ", " with ", " on ", " that ", " hello ", " world "});

    int best = en;
    std::string lang = "en";
    if (es > best) { best = es; lang = "es"; }
    if (fr > best) { best = fr; lang = "fr"; }
    if (de > best) { best = de; lang = "de"; }
    if (pt > best) { best = pt; lang = "pt"; }
    if (it > best) { best = it; lang = "it"; }
    return lang;
}


} // namespace cfire::ai
