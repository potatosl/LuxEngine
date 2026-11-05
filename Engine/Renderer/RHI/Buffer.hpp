#pragma once

#include "Engine/Core/Core.hpp"
#include <SDL3/SDL.h>

namespace LuxEngine {

    enum class BufferType {
        Vertex,
        Index,
        Uniform
    };

    class GpuBuffer {
    public:
        GpuBuffer() = default;
        ~GpuBuffer();

        GpuBuffer(const GpuBuffer&) = delete;
        GpuBuffer& operator=(const GpuBuffer&) = delete;
        GpuBuffer(GpuBuffer&& other) noexcept;
        GpuBuffer& operator=(GpuBuffer&& other) noexcept;

        bool Initialize(SDL_GPUDevice* device, BufferType type, uint32 sizeBytes);
        void SetData(const void* data, uint32 sizeBytes);
        void Release();

        SDL_GPUBuffer* GetHandle() const { return buffer_; }
        uint32 GetSize() const { return sizeBytes_; }
        BufferType GetType() const { return type_; }
        bool IsValid() const { return buffer_ != nullptr; }

    private:
        SDL_GPUDevice* device_ = nullptr;
        SDL_GPUBuffer* buffer_ = nullptr;
        BufferType type_ = BufferType::Vertex;
        uint32 sizeBytes_ = 0;
    };

} // namespace LuxEngine
