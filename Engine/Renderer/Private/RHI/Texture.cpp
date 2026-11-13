#include "Vulkan/VulkanDevice.hpp"
#include <LuxEngine/Renderer/RHI/GpuDevice.hpp>
#include <LuxEngine/Renderer/RHI/Texture.hpp>

namespace LuxEngine {

GpuTexture::~GpuTexture() {
    Release();
}

GpuTexture::GpuTexture(GpuTexture&& other) noexcept
    : owner_(std::move(other.owner_)), handle_(other.handle_), width_(other.width_),
      height_(other.height_), colorSpace_(other.colorSpace_) {
    other.owner_.reset();
    other.handle_ = 0;
    other.width_ = other.height_ = 0;
    other.colorSpace_ = TextureColorSpace::Linear;
}

GpuTexture& GpuTexture::operator=(GpuTexture&& other) noexcept {
    if (this != &other) {
        Release();
        owner_ = std::move(other.owner_);
        handle_ = other.handle_;
        width_ = other.width_;
        height_ = other.height_;
        colorSpace_ = other.colorSpace_;
        other.owner_.reset();
        other.handle_ = 0;
        other.width_ = other.height_ = 0;
        other.colorSpace_ = TextureColorSpace::Linear;
    }
    return *this;
}

bool GpuTexture::Initialize(GpuDevice* device, uint32 width, uint32 height, const void* pixelData,
                            TextureFilter filter, TextureColorSpace colorSpace) {
    Release();
    if (!device || !device->IsReady())
        return false;
    Vk::TextureRecord record;
    if (!device->state_->CreateTextureResource(width, height, pixelData, record, filter,
                                               colorSpace))
        return false;
    handle_ = device->state_->textures.Insert(record);
    owner_ = device->state_;
    width_ = width;
    height_ = height;
    colorSpace_ = colorSpace;
    return true;
}

void GpuTexture::Release() {
    if (auto owner = owner_.lock()) {
        Vk::TextureRecord record;
        if (owner->textures.Remove(handle_, record))
            owner->Retire({0, VK_NULL_HANDLE, record.image, record.view, record.allocation,
                           record.pool, record.set});
    }
    owner_.reset();
    handle_ = 0;
    width_ = height_ = 0;
    colorSpace_ = TextureColorSpace::Linear;
}

bool GpuTexture::IsValid() const {
    auto owner = owner_.lock();
    return owner && owner->textures.Get(handle_) != nullptr;
}

bool GpuTexture::BelongsTo(const GpuDevice& device) const {
    auto owner = owner_.lock();
    return owner && owner == device.state_ && owner->textures.Get(handle_) != nullptr;
}

} // namespace LuxEngine
