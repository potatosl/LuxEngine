#include <LuxEngine/Renderer/RHI/Buffer.hpp>

namespace LuxEngine {
uint32 GpuBuffer::GetSize() const {
    return IsValid() ? sizeBytes_ : 0;
}

BufferType GpuBuffer::GetType() const {
    return type_;
}

GpuBuffer::GpuBuffer() = default;
} // namespace LuxEngine
