#include "RHI/RenderBackend.hpp"
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
    uint64 handle = 0;
    if (!device->backend_->CreateTexture(width, height, pixelData, filter, colorSpace, handle))
        return false;
    handle_ = handle;
    owner_ = device->backend_;
    width_ = width;
    height_ = height;
    colorSpace_ = colorSpace;
    return true;
}

void GpuTexture::Release() {
    if (auto owner = owner_.lock())
        owner->DestroyTexture(handle_);
    owner_.reset();
    handle_ = 0;
    width_ = height_ = 0;
    colorSpace_ = TextureColorSpace::Linear;
}

bool GpuTexture::IsValid() const {
    auto owner = owner_.lock();
    return owner && owner->HasTexture(handle_);
}

bool GpuTexture::BelongsTo(const GpuDevice& device) const {
    auto owner = owner_.lock();
    return owner && owner == device.backend_ && owner->HasTexture(handle_);
}

} // namespace LuxEngine
