#pragma once

#include "Engine/Core/Core.hpp"
#include <string_view>

namespace LuxEngine {

    class AssetManager {
    public:
        static void Init();
        static void Shutdown();

        static bool LoadTexture(std::string_view filePath);
        static bool LoadModel(std::string_view filePath);
    };

} // namespace LuxEngine
