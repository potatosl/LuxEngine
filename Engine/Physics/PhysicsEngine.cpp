#include "Engine/Physics/PhysicsEngine.hpp"
#include "Engine/Core/Log.hpp"

namespace LuxEngine {

struct PhysicsStorage {
    Vec2 gravity2D{0.0f, -9.81f};
    Vec3 gravity3D{0.0f, -9.81f, 0.0f};
};

static PhysicsStorage s_PhysicsStorage;

void PhysicsEngine::Init() {
    s_PhysicsStorage.gravity2D = Vec2(0.0f, -9.81f);
    s_PhysicsStorage.gravity3D = Vec3(0.0f, -9.81f, 0.0f);
    Log::Info("PhysicsEngine initialized (2D & 3D dynamics enabled).");
}

void PhysicsEngine::Shutdown() {
    s_PhysicsStorage = PhysicsStorage{};
    Log::Info("PhysicsEngine shutdown.");
}

void PhysicsEngine::SetGravity2D(const Vec2& gravity) {
    s_PhysicsStorage.gravity2D = gravity;
}

Vec2 PhysicsEngine::GetGravity2D() {
    return s_PhysicsStorage.gravity2D;
}

void PhysicsEngine::SetGravity3D(const Vec3& gravity) {
    s_PhysicsStorage.gravity3D = gravity;
}

Vec3 PhysicsEngine::GetGravity3D() {
    return s_PhysicsStorage.gravity3D;
}

void PhysicsEngine::Step2D(Vec3& position, Rigidbody2DComponent& rb,
                           const BoxCollider2DComponent& collider, double dt) {
    if (!std::isfinite(dt) || dt <= 0.0 || dt > 0.25 || rb.type == BodyType::Static)
        return;

    const float fdt = static_cast<float>(dt);
    if (rb.type == BodyType::Dynamic) {
        rb.velocity.x += s_PhysicsStorage.gravity2D.x * rb.gravityScale * fdt;
        rb.velocity.y += s_PhysicsStorage.gravity2D.y * rb.gravityScale * fdt;
    }

    position.x += rb.velocity.x * fdt;
    position.y += rb.velocity.y * fdt;

    // Ground floor collision plane at y = 0
    const float bottomY = position.y + collider.offset.y - (collider.size.y * 0.5f);
    if (!collider.isTrigger && bottomY <= 0.0f && rb.velocity.y <= 0.0f) {
        position.y = (collider.size.y * 0.5f) - collider.offset.y;
        if (rb.velocity.y < 0.0f)
            rb.velocity.y = 0.0f;
        rb.isGrounded = true;
    } else {
        rb.isGrounded = false;
    }
}

void PhysicsEngine::Step3D(Vec3& position, Rigidbody3DComponent& rb,
                           const BoxCollider3DComponent& collider, double dt) {
    if (!std::isfinite(dt) || dt <= 0.0 || dt > 0.25 || rb.type == BodyType::Static)
        return;

    const float fdt = static_cast<float>(dt);
    if (rb.type == BodyType::Dynamic) {
        rb.velocity.x += s_PhysicsStorage.gravity3D.x * rb.gravityScale * fdt;
        rb.velocity.y += s_PhysicsStorage.gravity3D.y * rb.gravityScale * fdt;
        rb.velocity.z += s_PhysicsStorage.gravity3D.z * rb.gravityScale * fdt;
    }

    position.x += rb.velocity.x * fdt;
    position.y += rb.velocity.y * fdt;
    position.z += rb.velocity.z * fdt;

    const float bottomY = position.y + collider.offset.y - (collider.size.y * 0.5f);
    if (!collider.isTrigger && bottomY <= 0.0f && rb.velocity.y <= 0.0f) {
        position.y = (collider.size.y * 0.5f) - collider.offset.y;
        if (rb.velocity.y < 0.0f)
            rb.velocity.y = 0.0f;
    }
}

} // namespace LuxEngine
