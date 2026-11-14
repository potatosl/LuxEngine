#include "RHI/RenderBackend.hpp"
#include <LuxEngine/Renderer/RHI/GpuDevice.hpp>
#include <LuxEngine/Renderer/RHI/Texture.hpp>

namespace LuxEngine {

GpuTexture::~GpuTexture() {
    Release();
}

GpuTexture::GpuTexture(GpuTexture&& other) noexcept
    : owner_(std::move(other.owner_)), handle_(other.handle_), width_(other.width_),
      height_(other.height_), mipCount_(other.mipCount_), storageBytes_(other.storageBytes_),
      colorSpace_(other.colorSpace_) {
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
        mipCount_ = other.mipCount_;
        storageBytes_ = other.storageBytes_;
        other.owner_.reset();
        other.handle_ = 0;
        other.width_ = other.height_ = 0;
        other.colorSpace_ = TextureColorSpace::Linear;
    }
    return *this;
}

bool GpuTexture::Initialize(GpuDevice* device, uint32 width, uint32 height, const void* pixelData,
                            TextureFilter filter, TextureColorSpace colorSpace,
                            TextureSampling sampling) {
    Release();
    if (!device || !device->IsReady())
        return false;
    if (!width || !height || width > 16384 || height > 16384 ||
        (filter != TextureFilter::Linear && filter != TextureFilter::Nearest) ||
        (colorSpace != TextureColorSpace::Linear && colorSpace != TextureColorSpace::Srgb) ||
        (sampling.normalMap && colorSpace == TextureColorSpace::Srgb) ||
        (sampling.addressMode != TextureAddressMode::Clamp &&
         sampling.addressMode != TextureAddressMode::Repeat))
        return false;
    std::vector<TextureMipLevel> levels;
    std::vector<uint8> chain;
    uint32 mipLevels = 1;
    size_t bytes = static_cast<size_t>(width) * height * 4;
    if (sampling.mipmaps) {
        std::vector<uint8> zeros;
        if (!pixelData)
            zeros.resize(bytes);
        const auto* source = pixelData ? static_cast<const uint8*>(pixelData) : zeros.data();
        if (!BuildTextureMipChain(width, height, std::span(source, bytes), colorSpace,
                                  sampling.normalMap, levels))
            return false;
        for (const auto& level : levels)
            chain.insert(chain.end(), level.pixels.begin(), level.pixels.end());
        pixelData = chain.data();
        mipLevels = static_cast<uint32>(levels.size());
        bytes = chain.size();
    }
    uint64 handle = 0;
    if (!device->backend_->CreateTexture(width, height, pixelData, filter, colorSpace, mipLevels,
                                         sampling, handle))
        return false;
    handle_ = handle;
    owner_ = device->backend_;
    width_ = width;
    height_ = height;
    colorSpace_ = colorSpace;
    mipCount_ = mipLevels;
    storageBytes_ = mipLevels == 1 ? static_cast<size_t>(width == 1 ? 2 : width) *
                                         (height == 1 ? 2 : height) * 4
                                   : bytes;
    return true;
}

void GpuTexture::Release() {
    if (auto owner = owner_.lock())
        owner->DestroyTexture(handle_);
    owner_.reset();
    handle_ = 0;
    width_ = height_ = 0;
    colorSpace_ = TextureColorSpace::Linear;
    mipCount_ = 0;
    storageBytes_ = 0;
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
