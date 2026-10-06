#pragma once
#include <cmath>

namespace rr {

constexpr float kPi = 3.14159265358979323846f;

struct Vec2 {
    float x = 0, y = 0;
    Vec2() = default;
    Vec2(float x_, float y_) : x(x_), y(y_) {}
    Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float k) const { return {x * k, y * k}; }
    Vec2 operator-() const { return {-x, -y}; }
    Vec2& operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(Vec2 o) { x -= o.x; y -= o.y; return *this; }
};

inline float dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline float cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }
inline float length(Vec2 a) { return std::sqrt(dot(a, a)); }
inline Vec2 normalize(Vec2 a) { float l = length(a); return l > 1e-9f ? a * (1.0f / l) : Vec2{1, 0}; }
inline Vec2 perpLeft(Vec2 a) { return {-a.y, a.x}; }
inline Vec2 rotate(Vec2 a, float ang) {
    float c = std::cos(ang), s = std::sin(ang);
    return {c * a.x - s * a.y, s * a.x + c * a.y};
}
inline Vec2 fromAngle(float ang) { return {std::cos(ang), std::sin(ang)}; }
inline float wrapAngle(float a) {
    while (a > kPi) a -= 2 * kPi;
    while (a <= -kPi) a += 2 * kPi;
    return a;
}
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

}  // namespace rr
