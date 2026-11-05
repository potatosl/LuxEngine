#pragma once

#include "Engine/Core/Core.hpp"
#include "Engine/Core/Math.hpp"
#include <vector>

namespace LuxEngine {

    enum class BodyType {
        Static,
        Dynamic,
        Kinematic
    };

    struct Rigidbody2DComponent {
        BodyType type = BodyType::Dynamic;
        Vec2 velocity{ 0.0f, 0.0f };
        float mass = 1.0f;
        float gravityScale = 1.0f;
        bool isGrounded = false;
    };

    struct BoxCollider2DComponent {
        Vec2 size{ 1.0f, 1.0f };
        Vec2 offset{ 0.0f, 0.0f };
        bool isTrigger = false;
    };

    struct Rigidbody3DComponent {
        BodyType type = BodyType::Dynamic;
        Vec3 velocity{ 0.0f, 0.0f, 0.0f };
        float mass = 1.0f;
        float gravityScale = 1.0f;
    };

    struct BoxCollider3DComponent {
        Vec3 size{ 1.0f, 1.0f, 1.0f };
        Vec3 offset{ 0.0f, 0.0f, 0.0f };
        bool isTrigger = false;
    };

    struct Collision2DEvent {
        uint32 entityA = 0;
        uint32 entityB = 0;
        Vec2 normal{ 0.0f, 0.0f };
    };

    class LUX_API PhysicsEngine {
    public:
        static void Init();
        static void Shutdown();

        static void SetGravity2D(const Vec2& gravity);
        static Vec2 GetGravity2D();

        static void SetGravity3D(const Vec3& gravity);
        static Vec3 GetGravity3D();

        static void Step2D(Vec3& position, Rigidbody2DComponent& rb, const BoxCollider2DComponent& collider, double dt);
        static void Step3D(Vec3& position, Rigidbody3DComponent& rb, const BoxCollider3DComponent& collider, double dt);
    };

} // namespace LuxEngine
