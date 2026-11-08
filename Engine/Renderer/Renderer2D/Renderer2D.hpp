#pragma once

#include "Engine/Core/Core.hpp"
#include "Engine/Core/Math.hpp"
#include "Engine/Renderer/RHI/GpuDevice.hpp"
#include "Engine/Renderer/RHI/Texture.hpp"
#include <memory>
#include <vector>

namespace LuxEngine {

struct Vertex2D {
    Vec3 position;
    Vec4 color;
    Vec2 texCoords;
};

class LUX_API OrthographicCamera {
public:
    OrthographicCamera() = default;
    OrthographicCamera(float left, float right, float bottom, float top);

    void SetProjection(float left, float right, float bottom, float top);
    void SetPosition(const Vec3& position) {
        position_ = position;
        RecalculateMatrix();
    }
    void SetRotation(float rotationRadians) {
        rotation_ = rotationRadians;
        RecalculateMatrix();
    }

    const Vec3& GetPosition() const {
        return position_;
    }
    float GetRotation() const {
        return rotation_;
    }

    const Mat4& GetProjectionMatrix() const {
        return projectionMatrix_;
    }
    const Mat4& GetViewMatrix() const {
        return viewMatrix_;
    }
    const Mat4& GetViewProjectionMatrix() const {
        return viewProjectionMatrix_;
    }

private:
    void RecalculateMatrix();

    Mat4 projectionMatrix_ = Mat4::Identity();
    Mat4 viewMatrix_ = Mat4::Identity();
    Mat4 viewProjectionMatrix_ = Mat4::Identity();
    Vec3 position_{0.0f, 0.0f, 0.0f};
    float rotation_ = 0.0f;
};

class LUX_API Renderer2D {
public:
    struct Statistics {
        uint32 drawCalls = 0;
        uint32 quadCount = 0;
    };

    class LUX_API Context {
    public:
        Context();
        explicit Context(GpuDevice* device);
        ~Context();
        Context(const Context&) = delete;
        Context& operator=(const Context&) = delete;
        Context(Context&& other) noexcept;
        Context& operator=(Context&& other) noexcept;

        void Init(GpuDevice* device = nullptr);
        void Shutdown();
        void DetachDevice(const GpuDevice* device);
        bool IsDeviceValid() const;

        void BeginScene(const OrthographicCamera& camera);
        void EndScene();
        void AbortScene();

        void DrawQuad(const Vec2& position, const Vec2& size, const Vec4& color);
        void DrawQuad(const Vec3& position, const Vec2& size, const Vec4& color);
        void DrawRotatedQuad(const Vec2& position, const Vec2& size, float rotationRadians,
                             const Vec4& color);
        void DrawSprite(const Vec2& position, const Vec2& size, GpuTexture* texture,
                        const Vec4& tint = Vec4(1.0f, 1.0f, 1.0f, 1.0f));

        void ResetStats();
        Statistics GetStats() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

    class LUX_API ScopedContext {
    public:
        explicit ScopedContext(Context& context);
        ~ScopedContext();
        ScopedContext(const ScopedContext&) = delete;
        ScopedContext& operator=(const ScopedContext&) = delete;

    private:
        Context* previous_ = nullptr;
        Context* current_ = nullptr;
    };

    static void Init(GpuDevice* device = nullptr);
    static void Shutdown();
    static void DetachDevice(const GpuDevice* device);

    static void BeginScene(const OrthographicCamera& camera);
    static void EndScene();

    static void DrawQuad(const Vec2& position, const Vec2& size, const Vec4& color);
    static void DrawQuad(const Vec3& position, const Vec2& size, const Vec4& color);
    static void DrawRotatedQuad(const Vec2& position, const Vec2& size, float rotationRadians,
                                const Vec4& color);
    static void DrawSprite(const Vec2& position, const Vec2& size, GpuTexture* texture,
                           const Vec4& tint = Vec4(1.0f, 1.0f, 1.0f, 1.0f));

    static void ResetStats();
    static Statistics GetStats();
};

} // namespace LuxEngine
