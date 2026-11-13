#include "Vulkan/VulkanDevice.hpp"
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
    VkBufferUsageFlags usage = 0;
    switch (type) {
    case BufferType::Vertex:
        usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        break;
    case BufferType::Index:
        usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        break;
    case BufferType::Uniform:
        usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        break;
    case BufferType::Storage:
        usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        break;
    }
    Vk::BufferRecord record;
    if (!device->state_->CreateBufferResource(sizeBytes, usage, record))
        return false;
    handle_ = device->state_->buffers.Insert(record);
    owner_ = device->state_;
    type_ = type;
    sizeBytes_ = sizeBytes;
    return true;
}

bool GpuBuffer::SetData(const void* data, uint32 sizeBytes, uint32 offsetBytes) {
    auto owner = owner_.lock();
    if (!owner || !owner->Usable() || !data || sizeBytes == 0 ||
        static_cast<uint64>(offsetBytes) + sizeBytes > sizeBytes_)
        return false;
    Vk::BufferRecord* record = owner->buffers.Get(handle_);
    return record && owner->UploadBuffer(*record, data, sizeBytes, offsetBytes);
}

void GpuBuffer::Release() {
    if (auto owner = owner_.lock()) {
        Vk::BufferRecord record;
        if (owner->buffers.Remove(handle_, record))
            owner->Retire({0, record.buffer, VK_NULL_HANDLE, VK_NULL_HANDLE, record.allocation});
    }
    owner_.reset();
    handle_ = 0;
    sizeBytes_ = 0;
}

bool GpuBuffer::IsValid() const {
    auto owner = owner_.lock();
    return owner && owner->buffers.Get(handle_) != nullptr;
}

bool GpuBuffer::BelongsTo(const GpuDevice& device) const {
    auto owner = owner_.lock();
    return owner && owner == device.state_ && owner->buffers.Get(handle_) != nullptr;
}

} // namespace LuxEngine
