#include <LuxEngine/Renderer/RHI/Texture.hpp>

namespace LuxEngine {
bool GpuTexture::Initialize(std::nullptr_t, uint32, uint32, const void*, TextureFilter,
                            TextureColorSpace, TextureSampling) {
    Release();
    return false;
}

uint32 GpuTexture::GetWidth() const {
    return IsValid() ? width_ : 0;
}

uint32 GpuTexture::GetHeight() const {
    return IsValid() ? height_ : 0;
}

TextureColorSpace GpuTexture::GetColorSpace() const noexcept {
    return colorSpace_;
}

uint32 GpuTexture::GetMipCount() const noexcept {
    return IsValid() ? mipCount_ : 0;
}

size_t GpuTexture::GetStorageBytes() const noexcept {
    return IsValid() ? storageBytes_ : 0;
}

GpuTexture::GpuTexture() = default;
} // namespace LuxEngine
