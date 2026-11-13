#pragma once

#include "RHI/RenderBackend.hpp"
#include "VulkanDevice.hpp"

namespace LuxEngine::Vk {

class VulkanBackend final : public Rhi::RenderBackend {
public:
    using FrameStart = GpuDevice::FrameStart;

    VulkanBackend() = default;
    ~VulkanBackend() override;

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

    FrameStart BeginFrame() override;
    bool Clear(float r, float g, float b, float a) override;
    bool DrawTexturedTriangles(std::span<const RenderVertex> vertices, const Mat4& viewProjection,
                               uint64 textureHandle, bool depth) override;
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
                       TextureColorSpace colorSpace, uint64& handle) override;
    void DestroyTexture(uint64 handle) override;
    bool HasTexture(uint64 handle) const override;

    std::unique_ptr<Rhi::Scene3DRenderer> CreateScene3DRenderer() override;

private:
    bool IsReady() const {
        return Usable();
    }

    Ref<DeviceState> state_;
};

} // namespace LuxEngine::Vk
