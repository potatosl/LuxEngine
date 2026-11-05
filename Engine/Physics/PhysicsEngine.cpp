#include "Engine/Physics/PhysicsEngine.hpp"
#include "Engine/Core/Log.hpp"

namespace LuxEngine {

    void PhysicsEngine::Init() {
        Log::Info("PhysicsEngine initialized (Prepared for Box2D & Jolt Physics).");
    }

    void PhysicsEngine::Shutdown() {
        Log::Info("PhysicsEngine shutdown.");
    }

    void PhysicsEngine::Step(double /*dt*/) {
    }

} // namespace LuxEngine
