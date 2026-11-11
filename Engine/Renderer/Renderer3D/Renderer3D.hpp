#pragma once

#include "Engine/Core/Core.hpp"
#include "Engine/Core/Math.hpp"
#include "Engine/Renderer/RHI/GpuDevice.hpp"
#include <memory>
#include <vector>

namespace LuxEngine {

struct Vertex3D {
    Vec3 position;
    Vec3 normal;
    Vec2 texCoords;
};

struct Material3D {
    Vec4 albedoColor{1.0f, 1.0f, 1.0f, 1.0f};
    float metallic = 0.0f;
    float roughness = 0.5f;
    std::string baseColorTexture;
};

struct DirectionalLight {
    Vec3 direction{0.0f, -1.0f, 0.0f};
    Vec3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
};

struct PointLight {
    Vec3 position{0.0f, 0.0f, 0.0f};
    Vec3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    float radius = 10.0f;
};

class LUX_API PerspectiveCamera {
public:
    PerspectiveCamera() = default;
    PerspectiveCamera(float fovRadians, float aspect, float nearClip, float farClip);

    void SetPerspective(float fovRadians, float aspect, float nearClip, float farClip);
    void SetPosition(const Vec3& position) {
        position_ = position;
        RecalculateMatrix();
    }
    void SetTarget(const Vec3& target) {
        target_ = target;
        RecalculateMatrix();
    }
    void SetUp(const Vec3& up) {
        up_ = up;
        RecalculateMatrix();
    }

    const Vec3& GetPosition() const {
        return position_;
    }
    const Vec3& GetTarget() const {
        return target_;
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
    Vec3 position_{0.0f, 0.0f, 5.0f};
    Vec3 target_{0.0f, 0.0f, 0.0f};
    Vec3 up_{0.0f, 1.0f, 0.0f};
};

class LUX_API Mesh3D {
public:
    bool Upload(GpuDevice* device, std::span<const Vec3> positions,
                std::span<const uint32> indices);
    bool Upload(GpuDevice* device, std::span<const Vertex3D> vertices,
                std::span<const uint32> indices);
    void Release();
    bool IsValid() const;
    uint32 GetIndexCount() const {
        return IsValid() ? indexCount_ : 0;
    }

private:
    friend class Renderer3D;
    GpuBuffer vertices_;
    GpuBuffer indices_;
    uint32 indexCount_ = 0;
};

class LUX_API Renderer3D {
public:
    struct Statistics {
        uint32 drawCalls = 0;
        uint32 meshCount = 0;
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

        void BeginScene(const PerspectiveCamera& camera, const DirectionalLight& light);
        void BeginScene(const PerspectiveCamera& camera, const DirectionalLight& light,
                        std::span<const PointLight> pointLights);
        void EndScene();
        void AbortScene();

        void DrawMesh(const Mat4& transform, const Material3D& material);
        void DrawMesh(const Mesh3D& mesh, const Mat4& transform, const Material3D& material);
        void DrawMesh(const Mesh3D& mesh, const Mat4& transform, const Material3D& material,
                      GpuTexture* texture);
        bool DrawMeshAsset(const std::string& path, const Mat4& transform,
                           const Material3D& material);
        void DrawMesh(std::span<const Vec3> positions, std::span<const uint32> indices,
                      const Mat4& transform, const Material3D& material);
        void DrawCube(const Vec3& position, const Vec3& scale, const Material3D& material);

        void ResetStats();
        Statistics GetStats() const;
        const std::string& GetLastAssetError() const;
        bool GetMeshBounds(const std::string& path, Vec3& minimum, Vec3& maximum) const;

    private:
        GpuTexture* ResolveBaseColorTexture(const std::string& path);
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

    static void BeginScene(const PerspectiveCamera& camera, const DirectionalLight& light);
    static void BeginScene(const PerspectiveCamera& camera, const DirectionalLight& light,
                           std::span<const PointLight> pointLights);
    static void EndScene();

    static void DrawMesh(const Mat4& transform, const Material3D& material);
    static void DrawMesh(const Mesh3D& mesh, const Mat4& transform, const Material3D& material);
    static void DrawMesh(const Mesh3D& mesh, const Mat4& transform, const Material3D& material,
                         GpuTexture* texture);
    static bool DrawMeshAsset(const std::string& path, const Mat4& transform,
                              const Material3D& material);
    static void DrawMesh(std::span<const Vec3> positions, std::span<const uint32> indices,
                         const Mat4& transform, const Material3D& material);
    static void DrawCube(const Vec3& position, const Vec3& scale, const Material3D& material);

    static void ResetStats();
    static Statistics GetStats();
    static const std::string& GetLastAssetError();
};

} // namespace LuxEngine
