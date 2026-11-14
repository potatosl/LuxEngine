#pragma once

#include <LuxEngine/Core/Core.hpp>
#include <LuxEngine/Core/Math.hpp>
#include <LuxEngine/Renderer/RHI/GpuDevice.hpp>
#include <LuxEngine/Renderer/Renderer3D/Render3DSettings.hpp>
#include <memory>
#include <vector>

namespace LuxEngine {

struct Vertex3D {
    Vec3 position;
    Vec3 normal;
    Vec2 texCoords;
};

enum class MaterialAlphaMode { Opaque, Mask, Blend };

struct Material3D {
    Vec4 albedoColor{1.0f, 1.0f, 1.0f, 1.0f};
    float metallic = 0.0f;
    float roughness = 0.5f;
    std::string baseColorTexture;
    std::string normalTexture;
    std::string metallicTexture;
    std::string roughnessTexture;
    std::string occlusionTexture;
    float normalScale = 1.f;
    float occlusionStrength = 1.f;
    TextureSampling textureSampling{true, TextureAddressMode::Repeat, true, false};
    MaterialAlphaMode alphaMode = MaterialAlphaMode::Mask;
    float alphaCutoff = 0.001f;
    bool doubleSided = true;
};

struct PointLight {
    Vec3 position{0.0f, 0.0f, 0.0f};
    Vec3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    float radius = 10.0f;
};

class LUX_API PerspectiveCamera {
public:
    PerspectiveCamera();
    PerspectiveCamera(float fovRadians, float aspect, float nearClip, float farClip);

    void SetPerspective(float fovRadians, float aspect, float nearClip, float farClip);

    void SetPosition(const Vec3& position);

    void SetTarget(const Vec3& target);

    void SetUp(const Vec3& up);

    const Vec3& GetPosition() const;

    const Vec3& GetTarget() const;

    const Mat4& GetProjectionMatrix() const;

    const Mat4& GetViewMatrix() const;

    const Mat4& GetViewProjectionMatrix() const;

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
    bool UpdateVertices(std::span<const Vertex3D> vertices);
    void Release();
    bool IsValid() const;

    uint32 GetIndexCount() const;

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
        void BeginScene(const PerspectiveCamera& camera, const Render3DSettings& settings,
                        float exposure, std::span<const PointLight> pointLights = {});
        void EndScene();
        void AbortScene();

        void DrawMesh(const Mat4& transform, const Material3D& material);
        void DrawMesh(const Mesh3D& mesh, const Mat4& transform, const Material3D& material);
        void DrawMesh(const Mesh3D& mesh, const Mat4& transform, const Material3D& material,
                      GpuTexture* texture);
        bool DrawMeshAsset(const std::string& path, const Mat4& transform,
                           const Material3D& material, int32 modelSubmesh = -1, int32 clip = -1,
                           double time = 0.0, uint64 instance = 0);
        void DrawMesh(std::span<const Vec3> positions, std::span<const uint32> indices,
                      const Mat4& transform, const Material3D& material);
        void DrawCube(const Vec3& position, const Vec3& scale, const Material3D& material);

        void ResetStats();
        Statistics GetStats() const;
        const std::string& GetLastAssetError() const;
        bool IsEnvironmentReady() const;
        double GetEnvironmentPreparationMs() const;
        bool GetMeshBounds(const std::string& path, Vec3& minimum, Vec3& maximum) const;

    private:
        Ref<GpuTexture>
        ResolveBaseColorTexture(const std::string& path, TextureSampling sampling,
                                TextureColorSpace colorSpace = TextureColorSpace::Srgb);
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
    static void BeginScene(const PerspectiveCamera& camera, const Render3DSettings& settings,
                           float exposure, std::span<const PointLight> pointLights = {});
    static void EndScene();

    static void DrawMesh(const Mat4& transform, const Material3D& material);
    static void DrawMesh(const Mesh3D& mesh, const Mat4& transform, const Material3D& material);
    static void DrawMesh(const Mesh3D& mesh, const Mat4& transform, const Material3D& material,
                         GpuTexture* texture);
    static bool DrawMeshAsset(const std::string& path, const Mat4& transform,
                              const Material3D& material, int32 modelSubmesh = -1, int32 clip = -1,
                              double time = 0.0, uint64 instance = 0);
    static void DrawMesh(std::span<const Vec3> positions, std::span<const uint32> indices,
                         const Mat4& transform, const Material3D& material);
    static void DrawCube(const Vec3& position, const Vec3& scale, const Material3D& material);

    static void ResetStats();
    static Statistics GetStats();
    static const std::string& GetLastAssetError();
};

} // namespace LuxEngine
