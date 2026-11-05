#pragma once

#include "Engine/Core/Core.hpp"
#include "Engine/Scene/Scene.hpp"

namespace LuxEngine {

    class Entity {
    public:
        Entity(uint32 id, Scene* scene) : id_(id), scene_(scene) {}
        uint32 GetId() const { return id_; }

    private:
        uint32 id_ = 0;
        Scene* scene_ = nullptr;
    };

} // namespace LuxEngine
