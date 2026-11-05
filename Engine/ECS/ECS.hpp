#pragma once

#include "Engine/Core/Core.hpp"
#include "Engine/Core/Math.hpp"
#include "Engine/Renderer/Renderer3D/Renderer3D.hpp"
#include <string>

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

struct SpriteRendererComponent {
    Vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
};

struct MeshRendererComponent {
    Material3D material;
};

struct CameraComponent {
    bool isPrimary = true;
    bool isPerspective = false;
    float fov = 45.0f;
    float orthoSize = 10.0f;
};

} // namespace LuxEngine
