#pragma once

#include <LuxEngine/Core/Core.hpp>
#include <LuxEngine/Core/Math.hpp>
#include <LuxEngine/Renderer/RHI/Texture.hpp>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace LuxEngine {
class GpuDevice;

struct RawMeshData {
    std::vector<float> vertices;
    std::vector<uint32> indices;
    std::vector<float> normals;
    std::vector<float> texCoords;
    Vec3 boundsMin;
    Vec3 boundsMax;
    uint64 revision = 0;
    bool hasTexCoords = false;
};

class LUX_API AssetManager {
public:
    static void Init();
    static void Shutdown();

    static Ref<GpuTexture> LoadTexture(const std::string& filepath, GpuDevice* device = nullptr);
    static bool HasTexture(const std::string& filepath);
    static void UnloadTexture(const std::string& filepath);

    // Validates image file format, dimensions (<= 8192), file size (<= 64 MiB), and memory budget
    // (<= 256 MiB) on CPU.
    static bool ValidateTextureFile(const std::string& filepath, int* outWidth = nullptr,
                                    int* outHeight = nullptr);

    // Decodes valid texture pixels to RGBA buffer on CPU without requiring a GPU device.
    static bool DecodeTextureCpu(const std::string& filepath, int& outWidth, int& outHeight,
                                 std::vector<uint8>& outRgbaPixels);

    static bool LoadShaderBytes(const std::string& filepath, std::vector<uint8>& outBytes);
    static bool LoadMesh(const std::string& filepath, RawMeshData& outMesh, bool reload = false);
    static bool ValidateMeshFile(const std::string& filepath, bool* hasTexCoords = nullptr);
    static bool ReloadMesh(const std::string& filepath, std::string& error,
                           bool requireTexCoords = false);
    static bool ReloadTexture(const std::string& filepath, std::string& error);
    static bool IsBaseColorTextureFile(const std::string& filepath);
    static uint64 GetMeshRevision(const std::string& filepath);
    static uint64 GetTextureRevision(const std::string& filepath);
    static std::string GetAssetError(const std::string& filepath, bool texture = false);
    static bool GetMeshBounds(const std::string& filepath, Vec3& minimum, Vec3& maximum);

    static void Clear();
    static size_t GetCachedTextureCount();
    static size_t GetCachedMeshCount();

    // Cache memory instrumentation and budget management (BUG-07)
    static size_t GetTextureCacheMemoryBytes();
    static size_t GetMeshCacheMemoryBytes();
    static size_t GetTotalCacheMemoryBytes();

    static void SetCacheMemoryBudget(size_t maxBytes);
    static size_t GetCacheMemoryBudget();

private:
    static bool LoadMeshImpl(const std::string& filepath, RawMeshData& outMesh, bool reload,
                             std::string& error, bool requireTexCoords = false);
    static void EvictUnusedUnderBudget(size_t incomingBytes);

    static std::unordered_map<std::string, Ref<GpuTexture>> s_TextureCache;
    static std::unordered_map<std::string, Ref<const RawMeshData>> s_MeshCache;
    static size_t s_TextureCacheMemoryBytes;
    static size_t s_MeshCacheMemoryBytes;
    static size_t s_CacheMemoryBudget;
};

} // namespace LuxEngine
