#include "Engine/Assets/AssetManager.hpp"
#include "Engine/Core/Log.hpp"

namespace LuxEngine {

    void AssetManager::Init() {
        Log::Info("AssetManager initialized (Prepared for stb_image & cgltf).");
    }

    void AssetManager::Shutdown() {
        Log::Info("AssetManager shutdown.");
    }

    bool AssetManager::LoadTexture(std::string_view filePath) {
        Log::Info(std::string("AssetManager request loading texture: ") + std::string(filePath));
        return true;
    }

    bool AssetManager::LoadModel(std::string_view filePath) {
        Log::Info(std::string("AssetManager request loading model: ") + std::string(filePath));
        return true;
    }

} // namespace LuxEngine
