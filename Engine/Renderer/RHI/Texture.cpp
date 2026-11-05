#include "Engine/Renderer/RHI/Texture.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/Renderer/RHI/GpuDevice.hpp"
#if defined(LUX_USE_OGRE) && LUX_USE_OGRE
#include "Engine/Renderer/Ogre/OgreBridge.h"
#endif
#include <cstring>
#include <exception>
#include <limits>
#include <vector>

namespace LuxEngine {

GpuTexture::~GpuTexture() {
    Release();
}

GpuTexture::GpuTexture(GpuTexture&& other) noexcept
    : lifetime_(other.lifetime_), device_(other.device_), texture_(other.texture_),
      sampler_(other.sampler_), ogreContext_(other.ogreContext_), ogreTexture_(other.ogreTexture_),
      width_(other.width_), height_(other.height_) {
    other.device_ = nullptr;
    other.texture_ = nullptr;
    other.sampler_ = nullptr;
    other.ogreContext_ = nullptr;
    other.ogreTexture_ = nullptr;
    other.width_ = 0;
    other.height_ = 0;
}

GpuTexture& GpuTexture::operator=(GpuTexture&& other) noexcept {
    if (this != &other) {
        Release();
        lifetime_ = other.lifetime_;
        device_ = other.device_;
        texture_ = other.texture_;
        sampler_ = other.sampler_;
        ogreContext_ = other.ogreContext_;
        ogreTexture_ = other.ogreTexture_;
        width_ = other.width_;
        height_ = other.height_;
        other.device_ = nullptr;
        other.texture_ = nullptr;
        other.sampler_ = nullptr;
        other.ogreContext_ = nullptr;
        other.ogreTexture_ = nullptr;
        other.width_ = 0;
        other.height_ = 0;
    }
    return *this;
}

bool GpuTexture::Initialize(GpuDevice* device, uint32 width, uint32 height, const void* pixelData) {
    if (!device || !device->IsReady()) {
        Release();
        return false;
    }
#if defined(LUX_USE_OGRE) && LUX_USE_OGRE
    if (auto* context = device->GetOgreContext()) {
        Release();
        if (width == 0 || height == 0 ||
            static_cast<uint64>(width) * height > std::numeric_limits<uint32>::max() / 4)
            return false;
        lifetime_.Bind(context);
        if (!lifetime_.Tracked())
            return false;
        std::vector<uint8> emptyPixels;
        if (!pixelData) {
            try {
                emptyPixels.resize(static_cast<size_t>(width) * height * 4);
            } catch (const std::exception&) {
                return false;
            }
            pixelData = emptyPixels.data();
        }
        ogreTexture_ = lux_ogre_texture_create(context, width, height, pixelData);
        if (!ogreTexture_)
            return false;
        ogreContext_ = context;
        width_ = width;
        height_ = height;
        return true;
    }
#endif
    return Initialize(device->GetHandle(), width, height, pixelData);
}

bool GpuTexture::Initialize(SDL_GPUDevice* device, uint32 width, uint32 height,
                            const void* pixelData) {
    Release();
    device_ = device;
    lifetime_.Bind(device);
    width_ = width;
    height_ = height;

    if (!device_ || width_ == 0 || height_ == 0 ||
        static_cast<uint64>(width_) * height_ > std::numeric_limits<uint32>::max() / 4) {
        Release();
        return false;
    }

    SDL_GPUTextureCreateInfo texInfo{};
    texInfo.type = SDL_GPU_TEXTURETYPE_2D;
    texInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    texInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    texInfo.width = width_;
    texInfo.height = height_;
    texInfo.layer_count_or_depth = 1;
    texInfo.num_levels = 1;

    texture_ = SDL_CreateGPUTexture(device_, &texInfo);
    if (!texture_) {
        Log::Error(std::string("Failed to create GPU Texture: ") + SDL_GetError());
        return false;
    }

    SDL_GPUSamplerCreateInfo samplerInfo{};
    samplerInfo.min_filter = SDL_GPU_FILTER_LINEAR;
    samplerInfo.mag_filter = SDL_GPU_FILTER_LINEAR;
    samplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    samplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;

    sampler_ = SDL_CreateGPUSampler(device_, &samplerInfo);
    if (!sampler_) {
        Log::Error(std::string("Failed to create GPU Sampler: ") + SDL_GetError());
        Release();
        return false;
    }

    if (pixelData) {
        const uint32 imageSize = width_ * height_ * 4;
        SDL_GPUTransferBufferCreateInfo transferInfo{};
        transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transferInfo.size = imageSize;

        bool uploaded = false;
        SDL_GPUTransferBuffer* transferBuffer = SDL_CreateGPUTransferBuffer(device_, &transferInfo);
        if (transferBuffer) {
            void* mapped = SDL_MapGPUTransferBuffer(device_, transferBuffer, false);
            if (mapped) {
                std::memcpy(mapped, pixelData, imageSize);
                SDL_UnmapGPUTransferBuffer(device_, transferBuffer);

                SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
                if (cmd) {
                    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
                    if (copyPass) {
                        SDL_GPUTextureTransferInfo src{};
                        src.transfer_buffer = transferBuffer;
                        src.offset = 0;
                        src.pixels_per_row = width_;
                        src.rows_per_layer = height_;

                        SDL_GPUTextureRegion dst{};
                        dst.texture = texture_;
                        dst.w = width_;
                        dst.h = height_;
                        dst.d = 1;

                        SDL_UploadToGPUTexture(copyPass, &src, &dst, false);
                        SDL_EndGPUCopyPass(copyPass);
                        uploaded = true;
                    }
                    uploaded = SDL_SubmitGPUCommandBuffer(cmd) && uploaded;
                }
            }
            SDL_ReleaseGPUTransferBuffer(device_, transferBuffer);
        }
        if (!uploaded) {
            Release();
            return false;
        }
    }

    return true;
}

void GpuTexture::Release() {
    if (ogreContext_ && ogreTexture_ && lifetime_.Alive()) {
#if defined(LUX_USE_OGRE) && LUX_USE_OGRE
        lux_ogre_texture_destroy(ogreContext_, ogreTexture_);
#endif
    }
    if (device_ && lifetime_.Alive()) {
        if (sampler_) {
            SDL_ReleaseGPUSampler(device_, sampler_);
            sampler_ = nullptr;
        }
        if (texture_) {
            SDL_ReleaseGPUTexture(device_, texture_);
            texture_ = nullptr;
        }
        device_ = nullptr;
    }
    texture_ = nullptr;
    sampler_ = nullptr;
    device_ = nullptr;
    ogreContext_ = nullptr;
    ogreTexture_ = nullptr;
    width_ = 0;
    height_ = 0;
    lifetime_ = {};
}

} // namespace LuxEngine
