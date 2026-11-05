#pragma once

#include "Engine/Core/Core.hpp"

namespace LuxEngine {

    class Renderer2D {
    public:
        static void Init();
        static void Shutdown();
        static void BeginScene();
        static void EndScene();
    };

} // namespace LuxEngine
