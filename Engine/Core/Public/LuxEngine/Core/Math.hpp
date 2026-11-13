#pragma once

#include <LuxEngine/Core/Core.hpp>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace LuxEngine {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    constexpr Vec2() = default;
    constexpr Vec2(float inX, float inY) : x(inX), y(inY) {}

    Vec2 operator+(const Vec2& o) const {
        return {x + o.x, y + o.y};
    }
    Vec2 operator-(const Vec2& o) const {
        return {x - o.x, y - o.y};
    }
    Vec2 operator*(float scalar) const {
        return {x * scalar, y * scalar};
    }
    Vec2& operator+=(const Vec2& o) {
        x += o.x;
        y += o.y;
        return *this;
    }
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    constexpr Vec3() = default;
    constexpr Vec3(float inX, float inY, float inZ) : x(inX), y(inY), z(inZ) {}

    Vec3 operator+(const Vec3& o) const {
        return {x + o.x, y + o.y, z + o.z};
    }
    Vec3 operator-(const Vec3& o) const {
        return {x - o.x, y - o.y, z - o.z};
    }
    Vec3 operator*(float scalar) const {
        return {x * scalar, y * scalar, z * scalar};
    }
    Vec3& operator+=(const Vec3& o) {
        x += o.x;
        y += o.y;
        z += o.z;
        return *this;
    }

    float Dot(const Vec3& o) const {
        return x * o.x + y * o.y + z * o.z;
    }

    Vec3 Cross(const Vec3& o) const {
        return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
    }

    Vec3 Normalized() const {
        const float len = std::sqrt(x * x + y * y + z * z);
        if (len > 0.00001f) {
            return {x / len, y / len, z / len};
        }
        return *this;
    }
};

struct Vec4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;

    constexpr Vec4() = default;
    constexpr Vec4(float inX, float inY, float inZ, float inW) : x(inX), y(inY), z(inZ), w(inW) {}
};

struct Mat4 {
    float elements[16] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                          0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};

    static Mat4 Identity() {
        return Mat4{};
    }

    static Mat4 Orthographic(float left, float right, float bottom, float top, float nearClip,
                             float farClip) {
        if (!std::isfinite(left) || !std::isfinite(right) || !std::isfinite(bottom) ||
            !std::isfinite(top) || !std::isfinite(nearClip) || !std::isfinite(farClip) ||
            left == right || bottom == top || nearClip == farClip)
            throw std::invalid_argument("Invalid orthographic projection");
        Mat4 result{};
        result.elements[0] = 2.0f / (right - left);
        result.elements[5] = 2.0f / (top - bottom);
        result.elements[10] = -1.0f / (farClip - nearClip);
        result.elements[12] = -(right + left) / (right - left);
        result.elements[13] = -(top + bottom) / (top - bottom);
        result.elements[14] = -nearClip / (farClip - nearClip);
        result.elements[15] = 1.0f;
        return result;
    }

    static Mat4 Perspective(float fovRadians, float aspect, float nearClip, float farClip) {
        Mat4 result{};
        if (!std::isfinite(fovRadians) || !std::isfinite(aspect) || !std::isfinite(nearClip) ||
            !std::isfinite(farClip) || fovRadians <= 0 || fovRadians >= std::numbers::pi_v<float> ||
            aspect <= 0 || nearClip <= 0 || farClip <= nearClip)
            throw std::invalid_argument("Invalid perspective projection");
        const float tanHalfFov = std::tan(fovRadians / 2.0f);
        result.elements[0] = 1.0f / (aspect * tanHalfFov);
        result.elements[5] = 1.0f / tanHalfFov;
        result.elements[10] = farClip / (nearClip - farClip);
        result.elements[11] = -1.0f;
        result.elements[14] = -(farClip * nearClip) / (farClip - nearClip);
        result.elements[15] = 0.0f;
        return result;
    }

    static Mat4 Translate(const Vec3& translation) {
        Mat4 result = Identity();
        result.elements[12] = translation.x;
        result.elements[13] = translation.y;
        result.elements[14] = translation.z;
        return result;
    }

    static Mat4 Scale(const Vec3& scale) {
        Mat4 result = Identity();
        result.elements[0] = scale.x;
        result.elements[5] = scale.y;
        result.elements[10] = scale.z;
        return result;
    }

    static Mat4 RotateX(float angle) {
        Mat4 result;
        const float c = std::cos(angle), s = std::sin(angle);
        result.elements[5] = c;
        result.elements[6] = s;
        result.elements[9] = -s;
        result.elements[10] = c;
        return result;
    }
    static Mat4 RotateY(float angle) {
        Mat4 result;
        const float c = std::cos(angle), s = std::sin(angle);
        result.elements[0] = c;
        result.elements[2] = -s;
        result.elements[8] = s;
        result.elements[10] = c;
        return result;
    }
    static Mat4 RotateZ(float angleRadians) {
        Mat4 result = Identity();
        const float c = std::cos(angleRadians);
        const float s = std::sin(angleRadians);
        result.elements[0] = c;
        result.elements[1] = s;
        result.elements[4] = -s;
        result.elements[5] = c;
        return result;
    }

    static Mat4 LookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
        Vec3 forward = (target - eye).Normalized();
        if (forward.x == 0.0f && forward.y == 0.0f && forward.z == 0.0f) {
            forward = {0.0f, 0.0f, -1.0f};
        }
        Vec3 right = forward.Cross(up).Normalized();
        if (right.x == 0.0f && right.y == 0.0f && right.z == 0.0f) {
            const Vec3 alt =
                std::abs(forward.y) > 0.99f ? Vec3{0.0f, 0.0f, 1.0f} : Vec3{0.0f, 1.0f, 0.0f};
            right = forward.Cross(alt).Normalized();
        }
        const Vec3 trueUp = right.Cross(forward);

        Mat4 result = Identity();
        result.elements[0] = right.x;
        result.elements[4] = right.y;
        result.elements[8] = right.z;

        result.elements[1] = trueUp.x;
        result.elements[5] = trueUp.y;
        result.elements[9] = trueUp.z;

        result.elements[2] = -forward.x;
        result.elements[6] = -forward.y;
        result.elements[10] = -forward.z;

        result.elements[12] = -right.Dot(eye);
        result.elements[13] = -trueUp.Dot(eye);
        result.elements[14] = forward.Dot(eye);
        return result;
    }

    Mat4 operator*(const Mat4& o) const {
        Mat4 res{};
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k) {
                    sum += elements[k * 4 + r] * o.elements[c * 4 + k];
                }
                res.elements[c * 4 + r] = sum;
            }
        }
        return res;
    }
};

inline float ToRadians(float degrees) {
    return degrees * (static_cast<float>(std::numbers::pi) / 180.0f);
}

} // namespace LuxEngine
