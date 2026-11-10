#pragma once

#include "Engine/Core/Core.hpp"
#include <cstddef>
#include <memory>

namespace LuxEngine {
class GpuDevice;
namespace Vk {
struct DeviceState;
}

// Linear is the default for smooth artwork; Nearest preserves pixel-art texel edges.
enum class TextureFilter { Linear, Nearest };

class LUX_API GpuTexture {
public:
    GpuTexture() = default;
    ~GpuTexture();

    GpuTexture(const GpuTexture&) = delete;
    GpuTexture& operator=(const GpuTexture&) = delete;
    GpuTexture(GpuTexture&& other) noexcept;
    GpuTexture& operator=(GpuTexture&& other) noexcept;

    bool Initialize(GpuDevice* device, uint32 width, uint32 height, const void* pixelData = nullptr,
                    TextureFilter filter = TextureFilter::Linear);
    bool Initialize(std::nullptr_t, uint32, uint32, const void* = nullptr,
                    TextureFilter = TextureFilter::Linear) {
        Release();
        return false;
    }
    void Release();

    uint32 GetWidth() const {
        return IsValid() ? width_ : 0;
    }
    uint32 GetHeight() const {
        return IsValid() ? height_ : 0;
    }
    bool IsValid() const;
    bool BelongsTo(const GpuDevice& device) const;

private:
    friend class GpuDevice;
    friend class Renderer2D;
    std::weak_ptr<Vk::DeviceState> owner_;
    uint64 handle_ = 0;
    uint32 width_ = 0;
    uint32 height_ = 0;
};

} // namespace LuxEngine
