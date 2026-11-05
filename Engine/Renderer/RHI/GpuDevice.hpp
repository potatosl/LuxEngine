#pragma once
#include "Engine/Core/Core.hpp"
#include "Engine/Core/Math.hpp"
#include "Engine/Renderer/RHI/Buffer.hpp"
#include "Engine/Renderer/RHI/Texture.hpp"
#include <span>
#include <string>
#include <vector>

namespace LuxEngine {
class Window;
namespace Vk {
struct DeviceState;
}

struct RenderVertex {
    Vec3 position;
    Vec4 color;
    Vec2 uv;
};

enum class GpuValidation { Auto, Off, On, Synchronization };

enum class GpuStatus { Uninitialized, Ready, OutOfMemory, SurfaceLost, Timeout, DeviceLost, Error };

struct GpuDeviceConfig {
    uint32 framesInFlight = 2;
    bool vsync = true;
    GpuValidation validation = GpuValidation::Auto;
    std::string shaderDirectory;
    std::string preferredDevice;
    uint64 stagingFlushBytes = 64ull << 20;
    uint64 maxFrameVertexBytes = 256ull << 20;
    uint64 gpuTimeoutNs = 5'000'000'000ull;
};

struct GpuDeviceInfo {
    std::string deviceName;
    std::string deviceType;
    uint32 apiVersion = 0;
    uint32 driverVersion = 0;
    uint32 graphicsQueueFamily = 0;
    uint32 presentQueueFamily = 0;
    uint32 computeQueueFamily = 0;
    uint32 transferQueueFamily = 0;
    uint32 maxTextureSize = 0;
    uint32 framesInFlight = 0;
    bool timelineSemaphores = false;
    bool memoryBudget = false;
    bool gpuTimestamps = false;
    bool validation = false;
    bool synchronizationValidation = false;
};

struct GpuFrameStats {
    uint64 frameIndex = 0;
    uint32 drawCalls = 0;
    uint32 pipelineBinds = 0;
    uint32 descriptorBinds = 0;
    uint32 pushConstantUpdates = 0;
    uint32 submissions = 0;
    uint64 vertexBytes = 0;
    uint64 uploadBytes = 0;
    double cpuWaitMs = 0.0;
    double gpuTimeMs = -1.0;
};

struct GpuDeviceCounters {
    uint64 framesSubmitted = 0;
    uint64 framesSkipped = 0;
    uint64 submissions = 0;
    uint64 swapchainRecreations = 0;
    uint64 pipelinesCreated = 0;
    uint32 validationErrors = 0;
    uint32 validationWarnings = 0;
};

struct GpuMemoryStats {
    uint64 deviceLocalUsage = 0;
    uint64 deviceLocalBudget = 0;
    uint64 allocationBytes = 0;
    uint32 allocationCount = 0;
    uint32 liveBuffers = 0;
    uint32 liveTextures = 0;
    uint32 pendingDestructions = 0;
    uint32 descriptorPools = 0;
    uint64 pendingStagingBytes = 0;
};

struct GpuFrameCapture {
    uint32 width = 0;
    uint32 height = 0;
    std::vector<uint8> rgba;
};

class LUX_API GpuDevice {
public:
    enum class FrameStart { Ready, Skipped, Error };

    GpuDevice() = default;
    ~GpuDevice();
    GpuDevice(const GpuDevice&) = delete;
    GpuDevice& operator=(const GpuDevice&) = delete;
    GpuDevice(GpuDevice&&) = delete;
    GpuDevice& operator=(GpuDevice&&) = delete;

    bool Initialize(Window& window, const GpuDeviceConfig& config = {});
    void Shutdown();
    FrameStart BeginFrame();
    bool Clear(float r, float g, float b, float a);
    bool EndFrame();
    bool DrawTriangles(std::span<const RenderVertex> vertices, const Mat4& viewProjection,
                       GpuTexture* texture = nullptr, bool depth = false);
    bool SetVSync(bool enabled);
    void NotifyResize();
    bool FlushUploads();
    bool WaitIdle();
    bool RequestFrameCapture();
    bool ReadFrameCapture(GpuFrameCapture& capture);

    bool IsReady() const;
    GpuStatus GetStatus() const;
    GpuStatus GetLastFailure() const;
    const std::string& GetLastError() const;
    const GpuDeviceInfo& GetInfo() const;
    GpuFrameStats GetFrameStats() const;
    GpuDeviceCounters GetCounters() const;
    GpuMemoryStats GetMemoryStats() const;

private:
    friend class GpuBuffer;
    friend class GpuTexture;
    friend class Renderer2D;
    bool DrawTexturedTriangles(std::span<const RenderVertex> vertices, const Mat4& viewProjection,
                               uint64 textureHandle, bool depth);
    Ref<Vk::DeviceState> state_;
};

LUX_API const char* ToString(GpuStatus status);
} // namespace LuxEngine
