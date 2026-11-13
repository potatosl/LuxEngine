#pragma once

#include <LuxEngine/Core/Core.hpp>
#include <cstddef>
#include <memory>

namespace LuxEngine {
class GpuDevice;

namespace Rhi {
class RenderBackend;
}

// Linear is the default for smooth artwork; Nearest preserves pixel-art texel edges.
enum class TextureFilter { Linear, Nearest };
enum class TextureColorSpace { Linear, Srgb };

class LUX_API GpuTexture {
public:
    GpuTexture();
    ~GpuTexture();

    GpuTexture(const GpuTexture&) = delete;
    GpuTexture& operator=(const GpuTexture&) = delete;
    GpuTexture(GpuTexture&& other) noexcept;
    GpuTexture& operator=(GpuTexture&& other) noexcept;

    bool Initialize(GpuDevice* device, uint32 width, uint32 height, const void* pixelData = nullptr,
                    TextureFilter filter = TextureFilter::Linear,
                    TextureColorSpace colorSpace = TextureColorSpace::Linear);

    bool Initialize(std::nullptr_t, uint32, uint32, const void* = nullptr,
                    TextureFilter = TextureFilter::Linear,
                    TextureColorSpace = TextureColorSpace::Linear);

    void Release();

    uint32 GetWidth() const;

    uint32 GetHeight() const;

    bool IsValid() const;
    bool BelongsTo(const GpuDevice& device) const;

    TextureColorSpace GetColorSpace() const noexcept;

private:
    friend class GpuDevice;
    friend class Renderer2D;
    friend class Renderer3D;
    std::weak_ptr<Rhi::RenderBackend> owner_;
    uint64 handle_ = 0;
    uint32 width_ = 0;
    uint32 height_ = 0;
    TextureColorSpace colorSpace_ = TextureColorSpace::Linear;
};

} // namespace LuxEngine
