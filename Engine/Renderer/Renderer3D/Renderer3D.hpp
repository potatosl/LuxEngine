#pragma once

#include "Engine/Core/Core.hpp"
#include "Engine/Core/Math.hpp"
#include "Engine/Renderer/RHI/GpuDevice.hpp"
#include <vector>

namespace LuxEngine {

    struct Vertex3D {
        Vec3 position;
        Vec3 normal;
        Vec2 texCoords;
    };

    struct Material3D {
        Vec4 albedoColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        float metallic = 0.0f;
        float roughness = 0.5f;
    };

    struct DirectionalLight {
        Vec3 direction{ 0.0f, -1.0f, 0.0f };
        Vec3 color{ 1.0f, 1.0f, 1.0f };
        float intensity = 1.0f;
    };

    struct PointLight {
        Vec3 position{ 0.0f, 0.0f, 0.0f };
        Vec3 color{ 1.0f, 1.0f, 1.0f };
        float intensity = 1.0f;
        float radius = 10.0f;
    };

    class PerspectiveCamera {
    public:
        PerspectiveCamera() = default;
        PerspectiveCamera(float fovRadians, float aspect, float nearClip, float farClip);

        void SetPerspective(float fovRadians, float aspect, float nearClip, float farClip);
        void SetPosition(const Vec3& position) { position_ = position; RecalculateMatrix(); }
        void SetTarget(const Vec3& target) { target_ = target; RecalculateMatrix(); }
        void SetUp(const Vec3& up) { up_ = up; RecalculateMatrix(); }

        const Vec3& GetPosition() const { return position_; }
        const Vec3& GetTarget() const { return target_; }
        const Mat4& GetProjectionMatrix() const { return projectionMatrix_; }
        const Mat4& GetViewMatrix() const { return viewMatrix_; }
        const Mat4& GetViewProjectionMatrix() const { return viewProjectionMatrix_; }

    private:
        void RecalculateMatrix();

        Mat4 projectionMatrix_ = Mat4::Identity();
        Mat4 viewMatrix_ = Mat4::Identity();
        Mat4 viewProjectionMatrix_ = Mat4::Identity();
        Vec3 position_{ 0.0f, 0.0f, 5.0f };
        Vec3 target_{ 0.0f, 0.0f, 0.0f };
        Vec3 up_{ 0.0f, 1.0f, 0.0f };
    };

    class Renderer3D {
    public:
        struct Statistics {
            uint32 drawCalls = 0;
            uint32 meshCount = 0;
        };

        static void Init(GpuDevice* device = nullptr);
        static void Shutdown();

        static void BeginScene(const PerspectiveCamera& camera, const DirectionalLight& light);
        static void EndScene();

        static void DrawMesh(const Mat4& transform, const Material3D& material);
        static void DrawCube(const Vec3& position, const Vec3& scale, const Material3D& material);

        static void ResetStats();
        static Statistics GetStats();
    };

} // namespace LuxEngine
