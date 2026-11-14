#pragma once

#include <LuxEngine/Core/Core.hpp>
#include <LuxEngine/Core/Math.hpp>
#include <LuxEngine/ECS/ScriptFields.hpp>
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

    LUX_API Mat4 GetTransform() const;
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

    LUX_API uint8_t Get(uint32 x, uint32 y) const;

    LUX_API bool Set(uint32 x, uint32 y, uint8_t tile);

    LUX_API bool Resize(uint32 newWidth, uint32 newHeight);
};

struct MeshRendererComponent {
    Material3D material;
    std::string meshPath;
};

struct ScriptComponent {
    std::string typeName;
    bool enabled = true;
    ScriptFieldMap fields;
};

struct CameraComponent {
    bool isPrimary = true;
    bool isPerspective = false;
    float fov = 45.0f;
    float orthoSize = 10.0f;
};

} // namespace LuxEngine
