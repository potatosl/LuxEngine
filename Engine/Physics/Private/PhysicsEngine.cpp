#include <LuxEngine/Core/Log.hpp>
#include <LuxEngine/Physics/PhysicsEngine.hpp>
#include <cmath>

namespace LuxEngine {

namespace {
constexpr float kMaxCoordinate = 100'000.0f;

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
    Log::Info("Jolt 물리 시스템을 초기화했습니다.");
}

void PhysicsEngine::Shutdown() {
    s_PhysicsStorage = PhysicsStorage{};
    Log::Info("Jolt 물리 시스템을 종료했습니다.");
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

} // namespace LuxEngine
