#pragma once

#include "Engine/Core/Core.hpp"
#include "Engine/Renderer/RHI/Texture.hpp"
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace LuxEngine {
class GpuDevice;

struct RawMeshData {
    std::vector<float> vertices;
    std::vector<uint32> indices;
};

class LUX_API AssetManager {
public:
    static void Init();
    static void Shutdown();

    static Ref<GpuTexture> LoadTexture(const std::string& filepath,
                                       SDL_GPUDevice* device = nullptr);
    static Ref<GpuTexture> LoadTexture(const std::string& filepath, GpuDevice* device);
    static Ref<GpuTexture> LoadTexture(const std::string&, std::nullptr_t) {
        return {};
    }
    static bool HasTexture(const std::string& filepath);
    static void UnloadTexture(const std::string& filepath);

    static bool LoadShaderBytes(const std::string& filepath, std::vector<uint8>& outBytes);
    static bool LoadMesh(const std::string& filepath, RawMeshData& outMesh);

    static void Clear();
    static size_t GetCachedTextureCount();

private:
    static std::unordered_map<std::string, Ref<GpuTexture>> s_TextureCache;
    static std::unordered_map<std::string, RawMeshData> s_MeshCache;
};

} // namespace LuxEngine
