#pragma once

#include "Engine/Core/Core.hpp"
#include <SDL3/SDL.h>
#include "Engine/Renderer/RHI/DeviceLifetime.hpp"

namespace LuxEngine {

    enum class BufferType {
        Vertex,
        Index,
        Uniform
    };

    class LUX_API GpuBuffer {
    public:
        GpuBuffer() = default;
        ~GpuBuffer();

        GpuBuffer(const GpuBuffer&) = delete;
        GpuBuffer& operator=(const GpuBuffer&) = delete;
        GpuBuffer(GpuBuffer&& other) noexcept;
        GpuBuffer& operator=(GpuBuffer&& other) noexcept;

        bool Initialize(SDL_GPUDevice* device, BufferType type, uint32 sizeBytes);
        bool SetData(const void* data, uint32 sizeBytes, SDL_GPUCommandBuffer* command = nullptr);
        void Release();

        SDL_GPUBuffer* GetHandle() const { return IsValid() ? buffer_ : nullptr; }
        uint32 GetSize() const { return sizeBytes_; }
        BufferType GetType() const { return type_; }
        bool IsValid() const { return buffer_ != nullptr && lifetime_.Alive(); }

    private:
        DeviceLifetime lifetime_;
        SDL_GPUDevice* device_ = nullptr;
        SDL_GPUBuffer* buffer_ = nullptr;
        SDL_GPUTransferBuffer* transfer_ = nullptr;
        BufferType type_ = BufferType::Vertex;
        uint32 sizeBytes_ = 0;
    };

} // namespace LuxEngine
