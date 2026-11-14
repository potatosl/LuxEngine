// Jolt requires its platform definitions before every other Jolt header.
// clang-format off
#include <Jolt/Jolt.h>
// clang-format on
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>
#include <LuxEngine/Core/Log.hpp>
#include <LuxEngine/Physics/PhysicsWorld.hpp>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <map>
#include <set>
#include <stdexcept>

namespace LuxEngine {
namespace {
void JoltTrace(const char* format, ...) {
    char buffer[2048];
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    try {
        Log::Warn(buffer);
    } catch (...) {
    }
}
#ifdef JPH_ENABLE_ASSERTS
bool JoltAssertion(const char* expression, const char* message, const char*, JPH::uint) {
    try {
        Log::Error(std::string("Jolt 내부 검증 실패: ") + expression +
                   (message ? std::string(" / ") + message : std::string{}));
    } catch (...) {
    }
    return true;
}
#endif

struct JoltRuntime {
    JoltRuntime() {
        JPH::RegisterDefaultAllocator();
        JPH::Trace = JoltTrace;
#ifdef JPH_ENABLE_ASSERTS
        JPH::AssertFailed = JoltAssertion;
#endif
        JPH::Factory::sInstance = new JPH::Factory;
        JPH::RegisterTypes();
    }

    ~JoltRuntime() {
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }
};

void EnsureRuntime() {
    static JoltRuntime runtime;
}

bool Valid(Vec3 value, float limit = 100000.f) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) &&
           std::abs(value.x) <= limit && std::abs(value.y) <= limit && std::abs(value.z) <= limit;
}

bool Same(Vec3 a, Vec3 b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

JPH::Vec3 Vector(Vec3 v) {
    return {v.x, v.y, v.z};
}

Vec3 Vector(JPH::Vec3Arg v) {
    return {v.GetX(), v.GetY(), v.GetZ()};
}

struct Layers final : JPH::BroadPhaseLayerInterface {
    JPH::uint GetNumBroadPhaseLayers() const override {
        return 1;
    }

    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer) const override {
        return JPH::BroadPhaseLayer(0);
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer) const override {
        return "World";
    }
#endif
};

struct BroadFilter final : JPH::ObjectVsBroadPhaseLayerFilter {
    bool ShouldCollide(JPH::ObjectLayer, JPH::BroadPhaseLayer) const override {
        return true;
    }
};

struct PairFilter final : JPH::ObjectLayerPairFilter {
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        return a != 0 || b != 0;
    }
};
} // namespace

struct PhysicsWorld::Impl final : JPH::ContactListener {
    struct Entry {
        JPH::BodyID body;
        PhysicsBodyDescription description;
        PhysicsBodyState state;
    };

    bool planar;
    Layers layers;
    BroadFilter broadFilter;
    PairFilter pairFilter;
    JPH::PhysicsSystem system;
    JPH::TempAllocatorImpl allocator{16 * 1024 * 1024};
    JPH::JobSystemSingleThreaded jobs{JPH::cMaxPhysicsJobs};
    std::map<uint32, Entry> entries;
    std::vector<Collision3DEvent> contacts, began;

    explicit Impl(bool plane) : planar(plane) {
        system.Init(16384, 0, 65536, 16384, layers, broadFilter, pairFilter);
        system.SetContactListener(this);
    }

    ~Impl() override {
        system.SetContactListener(nullptr);
        while (!entries.empty())
            Remove(entries.begin()->first);
    }

    void Remove(uint32 id) {
        const auto it = entries.find(id);
        if (it == entries.end())
            return;
        auto& bodies = system.GetBodyInterface();
        bodies.RemoveBody(it->second.body);
        bodies.DestroyBody(it->second.body);
        entries.erase(it);
        const auto involves = [id](const auto& event) {
            return event.entityA == id || event.entityB == id;
        };
        std::erase_if(contacts, involves);
        std::erase_if(began, involves);
    }

    void Contact(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& manifold,
                 bool added) {
        Collision3DEvent event;
        event.entityA = static_cast<uint32>(a.GetUserData());
        event.entityB = static_cast<uint32>(b.GetUserData());
        event.normal = Vector(manifold.mWorldSpaceNormal);
        const auto point = manifold.GetWorldSpaceContactPointOn1(0);
        event.point = {static_cast<float>(point.GetX()), static_cast<float>(point.GetY()),
                       static_cast<float>(point.GetZ())};
        event.isTrigger = a.IsSensor() || b.IsSensor();
        contacts.push_back(event);
        if (added)
            began.push_back(event);
    }

    void OnContactAdded(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& m,
                        JPH::ContactSettings&) override {
        Contact(a, b, m, true);
    }

    void OnContactPersisted(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& m,
                            JPH::ContactSettings&) override {
        Contact(a, b, m, false);
    }
};

PhysicsWorld::PhysicsWorld(bool planar) {
    EnsureRuntime();
    impl_ = std::make_unique<Impl>(planar);
}

PhysicsWorld::~PhysicsWorld() = default;

bool PhysicsWorld::Synchronize(std::span<const PhysicsBodyDescription> input) {
    std::set<uint32> active;
    auto& bodies = impl_->system.GetBodyInterface();
    bool valid = true;
    for (auto description : input) {
        const auto id = description.entity;
        if (!id || active.contains(id) || !Valid(description.position) ||
            !Valid(description.rotation) || !Valid(description.velocity) ||
            !Valid(description.size, 10000) || !Valid(description.offset) ||
            description.size.x < .02f || description.size.y < .02f || description.size.z < .02f ||
            !std::isfinite(description.mass) || description.mass < .001f ||
            description.mass > 1000000 || !std::isfinite(description.gravityScale) ||
            std::abs(description.gravityScale) > 10000 || description.type < BodyType::Static ||
            description.type > BodyType::Kinematic) {
            valid = false;
            continue;
        }
        active.insert(id);
        if (impl_->planar) {
            description.position.z = 0;
            description.velocity.z = 0;
            description.rotation.x = description.rotation.y = 0;
        }
        auto found = impl_->entries.find(id);
        if (found != impl_->entries.end()) {
            const auto& old = found->second.description;
            if (old.type != description.type || !Same(old.size, description.size) ||
                !Same(old.offset, description.offset) || old.mass != description.mass ||
                old.trigger != description.trigger) {
                impl_->Remove(id);
                found = impl_->entries.end();
            }
        }
        if (found == impl_->entries.end()) {
            const auto half = Vector(description.size * .5f);
            JPH::RefConst<JPH::Shape> shape = new JPH::BoxShape(
                half, std::min(.02f, .2f * std::min({half.GetX(), half.GetY(), half.GetZ()})));
            if (!Same(description.offset, {}))
                shape = new JPH::RotatedTranslatedShape(Vector(description.offset),
                                                        JPH::Quat::sIdentity(), shape);
            const auto motion = description.type == BodyType::Static ? JPH::EMotionType::Static
                                : description.type == BodyType::Dynamic
                                    ? JPH::EMotionType::Dynamic
                                    : JPH::EMotionType::Kinematic;
            JPH::BodyCreationSettings settings(
                shape,
                JPH::RVec3(description.position.x, description.position.y, description.position.z),
                JPH::Quat::sEulerAngles(Vector(description.rotation)), motion,
                motion == JPH::EMotionType::Static ? 0 : 1);
            settings.mUserData = id;
            settings.mIsSensor = description.trigger;
            settings.mAllowedDOFs =
                impl_->planar ? JPH::EAllowedDOFs::Plane2D : JPH::EAllowedDOFs::All;
            settings.mLinearVelocity = Vector(description.velocity);
            settings.mGravityFactor = description.gravityScale;
            settings.mLinearDamping = settings.mAngularDamping = 0;
            settings.mMaxLinearVelocity = 100000.f;
            if (motion == JPH::EMotionType::Dynamic && !description.trigger)
                settings.mMotionQuality = JPH::EMotionQuality::LinearCast;
            settings.mAllowSleeping = false;
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            settings.mMassPropertiesOverride.mMass = description.mass;
            auto* body = bodies.CreateBody(settings);
            if (!body)
                throw std::runtime_error("Jolt 물리 바디 한도를 초과했습니다.");
            bodies.AddBody(body->GetID(), JPH::EActivation::Activate);
            found = impl_->entries
                        .emplace(id, Impl::Entry{body->GetID(),
                                                 description,
                                                 {description.position, description.rotation,
                                                  description.velocity, false}})
                        .first;
        }
        auto& entry = found->second;
        if (!Same(entry.state.position, description.position) ||
            !Same(entry.state.rotation, description.rotation))
            bodies.SetPositionAndRotation(
                entry.body,
                JPH::RVec3(description.position.x, description.position.y, description.position.z),
                JPH::Quat::sEulerAngles(Vector(description.rotation)), JPH::EActivation::Activate);
        if (!Same(entry.state.velocity, description.velocity))
            bodies.SetLinearVelocity(entry.body, Vector(description.velocity));
        if (description.type != BodyType::Static)
            bodies.SetGravityFactor(entry.body, description.gravityScale);
        entry.description = description;
    }
    for (auto it = impl_->entries.begin(); it != impl_->entries.end();) {
        const auto id = (it++)->first;
        if (!active.contains(id))
            impl_->Remove(id);
    }
    return valid;
}

void PhysicsWorld::Remove(uint32 entity) {
    impl_->Remove(entity);
}

void PhysicsWorld::Step(double dt, Vec3 gravity) {
    impl_->contacts.clear();
    impl_->began.clear();
    if (!std::isfinite(dt) || dt <= 0 || dt > .25 || !Valid(gravity))
        return;
    impl_->system.SetGravity(Vector(gravity));
    if (impl_->system.Update(static_cast<float>(dt), 1, &impl_->allocator, &impl_->jobs) !=
        JPH::EPhysicsUpdateError::None)
        throw std::runtime_error("Jolt 물리 시뮬레이션 버퍼가 부족합니다.");
    const auto& bodies = impl_->system.GetBodyInterface();
    for (auto& [id, entry] : impl_->entries) {
        const auto p = bodies.GetPosition(entry.body);
        entry.state = {{static_cast<float>(p.GetX()), static_cast<float>(p.GetY()),
                        static_cast<float>(p.GetZ())},
                       Vector(bodies.GetRotation(entry.body).GetEulerAngles()),
                       Vector(bodies.GetLinearVelocity(entry.body)),
                       false};
    }
    for (const auto& contact : impl_->contacts) {
        if (contact.isTrigger)
            continue;
        if (contact.normal.y < -.5f && impl_->entries.contains(contact.entityA))
            impl_->entries.at(contact.entityA).state.grounded = true;
        if (contact.normal.y > .5f && impl_->entries.contains(contact.entityB))
            impl_->entries.at(contact.entityB).state.grounded = true;
    }
}

std::optional<PhysicsBodyState> PhysicsWorld::GetState(uint32 entity) const {
    const auto it = impl_->entries.find(entity);
    return it == impl_->entries.end() ? std::nullopt : std::optional(it->second.state);
}

const std::vector<Collision3DEvent>& PhysicsWorld::Contacts() const {
    return impl_->contacts;
}

const std::vector<Collision3DEvent>& PhysicsWorld::BeganContacts() const {
    return impl_->began;
}

size_t PhysicsWorld::BodyCount() const {
    return impl_->entries.size();
}

} // namespace LuxEngine
