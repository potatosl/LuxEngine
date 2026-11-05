#pragma once

#include "Engine/Physics/PhysicsEngine.hpp"
#include <memory>
#include <vector>

namespace LuxEngine {
class Scene;

// Each Scene owns one persistent Box2D world. This class is kept separate from
// PhysicsEngine's standalone helpers so contacts can span multiple entities.
class PhysicsWorld2D final {
public:
    PhysicsWorld2D();
    ~PhysicsWorld2D();
    PhysicsWorld2D(const PhysicsWorld2D&) = delete;
    PhysicsWorld2D& operator=(const PhysicsWorld2D&) = delete;

    void Step(Scene& scene, double dt, std::vector<Collision2DEvent>& events);
    void Remove(uint32 entityId);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace LuxEngine
