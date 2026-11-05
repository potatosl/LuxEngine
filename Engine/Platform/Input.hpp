#pragma once

#include "Engine/Core/Core.hpp"
#include <utility>

namespace LuxEngine {

    class Input {
    public:
        static bool IsKeyPressed(int32 keycode);
        static bool IsMouseButtonPressed(int32 button);
        static std::pair<float, float> GetMousePosition();
        static float GetMouseX();
        static float GetMouseY();
    };

} // namespace LuxEngine
