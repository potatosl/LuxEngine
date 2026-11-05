#pragma once

#include "Engine/Core/Core.hpp"

namespace LuxEngine {

    class AnimationEngine {
    public:
        static void Init();
        static void Shutdown();
        static void Update(double dt);
    };

} // namespace LuxEngine
