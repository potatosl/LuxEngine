#pragma once

#include "Engine/Core/Core.hpp"

namespace LuxEngine {

    struct TagComponent {
        std::string tag;
    };

    struct TransformComponent {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

} // namespace LuxEngine
