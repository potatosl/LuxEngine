#pragma once

#include "Engine/Core/Core.hpp"

namespace LuxEngine {

    class PhysicsEngine {
    public:
        static void Init();
        static void Shutdown();
        static void Step(double dt);
    };

} // namespace LuxEngine
