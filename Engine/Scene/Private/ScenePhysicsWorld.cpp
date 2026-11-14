#include "ScenePhysicsWorld.hpp"
#include <LuxEngine/Scene/Entity.hpp>
#include <LuxEngine/Scene/Scene.hpp>
#include <cmath>

namespace LuxEngine {

ScenePhysicsWorld::ScenePhysicsWorld(bool planar) : planar_(planar), world_(planar) {}

void ScenePhysicsWorld::Remove(uint32 entity) {
    world_.Remove(entity);
}

void ScenePhysicsWorld::Step(Scene& scene, double dt) {
    std::vector<PhysicsBodyDescription> descriptions;
    active_.clear();
    for (const auto& entity : scene.GetEntitiesInHierarchyOrder()) {
        const auto id = entity.GetId();
        if (!scene.HasComponent<TransformComponent>(id))
            continue;
        const bool is2D = scene.HasComponent<BoxCollider2DComponent>(id) &&
                          (scene.HasComponent<Rigidbody2DComponent>(id) ||
                           !scene.HasComponent<BoxCollider3DComponent>(id));
        if (planar_ != is2D || (!planar_ && !scene.HasComponent<BoxCollider3DComponent>(id)))
            continue;
        const auto transform = scene.GetWorldTransform(id);
        PhysicsBodyDescription body;
        body.entity = id;
        body.position = transform.position;
        body.rotation = transform.rotation;
        if (planar_) {
            const auto& collider = scene.GetComponent<BoxCollider2DComponent>(id);
            body.size = {std::abs(collider.size.x * transform.scale.x),
                         std::abs(collider.size.y * transform.scale.y), .2f};
            body.offset = {collider.offset.x * transform.scale.x,
                           collider.offset.y * transform.scale.y, 0};
            body.trigger = collider.isTrigger;
            if (auto* rigid = scene.TryGetComponent<Rigidbody2DComponent>(id)) {
                body.type = rigid->type;
                body.velocity = {rigid->velocity.x, rigid->velocity.y, 0};
                body.gravityScale = rigid->gravityScale;
                body.mass = rigid->mass;
                rigid->isGrounded = false;
            }
        } else {
            const auto& collider = scene.GetComponent<BoxCollider3DComponent>(id);
            body.size = {std::abs(collider.size.x * transform.scale.x),
                         std::abs(collider.size.y * transform.scale.y),
                         std::abs(collider.size.z * transform.scale.z)};
            body.offset = {collider.offset.x * transform.scale.x,
                           collider.offset.y * transform.scale.y,
                           collider.offset.z * transform.scale.z};
            body.trigger = collider.isTrigger;
            if (const auto* rigid = scene.TryGetComponent<Rigidbody3DComponent>(id)) {
                body.type = rigid->type;
                body.velocity = rigid->velocity;
                body.gravityScale = rigid->gravityScale;
                body.mass = rigid->mass;
            }
        }
        descriptions.push_back(body);
        active_.push_back(id);
    }
    world_.Synchronize(descriptions);
    const auto& settings = scene.GetSettings();
    const auto gravity2D =
        settings.overrideGravity ? settings.gravity : PhysicsEngine::GetGravity2D();
    world_.Step(dt, planar_                    ? Vec3{gravity2D.x, gravity2D.y, 0}
                    : settings.overrideGravity ? settings.gravity3D
                                               : PhysicsEngine::GetGravity3D());
    for (const auto id : active_) {
        const auto state = world_.GetState(id);
        if (!state)
            continue;
        if (planar_) {
            if (auto* rigid = scene.TryGetComponent<Rigidbody2DComponent>(id)) {
                rigid->velocity = {state->velocity.x, state->velocity.y};
                rigid->isGrounded = state->grounded;
            }
        } else if (auto* rigid = scene.TryGetComponent<Rigidbody3DComponent>(id))
            rigid->velocity = state->velocity;
    }
}

std::unordered_map<uint32, TransformComponent>
ScenePhysicsWorld::GetWorldPoses(const Scene& scene) const {
    std::unordered_map<uint32, TransformComponent> poses;
    for (const auto id : active_) {
        const auto state = world_.GetState(id);
        if (!state || !scene.IsAlive(id))
            continue;
        auto transform = scene.GetWorldTransform(id);
        if (planar_) {
            transform.position.x = state->position.x;
            transform.position.y = state->position.y;
            transform.rotation.z = state->rotation.z;
        } else {
            transform.position = state->position;
            transform.rotation = state->rotation;
        }
        poses.emplace(id, transform);
    }
    return poses;
}

const std::vector<Collision3DEvent>& ScenePhysicsWorld::BeganContacts() const {
    return world_.BeganContacts();
}

} // namespace LuxEngine
