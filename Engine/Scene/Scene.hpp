#pragma once

#include "Engine/Core/Core.hpp"
#include "Engine/ECS/ECS.hpp"
#include <vector>
#include <unordered_map>
#include <typeindex>
#include <memory>
#include <string>

namespace LuxEngine {

    class Entity;

    class Scene {
    public:
        Scene();
        ~Scene();

        Entity CreateEntity(const std::string& name = "Entity");
        void DestroyEntity(Entity entity);

        void OnUpdate(double dt);
        void OnRender();

        bool Serialize(const std::string& filepath);
        bool Deserialize(const std::string& filepath);

        uint32 GetEntityCount() const { return entityCount_; }

        template<typename T, typename... Args>
        T& AddComponent(uint32 entityId, Args&&... args) {
            auto& pool = GetPool<T>();
            pool[entityId] = T{ std::forward<Args>(args)... };
            return pool[entityId];
        }

        template<typename T>
        T& GetComponent(uint32 entityId) {
            return GetPool<T>()[entityId];
        }

        template<typename T>
        const T& GetComponent(uint32 entityId) const {
            return const_cast<Scene*>(this)->GetPool<T>()[entityId];
        }

        template<typename T>
        bool HasComponent(uint32 entityId) const {
            auto it = componentPools_.find(typeid(T));
            if (it == componentPools_.end()) return false;
            auto* typedPool = static_cast<std::unordered_map<uint32, T>*>(it->second.get());
            return typedPool->find(entityId) != typedPool->end();
        }

        template<typename T>
        void RemoveComponent(uint32 entityId) {
            auto it = componentPools_.find(typeid(T));
            if (it != componentPools_.end()) {
                auto* typedPool = static_cast<std::unordered_map<uint32, T>*>(it->second.get());
                typedPool->erase(entityId);
            }
        }

    private:
        template<typename T>
        std::unordered_map<uint32, T>& GetPool() {
            auto tid = std::type_index(typeid(T));
            if (componentPools_.find(tid) == componentPools_.end()) {
                componentPools_[tid] = std::make_unique<std::unordered_map<uint32, T>>();
            }
            return *static_cast<std::unordered_map<uint32, T>*>(componentPools_[tid].get());
        }

        uint32 nextEntityId_ = 1;
        uint32 entityCount_ = 0;
        std::unordered_map<std::type_index, std::shared_ptr<void>> componentPools_;
    };

} // namespace LuxEngine
