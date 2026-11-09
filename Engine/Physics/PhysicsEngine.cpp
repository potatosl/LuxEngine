#include "Engine/Physics/PhysicsEngine.hpp"
#include "Engine/Core/Log.hpp"
#include <algorithm>
#include <cmath>

namespace LuxEngine {

namespace {
constexpr float kMaxCoordinate = 100'000.0f;
constexpr float kMaxGravityScale = 10'000.0f;

bool Finite(const Vec2& v) {
    return std::isfinite(v.x) && std::isfinite(v.y);
}
bool Finite(const Vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
bool Bounded(const Vec2& v, float limit) {
    return Finite(v) && std::abs(v.x) <= limit && std::abs(v.y) <= limit;
}
bool Bounded(const Vec3& v, float limit) {
    return Finite(v) && std::abs(v.x) <= limit && std::abs(v.y) <= limit && std::abs(v.z) <= limit;
}
} // namespace

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
    if (!Bounded(gravity, kMaxCoordinate))
        return;
    s_PhysicsStorage.gravity2D = gravity;
}

Vec2 PhysicsEngine::GetGravity2D() {
    return s_PhysicsStorage.gravity2D;
}

void PhysicsEngine::SetGravity3D(const Vec3& gravity) {
    if (!Bounded(gravity, kMaxCoordinate))
        return;
    s_PhysicsStorage.gravity3D = gravity;
}

Vec3 PhysicsEngine::GetGravity3D() {
    return s_PhysicsStorage.gravity3D;
}

void PhysicsEngine::Step2D(Vec3& position, Rigidbody2DComponent& rb,
                           const BoxCollider2DComponent& collider, double dt) {
    if (!std::isfinite(dt) || dt <= 0.0 || dt > 0.25 || rb.type == BodyType::Static)
        return;
    if (!Finite(position) || !Bounded(rb.velocity, kMaxCoordinate) ||
        !std::isfinite(rb.gravityScale) || std::abs(rb.gravityScale) > kMaxGravityScale ||
        !Bounded(collider.size, kMaxCoordinate) || collider.size.x <= 0.0f ||
        collider.size.y <= 0.0f || !Bounded(collider.offset, kMaxCoordinate)) {
        rb.isGrounded = false;
        return;
    }

    const float fdt = static_cast<float>(dt);
    Vec2 nextVelocity = rb.velocity;
    if (rb.type == BodyType::Dynamic) {
        nextVelocity.x += s_PhysicsStorage.gravity2D.x * rb.gravityScale * fdt;
        nextVelocity.y += s_PhysicsStorage.gravity2D.y * rb.gravityScale * fdt;
    }
    if (!Bounded(nextVelocity, kMaxCoordinate)) {
        rb.isGrounded = false;
        return;
    }
    rb.velocity = nextVelocity;

    Vec3 nextPosition = position;
    nextPosition.x += rb.velocity.x * fdt;
    nextPosition.y += rb.velocity.y * fdt;
    if (!Bounded(nextPosition, kMaxCoordinate)) {
        rb.isGrounded = false;
        return;
    }
    position = nextPosition;

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
    if (!Finite(position) || !Bounded(rb.velocity, kMaxCoordinate) ||
        !std::isfinite(rb.gravityScale) || std::abs(rb.gravityScale) > kMaxGravityScale ||
        !Bounded(collider.size, kMaxCoordinate) || collider.size.x <= 0.0f ||
        collider.size.y <= 0.0f || collider.size.z <= 0.0f ||
        !Bounded(collider.offset, kMaxCoordinate))
        return;

    const float fdt = static_cast<float>(dt);
    Vec3 nextVelocity = rb.velocity;
    if (rb.type == BodyType::Dynamic) {
        nextVelocity.x += s_PhysicsStorage.gravity3D.x * rb.gravityScale * fdt;
        nextVelocity.y += s_PhysicsStorage.gravity3D.y * rb.gravityScale * fdt;
        nextVelocity.z += s_PhysicsStorage.gravity3D.z * rb.gravityScale * fdt;
    }
    if (!Bounded(nextVelocity, kMaxCoordinate))
        return;
    rb.velocity = nextVelocity;

    Vec3 nextPosition = position;
    nextPosition.x += rb.velocity.x * fdt;
    nextPosition.y += rb.velocity.y * fdt;
    nextPosition.z += rb.velocity.z * fdt;
    if (!Bounded(nextPosition, kMaxCoordinate))
        return;
    position = nextPosition;

    const float bottomY = position.y + collider.offset.y - (collider.size.y * 0.5f);
    if (!collider.isTrigger && bottomY <= 0.0f && rb.velocity.y <= 0.0f) {
        position.y = (collider.size.y * 0.5f) - collider.offset.y;
        if (rb.velocity.y < 0.0f)
            rb.velocity.y = 0.0f;
    }
}

} // namespace LuxEngine
