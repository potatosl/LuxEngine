#pragma once

#include "Engine/Core/Core.hpp"
#include <SDL3/SDL.h>

namespace LuxEngine {

    class GpuDevice {
    public:
        GpuDevice() = default;
        ~GpuDevice();

        GpuDevice(const GpuDevice&) = delete;
        GpuDevice& operator=(const GpuDevice&) = delete;
        GpuDevice(GpuDevice&&) noexcept = default;
        GpuDevice& operator=(GpuDevice&&) noexcept = default;

        bool Initialize(SDL_Window* window);
        void Shutdown();

        bool BeginFrame();
        void Clear(float r, float g, float b, float a);
        void EndFrame();

        SDL_GPUDevice* GetHandle() const { return device_; }
        bool IsReady() const { return device_ != nullptr && claimed_; }

    private:
        SDL_Window* window_ = nullptr;
        SDL_GPUDevice* device_ = nullptr;
        SDL_GPUCommandBuffer* currentCommandBuffer_ = nullptr;
        SDL_GPUTexture* currentSwapchainTexture_ = nullptr;
        bool claimed_ = false;
    };

} // namespace LuxEngine
