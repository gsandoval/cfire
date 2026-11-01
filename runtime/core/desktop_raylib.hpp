#pragma once
// runtime/core/desktop_raylib.hpp — Desktop (macOS / Linux) Retina High-DPI & System Font Engine for vendor:raylib

#include <raylib.h>
#if __has_include(<raymath.h>)
#include <raymath.h>
#endif
#if __has_include(<rlgl.h>)
#include <rlgl.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <list>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#if defined(__APPLE__) && !defined(PLATFORM_IOS)
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>
#elif defined(__EMSCRIPTEN__) || defined(PLATFORM_WEB)
#include <emscripten.h>
#endif

namespace cfire::desktop_rl {

inline auto user_config_flags() noexcept -> unsigned int& {
    static unsigned int flags = 0;
    return flags;
}

#if defined(__APPLE__) && !defined(PLATFORM_IOS)

struct TextCacheKey {
    std::string text;
    int font_size = 0;
    int dpi_x100 = 100;

    bool operator==(const TextCacheKey& o) const noexcept {
        return font_size == o.font_size && dpi_x100 == o.dpi_x100 && text == o.text;
    }
};

struct TextCacheKeyHash {
    std::size_t operator()(const TextCacheKey& k) const noexcept {
        std::size_t h = std::hash<std::string>{}(k.text);
        h ^= static_cast<std::size_t>(k.font_size) * 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        h ^= static_cast<std::size_t>(k.dpi_x100) * 0x517cc1b727220a95ULL + (h << 6) + (h >> 2);
        return h;
    }
};

struct CachedTextEntry {
    Texture2D tex{};
    int tex_w_px = 0;
    int tex_h_px = 0;
    int pad_x_px = 0;
    float advance_logical = 0.0f;
    float draw_w_logical = 0.0f;
    float draw_h_logical = 0.0f;
    float y_offset_logical = 0.0f;
    bool has_texture = false;
};

class CoreTextCache {
public:
    static constexpr std::size_t kMaxEntries = 512;

    static auto instance() noexcept -> CoreTextCache& {
        static CoreTextCache inst;
        return inst;
    }

    auto get_or_create(std::string_view line_sv, int font_size, bool upload_gpu) -> const CachedTextEntry& {
        int clamped_size = std::max(1, font_size);
        float dpi = 1.0f;
        if (::IsWindowReady()) {
            Vector2 sc = ::GetWindowScaleDPI();
            if (sc.x > 0.5f) dpi = sc.x;
        }
        int dpi_x100 = static_cast<int>(std::lround(dpi * 100.0f));

        TextCacheKey key{std::string(line_sv), clamped_size, dpi_x100};
        auto it = map_.find(key);
        if (it != map_.end()) {
            lru_.splice(lru_.begin(), lru_, it->second.second);
            if (upload_gpu && !it->second.first.has_texture && ::IsWindowReady()) {
                rasterize_entry(key, dpi, true, it->second.first);
            }
            return it->second.first;
        }

        if (map_.size() >= kMaxEntries && !lru_.empty()) {
            const TextCacheKey& oldest_key = lru_.back();
            auto old_it = map_.find(oldest_key);
            if (old_it != map_.end()) {
                if (old_it->second.first.has_texture && old_it->second.first.tex.id != 0 && ::IsWindowReady()) {
                    ::UnloadTexture(old_it->second.first.tex);
                }
                map_.erase(old_it);
            }
            lru_.pop_back();
        }

        lru_.push_front(key);
        CachedTextEntry entry{};
        rasterize_entry(key, dpi, upload_gpu && ::IsWindowReady(), entry);
        auto [ins_it, _] = map_.emplace(key, std::make_pair(entry, lru_.begin()));
        return ins_it->second.first;
    }

    void clear_gpu_resources() noexcept {
        for (auto& [k, pair_val] : map_) {
            if (pair_val.first.has_texture && pair_val.first.tex.id != 0) {
                ::UnloadTexture(pair_val.first.tex);
                pair_val.first.tex = Texture2D{};
                pair_val.first.has_texture = false;
            }
        }
        map_.clear();
        lru_.clear();
    }

private:
    std::list<TextCacheKey> lru_;
    std::unordered_map<TextCacheKey, std::pair<CachedTextEntry, std::list<TextCacheKey>::iterator>, TextCacheKeyHash> map_;

    static void rasterize_entry(const TextCacheKey& key, float dpi, bool upload_gpu, CachedTextEntry& out) noexcept {
        if (key.text.empty()) return;

        CGFloat px_font_size = static_cast<CGFloat>(key.font_size) * static_cast<CGFloat>(dpi);
        CTFontUIFontType ui_type = (key.font_size <= 14) ? kCTFontUIFontEmphasizedSystem : kCTFontUIFontSystem;
        CTFontRef ct_font = CTFontCreateUIFontForLanguage(ui_type, px_font_size, nullptr);
        if (!ct_font) {
            ct_font = CTFontCreateWithName(CFSTR(".AppleSystemUIFont"), px_font_size, nullptr);
        }
        if (!ct_font) return;

        CFStringRef cf_str = CFStringCreateWithBytes(
            kCFAllocatorDefault,
            reinterpret_cast<const UInt8*>(key.text.data()),
            static_cast<CFIndex>(key.text.size()),
            kCFStringEncodingUTF8,
            false);
        if (!cf_str) {
            // Fallback to ASCII replacement if invalid UTF-8 byte sequence
            cf_str = CFStringCreateWithCString(kCFAllocatorDefault, key.text.c_str(), kCFStringEncodingISOLatin1);
        }
        if (!cf_str) {
            CFRelease(ct_font);
            return;
        }

        const void* keys[] = { kCTFontAttributeName, kCTForegroundColorFromContextAttributeName };
        const void* values[] = { ct_font, kCFBooleanTrue };
        CFDictionaryRef attrs = CFDictionaryCreate(
            kCFAllocatorDefault, keys, values, 2,
            &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);

        CFAttributedStringRef attr_str = CFAttributedStringCreate(kCFAllocatorDefault, cf_str, attrs);
        CTLineRef line = CTLineCreateWithAttributedString(attr_str);

        CGFloat ascent = 0.0;
        CGFloat descent = 0.0;
        CGFloat leading = 0.0;
        double typo_w = CTLineGetTypographicBounds(line, &ascent, &descent, &leading);

        int pad_x = std::max(2, static_cast<int>(std::ceil(2.0f * dpi)));
        int pad_y = std::max(2, static_cast<int>(std::ceil(2.0f * dpi)));
        int bmp_w = std::max(4, static_cast<int>(std::ceil(typo_w)) + pad_x * 2);
        int bmp_h = std::max(4, static_cast<int>(std::ceil(ascent + descent)) + pad_y * 2);

        out.tex_w_px = bmp_w;
        out.tex_h_px = bmp_h;
        out.pad_x_px = pad_x;
        out.advance_logical = static_cast<float>(typo_w) / dpi;
        out.draw_w_logical = static_cast<float>(bmp_w) / dpi;
        out.draw_h_logical = static_cast<float>(bmp_h) / dpi;
        out.y_offset_logical = -(out.draw_h_logical - static_cast<float>(key.font_size)) * 0.45f;

        if (upload_gpu) {
            std::vector<uint8_t> pixels(static_cast<std::size_t>(bmp_w * bmp_h * 4), 0);
            CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
            CGContextRef ctx = CGBitmapContextCreate(
                pixels.data(),
                static_cast<std::size_t>(bmp_w),
                static_cast<std::size_t>(bmp_h),
                8,
                static_cast<std::size_t>(bmp_w * 4),
                cs,
                static_cast<uint32_t>(kCGImageAlphaPremultipliedLast) | static_cast<uint32_t>(kCGBitmapByteOrder32Big));

            if (ctx) {
                CGContextSetAllowsAntialiasing(ctx, true);
                CGContextSetShouldAntialias(ctx, true);
                CGContextSetAllowsFontSmoothing(ctx, true);
                CGContextSetShouldSmoothFonts(ctx, true);
                CGContextSetRGBFillColor(ctx, 1.0, 1.0, 1.0, 1.0);
                CGContextSetTextPosition(ctx, static_cast<CGFloat>(pad_x), static_cast<CGFloat>(pad_y) + descent);
                CTLineDraw(line, ctx);
                CGContextRelease(ctx);

                // Un-premultiply RGB to pure white (255, 255, 255, alpha) for clean vertex color tinting
                for (std::size_t i = 0; i < pixels.size(); i += 4) {
                    uint8_t a = pixels[i + 3];
                    if (a > 0) {
                        pixels[i + 0] = 255;
                        pixels[i + 1] = 255;
                        pixels[i + 2] = 255;
                    }
                }

                Image img{};
                img.data = pixels.data();
                img.width = bmp_w;
                img.height = bmp_h;
                img.mipmaps = 1;
                img.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;

                out.tex = ::LoadTextureFromImage(img);
                if (out.tex.id != 0) {
                    ::SetTextureFilter(out.tex, TEXTURE_FILTER_BILINEAR);
                    out.has_texture = true;
                }
            }
            if (cs) CGColorSpaceRelease(cs);
        }

        CFRelease(line);
        CFRelease(attr_str);
        CFRelease(attrs);
        CFRelease(cf_str);
        CFRelease(ct_font);
    }
};

#elif defined(__linux__)

class LinuxFontCache {
public:
    static auto instance() noexcept -> LinuxFontCache& {
        static LinuxFontCache inst;
        return inst;
    }

    auto get_font(int font_size) -> Font* {
        if (!::IsWindowReady()) return nullptr;
        int clamped = std::max(8, font_size);
        auto it = fonts_.find(clamped);
        if (it != fonts_.end()) return &it->second;

        static const char* kCandidates[] = {
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/TTF/DejaVuSans.ttf",
            "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
            "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf"
        };
        for (const char* path : kCandidates) {
            if (::FileExists(path)) {
                Font f = ::LoadFontEx(path, clamped * 2, nullptr, 0);
                if (f.texture.id != 0) {
                    ::SetTextureFilter(f.texture, TEXTURE_FILTER_BILINEAR);
                    auto [ins, _] = fonts_.emplace(clamped, f);
                    return &ins->second;
                }
            }
        }
        return nullptr;
    }

    void clear_gpu_resources() noexcept {
        for (auto& [sz, f] : fonts_) {
            if (f.texture.id != 0) ::UnloadFont(f);
        }
        fonts_.clear();
    }

private:
    std::unordered_map<int, Font> fonts_;
};

#endif

} // namespace cfire::desktop_rl

inline void __cfire_SetConfigFlags(unsigned int flags) {
    cfire::desktop_rl::user_config_flags() |= flags;
    ::SetConfigFlags(cfire::desktop_rl::user_config_flags());
}

inline void __cfire_InitWindow(int width, int height, const char* title) {
    unsigned int flags = cfire::desktop_rl::user_config_flags() | FLAG_WINDOW_HIGHDPI | FLAG_MSAA_4X_HINT;
    ::SetConfigFlags(flags);
    ::InitWindow(width <= 0 ? 393 : width, height <= 0 ? 852 : height, title);
#if defined(__linux__)
    const char* icon_candidates[] = {
#ifdef CFIRE_APP_ICON_PATH
        CFIRE_APP_ICON_PATH,
#endif
        "icon.png",
        "assets/icon.png"
    };
    for (const char* ic : icon_candidates) {
        if (ic && *ic && ::FileExists(ic)) {
            Image img = ::LoadImage(ic);
            if (img.data != nullptr) {
                ::SetWindowIcon(img);
                ::UnloadImage(img);
                break;
            }
        }
    }
#endif
}

inline void __cfire_CloseWindow(void) {
#if defined(__APPLE__) && !defined(PLATFORM_IOS)
    cfire::desktop_rl::CoreTextCache::instance().clear_gpu_resources();
#elif defined(__linux__)
    cfire::desktop_rl::LinuxFontCache::instance().clear_gpu_resources();
#endif
    ::CloseWindow();
}

inline int __cfire_MeasureText(const char* text, int fontSize) {
    if (!text || !*text) return 0;
#if defined(__APPLE__) && !defined(PLATFORM_IOS)
    std::string_view sv(text);
    float max_w = 0.0f;
    std::size_t start = 0;
    while (start <= sv.size()) {
        std::size_t nl = sv.find('\n', start);
        std::string_view line = (nl == std::string_view::npos) ? sv.substr(start) : sv.substr(start, nl - start);
        if (!line.empty()) {
            const auto& entry = cfire::desktop_rl::CoreTextCache::instance().get_or_create(line, fontSize, false);
            max_w = std::max(max_w, entry.advance_logical);
        }
        if (nl == std::string_view::npos) break;
        start = nl + 1;
    }
    return static_cast<int>(std::ceil(max_w));
#elif defined(__linux__)
    if (Font* f = cfire::desktop_rl::LinuxFontCache::instance().get_font(fontSize)) {
        Vector2 sz = ::MeasureTextEx(*f, text, static_cast<float>(fontSize), 1.0f);
        return static_cast<int>(std::ceil(sz.x));
    }
    return ::MeasureText(text, fontSize);
#else
    return ::MeasureText(text, fontSize);
#endif
}

inline void __cfire_DrawText(const char* text, int posX, int posY, int fontSize, Color color) {
    if (!text || !*text || !::IsWindowReady()) return;
#if defined(__APPLE__) && !defined(PLATFORM_IOS)
    float dpi = 1.0f;
    Vector2 sc = ::GetWindowScaleDPI();
    if (sc.x > 0.5f) dpi = sc.x;

    std::string_view sv(text);
    float cur_y = static_cast<float>(posY);
    std::size_t start = 0;
    while (start <= sv.size()) {
        std::size_t nl = sv.find('\n', start);
        std::string_view line = (nl == std::string_view::npos) ? sv.substr(start) : sv.substr(start, nl - start);
        if (!line.empty()) {
            const auto& entry = cfire::desktop_rl::CoreTextCache::instance().get_or_create(line, fontSize, true);
            if (entry.has_texture && entry.tex.id != 0) {
                float pad_x_logical = static_cast<float>(entry.pad_x_px) / dpi;
                Rectangle src{0.0f, 0.0f, static_cast<float>(entry.tex_w_px), static_cast<float>(entry.tex_h_px)};
                Rectangle dst{
                    static_cast<float>(posX) - pad_x_logical,
                    cur_y + entry.y_offset_logical,
                    entry.draw_w_logical,
                    entry.draw_h_logical
                };
                ::DrawTexturePro(entry.tex, src, dst, Vector2{0.0f, 0.0f}, 0.0f, color);
            }
        }
        if (nl == std::string_view::npos) break;
        cur_y += static_cast<float>(fontSize) + 4.0f;
        start = nl + 1;
    }
#elif defined(__linux__)
    if (Font* f = cfire::desktop_rl::LinuxFontCache::instance().get_font(fontSize)) {
        ::DrawTextEx(*f, text, Vector2{static_cast<float>(posX), static_cast<float>(posY)}, static_cast<float>(fontSize), 1.0f, color);
        return;
    }
    ::DrawText(text, posX, posY, fontSize, color);
#else
    ::DrawText(text, posX, posY, fontSize, color);
#endif
}

#define SetConfigFlags __cfire_SetConfigFlags
#define InitWindow __cfire_InitWindow
#define CloseWindow __cfire_CloseWindow
#define MeasureText __cfire_MeasureText
#define DrawText __cfire_DrawText

inline Rectangle GetSafeAreaRect(void) {
    return Rectangle{0.0f, 0.0f, static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight())};
}
inline void RequestKeyboard(void) {}
inline void TriggerHaptic(int style) {
#if defined(__EMSCRIPTEN__) || defined(PLATFORM_WEB)
    int ms = (style <= 0) ? 10 : (style == 1 ? 20 : 35);
    EM_ASM({
        if (typeof navigator !== 'undefined' && navigator.vibrate) {
            navigator.vibrate($0);
        }
    }, ms);
#else
    (void)style;
#endif
}
