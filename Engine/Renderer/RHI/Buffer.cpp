#include "Engine/Renderer/RHI/Buffer.hpp"
#include "Engine/Core/Log.hpp"
#include <cstring>

namespace LuxEngine {

    GpuBuffer::~GpuBuffer() {
        Release();
    }

    GpuBuffer::GpuBuffer(GpuBuffer&& other) noexcept
        : device_(other.device_),
          buffer_(other.buffer_),
          type_(other.type_),
          sizeBytes_(other.sizeBytes_) {
        other.device_ = nullptr;
        other.buffer_ = nullptr;
        other.sizeBytes_ = 0;
    }

    GpuBuffer& GpuBuffer::operator=(GpuBuffer&& other) noexcept {
        if (this != &other) {
            Release();
            device_ = other.device_;
            buffer_ = other.buffer_;
            type_ = other.type_;
            sizeBytes_ = other.sizeBytes_;
            other.device_ = nullptr;
            other.buffer_ = nullptr;
            other.sizeBytes_ = 0;
        }
        return *this;
    }

    bool GpuBuffer::Initialize(SDL_GPUDevice* device, BufferType type, uint32 sizeBytes) {
        Release();
        device_ = device;
        type_ = type;
        sizeBytes_ = sizeBytes;

        if (!device_ || sizeBytes_ == 0) return false;

        SDL_GPUBufferCreateInfo createInfo{};
        createInfo.size = sizeBytes_;

        switch (type_) {
            case BufferType::Vertex:
                createInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
                break;
            case BufferType::Index:
                createInfo.usage = SDL_GPU_BUFFERUSAGE_INDEX;
                break;
            case BufferType::Uniform:
                createInfo.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
                break;
        }

        buffer_ = SDL_CreateGPUBuffer(device_, &createInfo);
        if (!buffer_) {
            Log::Error(std::string("Failed to create GPU Buffer: ") + SDL_GetError());
            return false;
        }

        return true;
    }

    void GpuBuffer::SetData(const void* data, uint32 sizeBytes) {
        if (!device_ || !buffer_ || !data || sizeBytes == 0) return;

        const uint32 bytesToCopy = (sizeBytes < sizeBytes_) ? sizeBytes : sizeBytes_;

        SDL_GPUTransferBufferCreateInfo transferInfo{};
        transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transferInfo.size = bytesToCopy;

        SDL_GPUTransferBuffer* transferBuffer = SDL_CreateGPUTransferBuffer(device_, &transferInfo);
        if (!transferBuffer) return;

        void* mapped = SDL_MapGPUTransferBuffer(device_, transferBuffer, false);
        if (mapped) {
            std::memcpy(mapped, data, bytesToCopy);
            SDL_UnmapGPUTransferBuffer(device_, transferBuffer);

            SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
            if (cmd) {
                SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
                if (copyPass) {
                    SDL_GPUTransferBufferLocation src{};
                    src.transfer_buffer = transferBuffer;
                    src.offset = 0;

                    SDL_GPUBufferRegion dst{};
                    dst.buffer = buffer_;
                    dst.offset = 0;
                    dst.size = bytesToCopy;

                    SDL_UploadToGPUBuffer(copyPass, &src, &dst, false);
                    SDL_EndGPUCopyPass(copyPass);
                }
                SDL_SubmitGPUCommandBuffer(cmd);
            }
        }

        SDL_ReleaseGPUTransferBuffer(device_, transferBuffer);
    }

    void GpuBuffer::Release() {
        if (device_ && buffer_) {
            SDL_ReleaseGPUBuffer(device_, buffer_);
            buffer_ = nullptr;
        }
        device_ = nullptr;
        sizeBytes_ = 0;
    }

} // namespace LuxEngine
