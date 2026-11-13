#pragma once
// A small C library used to test cfire's C header bridge (CF-185): structs, struct returns, string and pointer
// parameters, and bitflag enums. It replaced raylib as the test's foreign library.
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Vec2 { float x; float y; } Vec2;
typedef struct Rect { float x; float y; float width; float height; } Rect;
typedef struct Color { unsigned char r; unsigned char g; unsigned char b; unsigned char a; } Color;

typedef enum GeomFlags {
    GEOM_FLAG_NONE = 0,
    GEOM_FLAG_VISIBLE = 1,
    GEOM_FLAG_SOLID = 2,
    GEOM_FLAG_HIGHLIGHT = 4
} GeomFlags;

Vec2 geom_vec2_add(Vec2 a, Vec2 b);
Vec2 geom_vec2_scale(Vec2 v, float s);
float geom_clamp(float value, float lo, float hi);
int geom_rect_contains(Rect r, Vec2 p);
size_t geom_label_len(const char* label);
int32_t geom_sum_i32(const int32_t* values, size_t count);
void geom_fill_i32(int32_t* values, size_t count, int32_t value);
uint32_t geom_pack_color(Color c);

#ifdef __cplusplus
}
#endif
