#include "Engine/Assets/AssetManager.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/Renderer/RHI/GpuDevice.hpp"
#include <SDL3/SDL.h>
#include <cstring>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_FAILURE_USERMSG
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <sstream>
#include <stb_image.h>

namespace LuxEngine {
std::unordered_map<std::string, Ref<GpuTexture>> AssetManager::s_TextureCache;
std::unordered_map<std::string, RawMeshData> AssetManager::s_MeshCache;
size_t AssetManager::s_TextureCacheMemoryBytes = 0;
size_t AssetManager::s_MeshCacheMemoryBytes = 0;
size_t AssetManager::s_CacheMemoryBudget = 512ull * 1024ull * 1024ull; // 512 MiB default

namespace {
std::mutex s_AssetMutex;
std::unordered_map<std::string, size_t> s_TextureSizes;
std::unordered_map<std::string, size_t> s_MeshSizes;

constexpr uint64 MaxTextureFileSizeBytes =
    64ull * 1024ull * 1024ull;            // 64 MiB max compressed texture file
constexpr int MaxTextureDimension = 8192; // 8192 x 8192 max 2D texture width/height
constexpr uint64 MaxDecodedTextureBytes =
    256ull * 1024ull * 1024ull; // 256 MiB max uncompressed pixel memory
constexpr uint64 MaxMeshSizeBytes = 64ull * 1024ull * 1024ull; // 64 MiB
constexpr size_t MaxMeshVertexElements = 3'000'000;            // 1,000,000 vertices (x,y,z)
constexpr size_t MaxMeshIndexElements = 3'000'000;             // 1,000,000 triangles
constexpr size_t MaxFaceVertices = 1024;                       // 1024 vertices max per polygon face

std::filesystem::path Path(const std::string& path) {
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t*>(path.data()), path.size()));
}
std::string Key(const std::string& path) {
    std::error_code error;
    auto result = std::filesystem::weakly_canonical(Path(path), error);
    const auto utf8 = result.generic_u8string();
    return error ? path : std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
}
stbi_uc* DecodeTexture(const std::string& filepath, int& width, int& height) {
    std::error_code ec;
    const auto p = Path(filepath);
    const auto fileSize = std::filesystem::file_size(p, ec);
    if (ec || fileSize == 0 || fileSize > MaxTextureFileSizeBytes) {
        Log::Warn("Image file missing, empty or exceeds size limit (" +
                  std::to_string(MaxTextureFileSizeBytes) + " bytes): " + filepath);
        return nullptr;
    }

    size_t byteCount = 0;
    void* fileData = SDL_LoadFile(filepath.c_str(), &byteCount);
    if (!fileData)
        return nullptr;
    if (byteCount == 0 || byteCount > MaxTextureFileSizeBytes ||
        byteCount > static_cast<size_t>(std::numeric_limits<int>::max())) {
        SDL_free(fileData);
        return nullptr;
    }

    // Inspect image header before allocating decoded pixels to prevent decompression bombs
    int testW = 0, testH = 0, testChannels = 0;
    if (!stbi_info_from_memory(static_cast<const stbi_uc*>(fileData), static_cast<int>(byteCount),
                               &testW, &testH, &testChannels)) {
        SDL_free(fileData);
        Log::Warn("Image header validation failed: " + filepath);
        return nullptr;
    }

    if (testW <= 0 || testH <= 0 || testW > MaxTextureDimension || testH > MaxTextureDimension) {
        SDL_free(fileData);
        Log::Warn("Image dimensions exceed allowed maximum (" +
                  std::to_string(MaxTextureDimension) + "): " + filepath);
        return nullptr;
    }

    const uint64 requiredMemory = static_cast<uint64>(testW) * static_cast<uint64>(testH) * 4ull;
    if (requiredMemory > MaxDecodedTextureBytes) {
        SDL_free(fileData);
        Log::Warn("Image uncompressed pixel memory exceeds limit: " + filepath);
        return nullptr;
    }

    // Detect truncated BMP files where declared resolution exceeds actual file payload
    const auto* rawBytes = static_cast<const unsigned char*>(fileData);
    if (byteCount >= 54 && rawBytes[0] == 'B' && rawBytes[1] == 'M') {
        const uint32 claimedFileSize =
            rawBytes[2] | (rawBytes[3] << 8) | (rawBytes[4] << 16) | (rawBytes[5] << 24);
        const uint32 pixelOffset =
            rawBytes[10] | (rawBytes[11] << 8) | (rawBytes[12] << 16) | (rawBytes[13] << 24);
        const int32 bmpW = static_cast<int32>(rawBytes[18] | (rawBytes[19] << 8) |
                                              (rawBytes[20] << 16) | (rawBytes[21] << 24));
        const int32 bmpH = std::abs(static_cast<int32>(
            rawBytes[22] | (rawBytes[23] << 8) | (rawBytes[24] << 16) | (rawBytes[25] << 24)));
        const uint16 bpp = rawBytes[28] | (rawBytes[29] << 8);
        const uint32 compression =
            rawBytes[30] | (rawBytes[31] << 8) | (rawBytes[32] << 16) | (rawBytes[33] << 24);
        if (byteCount < pixelOffset || (claimedFileSize > 54 && byteCount < claimedFileSize)) {
            SDL_free(fileData);
            Log::Warn("BMP image file truncated before pixel data: " + filepath);
            return nullptr;
        }
        if (compression == 0 && bpp > 0 && bmpW > 0 && bmpH > 0) {
            const uint64 rowBytes = ((static_cast<uint64>(bmpW) * bpp + 31ull) / 32ull) * 4ull;
            const uint64 minExpected = static_cast<uint64>(pixelOffset) + rowBytes * bmpH;
            if (byteCount < minExpected) {
                SDL_free(fileData);
                Log::Warn("BMP image file truncated: declared resolution requires " +
                          std::to_string(minExpected) + " bytes but file has " +
                          std::to_string(byteCount));
                return nullptr;
            }
        }
    }

    int channels = 0;
    stbi_uc* pixels =
        stbi_load_from_memory(static_cast<const stbi_uc*>(fileData), static_cast<int>(byteCount),
                              &width, &height, &channels, STBI_rgb_alpha);
    const char* failureReason = stbi_failure_reason();
    SDL_free(fileData);
    if (!pixels) {
        Log::Warn(std::string("Image load failed: ") + filepath +
                  " (reason: " + (failureReason ? failureReason : "unknown") + ")");
        return nullptr;
    }

    if (width <= 0 || height <= 0 || width > MaxTextureDimension || height > MaxTextureDimension) {
        stbi_image_free(pixels);
        Log::Warn("Decoded image dimension check failed: " + filepath);
        return nullptr;
    }

    return pixels;
}
} // namespace

void AssetManager::EvictUnusedUnderBudget(size_t incomingBytes) {
    if (s_TextureCacheMemoryBytes + s_MeshCacheMemoryBytes + incomingBytes <= s_CacheMemoryBudget)
        return;

    auto it = s_TextureCache.begin();
    while (it != s_TextureCache.end() &&
           ((s_TextureCacheMemoryBytes + s_MeshCacheMemoryBytes) + incomingBytes >
            s_CacheMemoryBudget)) {
        // Only evict if no outside references exist (use_count == 1)
        if (it->second.use_count() <= 1) {
            auto sizeIt = s_TextureSizes.find(it->first);
            if (sizeIt != s_TextureSizes.end()) {
                s_TextureCacheMemoryBytes -= sizeIt->second;
                s_TextureSizes.erase(sizeIt);
            }
            it = s_TextureCache.erase(it);
        } else {
            ++it;
        }
    }
}

void AssetManager::Init() {
    Clear();
}
void AssetManager::Shutdown() {
    Clear();
}
Ref<GpuTexture> AssetManager::LoadTexture(const std::string& filepath, GpuDevice* device) {
    if (!device || !device->IsReady())
        return {};
    const auto key = Key(filepath);
    {
        std::lock_guard<std::mutex> lock(s_AssetMutex);
        auto it = s_TextureCache.find(key);
        if (it != s_TextureCache.end() && it->second && it->second->IsValid() &&
            it->second->BelongsTo(*device))
            return it->second;
    }
    int width = 0, height = 0;
    stbi_uc* pixels = DecodeTexture(filepath, width, height);
    if (!pixels)
        return {};
    auto texture = CreateRef<GpuTexture>();
    const bool valid = width > 0 && height > 0 &&
                       texture->Initialize(device, static_cast<uint32>(width),
                                           static_cast<uint32>(height), pixels);
    stbi_image_free(pixels);
    if (!valid)
        return {};

    const size_t textureBytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
    {
        std::lock_guard<std::mutex> lock(s_AssetMutex);
        EvictUnusedUnderBudget(textureBytes);

        auto prevIt = s_TextureSizes.find(key);
        if (prevIt != s_TextureSizes.end())
            s_TextureCacheMemoryBytes -= prevIt->second;

        s_TextureCache[key] = texture;
        s_TextureSizes[key] = textureBytes;
        s_TextureCacheMemoryBytes += textureBytes;
    }
    return texture;
}

bool AssetManager::ValidateTextureFile(const std::string& filepath, int* outWidth, int* outHeight) {
    if (outWidth)
        *outWidth = 0;
    if (outHeight)
        *outHeight = 0;

    std::error_code ec;
    const auto p = Path(filepath);
    const auto fileSize = std::filesystem::file_size(p, ec);
    if (ec || fileSize == 0 || fileSize > MaxTextureFileSizeBytes)
        return false;

    size_t byteCount = 0;
    void* fileData = SDL_LoadFile(filepath.c_str(), &byteCount);
    if (!fileData)
        return false;
    if (byteCount == 0 || byteCount > MaxTextureFileSizeBytes ||
        byteCount > static_cast<size_t>(std::numeric_limits<int>::max())) {
        SDL_free(fileData);
        return false;
    }

    int testW = 0, testH = 0, testChannels = 0;
    const int infoResult =
        stbi_info_from_memory(static_cast<const stbi_uc*>(fileData), static_cast<int>(byteCount),
                              &testW, &testH, &testChannels);
    SDL_free(fileData);
    if (!infoResult)
        return false;

    if (testW <= 0 || testH <= 0 || testW > MaxTextureDimension || testH > MaxTextureDimension)
        return false;

    const uint64 requiredMemory = static_cast<uint64>(testW) * static_cast<uint64>(testH) * 4ull;
    if (requiredMemory > MaxDecodedTextureBytes)
        return false;

    if (outWidth)
        *outWidth = testW;
    if (outHeight)
        *outHeight = testH;
    return true;
}

bool AssetManager::DecodeTextureCpu(const std::string& filepath, int& outWidth, int& outHeight,
                                    std::vector<uint8>& outRgbaPixels) {
    outWidth = 0;
    outHeight = 0;
    outRgbaPixels.clear();

    int width = 0, height = 0;
    stbi_uc* pixels = DecodeTexture(filepath, width, height);
    if (!pixels)
        return false;

    const size_t byteSize = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
    outRgbaPixels.resize(byteSize);
    std::memcpy(outRgbaPixels.data(), pixels, byteSize);
    stbi_image_free(pixels);

    outWidth = width;
    outHeight = height;
    return true;
}

bool AssetManager::HasTexture(const std::string& filepath) {
    std::lock_guard<std::mutex> lock(s_AssetMutex);
    const auto it = s_TextureCache.find(Key(filepath));
    return it != s_TextureCache.end() && it->second && it->second->IsValid();
}
void AssetManager::UnloadTexture(const std::string& filepath) {
    std::lock_guard<std::mutex> lock(s_AssetMutex);
    const auto key = Key(filepath);
    s_TextureCache.erase(key);
    auto it = s_TextureSizes.find(key);
    if (it != s_TextureSizes.end()) {
        s_TextureCacheMemoryBytes -= it->second;
        s_TextureSizes.erase(it);
    }
}
bool AssetManager::LoadShaderBytes(const std::string& filepath, std::vector<uint8>& outBytes) {
    outBytes.clear();
    std::ifstream in(Path(filepath), std::ios::binary | std::ios::ate);
    if (!in)
        return false;
    const auto length = in.tellg();
    if (length <= 0 || length > 64 * 1024 * 1024)
        return false;
    std::vector<uint8> bytes(static_cast<size_t>(length));
    in.seekg(0);
    if (!in.read(reinterpret_cast<char*>(bytes.data()), length))
        return false;
    outBytes.swap(bytes);
    return true;
}
bool AssetManager::LoadMesh(const std::string& filepath, RawMeshData& outMesh) {
    outMesh = {};
    const auto key = Key(filepath);
    {
        std::lock_guard<std::mutex> lock(s_AssetMutex);
        if (auto it = s_MeshCache.find(key); it != s_MeshCache.end()) {
            outMesh = it->second;
            return true;
        }
    }
    std::error_code ec;
    const auto fileSize = std::filesystem::file_size(Path(filepath), ec);
    if (ec || fileSize == 0 || fileSize > MaxMeshSizeBytes)
        return false;
    std::ifstream in(Path(filepath));
    if (!in)
        return false;
    RawMeshData mesh;
    std::string line;
    try {
        while (std::getline(in, line)) {
            std::istringstream row(line);
            std::string kind;
            row >> kind;
            if (kind == "v") {
                float x = 0, y = 0, z = 0;
                if (!(row >> x >> y >> z) || !std::isfinite(x) || !std::isfinite(y) ||
                    !std::isfinite(z))
                    return false;
                if (mesh.vertices.size() + 3 > MaxMeshVertexElements)
                    return false;
                mesh.vertices.insert(mesh.vertices.end(), {x, y, z});
            } else if (kind == "f") {
                std::vector<uint32> face;
                std::string token;
                while (row >> token) {
                    if (token.starts_with('#'))
                        break;
                    const auto slash = token.find('/');
                    const auto indexText =
                        slash == std::string::npos ? token : token.substr(0, slash);
                    if (indexText.empty())
                        return false;
                    size_t used = 0;
                    const auto index = std::stoll(indexText, &used);
                    if (used != indexText.size() || !index)
                        return false;
                    const auto count = static_cast<int64>(mesh.vertices.size() / 3);
                    if (count <= 0 || index < -count || index > count)
                        return false;
                    const auto resolved = index > 0 ? index - 1 : count + index;
                    if (resolved < 0 || resolved >= count ||
                        resolved > std::numeric_limits<uint32>::max())
                        return false;
                    face.push_back(static_cast<uint32>(resolved));
                    if (face.size() > MaxFaceVertices)
                        return false;
                }
                if (face.size() < 3)
                    return false;
                const size_t addedIndices = (face.size() - 2) * 3;
                if (mesh.indices.size() + addedIndices > MaxMeshIndexElements)
                    return false;
                for (size_t j = 1; j + 1 < face.size(); ++j) {
                    mesh.indices.insert(mesh.indices.end(), {face[0], face[j], face[j + 1]});
                }
            }
        }
    } catch (const std::exception&) {
        return false;
    }
    if (in.bad() || mesh.vertices.empty() || mesh.indices.empty())
        return false;
    const size_t meshBytes =
        mesh.vertices.size() * sizeof(float) + mesh.indices.size() * sizeof(uint32);
    {
        std::lock_guard<std::mutex> lock(s_AssetMutex);
        EvictUnusedUnderBudget(meshBytes);

        auto prevIt = s_MeshSizes.find(key);
        if (prevIt != s_MeshSizes.end())
            s_MeshCacheMemoryBytes -= prevIt->second;

        s_MeshCache[key] = mesh;
        s_MeshSizes[key] = meshBytes;
        s_MeshCacheMemoryBytes += meshBytes;
    }
    outMesh = std::move(mesh);
    return true;
}
void AssetManager::Clear() {
    std::lock_guard<std::mutex> lock(s_AssetMutex);
    s_TextureCache.clear();
    s_MeshCache.clear();
    s_TextureSizes.clear();
    s_MeshSizes.clear();
    s_TextureCacheMemoryBytes = 0;
    s_MeshCacheMemoryBytes = 0;
}
size_t AssetManager::GetCachedTextureCount() {
    std::lock_guard<std::mutex> lock(s_AssetMutex);
    return s_TextureCache.size();
}
size_t AssetManager::GetCachedMeshCount() {
    std::lock_guard<std::mutex> lock(s_AssetMutex);
    return s_MeshCache.size();
}
size_t AssetManager::GetTextureCacheMemoryBytes() {
    std::lock_guard<std::mutex> lock(s_AssetMutex);
    return s_TextureCacheMemoryBytes;
}
size_t AssetManager::GetMeshCacheMemoryBytes() {
    std::lock_guard<std::mutex> lock(s_AssetMutex);
    return s_MeshCacheMemoryBytes;
}
size_t AssetManager::GetTotalCacheMemoryBytes() {
    std::lock_guard<std::mutex> lock(s_AssetMutex);
    return s_TextureCacheMemoryBytes + s_MeshCacheMemoryBytes;
}
void AssetManager::SetCacheMemoryBudget(size_t maxBytes) {
    std::lock_guard<std::mutex> lock(s_AssetMutex);
    s_CacheMemoryBudget = maxBytes;
    EvictUnusedUnderBudget(0);
}
size_t AssetManager::GetCacheMemoryBudget() {
    std::lock_guard<std::mutex> lock(s_AssetMutex);
    return s_CacheMemoryBudget;
}
} // namespace LuxEngine
