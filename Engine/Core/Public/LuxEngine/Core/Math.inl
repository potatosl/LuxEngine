#pragma once

namespace LuxEngine {
constexpr Vec4::Vec4() = default;

constexpr Vec2::Vec2() = default;

constexpr Vec2::Vec2(float inX, float inY) : x(inX), y(inY) {}

constexpr Vec3::Vec3() = default;

constexpr Vec3::Vec3(float inX, float inY, float inZ) : x(inX), y(inY), z(inZ) {}

constexpr Vec4::Vec4(float inX, float inY, float inZ, float inW) : x(inX), y(inY), z(inZ), w(inW) {}
} // namespace LuxEngine
