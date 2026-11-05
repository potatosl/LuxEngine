#include "Engine/Renderer/RHI/Texture.hpp"
#include "Engine/Core/Log.hpp"
#include <cstring>
#include <limits>

namespace LuxEngine {

    GpuTexture::~GpuTexture() {
        Release();
    }

    GpuTexture::GpuTexture(GpuTexture&& other) noexcept
        : lifetime_(other.lifetime_), device_(other.device_),
          texture_(other.texture_),
          sampler_(other.sampler_),
          width_(other.width_),
          height_(other.height_) {
        other.device_ = nullptr;
        other.texture_ = nullptr;
        other.sampler_ = nullptr;
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
            width_ = other.width_;
            height_ = other.height_;
            other.device_ = nullptr;
            other.texture_ = nullptr;
            other.sampler_ = nullptr;
            other.width_ = 0;
            other.height_ = 0;
        }
        return *this;
    }

    bool GpuTexture::Initialize(SDL_GPUDevice* device, uint32 width, uint32 height, const void* pixelData) {
        Release();
        device_ = device;
        lifetime_.Bind(device);
        width_ = width;
        height_ = height;

        if (!device_ || width_ == 0 || height_ == 0 || static_cast<uint64>(width_) * height_ > std::numeric_limits<uint32>::max() / 4) { Release(); return false; }

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
            if (!uploaded) { Release(); return false; }
        }

        return true;
    }

    void GpuTexture::Release() {
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
        texture_ = nullptr; sampler_ = nullptr; device_ = nullptr;
        width_ = 0;
        height_ = 0;
    }

} // namespace LuxEngine
