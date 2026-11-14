#pragma once

#include "RHI/RenderBackend.hpp"

#include <memory>
#include <unordered_set>

namespace LuxEngine::Forge {

struct ForgeLibrary;
class ForgeScene3DRenderer;

// Render backend built on The Forge (Direct3D 12 on Windows). The Forge lives in
// LuxForge.dll, loaded at Initialize; select it with LUX_RENDER_BACKEND=forge.
// Implemented: frame loop, clear, present, resize, vsync, frame capture, textures,
// DrawTriangles (2D and depth-tested), buffers, lighting, DrawIndexed (lit meshes) and the HDR
// 3D scene renderer (shadow map, image-based lighting, sky, ACES tone mapping).
class ForgeBackend final : public Rhi::RenderBackend {
public:
    ForgeBackend();
    ~ForgeBackend() override;

    bool Initialize(Window& window, const GpuDeviceConfig& config) override;
    void Shutdown() override;
    bool Usable() const override;

    GpuStatus GetStatus() const override;
    GpuStatus GetLastFailure() const override;
    const std::string& GetLastError() const override;
    const GpuDeviceInfo& GetInfo() const override;
    GpuFrameStats GetFrameStats() const override;
    GpuDeviceCounters GetCounters() const override;
    GpuMemoryStats GetMemoryStats() const override;

    GpuDevice::FrameStart BeginFrame() override;
    bool Clear(float r, float g, float b, float a) override;
    bool DrawTexturedTriangles(std::span<const RenderVertex> vertices, const Mat4& viewProjection,
                               uint64 texture, bool depth) override;
    bool SetLighting(const GpuLighting& lighting) override;
    bool DrawIndexed(uint64 vertexBuffer, uint64 indexBuffer, uint32 indexCount,
                     const Mat4& viewProjection, const Mat4& model, const Vec4& albedo,
                     uint64 texture, float metallic, float roughness, bool pbr) override;
    bool EndFrame() override;
    bool SetVSync(bool enabled) override;
    void NotifyResize() override;
    bool FlushUploads() override;
    bool WaitIdle() override;
    bool RequestFrameCapture() override;
    bool ReadFrameCapture(GpuFrameCapture& capture) override;

    bool CreateBuffer(BufferType type, uint32 sizeBytes, uint64& handle) override;
    bool UpdateBuffer(uint64 handle, const void* data, uint32 sizeBytes,
                      uint32 offsetBytes) override;
    void DestroyBuffer(uint64 handle) override;
    bool HasBuffer(uint64 handle) const override;

    bool CreateTexture(uint32 width, uint32 height, const void* pixelData, TextureFilter filter,
                       TextureColorSpace colorSpace, uint32 mipLevels, TextureSampling sampling,
                       uint64& handle) override;
    void DestroyTexture(uint64 handle) override;
    bool HasTexture(uint64 handle) const override;

    std::unique_ptr<Rhi::Scene3DRenderer> CreateScene3DRenderer() override;

private:
    friend class ForgeScene3DRenderer;

    // A GPU/driver failure: the device stops being usable.
    bool Fail(GpuStatus status, const std::string& message);
    // Wrong use of the API (e.g. Clear outside a frame): reported, device stays usable.
    bool Reject(const std::string& message);
    // Failure reported by LuxForge.dll: device loss is fatal, anything else is a rejection.
    bool ForgeFailure();

    // Shared so scene renderers can tell (through a weak_ptr) whether the device they were
    // created for still exists.
    std::shared_ptr<ForgeLibrary> lib_;
    Window* window_ = nullptr;
    GpuStatus status_ = GpuStatus::Uninitialized;
    GpuStatus lastFailure_ = GpuStatus::Uninitialized;
    std::string lastError_;
    GpuDeviceInfo info_;
    GpuFrameStats frameStats_;
    GpuDeviceCounters counters_;
    std::unordered_set<uint64> textures_;
    std::unordered_set<uint64> buffers_;
    bool frameOpen_ = false;
    bool resizePending_ = false;
};

} // namespace LuxEngine::Forge
