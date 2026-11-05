#pragma once

#include "Engine/Core/Core.hpp"
#include "Engine/Core/Math.hpp"
#include "Engine/Renderer/RHI/GpuDevice.hpp"
#include "Engine/Renderer/RHI/Texture.hpp"
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
        void SetPosition(const Vec3& position) { position_ = position; RecalculateMatrix(); }
        void SetRotation(float rotationRadians) { rotation_ = rotationRadians; RecalculateMatrix(); }

        const Vec3& GetPosition() const { return position_; }
        float GetRotation() const { return rotation_; }

        const Mat4& GetProjectionMatrix() const { return projectionMatrix_; }
        const Mat4& GetViewMatrix() const { return viewMatrix_; }
        const Mat4& GetViewProjectionMatrix() const { return viewProjectionMatrix_; }

    private:
        void RecalculateMatrix();

        Mat4 projectionMatrix_ = Mat4::Identity();
        Mat4 viewMatrix_ = Mat4::Identity();
        Mat4 viewProjectionMatrix_ = Mat4::Identity();
        Vec3 position_{ 0.0f, 0.0f, 0.0f };
        float rotation_ = 0.0f;
    };

    class LUX_API Renderer2D {
    public:
        struct Statistics {
            uint32 drawCalls = 0;
            uint32 quadCount = 0;
        };

        static void Init(GpuDevice* device = nullptr);
        static void Shutdown();

        static void BeginScene(const OrthographicCamera& camera);
        static void EndScene();

        static void DrawQuad(const Vec2& position, const Vec2& size, const Vec4& color);
        static void DrawQuad(const Vec3& position, const Vec2& size, const Vec4& color);
        static void DrawRotatedQuad(const Vec2& position, const Vec2& size, float rotationRadians, const Vec4& color);
        static void DrawSprite(const Vec2& position, const Vec2& size, GpuTexture* texture, const Vec4& tint = Vec4(1.0f, 1.0f, 1.0f, 1.0f));

        static void ResetStats();
        static Statistics GetStats();
    };

} // namespace LuxEngine
