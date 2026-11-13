#pragma once

#include <LuxEngine/Core/Core.hpp>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace LuxEngine {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    constexpr Vec2();

    constexpr Vec2(float inX, float inY);

    LUX_API Vec2 operator+(const Vec2& o) const;

    LUX_API Vec2 operator-(const Vec2& o) const;

    LUX_API Vec2 operator*(float scalar) const;

    LUX_API Vec2& operator+=(const Vec2& o);
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    constexpr Vec3();

    constexpr Vec3(float inX, float inY, float inZ);

    LUX_API Vec3 operator+(const Vec3& o) const;

    LUX_API Vec3 operator-(const Vec3& o) const;

    LUX_API Vec3 operator*(float scalar) const;

    LUX_API Vec3& operator+=(const Vec3& o);

    LUX_API float Dot(const Vec3& o) const;

    LUX_API Vec3 Cross(const Vec3& o) const;

    LUX_API Vec3 Normalized() const;
};

struct Vec4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;

    constexpr Vec4();

    constexpr Vec4(float inX, float inY, float inZ, float inW);
};

struct Mat4 {
    float elements[16] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                          0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};

    LUX_API static Mat4 Identity();

    LUX_API static Mat4 Orthographic(float left, float right, float bottom, float top,
                                     float nearClip, float farClip);

    LUX_API static Mat4 Perspective(float fovRadians, float aspect, float nearClip, float farClip);

    LUX_API static Mat4 Translate(const Vec3& translation);

    LUX_API static Mat4 Scale(const Vec3& scale);

    LUX_API static Mat4 RotateX(float angle);

    LUX_API static Mat4 RotateY(float angle);

    LUX_API static Mat4 RotateZ(float angleRadians);

    LUX_API static Mat4 LookAt(const Vec3& eye, const Vec3& target, const Vec3& up);

    LUX_API Mat4 operator*(const Mat4& o) const;
};

LUX_API float ToRadians(float degrees);

} // namespace LuxEngine

#include <LuxEngine/Core/Math.inl>
