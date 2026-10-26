#pragma once
/*******************************************************************************************
*
*   raygui v4.0 - A simple and easy-to-use immediate-mode gui library for raylib (C++23 / C99)
*
*   Provided as part of the cfire vendor runtime (`import "vendor:raygui" as rg`).
*   Includes safe C++23 `std::string*` overloads for `GuiTextBox` and `GuiTextInputBox`.
*
********************************************************************************************/

#ifndef RAYGUI_H
#define RAYGUI_H

#define RAYGUI_VERSION_MAJOR 4
#define RAYGUI_VERSION_MINOR 0
#define RAYGUI_VERSION_PATCH 0
#define RAYGUI_VERSION  "4.0"

#if defined(PLATFORM_IOS) || defined(CFIRE_RAYLIB_FALLBACK)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#include "../core/ios_raylib.hpp"
#pragma clang diagnostic pop
#else
#include <raylib.h>
#endif
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <string>
#include <vector>

#ifndef RAYGUIAPI
    #define RAYGUIAPI inline
#endif

//----------------------------------------------------------------------------------
// Types and Structures Definition
//----------------------------------------------------------------------------------

// Gui control state
enum GuiState {
    STATE_NORMAL = 0,
    STATE_FOCUSED,
    STATE_PRESSED,
    STATE_DISABLED
};

// Gui control text alignment
enum GuiTextAlignment {
    TEXT_ALIGN_LEFT = 0,
    TEXT_ALIGN_CENTER,
    TEXT_ALIGN_RIGHT
};

// Gui control text alignment vertical
enum GuiTextAlignmentVertical {
    TEXT_ALIGN_TOP = 0,
    TEXT_ALIGN_MIDDLE,
    TEXT_ALIGN_BOTTOM
};

// Gui control text wrap mode
enum GuiTextWrapMode {
    TEXT_WRAP_NONE = 0,
    TEXT_WRAP_CHAR,
    TEXT_WRAP_WORD
};

// Gui controls
enum GuiControl {
    DEFAULT = 0,
    LABEL,
    BUTTON,
    TOGGLE,
    SLIDER,
    PROGRESSBAR,
    CHECKBOX,
    COMBOBOX,
    DROPDOWNBOX,
    TEXTBOX,
    VALUEBOX,
    SPINNER,
    LISTVIEW,
    COLORPICKER,
    SCROLLBAR,
    STATUSBAR
};

// Gui base properties for every control
enum GuiControlProperty {
    BORDER_COLOR_NORMAL = 0,
    BASE_COLOR_NORMAL,
    TEXT_COLOR_NORMAL,
    BORDER_COLOR_FOCUSED,
    BASE_COLOR_FOCUSED,
    TEXT_COLOR_FOCUSED,
    BORDER_COLOR_PRESSED,
    BASE_COLOR_PRESSED,
    TEXT_COLOR_PRESSED,
    BORDER_COLOR_DISABLED,
    BASE_COLOR_DISABLED,
    TEXT_COLOR_DISABLED,
    BORDER_WIDTH,
    TEXT_PADDING,
    TEXT_ALIGNMENT
};

// DEFAULT extended properties
enum GuiDefaultProperty {
    TEXT_SIZE = 16,
    TEXT_SPACING,
    LINE_COLOR,
    BACKGROUND_COLOR,
    TEXT_LINE_SPACING,
    TEXT_ALIGNMENT_VERTICAL,
    TEXT_WRAP_MODE
};

enum GuiScrollBarSide {
    SCROLLBAR_LEFT_SIDE = 0,
    SCROLLBAR_RIGHT_SIDE
};

//----------------------------------------------------------------------------------
// Internal State Management (Header-Only Inline Singleton)
//----------------------------------------------------------------------------------
namespace cfire::raygui_detail {

struct Context {
    int state = STATE_NORMAL;
    bool locked = false;
    float alpha = 1.0f;
    bool tooltip_enabled = false;
    std::string tooltip{};
    Font font{};
    bool custom_font = false;
    int style[16][24]{};
    bool style_initialized = false;

    void ensure_default_style() noexcept {
        if (style_initialized) return;
        style_initialized = true;
        for (int c = 0; c < 16; ++c) {
            style[c][BORDER_COLOR_NORMAL]   = 0x838383ff;
            style[c][BASE_COLOR_NORMAL]     = 0xc9c9c9ff;
            style[c][TEXT_COLOR_NORMAL]     = 0x686868ff;
            style[c][BORDER_COLOR_FOCUSED]  = 0x5bb2d9ff;
            style[c][BASE_COLOR_FOCUSED]    = 0xc9effeff;
            style[c][TEXT_COLOR_FOCUSED]    = 0x6c9bbcff;
            style[c][BORDER_COLOR_PRESSED]  = 0x0492c7ff;
            style[c][BASE_COLOR_PRESSED]    = 0x97e8ffff;
            style[c][TEXT_COLOR_PRESSED]    = 0x368bafff;
            style[c][BORDER_COLOR_DISABLED] = 0xb5c1c2ff;
            style[c][BASE_COLOR_DISABLED]   = 0xe6e9e9ff;
            style[c][TEXT_COLOR_DISABLED]   = 0xaeb7b8ff;
            style[c][BORDER_WIDTH]          = 1;
            style[c][TEXT_PADDING]          = 4;
            style[c][TEXT_ALIGNMENT]        = TEXT_ALIGN_CENTER;
        }
        style[DEFAULT][TEXT_SIZE]         = 14;
        style[DEFAULT][TEXT_SPACING]      = 1;
        style[DEFAULT][LINE_COLOR]        = 0x90abb5ff;
        style[DEFAULT][BACKGROUND_COLOR]  = 0xf5f5f5ff;
        style[DEFAULT][TEXT_LINE_SPACING] = 15;
    }
};

inline auto ctx() noexcept -> Context& {
    static Context instance{};
    instance.ensure_default_style();
    return instance;
}

inline auto rgba_to_color(int hex_val, float alpha) noexcept -> Color {
    unsigned int u = static_cast<unsigned int>(hex_val);
    unsigned char r = static_cast<unsigned char>((u >> 24) & 0xFF);
    unsigned char g = static_cast<unsigned char>((u >> 16) & 0xFF);
    unsigned char b = static_cast<unsigned char>((u >> 8) & 0xFF);
    unsigned char a = static_cast<unsigned char>(static_cast<float>(u & 0xFF) * std::clamp(alpha, 0.0f, 1.0f));
    return Color{r, g, b, a};
}

inline auto strip_icon_prefix(const char* text, int& icon_id) noexcept -> const char* {
    icon_id = -1;
    if (!text || text[0] != '#') return text;
    int id = 0;
    int i = 1;
    while (text[i] >= '0' && text[i] <= '9' && i <= 4) {
        id = id * 10 + (text[i] - '0');
        ++i;
    }
    if (i > 1 && text[i] == '#') {
        icon_id = id;
        const char* rest = text + i + 1;
        while (*rest == ' ') ++rest;
        return rest;
    }
    return text;
}

inline auto measure_text_width(const char* text) noexcept -> int {
    if (!text || text[0] == '\0') return 0;
    auto& c = ctx();
    int icon_id = -1;
    const char* clean = strip_icon_prefix(text, icon_id);
    int icon_pad = (icon_id >= 0) ? 14 : 0;
    int font_size = c.style[DEFAULT][TEXT_SIZE] > 0 ? c.style[DEFAULT][TEXT_SIZE] : 14;
    if (IsWindowReady()) {
        if (c.custom_font && c.font.texture.id > 0) {
            float sp = static_cast<float>(c.style[DEFAULT][TEXT_SPACING] > 0 ? c.style[DEFAULT][TEXT_SPACING] : 1);
            return icon_pad + static_cast<int>(MeasureTextEx(c.font, clean, static_cast<float>(font_size), sp).x);
        }
        return icon_pad + MeasureText(clean, font_size);
    }
    return icon_pad + static_cast<int>(std::strlen(clean)) * (font_size / 2 + 1);
}

inline void draw_control_box(Rectangle bounds, int control, int state) noexcept {
    if (!IsWindowReady()) return;
    auto& c = ctx();
    int base_prop = BASE_COLOR_NORMAL;
    int border_prop = BORDER_COLOR_NORMAL;
    if (state == STATE_FOCUSED) {
        base_prop = BASE_COLOR_FOCUSED;
        border_prop = BORDER_COLOR_FOCUSED;
    } else if (state == STATE_PRESSED) {
        base_prop = BASE_COLOR_PRESSED;
        border_prop = BORDER_COLOR_PRESSED;
    } else if (state == STATE_DISABLED) {
        base_prop = BASE_COLOR_DISABLED;
        border_prop = BORDER_COLOR_DISABLED;
    }
    DrawRectangleRec(bounds, rgba_to_color(c.style[control][base_prop], c.alpha));
    int bw = c.style[control][BORDER_WIDTH] > 0 ? c.style[control][BORDER_WIDTH] : 1;
    DrawRectangleLinesEx(bounds, static_cast<float>(bw), rgba_to_color(c.style[control][border_prop], c.alpha));
}

inline void draw_control_text(Rectangle bounds, const char* text, int control, int state) noexcept {
    if (!IsWindowReady() || !text || text[0] == '\0') return;
    auto& c = ctx();
    int text_prop = TEXT_COLOR_NORMAL;
    if (state == STATE_FOCUSED) text_prop = TEXT_COLOR_FOCUSED;
    else if (state == STATE_PRESSED) text_prop = TEXT_COLOR_PRESSED;
    else if (state == STATE_DISABLED) text_prop = TEXT_COLOR_DISABLED;

    int icon_id = -1;
    const char* clean = strip_icon_prefix(text, icon_id);
    int font_size = c.style[DEFAULT][TEXT_SIZE] > 0 ? c.style[DEFAULT][TEXT_SIZE] : 14;
    int tw = measure_text_width(text);
    int align = c.style[control][TEXT_ALIGNMENT];
    int pad = c.style[control][TEXT_PADDING];
    int tx = static_cast<int>(bounds.x) + pad;
    if (align == TEXT_ALIGN_CENTER) {
        tx = static_cast<int>(bounds.x + (bounds.width - static_cast<float>(tw)) * 0.5f);
    } else if (align == TEXT_ALIGN_RIGHT) {
        tx = static_cast<int>(bounds.x + bounds.width - static_cast<float>(tw) - static_cast<float>(pad));
    }
    int ty = static_cast<int>(bounds.y + (bounds.height - static_cast<float>(font_size)) * 0.5f);
    Color col = rgba_to_color(c.style[control][text_prop], c.alpha);
    if (icon_id >= 0) {
        int iy = static_cast<int>(bounds.y + (bounds.height - 6.0f) * 0.5f);
        DrawRectangle(tx, iy, 6, 6, col);
        tx += 12;
    }
    if (c.custom_font && c.font.texture.id > 0) {
        float sp = static_cast<float>(c.style[DEFAULT][TEXT_SPACING] > 0 ? c.style[DEFAULT][TEXT_SPACING] : 1);
        DrawTextEx(c.font, clean, Vector2{static_cast<float>(tx), static_cast<float>(ty)}, static_cast<float>(font_size), sp, col);
    } else {
        DrawText(clean, tx, ty, font_size, col);
    }
}

inline auto eval_hover_state(Rectangle bounds, bool& clicked) noexcept -> int {
    clicked = false;
    auto& c = ctx();
    int st = c.state;
    if (st != STATE_DISABLED && !c.locked && IsWindowReady()) {
        Vector2 mouse = GetMousePosition();
        if (CheckCollisionPointRec(mouse, bounds)) {
            if (IsMouseButtonDown(MOUSE_LEFT_BUTTON)) {
                st = STATE_PRESSED;
            } else {
                st = STATE_FOCUSED;
            }
            if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
                clicked = true;
            }
        }
    }
    return st;
}

} // namespace cfire::raygui_detail

//----------------------------------------------------------------------------------
// Global Gui State & Style Functions
//----------------------------------------------------------------------------------
RAYGUIAPI void GuiEnable(void) { cfire::raygui_detail::ctx().state = STATE_NORMAL; }
RAYGUIAPI void GuiDisable(void) { cfire::raygui_detail::ctx().state = STATE_DISABLED; }
RAYGUIAPI void GuiLock(void) { cfire::raygui_detail::ctx().locked = true; }
RAYGUIAPI void GuiUnlock(void) { cfire::raygui_detail::ctx().locked = false; }
RAYGUIAPI bool GuiIsLocked(void) { return cfire::raygui_detail::ctx().locked; }
RAYGUIAPI void GuiSetAlpha(float alpha) { cfire::raygui_detail::ctx().alpha = std::clamp(alpha, 0.0f, 1.0f); }
RAYGUIAPI void GuiSetState(int state) { cfire::raygui_detail::ctx().state = state; }
RAYGUIAPI int GuiGetState(void) { return cfire::raygui_detail::ctx().state; }

RAYGUIAPI void GuiSetFont(Font font) {
    auto& c = cfire::raygui_detail::ctx();
    c.font = font;
    c.custom_font = true;
}
RAYGUIAPI Font GuiGetFont(void) {
    auto& c = cfire::raygui_detail::ctx();
    if (c.custom_font) return c.font;
    if (IsWindowReady()) return GetFontDefault();
    return c.font;
}

RAYGUIAPI void GuiSetStyle(int control, int property, int value) {
    if (control >= 0 && control < 16 && property >= 0 && property < 24) {
        cfire::raygui_detail::ctx().style[control][property] = value;
    }
}
RAYGUIAPI int GuiGetStyle(int control, int property) {
    if (control >= 0 && control < 16 && property >= 0 && property < 24) {
        return cfire::raygui_detail::ctx().style[control][property];
    }
    return 0;
}

RAYGUIAPI void GuiLoadStyleDefault(void) {
    auto& c = cfire::raygui_detail::ctx();
    c.style_initialized = false;
    c.ensure_default_style();
}
RAYGUIAPI void GuiLoadStyle(const char* fileName) {
    (void)fileName;
    GuiLoadStyleDefault();
}

RAYGUIAPI void GuiEnableTooltip(void) { cfire::raygui_detail::ctx().tooltip_enabled = true; }
RAYGUIAPI void GuiDisableTooltip(void) { cfire::raygui_detail::ctx().tooltip_enabled = false; }
RAYGUIAPI void GuiSetTooltip(const char* tooltip) {
    cfire::raygui_detail::ctx().tooltip = tooltip ? tooltip : "";
}

RAYGUIAPI const char* GuiIconText(int iconId, const char* text) {
    static char buffer[512];
    if (text) {
        std::snprintf(buffer, sizeof(buffer), "#%03d#%s", iconId & 0xFF, text);
    } else {
        std::snprintf(buffer, sizeof(buffer), "#%03d#", iconId & 0xFF);
    }
    return buffer;
}

//----------------------------------------------------------------------------------
// Container / Separator Controls
//----------------------------------------------------------------------------------
RAYGUIAPI int GuiWindowBox(Rectangle bounds, const char* title) {
    constexpr float kStatusBarHeight = 24.0f;
    cfire::raygui_detail::draw_control_box(bounds, DEFAULT, cfire::raygui_detail::ctx().state);
    Rectangle header{bounds.x, bounds.y, bounds.width, kStatusBarHeight};
    cfire::raygui_detail::draw_control_box(header, STATUSBAR, cfire::raygui_detail::ctx().state);
    cfire::raygui_detail::draw_control_text(header, title, STATUSBAR, cfire::raygui_detail::ctx().state);
    Rectangle close_btn{bounds.x + bounds.width - 22.0f, bounds.y + 3.0f, 18.0f, 18.0f};
    bool close_clicked = false;
    int bst = cfire::raygui_detail::eval_hover_state(close_btn, close_clicked);
    cfire::raygui_detail::draw_control_box(close_btn, BUTTON, bst);
    cfire::raygui_detail::draw_control_text(close_btn, "x", BUTTON, bst);
    return close_clicked ? 1 : 0;
}

RAYGUIAPI int GuiGroupBox(Rectangle bounds, const char* text) {
    if (IsWindowReady()) {
        auto& c = cfire::raygui_detail::ctx();
        DrawRectangleLinesEx(bounds, 1.0f, cfire::raygui_detail::rgba_to_color(c.style[DEFAULT][LINE_COLOR], c.alpha));
        if (text && text[0] != '\0') {
            Rectangle label_bounds{bounds.x + 12.0f, bounds.y - 6.0f, static_cast<float>(cfire::raygui_detail::measure_text_width(text) + 8), 12.0f};
            cfire::raygui_detail::draw_control_text(label_bounds, text, LABEL, c.state);
        }
    }
    return 0;
}

RAYGUIAPI int GuiLine(Rectangle bounds, const char* text) {
    if (IsWindowReady()) {
        auto& c = cfire::raygui_detail::ctx();
        float y = bounds.y + bounds.height * 0.5f;
        DrawRectangleRec(Rectangle{bounds.x, y, bounds.width, 1.0f}, cfire::raygui_detail::rgba_to_color(c.style[DEFAULT][LINE_COLOR], c.alpha));
        if (text && text[0] != '\0') {
            cfire::raygui_detail::draw_control_text(bounds, text, LABEL, c.state);
        }
    }
    return 0;
}

RAYGUIAPI int GuiPanel(Rectangle bounds, const char* text) {
    cfire::raygui_detail::draw_control_box(bounds, DEFAULT, cfire::raygui_detail::ctx().state);
    if (text && text[0] != '\0') {
        Rectangle header{bounds.x, bounds.y, bounds.width, 24.0f};
        cfire::raygui_detail::draw_control_box(header, STATUSBAR, cfire::raygui_detail::ctx().state);
        cfire::raygui_detail::draw_control_text(header, text, STATUSBAR, cfire::raygui_detail::ctx().state);
    }
    return 0;
}

RAYGUIAPI int GuiScrollPanel(Rectangle bounds, const char* text, Rectangle content, Vector2* scroll, Rectangle* view) {
    (void)text;
    cfire::raygui_detail::draw_control_box(bounds, DEFAULT, cfire::raygui_detail::ctx().state);
    if (scroll) {
        float max_x = std::max(0.0f, content.width - bounds.width);
        float max_y = std::max(0.0f, content.height - bounds.height);
        scroll->x = std::clamp(scroll->x, -max_x, 0.0f);
        scroll->y = std::clamp(scroll->y, -max_y, 0.0f);
    }
    if (view) {
        *view = bounds;
    }
    return 0;
}

//----------------------------------------------------------------------------------
// Basic & Interactive Controls
//----------------------------------------------------------------------------------
RAYGUIAPI int GuiLabel(Rectangle bounds, const char* text) {
    cfire::raygui_detail::draw_control_text(bounds, text, LABEL, cfire::raygui_detail::ctx().state);
    return 0;
}

RAYGUIAPI int GuiButton(Rectangle bounds, const char* text) {
    bool clicked = false;
    int st = cfire::raygui_detail::eval_hover_state(bounds, clicked);
    cfire::raygui_detail::draw_control_box(bounds, BUTTON, st);
    cfire::raygui_detail::draw_control_text(bounds, text, BUTTON, st);
    return clicked ? 1 : 0;
}

RAYGUIAPI int GuiLabelButton(Rectangle bounds, const char* text) {
    bool clicked = false;
    int st = cfire::raygui_detail::eval_hover_state(bounds, clicked);
    cfire::raygui_detail::draw_control_text(bounds, text, LABEL, st);
    return clicked ? 1 : 0;
}

RAYGUIAPI int GuiToggle(Rectangle bounds, const char* text, bool* active) {
    bool clicked = false;
    int st = cfire::raygui_detail::eval_hover_state(bounds, clicked);
    if (clicked && active) *active = !(*active);
    int draw_st = (active && *active) ? STATE_PRESSED : st;
    cfire::raygui_detail::draw_control_box(bounds, TOGGLE, draw_st);
    cfire::raygui_detail::draw_control_text(bounds, text, TOGGLE, draw_st);
    return clicked ? 1 : 0;
}

RAYGUIAPI int GuiToggleGroup(Rectangle bounds, const char* text, int* active) {
    if (!text) return 0;
    std::string s(text);
    int idx = 0;
    std::size_t start = 0;
    float x = bounds.x;
    while (start <= s.size()) {
        auto end = s.find(';', start);
        std::string item = (end == std::string::npos) ? s.substr(start) : s.substr(start, end - start);
        Rectangle item_bounds{x, bounds.y, bounds.width, bounds.height};
        bool is_act = (active && *active == idx);
        if (GuiToggle(item_bounds, item.c_str(), &is_act) && active) {
            *active = idx;
        }
        x += bounds.width + 2.0f;
        ++idx;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return 0;
}

RAYGUIAPI int GuiToggleSlider(Rectangle bounds, const char* text, int* active) {
    bool clicked = false;
    int st = cfire::raygui_detail::eval_hover_state(bounds, clicked);
    if (clicked && active) *active = (*active == 0) ? 1 : 0;
    cfire::raygui_detail::draw_control_box(bounds, SLIDER, st);
    cfire::raygui_detail::draw_control_text(bounds, text, SLIDER, st);
    return clicked ? 1 : 0;
}

RAYGUIAPI int GuiCheckBox(Rectangle bounds, const char* text, bool* checked) {
    bool clicked = false;
    int st = cfire::raygui_detail::eval_hover_state(bounds, clicked);
    if (clicked && checked) *checked = !(*checked);
    cfire::raygui_detail::draw_control_box(bounds, CHECKBOX, st);
    if (checked && *checked && IsWindowReady()) {
        Rectangle inner{bounds.x + 4.0f, bounds.y + 4.0f, std::max(0.0f, bounds.width - 8.0f), std::max(0.0f, bounds.height - 8.0f)};
        auto& c = cfire::raygui_detail::ctx();
        DrawRectangleRec(inner, cfire::raygui_detail::rgba_to_color(c.style[CHECKBOX][TEXT_COLOR_PRESSED], c.alpha));
    }
    if (text && text[0] != '\0') {
        Rectangle label_bounds{bounds.x + bounds.width + 6.0f, bounds.y, static_cast<float>(cfire::raygui_detail::measure_text_width(text) + 8), bounds.height};
        cfire::raygui_detail::draw_control_text(label_bounds, text, LABEL, st);
    }
    return clicked ? 1 : 0;
}

RAYGUIAPI int GuiComboBox(Rectangle bounds, const char* text, int* active) {
    if (!text) return 0;
    std::vector<std::string> items;
    std::string s(text);
    std::size_t start = 0;
    while (start <= s.size()) {
        auto end = s.find(';', start);
        items.push_back((end == std::string::npos) ? s.substr(start) : s.substr(start, end - start));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    if (items.empty()) items.emplace_back("");
    int cur_idx = (active) ? std::clamp(*active, 0, static_cast<int>(items.size()) - 1) : 0;
    bool clicked = false;
    int st = cfire::raygui_detail::eval_hover_state(bounds, clicked);
    if (clicked && active && !items.empty()) {
        cur_idx = (cur_idx + 1) % static_cast<int>(items.size());
        *active = cur_idx;
    }
    cfire::raygui_detail::draw_control_box(bounds, COMBOBOX, st);
    cfire::raygui_detail::draw_control_text(bounds, items[static_cast<std::size_t>(cur_idx)].c_str(), COMBOBOX, st);
    return clicked ? 1 : 0;
}

RAYGUIAPI int GuiDropdownBox(Rectangle bounds, const char* text, int* active, bool editMode) {
    (void)editMode;
    return GuiComboBox(bounds, text, active);
}

RAYGUIAPI int GuiSpinner(Rectangle bounds, const char* text, int* value, int minValue, int maxValue, bool editMode) {
    (void)editMode;
    if (value) {
        *value = std::clamp(*value, minValue, maxValue);
    }
    if (text && text[0] != '\0') {
        Rectangle lb{bounds.x - static_cast<float>(cfire::raygui_detail::measure_text_width(text) + 8), bounds.y, static_cast<float>(cfire::raygui_detail::measure_text_width(text) + 4), bounds.height};
        cfire::raygui_detail::draw_control_text(lb, text, LABEL, cfire::raygui_detail::ctx().state);
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%d", value ? *value : 0);
    cfire::raygui_detail::draw_control_box(bounds, SPINNER, cfire::raygui_detail::ctx().state);
    cfire::raygui_detail::draw_control_text(bounds, buf, SPINNER, cfire::raygui_detail::ctx().state);
    return 0;
}

RAYGUIAPI int GuiValueBox(Rectangle bounds, const char* text, int* value, int minValue, int maxValue, bool editMode) {
    return GuiSpinner(bounds, text, value, minValue, maxValue, editMode);
}

RAYGUIAPI int GuiTextBox(Rectangle bounds, char* text, int textSize, bool editMode) {
    bool clicked = false;
    int st = cfire::raygui_detail::eval_hover_state(bounds, clicked);
    int result = clicked ? 1 : 0;
    if (editMode) {
#if defined(PLATFORM_IOS)
        cfire::ios_rl::request_keyboard_active();
#endif
        st = STATE_PRESSED;
        if (IsWindowReady() && text && textSize > 1) {
            int len = static_cast<int>(std::strlen(text));
            int key = GetCharPressed();
            while (key > 0) {
                if (key >= 32 && key <= 125 && len + 1 < textSize) {
                    text[len++] = static_cast<char>(key);
                    text[len] = '\0';
                }
                key = GetCharPressed();
            }
            if (IsKeyPressed(KEY_BACKSPACE) && len > 0) {
                text[--len] = '\0';
            }
            if (IsKeyPressed(KEY_ENTER)) {
                result = 1;
            }
        }
    }
    cfire::raygui_detail::draw_control_box(bounds, TEXTBOX, st);
    cfire::raygui_detail::draw_control_text(bounds, text ? text : "", TEXTBOX, st);
    return result;
}

// Safe C++23 overload for `cfire` `mut str` (`&mut my_str` -> `std::string*`)
RAYGUIAPI int GuiTextBox(Rectangle bounds, std::string* text, int textSize, bool editMode) {
    int cap = std::max(textSize, 2);
    std::vector<char> buf(static_cast<std::size_t>(cap), '\0');
    if (text && !text->empty()) {
        std::size_t copy_n = std::min(text->size(), static_cast<std::size_t>(cap - 1));
        std::memcpy(buf.data(), text->data(), copy_n);
        buf[copy_n] = '\0';
    }
    int res = GuiTextBox(bounds, buf.data(), cap, editMode);
    if (text) {
        *text = buf.data();
    }
    return res;
}

RAYGUIAPI int GuiSliderBar(Rectangle bounds, const char* textLeft, const char* textRight, float* value, float minValue, float maxValue) {
    bool clicked = false;
    int st = cfire::raygui_detail::eval_hover_state(bounds, clicked);
    int changed = 0;
    if (value) {
        if (st == STATE_PRESSED && IsWindowReady() && bounds.width > 0.0f) {
            Vector2 mouse = GetMousePosition();
            float norm = std::clamp((mouse.x - bounds.x) / bounds.width, 0.0f, 1.0f);
            *value = minValue + norm * (maxValue - minValue);
            changed = 1;
        }
        *value = std::clamp(*value, std::min(minValue, maxValue), std::max(minValue, maxValue));
    }
    cfire::raygui_detail::draw_control_box(bounds, SLIDER, st);
    if (value && IsWindowReady() && maxValue > minValue) {
        float ratio = std::clamp((*value - minValue) / (maxValue - minValue), 0.0f, 1.0f);
        Rectangle fill{bounds.x + 2.0f, bounds.y + 2.0f, std::max(0.0f, (bounds.width - 4.0f) * ratio), std::max(0.0f, bounds.height - 4.0f)};
        auto& c = cfire::raygui_detail::ctx();
        DrawRectangleRec(fill, cfire::raygui_detail::rgba_to_color(c.style[SLIDER][TEXT_COLOR_PRESSED], c.alpha));
    }
    if (textLeft && textLeft[0] != '\0') {
        int tw = cfire::raygui_detail::measure_text_width(textLeft);
        Rectangle lb{bounds.x - static_cast<float>(tw + 8), bounds.y, static_cast<float>(tw + 4), bounds.height};
        cfire::raygui_detail::draw_control_text(lb, textLeft, LABEL, st);
    }
    if (textRight && textRight[0] != '\0') {
        int tw = cfire::raygui_detail::measure_text_width(textRight);
        Rectangle rb{bounds.x + bounds.width + 4.0f, bounds.y, static_cast<float>(tw + 4), bounds.height};
        cfire::raygui_detail::draw_control_text(rb, textRight, LABEL, st);
    }
    return changed;
}

RAYGUIAPI int GuiSlider(Rectangle bounds, const char* textLeft, const char* textRight, float* value, float minValue, float maxValue) {
    return GuiSliderBar(bounds, textLeft, textRight, value, minValue, maxValue);
}

RAYGUIAPI int GuiProgressBar(Rectangle bounds, const char* textLeft, const char* textRight, float* value, float minValue, float maxValue) {
    if (value) {
        *value = std::clamp(*value, std::min(minValue, maxValue), std::max(minValue, maxValue));
    }
    cfire::raygui_detail::draw_control_box(bounds, PROGRESSBAR, cfire::raygui_detail::ctx().state);
    if (value && IsWindowReady() && maxValue > minValue) {
        float ratio = std::clamp((*value - minValue) / (maxValue - minValue), 0.0f, 1.0f);
        Rectangle fill{bounds.x + 2.0f, bounds.y + 2.0f, std::max(0.0f, (bounds.width - 4.0f) * ratio), std::max(0.0f, bounds.height - 4.0f)};
        auto& c = cfire::raygui_detail::ctx();
        DrawRectangleRec(fill, cfire::raygui_detail::rgba_to_color(c.style[PROGRESSBAR][TEXT_COLOR_PRESSED], c.alpha));
    }
    (void)textLeft;
    (void)textRight;
    return 0;
}

RAYGUIAPI int GuiStatusBar(Rectangle bounds, const char* text) {
    cfire::raygui_detail::draw_control_box(bounds, STATUSBAR, cfire::raygui_detail::ctx().state);
    cfire::raygui_detail::draw_control_text(bounds, text, STATUSBAR, cfire::raygui_detail::ctx().state);
    return 0;
}

RAYGUIAPI int GuiDummyRec(Rectangle bounds, const char* text) {
    cfire::raygui_detail::draw_control_box(bounds, DEFAULT, cfire::raygui_detail::ctx().state);
    cfire::raygui_detail::draw_control_text(bounds, text, DEFAULT, cfire::raygui_detail::ctx().state);
    return 0;
}

RAYGUIAPI int GuiGrid(Rectangle bounds, const char* text, float spacing, int subdivs, Vector2* mouseCell) {
    (void)text;
    (void)subdivs;
    if (mouseCell) {
        *mouseCell = Vector2{-1.0f, -1.0f};
        if (IsWindowReady() && spacing > 0.0f) {
            Vector2 m = GetMousePosition();
            if (CheckCollisionPointRec(m, bounds)) {
                mouseCell->x = static_cast<float>(static_cast<int>((m.x - bounds.x) / spacing));
                mouseCell->y = static_cast<float>(static_cast<int>((m.y - bounds.y) / spacing));
            }
        }
    }
    return 0;
}

//----------------------------------------------------------------------------------
// Advanced Controls
//----------------------------------------------------------------------------------
RAYGUIAPI int GuiListView(Rectangle bounds, const char* text, int* scrollIndex, int* active) {
    cfire::raygui_detail::draw_control_box(bounds, LISTVIEW, cfire::raygui_detail::ctx().state);
    if (scrollIndex && *scrollIndex < 0) *scrollIndex = 0;
    if (active && *active < -1) *active = -1;
    cfire::raygui_detail::draw_control_text(bounds, text, LISTVIEW, cfire::raygui_detail::ctx().state);
    return 0;
}

RAYGUIAPI int GuiMessageBox(Rectangle bounds, const char* title, const char* message, const char* buttons) {
    if (GuiWindowBox(bounds, title) != 0) return 0;
    Rectangle msg_bounds{bounds.x + 12.0f, bounds.y + 32.0f, bounds.width - 24.0f, bounds.height - 72.0f};
    cfire::raygui_detail::draw_control_text(msg_bounds, message, LABEL, cfire::raygui_detail::ctx().state);
    if (buttons && buttons[0] != '\0') {
        Rectangle btn_bounds{bounds.x + 12.0f, bounds.y + bounds.height - 32.0f, bounds.width - 24.0f, 24.0f};
        if (GuiButton(btn_bounds, buttons) != 0) return 1;
    }
    return -1;
}

RAYGUIAPI int GuiTextInputBox(Rectangle bounds, const char* title, const char* message, const char* buttons, char* text, int textMaxSize, bool* secretViewActive) {
    (void)secretViewActive;
    if (GuiWindowBox(bounds, title) != 0) return 0;
    Rectangle msg_bounds{bounds.x + 12.0f, bounds.y + 28.0f, bounds.width - 24.0f, 20.0f};
    cfire::raygui_detail::draw_control_text(msg_bounds, message, LABEL, cfire::raygui_detail::ctx().state);
    Rectangle tb_bounds{bounds.x + 12.0f, bounds.y + 52.0f, bounds.width - 24.0f, 24.0f};
    GuiTextBox(tb_bounds, text, textMaxSize, true);
    if (buttons && buttons[0] != '\0') {
        Rectangle btn_bounds{bounds.x + 12.0f, bounds.y + bounds.height - 32.0f, bounds.width - 24.0f, 24.0f};
        if (GuiButton(btn_bounds, buttons) != 0) return 1;
    }
    return -1;
}

RAYGUIAPI int GuiTextInputBox(Rectangle bounds, const char* title, const char* message, const char* buttons, std::string* text, int textMaxSize, bool* secretViewActive) {
    int cap = std::max(textMaxSize, 2);
    std::vector<char> buf(static_cast<std::size_t>(cap), '\0');
    if (text && !text->empty()) {
        std::size_t copy_n = std::min(text->size(), static_cast<std::size_t>(cap - 1));
        std::memcpy(buf.data(), text->data(), copy_n);
        buf[copy_n] = '\0';
    }
    int res = GuiTextInputBox(bounds, title, message, buttons, buf.data(), cap, secretViewActive);
    if (text) {
        *text = buf.data();
    }
    return res;
}

RAYGUIAPI int GuiColorPicker(Rectangle bounds, const char* text, Color* color) {
    (void)text;
    cfire::raygui_detail::draw_control_box(bounds, COLORPICKER, cfire::raygui_detail::ctx().state);
    if (color && IsWindowReady()) {
        Rectangle preview{bounds.x + 2.0f, bounds.y + 2.0f, std::max(0.0f, bounds.width - 4.0f), std::max(0.0f, bounds.height - 4.0f)};
        DrawRectangleRec(preview, *color);
    }
    return 0;
}

RAYGUIAPI int GuiColorPanel(Rectangle bounds, const char* text, Color* color) {
    return GuiColorPicker(bounds, text, color);
}

RAYGUIAPI int GuiColorBarAlpha(Rectangle bounds, const char* text, float* alpha) {
    return GuiSliderBar(bounds, "", text, alpha, 0.0f, 1.0f);
}

RAYGUIAPI int GuiColorBarHue(Rectangle bounds, const char* text, float* value) {
    return GuiSliderBar(bounds, "", text, value, 0.0f, 360.0f);
}

#endif // RAYGUI_H
