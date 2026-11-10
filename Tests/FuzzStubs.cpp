#include "Engine/Core/Log.hpp"
#include "Engine/Physics/PhysicsWorld2D.h"
#include "Engine/Renderer/Renderer2D/Renderer2D.hpp"
#include "Engine/Renderer/Renderer3D/Renderer3D.hpp"
#include "Engine/Scene/Scene.hpp"

namespace __coe_win {
bool ContinueOnError() {
    return false;
}
void RawWrite(const char*) {}
} // namespace __coe_win

namespace LuxEngine {
struct PhysicsWorld2D::Impl {};
PhysicsWorld2D::PhysicsWorld2D() = default;
PhysicsWorld2D::~PhysicsWorld2D() = default;
void PhysicsWorld2D::Remove(uint32) {}
void PhysicsWorld2D::Step(Scene&, double, std::vector<Collision2DEvent>&) {}

void Renderer2D::DrawQuad(const Vec3&, const Vec2&, const Vec4&) {}
void Renderer2D::DrawRotatedQuad(const Vec2&, const Vec2&, float, const Vec4&) {}

void Renderer3D::DrawMesh(const Mat4&, const Material3D&) {}

void Log::Error(std::string_view) {}
void Log::Warn(std::string_view) {}
void Log::Info(std::string_view) {}
} // namespace LuxEngine
