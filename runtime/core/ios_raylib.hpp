#pragma once
/*******************************************************************************************
*
*   cfire iOS Native Runtime Backend for `vendor:raylib` (`runtime/core/ios_raylib.hpp`)
*
*   Provides a zero-dependency Objective-C++ `UIKit` + `OpenGLES3` + `CoreAudio` (`AudioToolbox`)
*   implementation of Raylib 5.0 (`<raylib.h>`, `<raymath.h>`) for `--target ios-sim` and `--target ios`.
*
*   Features:
*     - Preserves standard blocking `for !rl.WindowShouldClose() { rl.BeginDrawing() ... rl.EndDrawing() }`
*       loop by launching `UIApplicationMain` and pumping `CFRunLoopRunInMode` per frame.
*     - Smart Adaptive Viewport: `InitWindow(0, 0, ...)` uses native iPhone logical points;
*       fixed sizes like `InitWindow(720, 480, ...)` scale & letterbox inside the iOS Safe Area
*       (avoiding Dynamic Island / Home Indicator) and remap touch coordinates automatically.
*     - Automatic `<UIKeyInput>` software keyboard pop-up when Raygui `GuiTextBox` enters `editMode`.
*     - Built-in 8x8 crisp bitmap font renderer (`DrawText`, `DrawTextEx`, `MeasureText`).
*     - Native CoreAudio (`AudioQueue`) streaming (`InitAudioDevice`, `LoadAudioStream`, `UpdateAudioStream`).
*
********************************************************************************************/

#ifndef RAYLIB_H
#define RAYLIB_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

#define RAYLIB_VERSION_MAJOR 5
#define RAYLIB_VERSION_MINOR 0
#define RAYLIB_VERSION_PATCH 0
#define RAYLIB_VERSION  "5.0"

#ifndef RLAPI
    #define RLAPI inline
#endif

#ifndef PI
    #define PI 3.14159265358979323846f
#endif
#ifndef DEG2RAD
    #define DEG2RAD (PI/180.0f)
#endif
#ifndef RAD2DEG
    #define RAD2DEG (180.0f/PI)
#endif

#if defined(__cplusplus)
    #define CLITERAL(type)      type
#else
    #define CLITERAL(type)      (type)
#endif

#define RL_COLOR_TYPE
#define RL_RECTANGLE_TYPE
#define RL_VECTOR2_TYPE
#define RL_VECTOR3_TYPE
#define RL_VECTOR4_TYPE
#define RL_QUATERNION_TYPE
#define RL_MATRIX_TYPE

// Basic Colors
#define LIGHTGRAY  CLITERAL(Color){ 200, 200, 200, 255 }
#define GRAY       CLITERAL(Color){ 130, 130, 130, 255 }
#define DARKGRAY   CLITERAL(Color){ 80, 80, 80, 255 }
#define YELLOW     CLITERAL(Color){ 253, 249, 0, 255 }
#define GOLD       CLITERAL(Color){ 255, 203, 0, 255 }
#define ORANGE     CLITERAL(Color){ 255, 161, 0, 255 }
#define PINK       CLITERAL(Color){ 255, 109, 194, 255 }
#define RED        CLITERAL(Color){ 230, 41, 55, 255 }
#define MAROON     CLITERAL(Color){ 190, 33, 55, 255 }
#define GREEN      CLITERAL(Color){ 0, 228, 48, 255 }
#define LIME       CLITERAL(Color){ 0, 158, 47, 255 }
#define DARKGREEN  CLITERAL(Color){ 0, 117, 44, 255 }
#define SKYBLUE    CLITERAL(Color){ 102, 191, 255, 255 }
#define BLUE       CLITERAL(Color){ 0, 121, 241, 255 }
#define DARKBLUE   CLITERAL(Color){ 0, 82, 172, 255 }
#define PURPLE     CLITERAL(Color){ 200, 122, 255, 255 }
#define VIOLET     CLITERAL(Color){ 135, 60, 190, 255 }
#define DARKPURPLE CLITERAL(Color){ 112, 31, 126, 255 }
#define BEIGE      CLITERAL(Color){ 211, 176, 131, 255 }
#define BROWN      CLITERAL(Color){ 127, 106, 79, 255 }
#define DARKBROWN  CLITERAL(Color){ 76, 63, 47, 255 }
#define WHITE      CLITERAL(Color){ 255, 255, 255, 255 }
#define BLACK      CLITERAL(Color){ 0, 0, 0, 255 }
#define BLANK      CLITERAL(Color){ 0, 0, 0, 0 }
#define MAGENTA    CLITERAL(Color){ 255, 0, 255, 255 }
#define RAYWHITE   CLITERAL(Color){ 245, 245, 245, 255 }

typedef struct Vector2 { float x; float y; } Vector2;
typedef struct Vector3 { float x; float y; float z; } Vector3;
typedef struct Vector4 { float x; float y; float z; float w; } Vector4;
typedef Vector4 Quaternion;
typedef struct Matrix {
    float m0, m4, m8, m12;
    float m1, m5, m9, m13;
    float m2, m6, m10, m14;
    float m3, m7, m11, m15;
} Matrix;
typedef struct Color { unsigned char r; unsigned char g; unsigned char b; unsigned char a; } Color;
typedef struct Rectangle { float x; float y; float width; float height; } Rectangle;
typedef struct Image { void *data; int width; int height; int mipmaps; int format; } Image;
typedef struct Texture { unsigned int id; int width; int height; int mipmaps; int format; } Texture;
typedef Texture Texture2D;
typedef Texture TextureCubemap;
typedef struct RenderTexture { unsigned int id; Texture texture; Texture depth; } RenderTexture;
typedef RenderTexture RenderTexture2D;
typedef struct NPatchInfo { Rectangle source; int left; int top; int right; int bottom; int layout; } NPatchInfo;
typedef struct GlyphInfo { int value; int offsetX; int offsetY; int advanceX; Image image; } GlyphInfo;
typedef struct Font { int baseSize; int glyphCount; int glyphPadding; Texture2D texture; Rectangle *recs; GlyphInfo *glyphs; } Font;
typedef struct Camera3D { Vector3 position; Vector3 target; Vector3 up; float fovy; int projection; } Camera3D;
typedef Camera3D Camera;
typedef struct Camera2D { Vector2 offset; Vector2 target; float rotation; float zoom; } Camera2D;
typedef struct Shader { unsigned int id; int *locs; } Shader;
typedef struct rAudioBuffer rAudioBuffer;
typedef struct rAudioProcessor rAudioProcessor;
typedef struct AudioStream { rAudioBuffer *buffer; rAudioProcessor *processor; unsigned int sampleRate; unsigned int sampleSize; unsigned int channels; } AudioStream;
typedef struct Sound { AudioStream stream; unsigned int frameCount; } Sound;
typedef struct Music { AudioStream stream; unsigned int frameCount; bool looping; int ctxType; void *ctxData; } Music;

typedef enum {
    FLAG_VSYNC_HINT         = 0x00000040,
    FLAG_FULLSCREEN_MODE    = 0x00000002,
    FLAG_WINDOW_RESIZABLE   = 0x00000004,
    FLAG_WINDOW_UNDECORATED = 0x00000008,
    FLAG_WINDOW_HIDDEN      = 0x00000080,
    FLAG_WINDOW_MINIMIZED   = 0x00000200,
    FLAG_WINDOW_MAXIMIZED   = 0x00000400,
    FLAG_WINDOW_UNFOCUSED   = 0x00000800,
    FLAG_WINDOW_TOPMOST     = 0x00001000,
    FLAG_WINDOW_ALWAYS_RUN  = 0x00000100,
    FLAG_WINDOW_TRANSPARENT = 0x00000010,
    FLAG_WINDOW_HIGHDPI     = 0x00002000,
    FLAG_MSAA_4X_HINT       = 0x00000020
} ConfigFlags;

typedef enum {
    KEY_NULL = 0,
    KEY_APOSTROPHE = 39, KEY_COMMA = 44, KEY_MINUS = 45, KEY_PERIOD = 46, KEY_SLASH = 47,
    KEY_ZERO = 48, KEY_ONE = 49, KEY_TWO = 50, KEY_THREE = 51, KEY_FOUR = 52,
    KEY_FIVE = 53, KEY_SIX = 54, KEY_SEVEN = 55, KEY_EIGHT = 56, KEY_NINE = 57,
    KEY_SEMICOLON = 59, KEY_EQUAL = 61,
    KEY_A = 65, KEY_B = 66, KEY_C = 67, KEY_D = 68, KEY_E = 69, KEY_F = 70, KEY_G = 71,
    KEY_H = 72, KEY_I = 73, KEY_J = 74, KEY_K = 75, KEY_L = 76, KEY_M = 77, KEY_N = 78,
    KEY_O = 79, KEY_P = 80, KEY_Q = 81, KEY_R = 82, KEY_S = 83, KEY_T = 84, KEY_U = 85,
    KEY_V = 86, KEY_W = 87, KEY_X = 88, KEY_Y = 89, KEY_Z = 90,
    KEY_SPACE = 32, KEY_ESCAPE = 256, KEY_ENTER = 257, KEY_TAB = 258, KEY_BACKSPACE = 259,
    KEY_INSERT = 260, KEY_DELETE = 261, KEY_RIGHT = 262, KEY_LEFT = 263, KEY_DOWN = 264, KEY_UP = 265
} KeyboardKey;

#define MOUSE_LEFT_BUTTON   MOUSE_BUTTON_LEFT
#define MOUSE_RIGHT_BUTTON  MOUSE_BUTTON_RIGHT
#define MOUSE_MIDDLE_BUTTON MOUSE_BUTTON_MIDDLE

typedef enum {
    MOUSE_BUTTON_LEFT    = 0,
    MOUSE_BUTTON_RIGHT   = 1,
    MOUSE_BUTTON_MIDDLE  = 2
} MouseButton;

typedef enum {
    BLEND_ALPHA = 0,
    BLEND_ADDITIVE,
    BLEND_MULTIPLIED,
    BLEND_ADD_COLORS,
    BLEND_SUBTRACT_COLORS,
    BLEND_ALPHA_PREMULTIPLY
} BlendMode;

//----------------------------------------------------------------------------------
// Objective-C++ UIKit + OpenGLES3 + CoreAudio Implementation
//----------------------------------------------------------------------------------
#if defined(__OBJC__)
#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#import <OpenGLES/ES3/gl.h>
#import <OpenGLES/ES3/glext.h>
#import <AudioToolbox/AudioToolbox.h>
#endif

namespace cfire::ios_rl {

struct Vertex {
    float x, y;
    float u, v;
    uint8_t r, g, b, a;
};

struct TouchPoint {
    int id = 0;
    Vector2 pos{0.0f, 0.0f};
    bool active = false;
};

struct State {
    bool window_ready = false;
    bool should_close = false;
    int virtual_width = 800;
    int virtual_height = 450;
    int screen_points_w = 393;
    int screen_points_h = 852;
    int fb_pixels_w = 1179;
    int fb_pixels_h = 2556;
    float dpi_scale = 3.0f;
    float safe_top = 0.0f;
    float safe_bottom = 0.0f;
    float safe_left = 0.0f;
    float safe_right = 0.0f;

    // Viewport letterbox transform (screen points -> virtual canvas)
    float vp_x = 0.0f;
    float vp_y = 0.0f;
    float vp_w = 800.0f;
    float vp_h = 450.0f;

    int target_fps = 60;
    double frame_time = 1.0 / 60.0;
    double total_time = 0.0;
    std::chrono::steady_clock::time_point start_tp = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point last_frame_tp = std::chrono::steady_clock::now();

    // Mouse / Touch input state
    Vector2 mouse_pos{0.0f, 0.0f};
    Vector2 prev_mouse_pos{0.0f, 0.0f};
    bool mouse_down[3]{false, false, false};
    bool mouse_prev[3]{false, false, false};
    bool mouse_pressed_latch[3]{false, false, false};
    bool mouse_released_latch[3]{false, false, false};
    TouchPoint touches[10]{};
    int touch_count = 0;

    // Keyboard & Text Input Queue
    bool key_down[512]{};
    bool key_pressed[512]{};
    std::vector<int> char_queue{};
    bool keyboard_visible = false;
    bool keyboard_requested_this_frame = false;

    // Camera2D state
    bool in_mode_2d = false;
    Camera2D cam2d{{0.0f, 0.0f}, {0.0f, 0.0f}, 0.0f, 1.0f};

    // OpenGL ES 3.0 batch state
    unsigned int prog = 0;
    unsigned int vao = 0;
    unsigned int vbo = 0;
    unsigned int white_tex = 0;
    unsigned int font_tex = 0;
    unsigned int cur_tex = 0;
    int u_mvp_loc = -1;
    std::vector<Vertex> batch{};
    Font default_font{};

#if defined(__OBJC__)
    UIWindow* window = nil;
    UIView* gl_view = nil;
    EAGLContext* eagl_ctx = nil;
    GLuint fbo = 0;
    GLuint color_rbo = 0;
#endif

    void update_viewport_mapping() noexcept {
        float avail_x = safe_left;
        float avail_y = safe_top;
        float avail_w = std::max(1.0f, static_cast<float>(screen_points_w) - safe_left - safe_right);
        float avail_h = std::max(1.0f, static_cast<float>(screen_points_h) - safe_top - safe_bottom);

        float scale_x = avail_w / static_cast<float>(std::max(1, virtual_width));
        float scale_y = avail_h / static_cast<float>(std::max(1, virtual_height));
        float scale = std::min(scale_x, scale_y);

        vp_w = static_cast<float>(virtual_width) * scale;
        vp_h = static_cast<float>(virtual_height) * scale;
        vp_x = avail_x + (avail_w - vp_w) * 0.5f;
        vp_y = avail_y + (avail_h - vp_h) * 0.5f;
    }

    auto map_point_to_virtual(float px, float py) const noexcept -> Vector2 {
        if (vp_w <= 0.0f || vp_h <= 0.0f) return Vector2{px, py};
        float vx = (px - vp_x) * (static_cast<float>(virtual_width) / vp_w);
        float vy = (py - vp_y) * (static_cast<float>(virtual_height) / vp_h);
        vx = std::clamp(vx, 0.0f, static_cast<float>(virtual_width));
        vy = std::clamp(vy, 0.0f, static_cast<float>(virtual_height));
        return Vector2{vx, vy};
    }

    auto transform_2d(float x, float y) const noexcept -> Vector2 {
        if (!in_mode_2d) return Vector2{x, y};
        float dx = (x - cam2d.target.x) * (cam2d.zoom > 0.0f ? cam2d.zoom : 1.0f);
        float dy = (y - cam2d.target.y) * (cam2d.zoom > 0.0f ? cam2d.zoom : 1.0f);
        if (cam2d.rotation != 0.0f) {
            float rad = cam2d.rotation * DEG2RAD;
            float cs = std::cos(rad);
            float sn = std::sin(rad);
            float rx = dx * cs - dy * sn;
            float ry = dx * sn + dy * cs;
            dx = rx;
            dy = ry;
        }
        return Vector2{dx + cam2d.offset.x, dy + cam2d.offset.y};
    }
};

inline auto state() noexcept -> State& {
    static State s{};
    return s;
}

// Built-in 8x8 ASCII Bitmap Font (Chars 32..126)
inline auto glyph_row_bits(int ch, int row) noexcept -> uint8_t {
    static const uint8_t font8x8[95][8] = {
        {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // ' '
        {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00}, // '!'
        {0x36,0x36,0x00,0x00,0x00,0x00,0x00,0x00}, // '"'
        {0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00}, // '#'
        {0x0C,0x3E,0x03,0x1E,0x30,0x1F,0x0C,0x00}, // '$'
        {0x00,0x63,0x33,0x18,0x0C,0x66,0x63,0x00}, // '%'
        {0x1C,0x36,0x1C,0x6E,0x3B,0x33,0x6E,0x00}, // '&'
        {0x06,0x06,0x03,0x00,0x00,0x00,0x00,0x00}, // '''
        {0x18,0x0C,0x06,0x06,0x06,0x0C,0x18,0x00}, // '('
        {0x06,0x0C,0x18,0x18,0x18,0x0C,0x06,0x00}, // ')'
        {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00}, // '*'
        {0x00,0x0C,0x0C,0x3F,0x0C,0x0C,0x00,0x00}, // '+'
        {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x06}, // ','
        {0x00,0x00,0x00,0x3F,0x00,0x00,0x00,0x00}, // '-'
        {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x00}, // '.'
        {0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0x00}, // '/'
        {0x3E,0x63,0x73,0x7B,0x6F,0x67,0x3E,0x00}, // '0'
        {0x0C,0x0E,0x0C,0x0C,0x0C,0x0C,0x3F,0x00}, // '1'
        {0x1E,0x33,0x30,0x1C,0x06,0x33,0x3F,0x00}, // '2'
        {0x1E,0x33,0x30,0x1C,0x30,0x33,0x1E,0x00}, // '3'
        {0x38,0x3C,0x36,0x33,0x7F,0x30,0x78,0x00}, // '4'
        {0x3F,0x03,0x1F,0x30,0x30,0x33,0x1E,0x00}, // '5'
        {0x1C,0x06,0x03,0x1F,0x33,0x33,0x1E,0x00}, // '6'
        {0x3F,0x33,0x30,0x18,0x0C,0x0C,0x0C,0x00}, // '7'
        {0x1E,0x33,0x33,0x1E,0x33,0x33,0x1E,0x00}, // '8'
        {0x1E,0x33,0x33,0x3E,0x30,0x18,0x0E,0x00}, // '9'
        {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x00}, // ':'
        {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x06}, // ';'
        {0x18,0x0C,0x06,0x03,0x06,0x0C,0x18,0x00}, // '<'
        {0x00,0x00,0x3F,0x00,0x00,0x3F,0x00,0x00}, // '='
        {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00}, // '>'
        {0x1E,0x33,0x30,0x18,0x0C,0x00,0x0C,0x00}, // '?'
        {0x3E,0x63,0x7B,0x7B,0x7B,0x03,0x1E,0x00}, // '@'
        {0x0C,0x1E,0x33,0x33,0x3F,0x33,0x33,0x00}, // 'A'
        {0x3F,0x66,0x66,0x3E,0x66,0x66,0x3F,0x00}, // 'B'
        {0x3C,0x66,0x03,0x03,0x03,0x66,0x3C,0x00}, // 'C'
        {0x1F,0x36,0x66,0x66,0x66,0x36,0x1F,0x00}, // 'D'
        {0x7F,0x46,0x16,0x1E,0x16,0x46,0x7F,0x00}, // 'E'
        {0x7F,0x46,0x16,0x1E,0x16,0x06,0x0F,0x00}, // 'F'
        {0x3C,0x66,0x03,0x03,0x73,0x66,0x7C,0x00}, // 'G'
        {0x33,0x33,0x33,0x3F,0x33,0x33,0x33,0x00}, // 'H'
        {0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, // 'I'
        {0x78,0x30,0x30,0x30,0x33,0x33,0x1E,0x00}, // 'J'
        {0x67,0x66,0x36,0x1E,0x36,0x66,0x67,0x00}, // 'K'
        {0x0F,0x06,0x06,0x06,0x46,0x66,0x7F,0x00}, // 'L'
        {0x63,0x77,0x7F,0x7F,0x6B,0x63,0x63,0x00}, // 'M'
        {0x63,0x67,0x6F,0x7B,0x73,0x63,0x63,0x00}, // 'N'
        {0x1C,0x36,0x63,0x63,0x63,0x36,0x1C,0x00}, // 'O'
        {0x3F,0x66,0x66,0x3E,0x06,0x06,0x0F,0x00}, // 'P'
        {0x1E,0x33,0x33,0x33,0x3B,0x1E,0x38,0x00}, // 'Q'
        {0x3F,0x66,0x66,0x3E,0x36,0x66,0x67,0x00}, // 'R'
        {0x1E,0x33,0x07,0x0E,0x38,0x33,0x1E,0x00}, // 'S'
        {0x3F,0x2D,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, // 'T'
        {0x33,0x33,0x33,0x33,0x33,0x33,0x3F,0x00}, // 'U'
        {0x33,0x33,0x33,0x33,0x33,0x1E,0x0C,0x00}, // 'V'
        {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00}, // 'W'
        {0x63,0x63,0x36,0x1C,0x1C,0x36,0x63,0x00}, // 'X'
        {0x33,0x33,0x33,0x1E,0x0C,0x0C,0x1E,0x00}, // 'Y'
        {0x7F,0x63,0x31,0x18,0x4C,0x66,0x7F,0x00}, // 'Z'
        {0x1E,0x06,0x06,0x06,0x06,0x06,0x1E,0x00}, // '['
        {0x03,0x06,0x0C,0x18,0x30,0x60,0x40,0x00}, // '\'
        {0x1E,0x18,0x18,0x18,0x18,0x18,0x1E,0x00}, // ']'
        {0x08,0x1C,0x36,0x63,0x00,0x00,0x00,0x00}, // '^'
        {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF}, // '_'
        {0x0C,0x0C,0x18,0x00,0x00,0x00,0x00,0x00}, // '`'
        {0x00,0x00,0x1E,0x30,0x3E,0x33,0x6E,0x00}, // 'a'
        {0x07,0x06,0x06,0x3E,0x66,0x66,0x3B,0x00}, // 'b'
        {0x00,0x00,0x1E,0x33,0x03,0x33,0x1E,0x00}, // 'c'
        {0x38,0x30,0x30,0x3e,0x33,0x33,0x6E,0x00}, // 'd'
        {0x00,0x00,0x1E,0x33,0x3f,0x03,0x1E,0x00}, // 'e'
        {0x1C,0x36,0x06,0x0f,0x06,0x06,0x0F,0x00}, // 'f'
        {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x1F}, // 'g'
        {0x07,0x06,0x36,0x6E,0x66,0x66,0x67,0x00}, // 'h'
        {0x0C,0x00,0x0E,0x0C,0x0C,0x0C,0x1E,0x00}, // 'i'
        {0x30,0x00,0x30,0x30,0x30,0x33,0x33,0x1E}, // 'j'
        {0x07,0x06,0x66,0x36,0x1E,0x36,0x67,0x00}, // 'k'
        {0x0E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, // 'l'
        {0x00,0x00,0x33,0x7F,0x7F,0x6B,0x63,0x00}, // 'm'
        {0x00,0x00,0x1F,0x33,0x33,0x33,0x33,0x00}, // 'n'
        {0x00,0x00,0x1E,0x33,0x33,0x33,0x1E,0x00}, // 'o'
        {0x00,0x00,0x3B,0x66,0x66,0x3E,0x06,0x0F}, // 'p'
        {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x78}, // 'q'
        {0x00,0x00,0x3B,0x6E,0x66,0x06,0x0F,0x00}, // 'r'
        {0x00,0x00,0x3E,0x03,0x1E,0x30,0x1F,0x00}, // 's'
        {0x08,0x0C,0x3E,0x0C,0x0C,0x2C,0x18,0x00}, // 't'
        {0x00,0x00,0x33,0x33,0x33,0x33,0x6E,0x00}, // 'u'
        {0x00,0x00,0x33,0x33,0x33,0x1E,0x0C,0x00}, // 'v'
        {0x00,0x00,0x63,0x6B,0x7F,0x7F,0x36,0x00}, // 'w'
        {0x00,0x00,0x63,0x36,0x1C,0x36,0x63,0x00}, // 'x'
        {0x00,0x00,0x33,0x33,0x33,0x3E,0x30,0x1F}, // 'y'
        {0x00,0x00,0x3F,0x19,0x0C,0x26,0x3F,0x00}, // 'z'
        {0x38,0x0C,0x0C,0x07,0x0C,0x0C,0x38,0x00}, // '{'
        {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00}, // '|'
        {0x07,0x0C,0x0C,0x38,0x0C,0x0C,0x07,0x00}, // '}'
        {0x6E,0x3B,0x00,0x00,0x00,0x00,0x00,0x00}  // '~'
    };
    if (ch < 32 || ch > 126 || row < 0 || row >= 8) return 0;
    return font8x8[ch - 32][row];
}

inline void flush_batch() noexcept {
#if defined(__OBJC__)
    auto& s = state();
    if (s.batch.empty() || s.prog == 0) return;

    glUseProgram(s.prog);
    float l = 0.0f;
    float r = static_cast<float>(std::max(1, s.virtual_width));
    float t = 0.0f;
    float b = static_cast<float>(std::max(1, s.virtual_height));
    float mvp[16] = {
        2.0f / (r - l), 0.0f,           0.0f, 0.0f,
        0.0f,           2.0f / (t - b), 0.0f, 0.0f,
        0.0f,           0.0f,          -1.0f, 0.0f,
       -(r + l)/(r - l), -(t + b)/(t - b), 0.0f, 1.0f
    };
    glUniformMatrix4fv(s.u_mvp_loc, 1, GL_FALSE, mvp);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s.cur_tex ? s.cur_tex : s.white_tex);

    glBindVertexArray(s.vao);
    glBindBuffer(GL_ARRAY_BUFFER, s.vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(s.batch.size() * sizeof(Vertex)), s.batch.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(s.batch.size()));
    s.batch.clear();
#endif
}

inline void set_batch_tex(unsigned int tex_id) noexcept {
    auto& s = state();
    if (s.cur_tex != tex_id) {
        flush_batch();
        s.cur_tex = tex_id;
    }
}

inline void push_quad(
    float x0, float y0, float u0, float v0,
    float x1, float y1, float u1, float v1,
    float x2, float y2, float u2, float v2,
    float x3, float y3, float u3, float v3,
    Color col
) noexcept {
    auto& s = state();
    Vector2 p0 = s.transform_2d(x0, y0);
    Vector2 p1 = s.transform_2d(x1, y1);
    Vector2 p2 = s.transform_2d(x2, y2);
    Vector2 p3 = s.transform_2d(x3, y3);

    s.batch.push_back(Vertex{p0.x, p0.y, u0, v0, col.r, col.g, col.b, col.a});
    s.batch.push_back(Vertex{p1.x, p1.y, u1, v1, col.r, col.g, col.b, col.a});
    s.batch.push_back(Vertex{p2.x, p2.y, u2, v2, col.r, col.g, col.b, col.a});

    s.batch.push_back(Vertex{p0.x, p0.y, u0, v0, col.r, col.g, col.b, col.a});
    s.batch.push_back(Vertex{p2.x, p2.y, u2, v2, col.r, col.g, col.b, col.a});
    s.batch.push_back(Vertex{p3.x, p3.y, u3, v3, col.r, col.g, col.b, col.a});
}

inline void request_keyboard_active() noexcept {
    state().keyboard_requested_this_frame = true;
}

} // namespace cfire::ios_rl

#if defined(__OBJC__)

@interface CfireEAGLView : UIView <UIKeyInput>
@end

@implementation CfireEAGLView

+ (Class)layerClass {
    return [CAEAGLLayer class];
}

- (BOOL)canBecomeFirstResponder {
    return YES;
}

- (BOOL)hasText {
    return YES;
}

- (void)insertText:(NSString *)text {
    auto& s = cfire::ios_rl::state();
    for (NSUInteger i = 0; i < [text length]; ++i) {
        unichar ch = [text characterAtIndex:i];
        if (ch == '\n' || ch == '\r') {
            s.key_pressed[KEY_ENTER] = true;
            s.key_down[KEY_ENTER] = true;
            [self resignFirstResponder];
            s.keyboard_visible = false;
        } else if (ch >= 32 && ch <= 126) {
            s.char_queue.push_back(static_cast<int>(ch));
        }
    }
}

- (void)deleteBackward {
    auto& s = cfire::ios_rl::state();
    s.key_pressed[KEY_BACKSPACE] = true;
    s.key_down[KEY_BACKSPACE] = true;
}

- (void)updateTouches:(NSSet<UITouch *> *)touches isDown:(BOOL)down isEnd:(BOOL)ended {
    auto& s = cfire::ios_rl::state();
    UITouch *first = [touches anyObject];
    if (first) {
        CGPoint pt = [first locationInView:self];
        s.mouse_pos = s.map_point_to_virtual(static_cast<float>(pt.x), static_cast<float>(pt.y));
        if (down) {
            s.mouse_down[MOUSE_BUTTON_LEFT] = true;
            s.mouse_pressed_latch[MOUSE_BUTTON_LEFT] = true;
        }
        if (ended) {
            s.mouse_down[MOUSE_BUTTON_LEFT] = false;
            s.mouse_released_latch[MOUSE_BUTTON_LEFT] = true;
        }
    }
    int idx = 0;
    for (UITouch *t in touches) {
        if (idx >= 10) break;
        if (t.phase != UITouchPhaseEnded && t.phase != UITouchPhaseCancelled) {
            CGPoint p = [t locationInView:self];
            s.touches[idx].id = idx;
            s.touches[idx].pos = s.map_point_to_virtual(static_cast<float>(p.x), static_cast<float>(p.y));
            s.touches[idx].active = true;
            ++idx;
        }
    }
    s.touch_count = idx;
}

- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    (void)event;
    [self updateTouches:touches isDown:YES isEnd:NO];
}

- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    (void)event;
    [self updateTouches:touches isDown:YES isEnd:NO];
}

- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    (void)event;
    [self updateTouches:touches isDown:NO isEnd:YES];
}

- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    (void)event;
    [self updateTouches:touches isDown:NO isEnd:YES];
}

@end

@interface CfireViewController : UIViewController
@end

@implementation CfireViewController
- (BOOL)prefersStatusBarHidden {
    return NO;
}
- (UIInterfaceOrientationMask)supportedInterfaceOrientations {
    return UIInterfaceOrientationMaskAll;
}
- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    auto& s = cfire::ios_rl::state();
    CGRect bounds = self.view.bounds;
    s.screen_points_w = static_cast<int>(bounds.size.width);
    s.screen_points_h = static_cast<int>(bounds.size.height);
    UIEdgeInsets insets = self.view.safeAreaInsets;
    s.safe_top = static_cast<float>(insets.top);
    s.safe_bottom = static_cast<float>(insets.bottom);
    s.safe_left = static_cast<float>(insets.left);
    s.safe_right = static_cast<float>(insets.right);
    s.update_viewport_mapping();
    if (s.eagl_ctx && s.color_rbo && s.gl_view) {
        [EAGLContext setCurrentContext:s.eagl_ctx];
        glBindRenderbuffer(GL_RENDERBUFFER, s.color_rbo);
        [s.eagl_ctx renderbufferStorage:GL_RENDERBUFFER fromDrawable:(CAEAGLLayer *)s.gl_view.layer];
        GLint rw = 0, rh = 0;
        glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &rw);
        glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &rh);
        if (rw > 0 && rh > 0) {
            s.fb_pixels_w = rw;
            s.fb_pixels_h = rh;
        }
    }
}
@end

namespace cfire::ios_rl {
using UserMainFn = int(*)(int, char**);
inline UserMainFn g_user_main = nullptr;
inline int g_argc = 0;
inline char** g_argv = nullptr;
} // namespace cfire::ios_rl

@interface CfireAppDelegate : UIResponder <UIApplicationDelegate>
@property (strong, nonatomic) UIWindow *window;
@end

@implementation CfireAppDelegate

- (void)runCfireMain {
    int rc = 0;
    if (cfire::ios_rl::g_user_main) {
        rc = cfire::ios_rl::g_user_main(cfire::ios_rl::g_argc, cfire::ios_rl::g_argv);
    }
    std::fflush(stdout);
    std::fflush(stderr);
    // If this was a non-GUI CLI/test binary (InitWindow was never called), exit cleanly with return code
    if (!cfire::ios_rl::state().window_ready) {
        std::_Exit(rc);
    }
}

- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
    (void)application;
    (void)launchOptions;
    [self performSelector:@selector(runCfireMain) withObject:nil afterDelay:0.0];
    return YES;
}

@end

namespace cfire::ios_rl {
inline auto run_ios_app(int argc, char** argv, UserMainFn user_main) -> int {
    g_argc = argc;
    g_argv = argv;
    g_user_main = user_main;
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([CfireAppDelegate class]));
    }
}
} // namespace cfire::ios_rl

#endif // __OBJC__

namespace cfire::ios_rl {

inline void init_gles_resources() noexcept {
#if defined(__OBJC__)
    auto& s = state();
    const char* vs_src =
        "#version 300 es\n"
        "precision highp float;\n"
        "layout(location = 0) in vec2 aPos;\n"
        "layout(location = 1) in vec2 aUV;\n"
        "layout(location = 2) in vec4 aColor;\n"
        "uniform mat4 uMVP;\n"
        "out vec2 vUV;\n"
        "out vec4 vColor;\n"
        "void main() {\n"
        "    vUV = aUV;\n"
        "    vColor = aColor;\n"
        "    gl_Position = uMVP * vec4(aPos.x, aPos.y, 0.0, 1.0);\n"
        "}\n";

    const char* fs_src =
        "#version 300 es\n"
        "precision mediump float;\n"
        "in vec2 vUV;\n"
        "in vec4 vColor;\n"
        "uniform sampler2D uTex;\n"
        "out vec4 fragColor;\n"
        "void main() {\n"
        "    fragColor = texture(uTex, vUV) * vColor;\n"
        "}\n";

    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vs_src, nullptr);
    glCompileShader(vs);

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fs_src, nullptr);
    glCompileShader(fs);

    s.prog = glCreateProgram();
    glAttachShader(s.prog, vs);
    glAttachShader(s.prog, fs);
    glLinkProgram(s.prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    s.u_mvp_loc = glGetUniformLocation(s.prog, "uMVP");

    glGenVertexArrays(1, &s.vao);
    glGenBuffers(1, &s.vbo);
    glBindVertexArray(s.vao);
    glBindBuffer(GL_ARRAY_BUFFER, s.vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(2 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), reinterpret_cast<void*>(4 * sizeof(float)));

    // 1x1 white pixel texture
    uint8_t white_px[4] = {255, 255, 255, 255};
    glGenTextures(1, &s.white_tex);
    glBindTexture(GL_TEXTURE_2D, s.white_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white_px);

    // 128x64 font atlas for ASCII 32..126 (16 cols x 6 rows of 8x8 glyphs)
    std::vector<uint8_t> atlas(128 * 64 * 4, 0);
    for (int ch = 32; ch <= 126; ++ch) {
        int idx = ch - 32;
        int gx = (idx % 16) * 8;
        int gy = (idx / 16) * 8;
        for (int r = 0; r < 8; ++r) {
            uint8_t bits = glyph_row_bits(ch, r);
            for (int c = 0; c < 8; ++c) {
                if ((bits >> c) & 1) {
                    int px_idx = ((gy + r) * 128 + (gx + c)) * 4;
                    atlas[static_cast<std::size_t>(px_idx + 0)] = 255;
                    atlas[static_cast<std::size_t>(px_idx + 1)] = 255;
                    atlas[static_cast<std::size_t>(px_idx + 2)] = 255;
                    atlas[static_cast<std::size_t>(px_idx + 3)] = 255;
                }
            }
        }
    }
    glGenTextures(1, &s.font_tex);
    glBindTexture(GL_TEXTURE_2D, s.font_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 128, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, atlas.data());

    s.default_font.baseSize = 8;
    s.default_font.texture = Texture2D{s.font_tex, 128, 64, 1, 7};
    s.cur_tex = s.white_tex;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
#endif
}

} // namespace cfire::ios_rl

//----------------------------------------------------------------------------------
// Raylib Window & Frame Lifecycle Functions
//----------------------------------------------------------------------------------

RLAPI void InitWindow(int width, int height, const char *title) {
    (void)title;
    auto& s = cfire::ios_rl::state();
    s.start_tp = std::chrono::steady_clock::now();
    s.last_frame_tp = s.start_tp;

#if defined(__OBJC__)
    // Pump UIApplication initialization if not yet created
    if (UIApplication.sharedApplication == nil) {
        // Initialize UIApplication singleton directly without losing control of main()
        [UIApplication sharedApplication];
    }

    UIScreen *mainScreen = [UIScreen mainScreen];
    CGRect screenBounds = mainScreen.bounds;
    s.dpi_scale = static_cast<float>(mainScreen.scale);
    s.screen_points_w = static_cast<int>(screenBounds.size.width);
    s.screen_points_h = static_cast<int>(screenBounds.size.height);

    if (width <= 0 || height <= 0) {
        s.virtual_width = s.screen_points_w;
        s.virtual_height = s.screen_points_h;
    } else {
        s.virtual_width = width;
        s.virtual_height = height;
    }

    s.window = [[UIWindow alloc] initWithFrame:screenBounds];
    CfireViewController *vc = [[CfireViewController alloc] init];
    CfireEAGLView *glView = [[CfireEAGLView alloc] initWithFrame:screenBounds];
    glView.contentScaleFactor = mainScreen.scale;
    glView.multipleTouchEnabled = YES;

    CAEAGLLayer *eaglLayer = (CAEAGLLayer *)glView.layer;
    eaglLayer.opaque = YES;
    eaglLayer.drawableProperties = @{
        kEAGLDrawablePropertyRetainedBacking: @NO,
        kEAGLDrawablePropertyColorFormat: kEAGLColorFormatRGBA8
    };

    s.eagl_ctx = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES3];
    if (!s.eagl_ctx) {
        s.eagl_ctx = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES2];
    }
    [EAGLContext setCurrentContext:s.eagl_ctx];

    glGenFramebuffers(1, &s.fbo);
    glGenRenderbuffers(1, &s.color_rbo);
    glBindFramebuffer(GL_FRAMEBUFFER, s.fbo);
    glBindRenderbuffer(GL_RENDERBUFFER, s.color_rbo);
    [s.eagl_ctx renderbufferStorage:GL_RENDERBUFFER fromDrawable:eaglLayer];
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, s.color_rbo);

    GLint rw = 0, rh = 0;
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &rw);
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &rh);
    s.fb_pixels_w = rw > 0 ? rw : static_cast<int>(s.screen_points_w * s.dpi_scale);
    s.fb_pixels_h = rh > 0 ? rh : static_cast<int>(s.screen_points_h * s.dpi_scale);

    vc.view = glView;
    s.gl_view = glView;
    s.window.rootViewController = vc;
    id<UIApplicationDelegate> appDel = UIApplication.sharedApplication.delegate;
    if ([appDel respondsToSelector:@selector(setWindow:)]) {
        [appDel setWindow:s.window];
    }
    [s.window makeKeyAndVisible];

    // Pump runloop briefly so UIWindow and safeAreaInsets settle
    for (int i = 0; i < 5; ++i) {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.002, false);
    }
    [EAGLContext setCurrentContext:s.eagl_ctx];
    glBindFramebuffer(GL_FRAMEBUFFER, s.fbo);
    glBindRenderbuffer(GL_RENDERBUFFER, s.color_rbo);
    [s.eagl_ctx renderbufferStorage:GL_RENDERBUFFER fromDrawable:eaglLayer];
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, s.color_rbo);
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &rw);
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &rh);
    if (rw > 0 && rh > 0) {
        s.fb_pixels_w = rw;
        s.fb_pixels_h = rh;
    }
    UIEdgeInsets insets = glView.safeAreaInsets;
    s.safe_top = static_cast<float>(insets.top);
    s.safe_bottom = static_cast<float>(insets.bottom);
    s.safe_left = static_cast<float>(insets.left);
    s.safe_right = static_cast<float>(insets.right);
    s.update_viewport_mapping();

    cfire::ios_rl::init_gles_resources();
#else
    s.virtual_width = width > 0 ? width : 800;
    s.virtual_height = height > 0 ? height : 450;
#endif
    s.window_ready = true;
    s.should_close = false;
}

RLAPI bool IsWindowReady(void) { return cfire::ios_rl::state().window_ready; }
RLAPI bool WindowShouldClose(void) { return cfire::ios_rl::state().should_close; }
RLAPI void CloseWindow(void) { cfire::ios_rl::state().window_ready = false; }
RLAPI void SetConfigFlags(unsigned int flags) { (void)flags; }
RLAPI void SetWindowState(unsigned int flags) { (void)flags; }
RLAPI void ClearWindowState(unsigned int flags) { (void)flags; }
RLAPI void SetWindowTitle(const char *title) { (void)title; }
RLAPI void SetWindowMinSize(int width, int height) { (void)width; (void)height; }
RLAPI int GetScreenWidth(void) { return cfire::ios_rl::state().virtual_width; }
RLAPI int GetScreenHeight(void) { return cfire::ios_rl::state().virtual_height; }
RLAPI int GetRenderWidth(void) { return cfire::ios_rl::state().fb_pixels_w; }
RLAPI int GetRenderHeight(void) { return cfire::ios_rl::state().fb_pixels_h; }
RLAPI Vector2 GetWindowScaleDPI(void) {
    float sc = cfire::ios_rl::state().dpi_scale;
    return Vector2{sc, sc};
}

RLAPI void SetTargetFPS(int fps) {
    cfire::ios_rl::state().target_fps = fps > 0 ? fps : 60;
}
RLAPI int GetFPS(void) {
    double dt = cfire::ios_rl::state().frame_time;
    return dt > 0.0001 ? static_cast<int>(1.0 / dt + 0.5) : 60;
}
RLAPI float GetFrameTime(void) {
    return static_cast<float>(cfire::ios_rl::state().frame_time);
}
RLAPI double GetTime(void) {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(now - cfire::ios_rl::state().start_tp).count();
}

RLAPI void BeginDrawing(void) {
    auto& s = cfire::ios_rl::state();
    s.keyboard_requested_this_frame = false;
    s.batch.clear();
#if defined(__OBJC__)
    [EAGLContext setCurrentContext:s.eagl_ctx];
    glBindFramebuffer(GL_FRAMEBUFFER, s.fbo);

    // Clear full physical screen to dark letterbox background first
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, s.fb_pixels_w, s.fb_pixels_h);
    glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Set viewport & scissor to the Smart Adaptive Safe-Area viewport
    int vx = static_cast<int>(s.vp_x * s.dpi_scale);
    int vy = static_cast<int>((static_cast<float>(s.screen_points_h) - (s.vp_y + s.vp_h)) * s.dpi_scale);
    int vw = static_cast<int>(s.vp_w * s.dpi_scale);
    int vh = static_cast<int>(s.vp_h * s.dpi_scale);
    glViewport(vx, vy, std::max(1, vw), std::max(1, vh));
    glEnable(GL_SCISSOR_TEST);
    glScissor(vx, vy, std::max(1, vw), std::max(1, vh));
#endif
}

RLAPI void EndDrawing(void) {
    auto& s = cfire::ios_rl::state();
    cfire::ios_rl::flush_batch();

#if defined(__OBJC__)
    // Automatic Software Keyboard Toggle for Raygui GuiTextBox
    if (s.gl_view) {
        if (s.keyboard_requested_this_frame && !s.keyboard_visible) {
            [s.gl_view becomeFirstResponder];
            s.keyboard_visible = true;
        } else if (!s.keyboard_requested_this_frame && s.keyboard_visible) {
            [s.gl_view resignFirstResponder];
            s.keyboard_visible = false;
        }
    }

    glBindRenderbuffer(GL_RENDERBUFFER, s.color_rbo);
    [s.eagl_ctx presentRenderbuffer:GL_RENDERBUFFER];

    // Clear per-frame input latches before pumping next frame's UIKit events
    for (int i = 0; i < 3; ++i) {
        s.mouse_prev[i] = s.mouse_down[i];
        s.mouse_pressed_latch[i] = false;
        s.mouse_released_latch[i] = false;
    }
    for (int k = 0; k < 512; ++k) {
        s.key_pressed[k] = false;
    }
    s.prev_mouse_pos = s.mouse_pos;

    // Frame pacing + UIKit CFRunLoop event pump
    double target_dt = 1.0 / static_cast<double>(std::max(1, s.target_fps));
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - s.last_frame_tp).count();
    double wait_s = std::max(0.001, target_dt - elapsed);
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, wait_s, false);

    auto end_tp = std::chrono::steady_clock::now();
    s.frame_time = std::chrono::duration<double>(end_tp - s.last_frame_tp).count();
    s.last_frame_tp = end_tp;
#endif
}

RLAPI void ClearBackground(Color color) {
#if defined(__OBJC__)
    cfire::ios_rl::flush_batch();
    glClearColor(color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f);
    glClear(GL_COLOR_BUFFER_BIT);
#else
    (void)color;
#endif
}

RLAPI void BeginMode2D(Camera2D camera) {
    cfire::ios_rl::flush_batch();
    cfire::ios_rl::state().in_mode_2d = true;
    cfire::ios_rl::state().cam2d = camera;
}

RLAPI void EndMode2D(void) {
    cfire::ios_rl::flush_batch();
    cfire::ios_rl::state().in_mode_2d = false;
}

RLAPI void BeginBlendMode(int mode) {
#if defined(__OBJC__)
    cfire::ios_rl::flush_batch();
    if (mode == BLEND_ADDITIVE) {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    } else {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
#else
    (void)mode;
#endif
}

RLAPI void EndBlendMode(void) {
#if defined(__OBJC__)
    cfire::ios_rl::flush_batch();
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
#endif
}

//----------------------------------------------------------------------------------
// Input Handling (Keyboard, Mouse & Multi-Touch)
//----------------------------------------------------------------------------------

RLAPI bool IsKeyPressed(int key) {
    if (key < 0 || key >= 512) return false;
    return cfire::ios_rl::state().key_pressed[key];
}
RLAPI bool IsKeyDown(int key) {
    if (key < 0 || key >= 512) return false;
    return cfire::ios_rl::state().key_down[key];
}
RLAPI bool IsKeyReleased(int key) { (void)key; return false; }
RLAPI int GetKeyPressed(void) { return 0; }
RLAPI int GetCharPressed(void) {
    auto& q = cfire::ios_rl::state().char_queue;
    if (q.empty()) return 0;
    int c = q.front();
    q.erase(q.begin());
    return c;
}
RLAPI void SetExitKey(int key) { (void)key; }

RLAPI bool IsMouseButtonPressed(int button) {
    if (button < 0 || button >= 3) return false;
    return cfire::ios_rl::state().mouse_pressed_latch[button];
}
RLAPI bool IsMouseButtonDown(int button) {
    if (button < 0 || button >= 3) return false;
    return cfire::ios_rl::state().mouse_down[button];
}
RLAPI bool IsMouseButtonReleased(int button) {
    if (button < 0 || button >= 3) return false;
    return cfire::ios_rl::state().mouse_released_latch[button];
}
RLAPI bool IsMouseButtonUp(int button) {
    return !IsMouseButtonDown(button);
}
RLAPI int GetMouseX(void) { return static_cast<int>(cfire::ios_rl::state().mouse_pos.x); }
RLAPI int GetMouseY(void) { return static_cast<int>(cfire::ios_rl::state().mouse_pos.y); }
RLAPI Vector2 GetMousePosition(void) { return cfire::ios_rl::state().mouse_pos; }
RLAPI Vector2 GetMouseDelta(void) {
    auto& s = cfire::ios_rl::state();
    return Vector2{s.mouse_pos.x - s.prev_mouse_pos.x, s.mouse_pos.y - s.prev_mouse_pos.y};
}
RLAPI float GetMouseWheelMove(void) { return 0.0f; }

RLAPI int GetTouchX(void) { return GetMouseX(); }
RLAPI int GetTouchY(void) { return GetMouseY(); }
RLAPI Vector2 GetTouchPosition(int index) {
    auto& s = cfire::ios_rl::state();
    if (index >= 0 && index < s.touch_count) return s.touches[index].pos;
    return s.mouse_pos;
}
RLAPI int GetTouchPointId(int index) {
    auto& s = cfire::ios_rl::state();
    if (index >= 0 && index < s.touch_count) return s.touches[index].id;
    return 0;
}
RLAPI int GetTouchPointCount(void) { return cfire::ios_rl::state().touch_count; }

//----------------------------------------------------------------------------------
// 2D Shapes Drawing Functions
//----------------------------------------------------------------------------------

RLAPI void DrawPixel(int posX, int posY, Color color) {
    auto& s = cfire::ios_rl::state();
    cfire::ios_rl::set_batch_tex(s.white_tex);
    float x = static_cast<float>(posX);
    float y = static_cast<float>(posY);
    cfire::ios_rl::push_quad(x, y, 0, 0, x + 1, y, 1, 0, x + 1, y + 1, 1, 1, x, y + 1, 0, 1, color);
}

RLAPI void DrawRectangleRec(Rectangle rec, Color color) {
    auto& s = cfire::ios_rl::state();
    cfire::ios_rl::set_batch_tex(s.white_tex);
    float x0 = rec.x, y0 = rec.y;
    float x1 = rec.x + rec.width, y1 = rec.y + rec.height;
    cfire::ios_rl::push_quad(x0, y0, 0, 0, x1, y0, 1, 0, x1, y1, 1, 1, x0, y1, 0, 1, color);
}

RLAPI void DrawRectangle(int posX, int posY, int width, int height, Color color) {
    DrawRectangleRec(Rectangle{static_cast<float>(posX), static_cast<float>(posY), static_cast<float>(width), static_cast<float>(height)}, color);
}

RLAPI void DrawRectangleV(Vector2 position, Vector2 size, Color color) {
    DrawRectangleRec(Rectangle{position.x, position.y, size.x, size.y}, color);
}

RLAPI void DrawRectanglePro(Rectangle rec, Vector2 origin, float rotation, Color color) {
    auto& s = cfire::ios_rl::state();
    cfire::ios_rl::set_batch_tex(s.white_tex);
    float rad = rotation * DEG2RAD;
    float cs = std::cos(rad), sn = std::sin(rad);
    auto rot_pt = [&](float lx, float ly) -> Vector2 {
        float dx = lx - origin.x;
        float dy = ly - origin.y;
        return Vector2{rec.x + dx * cs - dy * sn, rec.y + dx * sn + dy * cs};
    };
    Vector2 p0 = rot_pt(0.0f, 0.0f);
    Vector2 p1 = rot_pt(rec.width, 0.0f);
    Vector2 p2 = rot_pt(rec.width, rec.height);
    Vector2 p3 = rot_pt(0.0f, rec.height);
    cfire::ios_rl::push_quad(p0.x, p0.y, 0, 0, p1.x, p1.y, 1, 0, p2.x, p2.y, 1, 1, p3.x, p3.y, 0, 1, color);
}

RLAPI void DrawRectangleRounded(Rectangle rec, float roundness, int segments, Color color) {
    (void)roundness; (void)segments;
    DrawRectangleRec(rec, color);
}

RLAPI void DrawRectangleLinesEx(Rectangle rec, float lineThick, Color color) {
    float t = std::max(1.0f, lineThick);
    DrawRectangleRec(Rectangle{rec.x, rec.y, rec.width, t}, color);
    DrawRectangleRec(Rectangle{rec.x, rec.y + rec.height - t, rec.width, t}, color);
    DrawRectangleRec(Rectangle{rec.x, rec.y + t, t, std::max(0.0f, rec.height - 2.0f * t)}, color);
    DrawRectangleRec(Rectangle{rec.x + rec.width - t, rec.y + t, t, std::max(0.0f, rec.height - 2.0f * t)}, color);
}

RLAPI void DrawRectangleLines(int posX, int posY, int width, int height, Color color) {
    DrawRectangleLinesEx(Rectangle{static_cast<float>(posX), static_cast<float>(posY), static_cast<float>(width), static_cast<float>(height)}, 1.0f, color);
}

RLAPI void DrawLineEx(Vector2 startPos, Vector2 endPos, float thick, Color color) {
    auto& s = cfire::ios_rl::state();
    cfire::ios_rl::set_batch_tex(s.white_tex);
    float dx = endPos.x - startPos.x;
    float dy = endPos.y - startPos.y;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.0001f) return;
    float nx = -dy / len * (thick * 0.5f);
    float ny =  dx / len * (thick * 0.5f);
    cfire::ios_rl::push_quad(
        startPos.x + nx, startPos.y + ny, 0, 0,
        endPos.x + nx,   endPos.y + ny,   1, 0,
        endPos.x - nx,   endPos.y - ny,   1, 1,
        startPos.x - nx, startPos.y - ny, 0, 1,
        color);
}

RLAPI void DrawLine(int startPosX, int startPosY, int endPosX, int endPosY, Color color) {
    DrawLineEx(Vector2{static_cast<float>(startPosX), static_cast<float>(startPosY)},
               Vector2{static_cast<float>(endPosX), static_cast<float>(endPosY)}, 1.0f, color);
}

RLAPI void DrawLineV(Vector2 startPos, Vector2 endPos, Color color) {
    DrawLineEx(startPos, endPos, 1.0f, color);
}

RLAPI void DrawLineStrip(Vector2 *points, int pointCount, Color color) {
    if (!points || pointCount < 2) return;
    for (int i = 0; i + 1 < pointCount; ++i) {
        DrawLineEx(points[i], points[i + 1], 1.5f, color);
    }
}

RLAPI void DrawCircleV(Vector2 center, float radius, Color color) {
    auto& s = cfire::ios_rl::state();
    cfire::ios_rl::set_batch_tex(s.white_tex);
    constexpr int segs = 24;
    for (int i = 0; i < segs; ++i) {
        float a0 = static_cast<float>(i) * (2.0f * PI / segs);
        float a1 = static_cast<float>(i + 1) * (2.0f * PI / segs);
        float x0 = center.x + std::cos(a0) * radius;
        float y0 = center.y + std::sin(a0) * radius;
        float x1 = center.x + std::cos(a1) * radius;
        float y1 = center.y + std::sin(a1) * radius;
        cfire::ios_rl::push_quad(center.x, center.y, 0, 0, x0, y0, 1, 0, x1, y1, 1, 1, center.x, center.y, 0, 1, color);
    }
}

RLAPI void DrawCircle(int centerX, int centerY, float radius, Color color) {
    DrawCircleV(Vector2{static_cast<float>(centerX), static_cast<float>(centerY)}, radius, color);
}

RLAPI void DrawCircleLines(int centerX, int centerY, float radius, Color color) {
    constexpr int segs = 28;
    Vector2 c{static_cast<float>(centerX), static_cast<float>(centerY)};
    for (int i = 0; i < segs; ++i) {
        float a0 = static_cast<float>(i) * (2.0f * PI / segs);
        float a1 = static_cast<float>(i + 1) * (2.0f * PI / segs);
        DrawLineEx(Vector2{c.x + std::cos(a0) * radius, c.y + std::sin(a0) * radius},
                   Vector2{c.x + std::cos(a1) * radius, c.y + std::sin(a1) * radius}, 1.0f, color);
    }
}

RLAPI void DrawTriangle(Vector2 v1, Vector2 v2, Vector2 v3, Color color) {
    auto& s = cfire::ios_rl::state();
    cfire::ios_rl::set_batch_tex(s.white_tex);
    cfire::ios_rl::push_quad(v1.x, v1.y, 0, 0, v2.x, v2.y, 1, 0, v3.x, v3.y, 1, 1, v1.x, v1.y, 0, 1, color);
}

//----------------------------------------------------------------------------------
// Font & Text Drawing Functions
//----------------------------------------------------------------------------------

RLAPI Font GetFontDefault(void) { return cfire::ios_rl::state().default_font; }

RLAPI int MeasureText(const char *text, int fontSize) {
    if (!text) return 0;
    float scale = static_cast<float>(std::max(1, fontSize)) / 10.0f;
    float char_w = 5.3f * scale;
    int max_len = 0, cur_len = 0;
    for (const char* p = text; *p; ++p) {
        if (*p == '\n') { max_len = std::max(max_len, cur_len); cur_len = 0; }
        else ++cur_len;
    }
    max_len = std::max(max_len, cur_len);
    return static_cast<int>(static_cast<float>(max_len) * char_w);
}

RLAPI Vector2 MeasureTextEx(Font font, const char *text, float fontSize, float spacing) {
    (void)font; (void)spacing;
    return Vector2{static_cast<float>(MeasureText(text, static_cast<int>(fontSize))), fontSize};
}

RLAPI void DrawText(const char *text, int posX, int posY, int fontSize, Color color) {
    if (!text || !*text) return;
    auto& s = cfire::ios_rl::state();
    cfire::ios_rl::set_batch_tex(s.font_tex);

    float scale = static_cast<float>(std::max(1, fontSize)) / 10.0f;
    float gw = 5.5f * scale;
    float gh = 8.0f * scale;
    float advance = 5.3f * scale;
    float x = static_cast<float>(posX);
    float y = static_cast<float>(posY) + (static_cast<float>(fontSize) - gh) * 0.5f;
    float start_x = x;

    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p) {
        unsigned char ch = *p;
        if (ch == '\n') {
            x = start_x;
            y += static_cast<float>(fontSize) + 2.0f;
            continue;
        }
        if (ch < 32 || ch > 126) ch = '?';
        int idx = static_cast<int>(ch) - 32;
        float u0 = static_cast<float>((idx % 16) * 8) / 128.0f;
        float v0 = static_cast<float>((idx / 16) * 8) / 64.0f;
        float u1 = u0 + (8.0f / 128.0f);
        float v1 = v0 + (8.0f / 64.0f);

        cfire::ios_rl::push_quad(
            x,      y,      u0, v0,
            x + gw, y,      u1, v0,
            x + gw, y + gh, u1, v1,
            x,      y + gh, u0, v1,
            color);
        x += advance;
    }
}

RLAPI void DrawTextEx(Font font, const char *text, Vector2 position, float fontSize, float spacing, Color tint) {
    (void)font; (void)spacing;
    DrawText(text, static_cast<int>(position.x), static_cast<int>(position.y), static_cast<int>(fontSize), tint);
}

RLAPI void DrawFPS(int posX, int posY) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d FPS", GetFPS());
    DrawText(buf, posX, posY, 20, LIME);
}

//----------------------------------------------------------------------------------
// Collision Detection & Color Helpers
//----------------------------------------------------------------------------------

RLAPI bool CheckCollisionRecs(Rectangle rec1, Rectangle rec2) {
    return (rec1.x < (rec2.x + rec2.width) && (rec1.x + rec1.width) > rec2.x) &&
           (rec1.y < (rec2.y + rec2.height) && (rec1.y + rec1.height) > rec2.y);
}

RLAPI bool CheckCollisionCircles(Vector2 center1, float radius1, Vector2 center2, float radius2) {
    float dx = center2.x - center1.x;
    float dy = center2.y - center1.y;
    float r = radius1 + radius2;
    return (dx * dx + dy * dy) <= (r * r);
}

RLAPI bool CheckCollisionCircleRec(Vector2 center, float radius, Rectangle rec) {
    float cx = std::clamp(center.x, rec.x, rec.x + rec.width);
    float cy = std::clamp(center.y, rec.y, rec.y + rec.height);
    float dx = center.x - cx;
    float dy = center.y - cy;
    return (dx * dx + dy * dy) <= (radius * radius);
}

RLAPI bool CheckCollisionPointRec(Vector2 point, Rectangle rec) {
    return (point.x >= rec.x) && (point.x <= (rec.x + rec.width)) &&
           (point.y >= rec.y) && (point.y <= (rec.y + rec.height));
}

RLAPI bool CheckCollisionPointCircle(Vector2 point, Vector2 center, float radius) {
    return CheckCollisionCircles(point, 0.0f, center, radius);
}

RLAPI Color Fade(Color color, float alpha) {
    float a = std::clamp(alpha, 0.0f, 1.0f);
    return Color{color.r, color.g, color.b, static_cast<unsigned char>(255.0f * a)};
}

RLAPI Color ColorAlpha(Color color, float alpha) { return Fade(color, alpha); }

RLAPI Color ColorFromHSV(float hue, float saturation, float value) {
    float k_r = std::fmod(5.0f + hue / 60.0f, 6.0f);
    float k_g = std::fmod(3.0f + hue / 60.0f, 6.0f);
    float k_b = std::fmod(1.0f + hue / 60.0f, 6.0f);
    float r = value - value * saturation * std::max(0.0f, std::min({k_r, 4.0f - k_r, 1.0f}));
    float g = value - value * saturation * std::max(0.0f, std::min({k_g, 4.0f - k_g, 1.0f}));
    float b = value - value * saturation * std::max(0.0f, std::min({k_b, 4.0f - k_b, 1.0f}));
    return Color{
        static_cast<unsigned char>(r * 255.0f),
        static_cast<unsigned char>(g * 255.0f),
        static_cast<unsigned char>(b * 255.0f),
        255
    };
}

//----------------------------------------------------------------------------------
// Audio Device & AudioStream Functions (`OPP-04` Pocket Studio Ready)
//----------------------------------------------------------------------------------

namespace cfire::ios_rl {
struct IosAudioStreamCtx {
#if defined(__OBJC__)
    AudioQueueRef queue = nullptr;
    AudioQueueBufferRef buffers[3] = {nullptr, nullptr, nullptr};
#endif
    bool buffer_free[3] = {true, true, true};
    unsigned int sample_rate = 44100;
    unsigned int sample_size = 16;
    unsigned int channels = 2;
};

#if defined(__OBJC__)
inline void audio_queue_cb(void* user_data, AudioQueueRef q, AudioQueueBufferRef buf) {
    (void)q;
    auto* ctx = static_cast<IosAudioStreamCtx*>(user_data);
    if (!ctx) return;
    for (int i = 0; i < 3; ++i) {
        if (ctx->buffers[i] == buf) {
            ctx->buffer_free[i] = true;
            break;
        }
    }
}
#endif
} // namespace cfire::ios_rl

RLAPI void InitAudioDevice(void) {}
RLAPI void CloseAudioDevice(void) {}
RLAPI bool IsAudioDeviceReady(void) { return true; }
RLAPI void SetMasterVolume(float volume) { (void)volume; }

RLAPI AudioStream LoadAudioStream(unsigned int sampleRate, unsigned int sampleSize, unsigned int channels) {
    auto* ctx = new cfire::ios_rl::IosAudioStreamCtx();
    ctx->sample_rate = sampleRate;
    ctx->sample_size = sampleSize;
    ctx->channels = channels;
#if defined(__OBJC__)
    AudioStreamBasicDescription fmt{};
    fmt.mSampleRate = static_cast<Float64>(sampleRate);
    fmt.mFormatID = kAudioFormatLinearPCM;
    fmt.mFormatFlags = (sampleSize == 32)
        ? (kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked)
        : (kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked);
    fmt.mBitsPerChannel = sampleSize;
    fmt.mChannelsPerFrame = channels;
    fmt.mBytesPerFrame = (sampleSize / 8) * channels;
    fmt.mFramesPerPacket = 1;
    fmt.mBytesPerPacket = fmt.mBytesPerFrame;

    AudioQueueNewOutput(&fmt, cfire::ios_rl::audio_queue_cb, ctx, nullptr, kCFRunLoopCommonModes, 0, &ctx->queue);
    if (ctx->queue) {
        UInt32 buf_bytes = 4096 * fmt.mBytesPerFrame;
        for (int i = 0; i < 3; ++i) {
            AudioQueueAllocateBuffer(ctx->queue, buf_bytes, &ctx->buffers[i]);
            ctx->buffer_free[i] = true;
        }
    }
#endif
    return AudioStream{reinterpret_cast<rAudioBuffer*>(ctx), nullptr, sampleRate, sampleSize, channels};
}

RLAPI void UnloadAudioStream(AudioStream stream) {
    auto* ctx = reinterpret_cast<cfire::ios_rl::IosAudioStreamCtx*>(stream.buffer);
    if (!ctx) return;
#if defined(__OBJC__)
    if (ctx->queue) {
        AudioQueueStop(ctx->queue, true);
        AudioQueueDispose(ctx->queue, true);
    }
#endif
    delete ctx;
}

RLAPI bool IsAudioStreamProcessed(AudioStream stream) {
    auto* ctx = reinterpret_cast<const cfire::ios_rl::IosAudioStreamCtx*>(stream.buffer);
    if (!ctx) return false;
    return ctx->buffer_free[0] || ctx->buffer_free[1] || ctx->buffer_free[2];
}

RLAPI void UpdateAudioStream(AudioStream stream, const void *data, int frameCount) {
    auto* ctx = reinterpret_cast<cfire::ios_rl::IosAudioStreamCtx*>(stream.buffer);
    if (!ctx || !data || frameCount <= 0) return;
#if defined(__OBJC__)
    if (!ctx->queue) return;
    for (int i = 0; i < 3; ++i) {
        if (ctx->buffer_free[i] && ctx->buffers[i]) {
            UInt32 bytes = static_cast<UInt32>(frameCount) * (ctx->sample_size / 8) * ctx->channels;
            if (bytes > ctx->buffers[i]->mAudioDataBytesCapacity) {
                bytes = ctx->buffers[i]->mAudioDataBytesCapacity;
            }
            std::memcpy(ctx->buffers[i]->mAudioData, data, bytes);
            ctx->buffers[i]->mAudioDataByteSize = bytes;
            ctx->buffer_free[i] = false;
            AudioQueueEnqueueBuffer(ctx->queue, ctx->buffers[i], 0, nullptr);
            break;
        }
    }
#endif
}

RLAPI void PlayAudioStream(AudioStream stream) {
#if defined(__OBJC__)
    auto* ctx = reinterpret_cast<cfire::ios_rl::IosAudioStreamCtx*>(stream.buffer);
    if (ctx && ctx->queue) AudioQueueStart(ctx->queue, nullptr);
#else
    (void)stream;
#endif
}

RLAPI void PauseAudioStream(AudioStream stream) {
#if defined(__OBJC__)
    auto* ctx = reinterpret_cast<cfire::ios_rl::IosAudioStreamCtx*>(stream.buffer);
    if (ctx && ctx->queue) AudioQueuePause(ctx->queue);
#else
    (void)stream;
#endif
}

RLAPI void ResumeAudioStream(AudioStream stream) { PlayAudioStream(stream); }

RLAPI void StopAudioStream(AudioStream stream) {
#if defined(__OBJC__)
    auto* ctx = reinterpret_cast<cfire::ios_rl::IosAudioStreamCtx*>(stream.buffer);
    if (ctx && ctx->queue) AudioQueueStop(ctx->queue, true);
#else
    (void)stream;
#endif
}

//----------------------------------------------------------------------------------
// Built-in `<raymath.h>` Equivalents
//----------------------------------------------------------------------------------
#ifndef RAYMATH_H
#define RAYMATH_H

RLAPI float Clamp(float value, float min, float max) { return std::clamp(value, min, max); }
RLAPI float Lerp(float start, float end, float amount) { return start + amount * (end - start); }
RLAPI Vector2 Vector2Zero(void) { return Vector2{0.0f, 0.0f}; }
RLAPI Vector2 Vector2One(void) { return Vector2{1.0f, 1.0f}; }
RLAPI Vector2 Vector2Add(Vector2 v1, Vector2 v2) { return Vector2{v1.x + v2.x, v1.y + v2.y}; }
RLAPI Vector2 Vector2Subtract(Vector2 v1, Vector2 v2) { return Vector2{v1.x - v2.x, v1.y - v2.y}; }
RLAPI float Vector2Length(Vector2 v) { return std::sqrt(v.x * v.x + v.y * v.y); }
RLAPI float Vector2Distance(Vector2 v1, Vector2 v2) { return Vector2Length(Vector2Subtract(v1, v2)); }
RLAPI Vector2 Vector2Scale(Vector2 v, float scale) { return Vector2{v.x * scale, v.y * scale}; }
RLAPI Vector2 Vector2Normalize(Vector2 v) {
    float len = Vector2Length(v);
    return len > 0.0f ? Vector2{v.x / len, v.y / len} : Vector2{0.0f, 0.0f};
}
RLAPI Vector2 Vector2Lerp(Vector2 v1, Vector2 v2, float amount) {
    return Vector2{Lerp(v1.x, v2.x, amount), Lerp(v1.y, v2.y, amount)};
}

#endif // RAYMATH_H

#endif // RAYLIB_H
