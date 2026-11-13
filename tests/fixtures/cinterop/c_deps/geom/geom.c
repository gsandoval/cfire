#include "geom.h"
#include <string.h>

Vec2 geom_vec2_add(Vec2 a, Vec2 b) { Vec2 r = {a.x + b.x, a.y + b.y}; return r; }
Vec2 geom_vec2_scale(Vec2 v, float s) { Vec2 r = {v.x * s, v.y * s}; return r; }
float geom_clamp(float value, float lo, float hi) { return value < lo ? lo : (value > hi ? hi : value); }
int geom_rect_contains(Rect r, Vec2 p) { return p.x >= r.x && p.y >= r.y && p.x < r.x + r.width && p.y < r.y + r.height; }
size_t geom_label_len(const char* label) { return label ? strlen(label) : 0; }
int32_t geom_sum_i32(const int32_t* values, size_t count) {
    int32_t total = 0;
    for (size_t i = 0; i < count; ++i) total += values[i];
    return total;
}
void geom_fill_i32(int32_t* values, size_t count, int32_t value) {
    for (size_t i = 0; i < count; ++i) values[i] = value;
}
uint32_t geom_pack_color(Color c) { return ((uint32_t)c.r << 24) | ((uint32_t)c.g << 16) | ((uint32_t)c.b << 8) | c.a; }
