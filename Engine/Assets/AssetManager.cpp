#include "Engine/Assets/AssetManager.hpp"
#include "Engine/Core/Log.hpp"
#include <fstream>

namespace LuxEngine {

    std::unordered_map<std::string, Ref<GpuTexture>> AssetManager::s_TextureCache;
    std::unordered_map<std::string, RawMeshData> AssetManager::s_MeshCache;

    void AssetManager::Init() {
        Clear();
        Log::Info("AssetManager initialized.");
    }

    void AssetManager::Shutdown() {
        Clear();
        Log::Info("AssetManager shutdown.");
    }

    Ref<GpuTexture> AssetManager::LoadTexture(const std::string& filepath, SDL_GPUDevice* device) {
        auto it = s_TextureCache.find(filepath);
        if (it != s_TextureCache.end()) {
            return it->second;
        }

        auto texture = CreateRef<GpuTexture>();
        if (device) {
            // Default 2x2 white fallback texture
            uint32 whitePixels[4] = { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF };
            if (texture->Initialize(device, 2, 2, whitePixels)) {
                s_TextureCache[filepath] = texture;
                Log::Info(std::string("Loaded texture asset: ") + filepath);
                return texture;
            }
        }

        s_TextureCache[filepath] = texture;
        return texture;
    }

    bool AssetManager::HasTexture(const std::string& filepath) {
        return s_TextureCache.find(filepath) != s_TextureCache.end();
    }

    void AssetManager::UnloadTexture(const std::string& filepath) {
        auto it = s_TextureCache.find(filepath);
        if (it != s_TextureCache.end()) {
            s_TextureCache.erase(it);
            Log::Info(std::string("Unloaded texture asset: ") + filepath);
        }
    }

    bool AssetManager::LoadShaderBytes(const std::string& filepath, std::vector<uint8>& outBytes) {
        std::ifstream file(filepath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            Log::Warn(std::string("Shader asset not found: ") + filepath);
            return false;
        }

        const std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        outBytes.resize(static_cast<size_t>(size));
        if (file.read(reinterpret_cast<char*>(outBytes.data()), size)) {
            return true;
        }
        return false;
    }

    bool AssetManager::LoadMesh(const std::string& filepath, RawMeshData& outMesh) {
        auto it = s_MeshCache.find(filepath);
        if (it != s_MeshCache.end()) {
            outMesh = it->second;
            return true;
        }

        // Default unit cube fallback mesh
        outMesh.vertices = {
            -0.5f, -0.5f,  0.5f,
             0.5f, -0.5f,  0.5f,
             0.5f,  0.5f,  0.5f,
            -0.5f,  0.5f,  0.5f
        };
        outMesh.indices = { 0, 1, 2, 2, 3, 0 };

        s_MeshCache[filepath] = outMesh;
        return true;
    }

    void AssetManager::Clear() {
        s_TextureCache.clear();
        s_MeshCache.clear();
    }

    size_t AssetManager::GetCachedTextureCount() {
        return s_TextureCache.size();
    }

} // namespace LuxEngine
