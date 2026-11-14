#pragma once

#include <LuxEngine/ECS/ECS.hpp>
#include <LuxEngine/Physics/PhysicsWorld.hpp>
#include <unordered_map>

namespace LuxEngine {
class Scene;

class ScenePhysicsWorld final {
public:
    explicit ScenePhysicsWorld(bool planar);
    void Step(Scene& scene, double dt);
    void Remove(uint32 entity);
    std::unordered_map<uint32, TransformComponent> GetWorldPoses(const Scene& scene) const;
    const std::vector<Collision3DEvent>& BeganContacts() const;

private:
    bool planar_;
    PhysicsWorld world_;
    std::vector<uint32> active_;
};
} // namespace LuxEngine
