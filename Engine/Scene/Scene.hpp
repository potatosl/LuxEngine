#pragma once

#include "Engine/Core/Core.hpp"

namespace LuxEngine {

    class Entity;

    class Scene {
    public:
        Scene();
        ~Scene();

        void OnUpdate(double dt);
        void OnRender();

    private:
        friend class Entity;
    };

} // namespace LuxEngine
