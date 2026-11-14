#pragma once

#include <LuxEngine/Renderer/RHI/GpuDevice.hpp>
#include <memory>
#include <span>
#include <string>

namespace LuxEngine {
class PerspectiveCamera;
struct Render3DSettings;

namespace Rhi {

// One mesh submitted to the 3D scene renderer. Handles come from the owning backend;
// a zero texture selects the backend's white texture.
struct SceneDrawDesc {
    uint64 vertexBuffer = 0;
    uint64 indexBuffer = 0;
    uint64 texture = 0;
    uint32 indexCount = 0;
    Mat4 model;
    Vec4 albedo;
    float metallic = 0;
    float roughness = .5f;
    uint32 alphaMode = 1;
    float alphaCutoff = .001f;
    bool doubleSided = true;
    bool legacy = false;
    uint64 normalTexture = 0, metallicTexture = 0, roughnessTexture = 0, occlusionTexture = 0;
    float normalScale = 1.f, occlusionStrength = 1.f;
    uint32 sampling = 0;
};

// HDR scene path (PBR, shadows, image-based lighting) owned by a backend.
class Scene3DRenderer {
public:
    virtual ~Scene3DRenderer();
    // Resolves the handles immediately; returns false when one of them is not live.
    virtual bool AddDraw(const SceneDrawDesc& draw) = 0;
    virtual size_t DrawCount() const = 0;
    virtual void ClearDraws() = 0;
    virtual bool Render(const PerspectiveCamera& camera, const Render3DSettings& settings,
                        float exposure, std::string& error) = 0;
    virtual bool EnvironmentReady() const = 0;
    virtual double EnvironmentPreparationMs() const = 0;
};

// Graphics API implementation behind GpuDevice. GpuDevice validates arguments that only
// depend on the public objects (buffer type, ownership, size) before forwarding here.
class RenderBackend {
public:
    virtual ~RenderBackend();

    virtual bool Initialize(Window& window, const GpuDeviceConfig& config) = 0;
    virtual void Shutdown() = 0;
    virtual bool Usable() const = 0;

    virtual GpuStatus GetStatus() const = 0;
    virtual GpuStatus GetLastFailure() const = 0;
    virtual const std::string& GetLastError() const = 0;
    virtual const GpuDeviceInfo& GetInfo() const = 0;
    virtual GpuFrameStats GetFrameStats() const = 0;
    virtual GpuDeviceCounters GetCounters() const = 0;
    virtual GpuMemoryStats GetMemoryStats() const = 0;

    virtual GpuDevice::FrameStart BeginFrame() = 0;
    virtual bool Clear(float r, float g, float b, float a) = 0;
    virtual bool DrawTexturedTriangles(std::span<const RenderVertex> vertices,
                                       const Mat4& viewProjection, uint64 texture, bool depth) = 0;
    virtual bool SetLighting(const GpuLighting& lighting) = 0;
    virtual bool DrawIndexed(uint64 vertexBuffer, uint64 indexBuffer, uint32 indexCount,
                             const Mat4& viewProjection, const Mat4& model, const Vec4& albedo,
                             uint64 texture, float metallic, float roughness, bool pbr) = 0;
    virtual bool EndFrame() = 0;
    virtual bool SetVSync(bool enabled) = 0;
    virtual void NotifyResize() = 0;
    virtual bool FlushUploads() = 0;
    virtual bool WaitIdle() = 0;
    virtual bool RequestFrameCapture() = 0;
    virtual bool ReadFrameCapture(GpuFrameCapture& capture) = 0;

    virtual bool CreateBuffer(BufferType type, uint32 sizeBytes, uint64& handle) = 0;
    virtual bool UpdateBuffer(uint64 handle, const void* data, uint32 sizeBytes,
                              uint32 offsetBytes) = 0;
    virtual void DestroyBuffer(uint64 handle) = 0;
    virtual bool HasBuffer(uint64 handle) const = 0;

    virtual bool CreateTexture(uint32 width, uint32 height, const void* pixelData,
                               TextureFilter filter, TextureColorSpace colorSpace, uint32 mipLevels,
                               TextureSampling sampling, uint64& handle) = 0;
    virtual void DestroyTexture(uint64 handle) = 0;
    virtual bool HasTexture(uint64 handle) const = 0;

    virtual std::unique_ptr<Scene3DRenderer> CreateScene3DRenderer() = 0;
};

// Creates the backend used by GpuDevice::Initialize.
Ref<RenderBackend> CreateRenderBackend(const GpuDeviceConfig& config);

} // namespace Rhi
} // namespace LuxEngine
