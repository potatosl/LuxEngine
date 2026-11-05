#pragma once

#include "Engine/Core/Core.hpp"
#include "Engine/Scene/Scene.hpp"

namespace LuxEngine {

    class Entity {
    public:
        Entity() = default;
        Entity(uint32 id, Scene* scene) : id_(id), scene_(scene), lifetime_(scene ? scene->Lifetime() : std::weak_ptr<int>{}) {}

        uint32 GetId() const { return id_; }
        Scene* GetScene() const { return lifetime_.expired() ? nullptr : scene_; }
        bool IsValid() const { return !lifetime_.expired() && scene_ && scene_->IsAlive(id_); }
        explicit operator bool() const { return IsValid(); }

        template<typename T, typename... Args>
        T& AddComponent(Args&&... args) {
            RequireValid();
            return scene_->AddComponent<T>(id_, std::forward<Args>(args)...);
        }

        template<typename T>
        T& GetComponent() {
            RequireValid();
            return scene_->GetComponent<T>(id_);
        }

        template<typename T>
        const T& GetComponent() const {
            RequireValid();
            return scene_->GetComponent<T>(id_);
        }

        template<typename T>
        bool HasComponent() const {
            return IsValid() && scene_->HasComponent<T>(id_);
        }

        template<typename T>
        void RemoveComponent() {
            if (IsValid()) scene_->RemoveComponent<T>(id_);
        }

        bool operator==(const Entity& other) const { return id_ == other.id_ && scene_ == other.scene_; }
        bool operator!=(const Entity& other) const { return !(*this == other); }

    private:
        void RequireValid() const { if (!IsValid()) throw std::out_of_range("Invalid entity"); }
        uint32 id_ = 0;
        Scene* scene_ = nullptr;
        std::weak_ptr<int> lifetime_;
    };

} // namespace LuxEngine
