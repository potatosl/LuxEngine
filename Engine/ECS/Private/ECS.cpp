#include <LuxEngine/ECS/ECS.hpp>

namespace LuxEngine {
Mat4 TransformComponent::GetTransform() const {
    return Mat4::Translate(position) * Mat4::RotateZ(rotation.z) * Mat4::RotateY(rotation.y) *
           Mat4::RotateX(rotation.x) * Mat4::Scale(scale);
}

uint8_t TilemapComponent::Get(uint32 x, uint32 y) const {
    return x < width && y < height && y * width + x < tiles.size() ? tiles[y * width + x] : 0;
}

bool TilemapComponent::Set(uint32 x, uint32 y, uint8_t tile) {
    if (x >= width || y >= height || y * width + x >= tiles.size() || tile > 5)
        return false;
    tiles[y * width + x] = tile;
    return true;
}

bool TilemapComponent::Resize(uint32 newWidth, uint32 newHeight) {
    if (newWidth == 0 || newHeight == 0 || newWidth > 256 || newHeight > 256)
        return false;
    std::vector<uint8_t> next(newWidth * newHeight, 0);
    for (uint32 y = 0; y < std::min(height, newHeight); ++y)
        for (uint32 x = 0; x < std::min(width, newWidth); ++x)
            next[y * newWidth + x] = Get(x, y);
    width = newWidth;
    height = newHeight;
    tiles.swap(next);
    return true;
}
} // namespace LuxEngine
