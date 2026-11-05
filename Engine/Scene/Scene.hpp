#pragma once
#include "Engine/Core/Core.hpp"
#include "Engine/ECS/ECS.hpp"
#include "Engine/Physics/PhysicsEngine.hpp"
#include <stdexcept>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace LuxEngine {
class Entity;
class PhysicsWorld2D;
class LUX_API Scene {
public:
    Scene();
    ~Scene();
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&) = delete;
    Scene& operator=(Scene&&) = delete;
    std::vector<Entity> GetEntities();
    Entity CreateEntity(const std::string& name = "Entity");
    void DestroyEntity(Entity entity);
    void OnUpdate(double dt);
    void OnRender();
    // BeginContact events from the most recent 2D physics step, including sensors.
    const std::vector<Collision2DEvent>& GetCollisionEvents2D() const {
        return collisionEvents2D_;
    }
    bool Serialize(const std::string& filepath);
    bool Deserialize(const std::string& filepath);
    uint32 GetEntityCount() const {
        return static_cast<uint32>(entities_.size());
    }
    bool IsAlive(uint32 id) const {
        return entities_.contains(id);
    }
    std::weak_ptr<int> Lifetime() const {
        return lifetime_;
    }
    template <class T, class... Args> T& AddComponent(uint32 id, Args&&... args) {
        RequireAlive(id);
        auto& values = GetPool<T>().values;
        auto [it, added] = values.try_emplace(id, T{std::forward<Args>(args)...});
        if (!added)
            throw std::logic_error("Component already exists");
        return it->second;
    }
    template <class T> T& GetComponent(uint32 id) {
        return const_cast<T&>(std::as_const(*this).GetComponent<T>(id));
    }
    template <class T> const T& GetComponent(uint32 id) const {
        RequireAlive(id);
        auto it = pools_.find(typeid(T));
        if (it == pools_.end())
            throw std::out_of_range("Component does not exist");
        return static_cast<const Pool<T>&>(*it->second).values.at(id);
    }
    template <class T> bool HasComponent(uint32 id) const {
        auto it = pools_.find(typeid(T));
        return IsAlive(id) && it != pools_.end() &&
               static_cast<const Pool<T>&>(*it->second).values.contains(id);
    }
    template <class T> void RemoveComponent(uint32 id) {
        auto it = pools_.find(typeid(T));
        if (it != pools_.end()) {
            it->second->Erase(id);
            if constexpr (std::is_same_v<T, TransformComponent> ||
                          std::is_same_v<T, Rigidbody2DComponent> ||
                          std::is_same_v<T, BoxCollider2DComponent>)
                RemovePhysicsBody2D(id);
        }
    }

private:
    struct PoolBase {
        virtual ~PoolBase() = default;
        virtual void Erase(uint32 id) = 0;
        virtual bool Empty() const = 0;
    };
    template <class T> struct Pool final : PoolBase {
        std::unordered_map<uint32, T> values;
        void Erase(uint32 id) override {
            values.erase(id);
        }
        bool Empty() const override {
            return values.empty();
        }
    };
    template <class T> Pool<T>& GetPool() {
        auto it = pools_.find(typeid(T));
        if (it == pools_.end())
            it = pools_.emplace(typeid(T), std::make_unique<Pool<T>>()).first;
        return static_cast<Pool<T>&>(*it->second);
    }
    void RequireAlive(uint32 id) const {
        if (!IsAlive(id))
            throw std::out_of_range("Entity is not alive");
    }
    void RemovePhysicsBody2D(uint32 id);
    uint32 nextEntityId_ = 1;
    std::shared_ptr<int> lifetime_ = std::make_shared<int>(0);
    std::unordered_set<uint32> entities_;
    std::unordered_map<std::type_index, std::unique_ptr<PoolBase>> pools_;
    std::unique_ptr<PhysicsWorld2D> physics2D_;
    std::vector<Collision2DEvent> collisionEvents2D_;
};
} // namespace LuxEngine
