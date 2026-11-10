#include "Engine/Physics/PhysicsWorld2D.h"
#include "Engine/Scene/Entity.hpp"
#include "Engine/Scene/Scene.hpp"
#include <box2d/box2d.h>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace LuxEngine {
namespace {
// Keep Box2D's float geometry and inertia calculations in a stable range.
constexpr float kMinFixtureSize = 0.02f;
constexpr float kMaxFixtureSize = 10'000.0f;
constexpr float kMaxCoordinate = 100'000.0f;
constexpr float kMinDynamicMass = 0.001f;
constexpr float kMaxDynamicMass = 1'000'000.0f;
bool Same(Vec2 a, Vec2 b) {
    return a.x == b.x && a.y == b.y;
}
bool Finite(Vec2 v) {
    return std::isfinite(v.x) && std::isfinite(v.y);
}
bool Bounded(Vec2 v, float limit) {
    return Finite(v) && std::abs(v.x) <= limit && std::abs(v.y) <= limit;
}
struct Geometry {
    Vec2 size;
    Vec2 offset;
};
Vec2 Rotate(Vec2 point, float angle) {
    const float c = std::cos(angle), s = std::sin(angle);
    return {c * point.x - s * point.y, s * point.x + c * point.y};
}
Vec2 BodyPosition(Vec2 entityPosition, const Geometry& geometry, float angle) {
    return entityPosition + Rotate(geometry.offset, angle);
}
Vec2 EntityPosition(Vec2 bodyPosition, const Geometry& geometry, float angle) {
    return bodyPosition - Rotate(geometry.offset, angle);
}

b2BodyType ToBox2D(BodyType type) {
    switch (type) {
    case BodyType::Static:
        return b2_staticBody;
    case BodyType::Kinematic:
        return b2_kinematicBody;
    case BodyType::Dynamic:
        return b2_dynamicBody;
    }
    return b2_staticBody;
}

bool MakeGeometry(const TransformComponent& transform, const BoxCollider2DComponent& collider,
                  Geometry& result) {
    if (!Finite(collider.size) || !Finite(collider.offset) || !std::isfinite(transform.scale.x) ||
        !std::isfinite(transform.scale.y) || collider.size.x <= 0.0f || collider.size.y <= 0.0f)
        return false;
    result.size = {std::abs(collider.size.x * transform.scale.x),
                   std::abs(collider.size.y * transform.scale.y)};
    result.offset = {collider.offset.x * transform.scale.x, collider.offset.y * transform.scale.y};
    return Finite(result.size) && Bounded(result.offset, kMaxCoordinate) &&
           result.size.x >= kMinFixtureSize && result.size.y >= kMinFixtureSize &&
           result.size.x <= kMaxFixtureSize && result.size.y <= kMaxFixtureSize;
}

struct ContactCollector final : b2ContactListener {
    std::vector<Collision2DEvent>* events = nullptr;

    void BeginContact(b2Contact* contact) override {
        if (!events)
            return;
        auto* fixtureA = contact->GetFixtureA();
        auto* fixtureB = contact->GetFixtureB();
        Collision2DEvent event;
        event.entityA = static_cast<uint32>(fixtureA->GetBody()->GetUserData().pointer);
        event.entityB = static_cast<uint32>(fixtureB->GetBody()->GetUserData().pointer);
        event.isTrigger = fixtureA->IsSensor() || fixtureB->IsSensor();
        if (!event.isTrigger) {
            b2WorldManifold manifold;
            contact->GetWorldManifold(&manifold);
            event.normal = {manifold.normal.x, manifold.normal.y};
        }
        events->push_back(event);
    }
};
} // namespace

struct PhysicsWorld2D::Impl {
    struct BodyState {
        b2Body* body = nullptr;
        BodyType type = BodyType::Static;
        Geometry geometry{};
        float mass = 0.0f;
        bool trigger = false;
        Vec2 position{};
        float angle = 0.0f;
        Vec2 velocity{};
    };

    b2World world{{0.0f, -9.81f}};
    ContactCollector contacts;
    std::unordered_map<uint32, BodyState> bodies;

    Impl() {
        world.SetContactListener(&contacts);
    }
    ~Impl() {
        world.SetContactListener(nullptr);
    }

    void Remove(uint32 id) {
        const auto it = bodies.find(id);
        if (it == bodies.end())
            return;
        world.DestroyBody(it->second.body);
        bodies.erase(it);
    }

    void WakeDynamics() {
        for (auto& entry : bodies) {
            auto& state = entry.second;
            if (state.type == BodyType::Dynamic)
                state.body->SetAwake(true);
        }
    }

    void Add(uint32 id, const TransformComponent& transform, const Rigidbody2DComponent* rb,
             const BoxCollider2DComponent& collider, const Geometry& geometry) {
        const BodyType type = rb ? rb->type : BodyType::Static;
        b2BodyDef bodyDef;
        bodyDef.type = ToBox2D(type);
        const Vec2 center = BodyPosition({transform.position.x, transform.position.y}, geometry,
                                         transform.rotation.z);
        bodyDef.position.Set(center.x, center.y);
        bodyDef.angle = transform.rotation.z;
        bodyDef.userData.pointer = id;
        if (rb) {
            bodyDef.linearVelocity.Set(rb->velocity.x, rb->velocity.y);
            bodyDef.gravityScale = rb->gravityScale;
        }
        b2Body* body = world.CreateBody(&bodyDef);
        b2PolygonShape shape;
        shape.SetAsBox(geometry.size.x * 0.5f, geometry.size.y * 0.5f);
        b2FixtureDef fixture;
        fixture.shape = &shape;
        fixture.isSensor = collider.isTrigger;
        if (type == BodyType::Dynamic)
            fixture.density = rb->mass / (geometry.size.x * geometry.size.y);
        body->CreateFixture(&fixture);
        if (type != BodyType::Dynamic)
            WakeDynamics();

        BodyState state;
        state.body = body;
        state.type = type;
        state.geometry = geometry;
        state.mass = rb ? rb->mass : 0.0f;
        state.trigger = collider.isTrigger;
        state.position = {transform.position.x, transform.position.y};
        state.angle = transform.rotation.z;
        state.velocity = rb ? rb->velocity : Vec2{};
        bodies.emplace(id, state);
    }
};

PhysicsWorld2D::PhysicsWorld2D() : impl_(std::make_unique<Impl>()) {}
PhysicsWorld2D::~PhysicsWorld2D() = default;

void PhysicsWorld2D::Remove(uint32 id) {
    impl_->Remove(id);
}

std::unordered_map<uint32, TransformComponent>
PhysicsWorld2D::GetWorldPoses(const Scene& scene) const {
    std::unordered_map<uint32, TransformComponent> poses;
    poses.reserve(impl_->bodies.size());
    for (const auto& [id, state] : impl_->bodies) {
        if (!scene.HasComponent<TransformComponent>(id))
            continue;
        auto world = scene.GetWorldTransform(id);
        world.rotation.z = state.body->GetAngle();
        const auto center = state.body->GetPosition();
        const auto position =
            EntityPosition({center.x, center.y}, state.geometry, world.rotation.z);
        world.position.x = position.x;
        world.position.y = position.y;
        poses.emplace(id, world);
    }
    return poses;
}

void PhysicsWorld2D::Step(Scene& scene, double dt, std::vector<Collision2DEvent>& events) {
    events.clear();
    if (!std::isfinite(dt) || dt <= 0.0 || dt > 0.25)
        return;

    const Vec2 gravity = scene.GetSettings().overrideGravity ? scene.GetSettings().gravity
                                                             : PhysicsEngine::GetGravity2D();
    if (!Bounded(gravity, kMaxCoordinate))
        return;
    const b2Vec2 previousGravity = impl_->world.GetGravity();
    if (previousGravity.x != gravity.x || previousGravity.y != gravity.y) {
        impl_->world.SetGravity({gravity.x, gravity.y});
        impl_->WakeDynamics();
    }

    std::unordered_set<uint32> active;
    for (Entity& entity : scene.GetEntities()) {
        const uint32 id = entity.GetId();
        if (!entity.HasComponent<TransformComponent>() ||
            !entity.HasComponent<BoxCollider2DComponent>())
            continue;
        if (!entity.HasComponent<Rigidbody2DComponent>() &&
            entity.HasComponent<Rigidbody3DComponent>() &&
            entity.HasComponent<BoxCollider3DComponent>())
            continue;
        const auto transform = scene.GetWorldTransform(id);
        const auto& collider = entity.GetComponent<BoxCollider2DComponent>();
        auto* rb = entity.HasComponent<Rigidbody2DComponent>()
                       ? &entity.GetComponent<Rigidbody2DComponent>()
                       : nullptr;
        Geometry geometry;
        const Vec2 entityPosition{transform.position.x, transform.position.y};
        const bool valid =
            MakeGeometry(transform, collider, geometry) &&
            Bounded(entityPosition, kMaxCoordinate) && std::isfinite(transform.rotation.z) &&
            Bounded(BodyPosition(entityPosition, geometry, transform.rotation.z), kMaxCoordinate) &&
            (!rb || (Bounded(rb->velocity, kMaxCoordinate) && std::isfinite(rb->gravityScale) &&
                     std::abs(rb->gravityScale) <= 10'000.0f &&
                     (rb->type != BodyType::Dynamic ||
                      (std::isfinite(rb->mass) && rb->mass >= kMinDynamicMass &&
                       rb->mass <= kMaxDynamicMass))));
        if (!valid) {
            impl_->Remove(id);
            if (rb)
                rb->isGrounded = false;
            continue;
        }
        active.insert(id);
        const BodyType type = rb ? rb->type : BodyType::Static;
        auto it = impl_->bodies.find(id);
        if (it != impl_->bodies.end()) {
            const auto& state = it->second;
            if (state.type != type || !Same(state.geometry.size, geometry.size) ||
                !Same(state.geometry.offset, geometry.offset) ||
                state.trigger != collider.isTrigger ||
                (type == BodyType::Dynamic && state.mass != rb->mass)) {
                impl_->Remove(id);
                it = impl_->bodies.end();
            }
        }
        if (it == impl_->bodies.end()) {
            impl_->Add(id, transform, rb, collider, geometry);
            continue;
        }
        auto& state = it->second;
        const Vec2 position{transform.position.x, transform.position.y};
        if (!Same(position, state.position) || transform.rotation.z != state.angle) {
            if (type != BodyType::Dynamic)
                impl_->WakeDynamics();
            const Vec2 center = BodyPosition(position, geometry, transform.rotation.z);
            state.body->SetTransform({center.x, center.y}, transform.rotation.z);
            if (type == BodyType::Dynamic)
                state.body->SetAwake(true);
        }
        if (rb) {
            if (!Same(rb->velocity, state.velocity))
                state.body->SetLinearVelocity({rb->velocity.x, rb->velocity.y});
            if (state.body->GetGravityScale() != rb->gravityScale) {
                state.body->SetGravityScale(rb->gravityScale);
                state.body->SetAwake(true);
            }
        }
    }

    for (auto it = impl_->bodies.begin(); it != impl_->bodies.end();) {
        if (!active.contains(it->first)) {
            impl_->world.DestroyBody(it->second.body);
            it = impl_->bodies.erase(it);
        } else
            ++it;
    }

    impl_->contacts.events = &events;
    try {
        impl_->world.Step(static_cast<float>(dt), 8, 3);
    } catch (...) {
        impl_->contacts.events = nullptr;
        throw;
    }
    impl_->contacts.events = nullptr;

    std::unordered_map<uint32, TransformComponent> worldTransforms;
    worldTransforms.reserve(impl_->bodies.size());
    for (const auto& [id, state] : impl_->bodies)
        if (scene.HasComponent<TransformComponent>(id))
            worldTransforms.emplace(id, scene.GetWorldTransform(id));
    // A child is converted back to local coordinates only after its parent's new world pose is
    // applied. Unordered-map iteration would otherwise apply parent motion twice to some bodies.
    for (const auto& entity : scene.GetEntitiesInHierarchyOrder()) {
        const auto id = entity.GetId();
        const auto found = impl_->bodies.find(id);
        if (found == impl_->bodies.end() || !worldTransforms.contains(id))
            continue;
        auto& state = found->second;
        auto transform = worldTransforms.at(id);
        const b2Vec2 center = state.body->GetPosition();
        transform.rotation.z = state.body->GetAngle();
        state.position = EntityPosition({center.x, center.y}, state.geometry, transform.rotation.z);
        transform.position.x = state.position.x;
        transform.position.y = state.position.y;
        state.angle = transform.rotation.z;
        // Box2D integrates in world space. Converting back avoids double-applying a parent's
        // translation/rotation on the next frame and keeps static children attached to groups.
        if (!scene.SetWorldTransform(id, transform)) {
            state.body->SetLinearVelocity({0, 0});
            if (scene.HasComponent<Rigidbody2DComponent>(id))
                scene.GetComponent<Rigidbody2DComponent>(id).velocity = {};
            continue;
        }
        if (scene.HasComponent<Rigidbody2DComponent>(id)) {
            auto& rb = scene.GetComponent<Rigidbody2DComponent>(id);
            const b2Vec2 velocity = state.body->GetLinearVelocity();
            rb.velocity = {velocity.x, velocity.y};
            state.velocity = rb.velocity;
            rb.isGrounded = false;
            if (rb.type != BodyType::Dynamic)
                continue;
            for (const b2ContactEdge* edge = state.body->GetContactList(); edge;
                 edge = edge->next) {
                const b2Contact* contact = edge->contact;
                if (!contact->IsTouching() || contact->GetFixtureA()->IsSensor() ||
                    contact->GetFixtureB()->IsSensor())
                    continue;
                b2WorldManifold manifold;
                contact->GetWorldManifold(&manifold);
                const bool bodyA = contact->GetFixtureA()->GetBody() == state.body;
                if ((bodyA && manifold.normal.y < -0.5f) || (!bodyA && manifold.normal.y > 0.5f)) {
                    rb.isGrounded = true;
                    break;
                }
            }
        }
    }
}
} // namespace LuxEngine
