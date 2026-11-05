#pragma once

#include "Engine/Core/Core.hpp"
#include <SDL3/SDL.h>

namespace LuxEngine {

    class GpuTexture {
    public:
        GpuTexture() = default;
        ~GpuTexture();

        GpuTexture(const GpuTexture&) = delete;
        GpuTexture& operator=(const GpuTexture&) = delete;
        GpuTexture(GpuTexture&& other) noexcept;
        GpuTexture& operator=(GpuTexture&& other) noexcept;

        bool Initialize(SDL_GPUDevice* device, uint32 width, uint32 height, const void* pixelData = nullptr);
        void Release();

        SDL_GPUTexture* GetHandle() const { return texture_; }
        SDL_GPUSampler* GetSampler() const { return sampler_; }
        uint32 GetWidth() const { return width_; }
        uint32 GetHeight() const { return height_; }
        bool IsValid() const { return texture_ != nullptr; }

    private:
        SDL_GPUDevice* device_ = nullptr;
        SDL_GPUTexture* texture_ = nullptr;
        SDL_GPUSampler* sampler_ = nullptr;
        uint32 width_ = 0;
        uint32 height_ = 0;
    };

} // namespace LuxEngine
