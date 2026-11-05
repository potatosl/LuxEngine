#pragma once

#include "Engine/Core/Core.hpp"
#include "Engine/Scene/Scene.hpp"

namespace LuxEngine {

    class Entity {
    public:
        Entity() = default;
        Entity(uint32 id, Scene* scene) : id_(id), scene_(scene) {}

        uint32 GetId() const { return id_; }
        bool IsValid() const { return scene_ != nullptr && id_ != 0; }
        explicit operator bool() const { return IsValid(); }

        template<typename T, typename... Args>
        T& AddComponent(Args&&... args) {
            return scene_->AddComponent<T>(id_, std::forward<Args>(args)...);
        }

        template<typename T>
        T& GetComponent() {
            return scene_->GetComponent<T>(id_);
        }

        template<typename T>
        const T& GetComponent() const {
            return scene_->GetComponent<T>(id_);
        }

        template<typename T>
        bool HasComponent() const {
            return scene_ ? scene_->HasComponent<T>(id_) : false;
        }

        template<typename T>
        void RemoveComponent() {
            if (scene_) scene_->RemoveComponent<T>(id_);
        }

        bool operator==(const Entity& other) const { return id_ == other.id_ && scene_ == other.scene_; }
        bool operator!=(const Entity& other) const { return !(*this == other); }

    private:
        uint32 id_ = 0;
        Scene* scene_ = nullptr;
    };

} // namespace LuxEngine
