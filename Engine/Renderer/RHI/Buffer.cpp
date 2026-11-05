#include "Engine/Renderer/RHI/Buffer.hpp"
#include "Engine/Core/Log.hpp"
#include <cstring>

namespace LuxEngine {

GpuBuffer::~GpuBuffer() {
    Release();
}

GpuBuffer::GpuBuffer(GpuBuffer&& other) noexcept
    : lifetime_(other.lifetime_), device_(other.device_), buffer_(other.buffer_),
      transfer_(other.transfer_), type_(other.type_), sizeBytes_(other.sizeBytes_) {
    other.device_ = nullptr;
    other.buffer_ = nullptr;
    other.transfer_ = nullptr;
    other.sizeBytes_ = 0;
}

GpuBuffer& GpuBuffer::operator=(GpuBuffer&& other) noexcept {
    if (this != &other) {
        Release();
        lifetime_ = other.lifetime_;
        device_ = other.device_;
        buffer_ = other.buffer_;
        transfer_ = other.transfer_;
        type_ = other.type_;
        sizeBytes_ = other.sizeBytes_;
        other.device_ = nullptr;
        other.buffer_ = nullptr;
        other.transfer_ = nullptr;
        other.sizeBytes_ = 0;
    }
    return *this;
}

bool GpuBuffer::Initialize(SDL_GPUDevice* device, BufferType type, uint32 sizeBytes) {
    Release();
    device_ = device;
    lifetime_.Bind(device);
    type_ = type;
    sizeBytes_ = sizeBytes;

    if (!device_ || sizeBytes_ == 0)
        return false;

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

bool GpuBuffer::SetData(const void* data, uint32 sizeBytes, SDL_GPUCommandBuffer* command) {
    if (!lifetime_.Alive() || !device_ || !buffer_ || !data || !sizeBytes || sizeBytes > sizeBytes_)
        return false;
    if (!transfer_) {
        SDL_GPUTransferBufferCreateInfo info{};
        info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        info.size = sizeBytes_;
        transfer_ = SDL_CreateGPUTransferBuffer(device_, &info);
        if (!transfer_)
            return false;
    }
    void* mapped = SDL_MapGPUTransferBuffer(device_, transfer_, true);
    if (!mapped)
        return false;
    std::memcpy(mapped, data, sizeBytes);
    SDL_UnmapGPUTransferBuffer(device_, transfer_);
    const bool own = !command;
    if (own)
        command = SDL_AcquireGPUCommandBuffer(device_);
    if (!command)
        return false;
    auto* pass = SDL_BeginGPUCopyPass(command);
    if (!pass) {
        if (own)
            SDL_CancelGPUCommandBuffer(command);
        return false;
    }
    SDL_GPUTransferBufferLocation source{transfer_, 0};
    SDL_GPUBufferRegion destination{buffer_, 0, sizeBytes};
    SDL_UploadToGPUBuffer(pass, &source, &destination, true);
    SDL_EndGPUCopyPass(pass);
    return !own || SDL_SubmitGPUCommandBuffer(command);
}

void GpuBuffer::Release() {
    if (device_ && lifetime_.Alive() && transfer_)
        SDL_ReleaseGPUTransferBuffer(device_, transfer_);
    transfer_ = nullptr;
    if (device_ && lifetime_.Alive() && buffer_) {
        SDL_ReleaseGPUBuffer(device_, buffer_);
        buffer_ = nullptr;
    }
    buffer_ = nullptr;
    device_ = nullptr;
    sizeBytes_ = 0;
}

} // namespace LuxEngine
