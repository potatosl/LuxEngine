#include "Engine/Physics/PhysicsWorld2D.h"
#include "Entity.hpp"
#include "Scene.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <tuple>

namespace LuxEngine {
namespace {
bool Finite(const Vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
bool Finite(const TransformComponent& value) {
    return Finite(value.position) && Finite(value.rotation) && Finite(value.scale);
}
float Length(Vec3 value) {
    return std::sqrt(value.Dot(value));
}
bool Decompose(const Mat4& matrix, TransformComponent& value, bool requireTrs) {
    const auto* m = matrix.elements;
    value.position = {m[12], m[13], m[14]};
    Vec3 x{m[0], m[1], m[2]}, y{m[4], m[5], m[6]}, z{m[8], m[9], m[10]};
    value.scale = {Length(x), Length(y), Length(z)};
    constexpr float epsilon = 1e-6f;
    if (value.scale.x < epsilon || value.scale.y < epsilon || value.scale.z < epsilon) {
        value.rotation = {};
        return !requireTrs && Finite(value);
    }
    if (x.Dot(y.Cross(z)) < 0)
        value.scale.x = -value.scale.x;
    x = x * (1 / value.scale.x);
    y = y * (1 / value.scale.y);
    z = z * (1 / value.scale.z);
    // A rotated child below a non-uniformly scaled parent can contain shear. Keep-world edits
    // reject a non-TRS result rather than silently moving or stretching the object.
    if (requireTrs &&
        (std::abs(x.Dot(y)) > 1e-4f || std::abs(x.Dot(z)) > 1e-4f || std::abs(y.Dot(z)) > 1e-4f))
        return false;
    value.rotation.y = std::asin(std::clamp(-x.z, -1.0f, 1.0f));
    if (std::abs(std::cos(value.rotation.y)) > 1e-5f) {
        value.rotation.x = std::atan2(y.z, z.z);
        value.rotation.z = std::atan2(x.y, x.x);
    } else {
        value.rotation.x = 0;
        value.rotation.z = std::atan2(-y.x, y.y);
    }
    return Finite(value);
}
bool InverseAffine(const Mat4& input, Mat4& result) {
    const auto* m = input.elements;
    const double a = m[0], b = m[4], c = m[8], d = m[1], e = m[5], f = m[9], g = m[2], h = m[6],
                 i = m[10];
    const double determinant = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (!std::isfinite(determinant) || std::abs(determinant) < 1e-12)
        return false;
    const double scale = 1 / determinant;
    auto* out = result.elements;
    out[0] = static_cast<float>((e * i - f * h) * scale);
    out[4] = static_cast<float>((c * h - b * i) * scale);
    out[8] = static_cast<float>((b * f - c * e) * scale);
    out[1] = static_cast<float>((f * g - d * i) * scale);
    out[5] = static_cast<float>((a * i - c * g) * scale);
    out[9] = static_cast<float>((c * d - a * f) * scale);
    out[2] = static_cast<float>((d * h - e * g) * scale);
    out[6] = static_cast<float>((b * g - a * h) * scale);
    out[10] = static_cast<float>((a * e - b * d) * scale);
    out[12] = -(out[0] * m[12] + out[4] * m[13] + out[8] * m[14]);
    out[13] = -(out[1] * m[12] + out[5] * m[13] + out[9] * m[14]);
    out[14] = -(out[2] * m[12] + out[6] * m[13] + out[10] * m[14]);
    return std::all_of(std::begin(result.elements), std::end(result.elements),
                       [](float v) { return std::isfinite(v); });
}
using AuthoredComponents =
    std::tuple<TagComponent, TransformComponent, SpriteRendererComponent, MeshRendererComponent,
               CameraComponent, Rigidbody2DComponent, BoxCollider2DComponent, Rigidbody3DComponent,
               BoxCollider3DComponent, TilemapComponent, AnimationComponent,
               SceneReferenceComponent>;
template <typename Tuple, typename Callback, size_t... Indices>
void ForEachType(Callback&& callback, std::index_sequence<Indices...>) {
    (callback.template operator()<std::tuple_element_t<Indices, Tuple>>(), ...);
}
template <typename Tuple, typename Callback> void ForEachType(Callback&& callback) {
    ForEachType<Tuple>(std::forward<Callback>(callback),
                       std::make_index_sequence<std::tuple_size_v<Tuple>>{});
}
} // namespace

bool Scene::SetSettings(const SceneSettings& value) {
    const auto bounded = [](float v, float limit) {
        return std::isfinite(v) && std::abs(v) <= limit;
    };
    if (!bounded(value.clearColor.x, 1) || value.clearColor.x < 0 ||
        !bounded(value.clearColor.y, 1) || value.clearColor.y < 0 ||
        !bounded(value.clearColor.z, 1) || value.clearColor.z < 0 ||
        !bounded(value.clearColor.w, 1) || value.clearColor.w < 0 ||
        !std::isfinite(value.exposure) || value.exposure < 0 || value.exposure > 16 ||
        !bounded(value.gravity.x, 100000) || !bounded(value.gravity.y, 100000) ||
        !Finite(value.gravity3D) || !bounded(value.gravity3D.x, 100000) ||
        !bounded(value.gravity3D.y, 100000) || !bounded(value.gravity3D.z, 100000) ||
        !std::isfinite(value.fixedStep) || value.fixedStep < 1.0f / 240 || value.fixedStep > .25f ||
        value.physicsSubsteps < 1 || value.physicsSubsteps > 16)
        return false;
    if (!ValidateRender3DSettings(value.render3D))
        return false;
    settings_ = value;
    physicsAccumulator_ = 0;
    return true;
}

uint32 Scene::GetParent(uint32 child) const {
    const auto* hierarchy = TryGetComponent<HierarchyComponent>(child);
    return hierarchy ? hierarchy->parent : 0;
}

std::vector<uint32> Scene::GetChildren(uint32 parent) const {
    std::vector<uint32> children;
    if (parent && !IsAlive(parent))
        return children;
    for (const uint32 id : entities_)
        if (GetParent(id) == parent)
            children.push_back(id);
    std::sort(children.begin(), children.end());
    return children;
}

std::vector<Entity> Scene::GetEntitiesInHierarchyOrder() {
    std::unordered_map<uint32, std::vector<uint32>> children;
    for (const auto& entity : GetEntities())
        children[GetParent(entity.GetId())].push_back(entity.GetId());
    std::vector<Entity> result;
    result.reserve(entities_.size());
    std::vector<uint32> pending(children[0].rbegin(), children[0].rend());
    while (!pending.empty()) {
        const auto id = pending.back();
        pending.pop_back();
        result.emplace_back(id, this);
        const auto& descendants = children[id];
        pending.insert(pending.end(), descendants.rbegin(), descendants.rend());
    }
    if (result.size() != entities_.size())
        throw std::logic_error("Invalid scene hierarchy");
    return result;
}

bool Scene::ValidateHierarchy() const {
    std::unordered_map<uint32, uint8_t> marks;
    marks.reserve(entities_.size());
    for (const uint32 id : entities_) {
        if (marks[id] == 2)
            continue;
        std::vector<uint32> chain;
        uint32 cursor = id;
        while (cursor) {
            if (!IsAlive(cursor) || marks[cursor] == 1)
                return false;
            if (marks[cursor] == 2)
                break;
            marks[cursor] = 1;
            chain.push_back(cursor);
            cursor = GetParent(cursor);
        }
        for (const uint32 item : chain)
            marks[item] = 2;
    }
    return true;
}

Mat4 Scene::GetWorldMatrix(uint32 id) const {
    RequireAlive(id);
    std::vector<uint32> chain;
    uint32 cursor = id;
    while (cursor) {
        if (!IsAlive(cursor) || chain.size() >= entities_.size())
            throw std::logic_error("Invalid scene hierarchy");
        chain.push_back(cursor);
        cursor = GetParent(cursor);
    }
    Mat4 matrix;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it)
        if (const auto* transform = TryGetComponent<TransformComponent>(*it))
            matrix = matrix * transform->GetTransform();
    return matrix;
}

TransformComponent Scene::GetWorldTransform(uint32 id) const {
    RequireAlive(id);
    if (!GetParent(id)) {
        const auto* transform = TryGetComponent<TransformComponent>(id);
        return transform ? *transform : TransformComponent{};
    }
    TransformComponent result;
    if (!Decompose(GetWorldMatrix(id), result, false))
        throw std::logic_error("Invalid world transform");
    return result;
}

bool Scene::SetWorldTransform(uint32 id, const TransformComponent& value) {
    if (!HasComponent<TransformComponent>(id) || !Finite(value))
        return false;
    if (!GetParent(id)) {
        GetComponent<TransformComponent>(id) = value;
        return true;
    }
    Mat4 inverse;
    if (!InverseAffine(GetWorldMatrix(GetParent(id)), inverse))
        return false;
    const auto current = GetWorldTransform(id);
    const auto same = [](const Vec3& a, const Vec3& b) {
        return a.x == b.x && a.y == b.y && a.z == b.z;
    };
    if (same(value.rotation, current.rotation) && same(value.scale, current.scale)) {
        // Translation does not require decomposing a sheared linear matrix. Preserve the
        // original local rotation/scale and transform only the requested world position.
        const auto* m = inverse.elements;
        const auto p = value.position;
        const Vec3 localPosition{m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12],
                                 m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13],
                                 m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]};
        if (!Finite(localPosition))
            return false;
        GetComponent<TransformComponent>(id).position = localPosition;
        return true;
    }
    TransformComponent local;
    if (!Decompose(inverse * value.GetTransform(), local, true))
        return false;
    GetComponent<TransformComponent>(id) = local;
    return true;
}

bool Scene::SetParent(uint32 child, uint32 parent, bool keepWorld) {
    if (!IsAlive(child) || (parent && !IsAlive(parent)) || child == parent)
        return false;
    if (GetParent(child) == parent)
        return true;
    uint32 cursor = parent;
    size_t depth = 0;
    while (cursor) {
        if (cursor == child || !IsAlive(cursor) || ++depth > entities_.size())
            return false;
        cursor = GetParent(cursor);
    }
    TransformComponent local;
    if (keepWorld) {
        if (!HasComponent<TransformComponent>(child))
            return false;
        const auto world = GetWorldMatrix(child);
        Mat4 inverse;
        if ((parent && !InverseAffine(GetWorldMatrix(parent), inverse)) ||
            !Decompose(inverse * world, local, true))
            return false;
    }
    auto* hierarchy = TryGetComponent<HierarchyComponent>(child);
    if (!hierarchy)
        hierarchy = &AddComponent<HierarchyComponent>(child);
    hierarchy->parent = parent;
    if (keepWorld)
        GetComponent<TransformComponent>(child) = local;
    RepairReferenceLinks();
    return true;
}

void Scene::RepairReferenceLinks() {
    const auto descendant = [this](uint32 child, uint32 parent) {
        if (!parent || !IsAlive(parent) || child == parent)
            return false;
        size_t depth = 0;
        for (auto cursor = GetParent(child); cursor && ++depth <= entities_.size();
             cursor = GetParent(cursor))
            if (cursor == parent)
                return true;
        return false;
    };
    for (const auto id : entities_) {
        if (auto* reference = TryGetComponent<SceneReferenceComponent>(id))
            std::erase_if(reference->entityMap, [&](const auto& item) {
                return !IsAlive(item.second) || !descendant(item.second, id);
            });
        if (const auto* source = TryGetComponent<InstanceSourceComponent>(id))
            if (!HasComponent<SceneReferenceComponent>(source->instanceRoot) ||
                !descendant(id, source->instanceRoot))
                RemoveComponent<InstanceSourceComponent>(id);
    }
}

bool Scene::CopyEntityComponents(const Scene& source, uint32 sourceId, uint32 targetId) {
    if (!source.IsAlive(sourceId) || !IsAlive(targetId))
        return false;
    if (&source == this && sourceId == targetId)
        return true;
    std::unordered_set<std::type_index> supported{typeid(HierarchyComponent),
                                                  typeid(InstanceSourceComponent)};
    ForEachType<AuthoredComponents>([&]<typename T> { supported.emplace(typeid(T)); });
    for (const auto& [type, pool] : source.pools_)
        if (pool->Contains(sourceId) && !supported.contains(type))
            return false;
    Scene prepared;
    try {
        prepared.entities_.insert(targetId);
        ForEachType<AuthoredComponents>([&]<typename T> {
            if (const auto* component = source.TryGetComponent<T>(sourceId))
                prepared.AddComponent<T>(targetId, *component);
            prepared.GetPool<T>();
            // Allocate before replacing any authored component. Prepared map nodes are moved
            // into the destination below; no vector/string allocations occur during commit.
            auto& destination = GetPool<T>().values;
            destination.reserve(destination.size() + 1);
        });
    } catch (...) {
        return false;
    }
    RemovePhysicsBody2D(targetId);
    ForEachType<AuthoredComponents>([&]<typename T> {
        auto& destination = GetPool<T>().values;
        destination.erase(targetId);
        auto node = prepared.GetPool<T>().values.extract(targetId);
        if (!node.empty())
            destination.insert(std::move(node));
    });
    return true;
}

bool Scene::ApplyState(Scene& prepared) {
    if (&prepared == this)
        return true;
    std::shared_ptr<int> nextLifetime, preparedLifetime;
    std::vector<uint32> removedBodies;
    try {
        std::string validation;
        if (!SerializeToString(validation) || !prepared.SerializeToString(validation))
            return false;
        nextLifetime = std::make_shared<int>(0);
        preparedLifetime = std::make_shared<int>(0);
        removedBodies.reserve(entities_.size());
        for (const auto id : entities_)
            if (!prepared.HasComponent<TransformComponent>(id) ||
                !prepared.HasComponent<BoxCollider2DComponent>(id) ||
                (!prepared.HasComponent<Rigidbody2DComponent>(id) &&
                 prepared.HasComponent<Rigidbody3DComponent>(id) &&
                 prepared.HasComponent<BoxCollider3DComponent>(id)))
                removedBodies.push_back(id);
    } catch (...) {
        return false;
    }
    for (const auto id : removedBodies)
        RemovePhysicsBody2D(id);
    const bool timingChanged = settings_.fixedStep != prepared.settings_.fixedStep ||
                               settings_.physicsSubsteps != prepared.settings_.physicsSubsteps;
    pools_.swap(prepared.pools_);
    entities_.swap(prepared.entities_);
    std::swap(nextEntityId_, prepared.nextEntityId_);
    std::swap(settings_, prepared.settings_);
    for (const auto id : removedBodies)
        if (auto* body = TryGetComponent<Rigidbody2DComponent>(id))
            body->isGrounded = false;
    lifetime_ = std::move(nextLifetime);
    prepared.lifetime_ = std::move(preparedLifetime);
    if (timingChanged)
        physicsAccumulator_ = 0;
    std::erase_if(collisionEvents2D_, [this](const auto& event) {
        return !IsAlive(event.entityA) || !IsAlive(event.entityB);
    });
    updateBuffer_.clear();
    prepared.physics2D_.reset();
    prepared.physicsAccumulator_ = 0;
    prepared.collisionEvents2D_.clear();
    prepared.updateBuffer_.clear();
    return true;
}

std::unordered_map<uint32, uint32>
Scene::ImportEntities(const Scene& source, const std::vector<uint32>& roots, uint32 parent) {
    std::unordered_map<uint32, uint32> mapping;
    if ((parent && !IsAlive(parent)) || !ValidateHierarchy() || !source.ValidateHierarchy())
        return mapping;
    std::unordered_map<uint32, std::vector<uint32>> children;
    std::vector<uint32> ordered(source.entities_.begin(), source.entities_.end());
    std::sort(ordered.begin(), ordered.end());
    for (const uint32 id : ordered)
        children[source.GetParent(id)].push_back(id);
    std::vector<uint32> pending = roots.empty() ? children[0] : roots;
    std::unordered_set<uint32> included;
    while (!pending.empty()) {
        const auto id = pending.back();
        pending.pop_back();
        if (!source.IsAlive(id))
            return {};
        if (!included.insert(id).second)
            continue;
        const auto found = children.find(id);
        if (found != children.end())
            pending.insert(pending.end(), found->second.begin(), found->second.end());
    }
    if (included.size() > 100000 || entities_.size() > 100000 - included.size() ||
        included.size() > std::numeric_limits<uint32>::max() - nextEntityId_)
        return {};
    std::erase_if(ordered, [&](uint32 id) { return !included.contains(id); });
    const auto previousNext = nextEntityId_;
    std::vector<uint32> created;
    try {
        mapping.reserve(ordered.size());
        created.reserve(ordered.size());
        for (const auto id : ordered) {
            const auto destination = CreateEntity();
            created.push_back(destination.GetId());
            mapping.emplace(id, destination.GetId());
        }
        for (const auto id : ordered) {
            const auto destination = mapping.at(id);
            if (!CopyEntityComponents(source, id, destination))
                throw std::runtime_error("Cannot import unsupported scene component");
            const auto originalParent = source.GetParent(id);
            const bool parentIncluded = mapping.contains(originalParent);
            HierarchyComponent hierarchy;
            if (const auto* original = source.TryGetComponent<HierarchyComponent>(id))
                hierarchy.group = original->group;
            hierarchy.parent = parentIncluded ? mapping.at(originalParent) : parent;
            if (hierarchy.parent || hierarchy.group || source.HasComponent<HierarchyComponent>(id))
                AddComponent<HierarchyComponent>(destination, hierarchy);
            if (!parentIncluded && originalParent &&
                HasComponent<TransformComponent>(destination)) {
                TransformComponent world;
                if (!Decompose(source.GetWorldMatrix(id), world, true))
                    throw std::runtime_error("Imported subtree has an unsupported sheared root");
                GetComponent<TransformComponent>(destination) = world;
            }
            if (const auto* origin = source.TryGetComponent<InstanceSourceComponent>(id))
                if (mapping.contains(origin->instanceRoot))
                    AddComponent<InstanceSourceComponent>(
                        destination, InstanceSourceComponent{mapping.at(origin->instanceRoot),
                                                             origin->sourceEntity});
        }
        for (const auto id : created)
            if (auto* reference = TryGetComponent<SceneReferenceComponent>(id)) {
                std::erase_if(reference->entityMap,
                              [&](const auto& item) { return !mapping.contains(item.second); });
                for (auto& [sourceId, localId] : reference->entityMap)
                    localId = mapping.at(localId);
            }
    } catch (...) {
        for (const auto id : created) {
            for (auto& [type, pool] : pools_)
                pool->Erase(id);
            entities_.erase(id);
        }
        nextEntityId_ = previousNext;
        return {};
    }
    return mapping;
}
} // namespace LuxEngine
