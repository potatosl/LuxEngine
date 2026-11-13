#pragma once

#include <LuxEngine/Core/Core.hpp>
#include <memory>

namespace LuxEngine {
class GpuDevice;

namespace Rhi {
class RenderBackend;
}

enum class BufferType { Vertex, Index, Uniform, Storage };

class LUX_API GpuBuffer {
public:
    GpuBuffer();
    ~GpuBuffer();

    GpuBuffer(const GpuBuffer&) = delete;
    GpuBuffer& operator=(const GpuBuffer&) = delete;
    GpuBuffer(GpuBuffer&& other) noexcept;
    GpuBuffer& operator=(GpuBuffer&& other) noexcept;

    bool Initialize(GpuDevice* device, BufferType type, uint32 sizeBytes);
    bool SetData(const void* data, uint32 sizeBytes, uint32 offsetBytes = 0);
    void Release();

    uint32 GetSize() const;

    BufferType GetType() const;

    bool IsValid() const;
    bool BelongsTo(const GpuDevice& device) const;

private:
    friend class GpuDevice;
    friend class Renderer3D;
    std::weak_ptr<Rhi::RenderBackend> owner_;
    uint64 handle_ = 0;
    BufferType type_ = BufferType::Vertex;
    uint32 sizeBytes_ = 0;
};

} // namespace LuxEngine
