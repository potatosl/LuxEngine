#pragma once
#include "Engine/Core/Core.hpp"
#include "Engine/Core/Math.hpp"
#include "Engine/Renderer/RHI/Buffer.hpp"
#include "Engine/Renderer/RHI/Texture.hpp"
#include <span>

struct LuxOgreContext;

namespace LuxEngine {
struct RenderVertex {
    Vec3 position;
    Vec4 color;
    Vec2 uv;
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
    bool Initialize(SDL_Window* window);
    void Shutdown();
    FrameStart BeginFrame();
    bool Clear(float r, float g, float b, float a);
    bool EndFrame();
    bool DrawTriangles(std::span<const RenderVertex> vertices, const Mat4& viewProjection,
                       GpuTexture* texture = nullptr, bool depth = false);
    bool SetVSync(bool enabled);
    SDL_GPUDevice* GetHandle() const {
        return device_;
    }
    LuxOgreContext* GetOgreContext() const {
        return ogreContext_;
    }
    bool IsReady() const {
        return ogreContext_ || (device_ && claimed_);
    }

private:
    bool CreatePipelines();
    bool ResizeDepth(uint32 width, uint32 height);
    SDL_Window* window_ = nullptr;
    LuxOgreContext* ogreContext_ = nullptr;
    bool frameActive_ = false;
    SDL_GPUDevice* device_ = nullptr;
    SDL_GPUCommandBuffer* currentCommandBuffer_ = nullptr;
    SDL_GPUTexture* currentSwapchainTexture_ = nullptr;
    SDL_GPUTexture* depth_ = nullptr;
    SDL_GPUGraphicsPipeline* pipeline2D_ = nullptr;
    SDL_GPUGraphicsPipeline* pipeline3D_ = nullptr;
    GpuBuffer vertices_;
    GpuTexture white_;
    uint32 depthWidth_ = 0, depthHeight_ = 0;
    bool claimed_ = false;
    bool depthCleared_ = false;
};
} // namespace LuxEngine
