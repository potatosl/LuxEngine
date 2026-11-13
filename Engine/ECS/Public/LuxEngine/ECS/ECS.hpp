#pragma once

#include <LuxEngine/Core/Core.hpp>
#include <LuxEngine/Core/Math.hpp>
#include <LuxEngine/Renderer/Renderer3D/Renderer3D.hpp>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace LuxEngine {

struct TagComponent {
    std::string tag = "Entity";
};

struct TransformComponent {
    Vec3 position{0.0f, 0.0f, 0.0f};
    Vec3 rotation{0.0f, 0.0f, 0.0f};
    Vec3 scale{1.0f, 1.0f, 1.0f};

    Mat4 GetTransform() const {
        return Mat4::Translate(position) * Mat4::RotateZ(rotation.z) * Mat4::RotateY(rotation.y) *
               Mat4::RotateX(rotation.x) * Mat4::Scale(scale);
    }
};

struct HierarchyComponent {
    uint32 parent = 0;
    bool group = false;
};

enum class SceneReferenceMode { Nested, Inherited, Additive };

// The reference root owns its imported descendants. The baseline records the source at the
// previous refresh so the scene manager can retain authored overrides when the source changes.
struct SceneReferenceComponent {
    std::string path;
    SceneReferenceMode mode = SceneReferenceMode::Nested;
    std::string baseline;
    std::string suspended;
    std::vector<std::pair<uint32, uint32>> entityMap;
    Vec2 streamCenter{};
    float streamLoadRadius = 0;
    float streamUnloadRadius = 0;
    bool loaded = true;
};

struct InstanceSourceComponent {
    uint32 instanceRoot = 0;
    uint32 sourceEntity = 0;
};

struct SpriteRendererComponent {
    Vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
};

struct TilemapComponent {
    uint32 width = 32;
    uint32 height = 16;
    float tileSize = 1.0f;
    std::vector<uint8_t> tiles = std::vector<uint8_t>(width * height, 0);

    uint8_t Get(uint32 x, uint32 y) const {
        return x < width && y < height && y * width + x < tiles.size() ? tiles[y * width + x] : 0;
    }

    bool Set(uint32 x, uint32 y, uint8_t tile) {
        if (x >= width || y >= height || y * width + x >= tiles.size() || tile > 5)
            return false;
        tiles[y * width + x] = tile;
        return true;
    }

    bool Resize(uint32 newWidth, uint32 newHeight) {
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
};

struct MeshRendererComponent {
    Material3D material;
    std::string meshPath;
};

struct CameraComponent {
    bool isPrimary = true;
    bool isPerspective = false;
    float fov = 45.0f;
    float orthoSize = 10.0f;
};

} // namespace LuxEngine
