#include "RHI/RenderBackend.hpp"
#include <LuxEngine/Renderer/RHI/Buffer.hpp>
#include <LuxEngine/Renderer/RHI/GpuDevice.hpp>

namespace LuxEngine {

GpuBuffer::~GpuBuffer() {
    Release();
}

GpuBuffer::GpuBuffer(GpuBuffer&& other) noexcept
    : owner_(std::move(other.owner_)), handle_(other.handle_), type_(other.type_),
      sizeBytes_(other.sizeBytes_) {
    other.owner_.reset();
    other.handle_ = 0;
    other.sizeBytes_ = 0;
}

GpuBuffer& GpuBuffer::operator=(GpuBuffer&& other) noexcept {
    if (this != &other) {
        Release();
        owner_ = std::move(other.owner_);
        handle_ = other.handle_;
        type_ = other.type_;
        sizeBytes_ = other.sizeBytes_;
        other.owner_.reset();
        other.handle_ = 0;
        other.sizeBytes_ = 0;
    }
    return *this;
}

bool GpuBuffer::Initialize(GpuDevice* device, BufferType type, uint32 sizeBytes) {
    Release();
    if (!device || !device->IsReady() || sizeBytes == 0)
        return false;
    uint64 handle = 0;
    if (!device->backend_->CreateBuffer(type, sizeBytes, handle))
        return false;
    handle_ = handle;
    owner_ = device->backend_;
    type_ = type;
    sizeBytes_ = sizeBytes;
    return true;
}

bool GpuBuffer::SetData(const void* data, uint32 sizeBytes, uint32 offsetBytes) {
    auto owner = owner_.lock();
    if (!owner || !owner->Usable() || !data || sizeBytes == 0 ||
        static_cast<uint64>(offsetBytes) + sizeBytes > sizeBytes_)
        return false;
    return owner->UpdateBuffer(handle_, data, sizeBytes, offsetBytes);
}

void GpuBuffer::Release() {
    if (auto owner = owner_.lock())
        owner->DestroyBuffer(handle_);
    owner_.reset();
    handle_ = 0;
    sizeBytes_ = 0;
}

bool GpuBuffer::IsValid() const {
    auto owner = owner_.lock();
    return owner && owner->HasBuffer(handle_);
}

bool GpuBuffer::BelongsTo(const GpuDevice& device) const {
    auto owner = owner_.lock();
    return owner && owner == device.backend_ && owner->HasBuffer(handle_);
}

} // namespace LuxEngine
