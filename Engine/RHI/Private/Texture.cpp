#include <LuxEngine/Renderer/RHI/Texture.hpp>

namespace LuxEngine {
bool GpuTexture::Initialize(std::nullptr_t, uint32, uint32, const void*, TextureFilter,
                            TextureColorSpace) {
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

GpuTexture::GpuTexture() = default;
} // namespace LuxEngine
