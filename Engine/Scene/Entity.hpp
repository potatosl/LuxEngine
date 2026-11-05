#pragma once

#include "Engine/Core/Core.hpp"
#include "Engine/Scene/Scene.hpp"

namespace LuxEngine {

    class Entity {
    public:
        Entity() = default;
        Entity(uint32 id, Scene* scene) : id_(id), scene_(scene) {}

        uint32 GetId() const { return id_; }
        bool IsValid() const { return scene_ != nullptr; }
        explicit operator bool() const { return IsValid(); }

        bool operator==(const Entity& other) const { return id_ == other.id_ && scene_ == other.scene_; }
        bool operator!=(const Entity& other) const { return !(*this == other); }

    private:
        uint32 id_ = 0;
        Scene* scene_ = nullptr;
    };

} // namespace LuxEngine
