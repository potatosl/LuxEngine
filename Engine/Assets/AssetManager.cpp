#include "Engine/Assets/AssetManager.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/Renderer/RHI/GpuDevice.hpp"
#include <SDL3/SDL.h>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_FAILURE_USERMSG
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stb_image.h>

namespace LuxEngine {
std::unordered_map<std::string, Ref<GpuTexture>> AssetManager::s_TextureCache;
std::unordered_map<std::string, RawMeshData> AssetManager::s_MeshCache;
namespace {
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
    size_t byteCount = 0;
    void* fileData = SDL_LoadFile(filepath.c_str(), &byteCount);
    if (!fileData)
        return nullptr;
    if (byteCount > static_cast<size_t>(std::numeric_limits<int>::max())) {
        SDL_free(fileData);
        return nullptr;
    }
    int channels = 0;
    stbi_uc* pixels =
        stbi_load_from_memory(static_cast<const stbi_uc*>(fileData), static_cast<int>(byteCount),
                              &width, &height, &channels, STBI_rgb_alpha);
    SDL_free(fileData);
    if (!pixels)
        Log::Warn(std::string("Image load failed: ") + filepath);
    return pixels;
}
} // namespace
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
    auto it = s_TextureCache.find(key);
    if (it != s_TextureCache.end() && it->second && it->second->IsValid() &&
        it->second->BelongsTo(*device))
        return it->second;
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
    s_TextureCache[key] = texture;
    return texture;
}
bool AssetManager::HasTexture(const std::string& filepath) {
    const auto it = s_TextureCache.find(Key(filepath));
    return it != s_TextureCache.end() && it->second && it->second->IsValid();
}
void AssetManager::UnloadTexture(const std::string& filepath) {
    s_TextureCache.erase(Key(filepath));
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
    if (auto it = s_MeshCache.find(key); it != s_MeshCache.end()) {
        outMesh = it->second;
        return true;
    }
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
                mesh.vertices.insert(mesh.vertices.end(), {x, y, z});
            } else if (kind == "f") {
                std::vector<uint32> face;
                std::string token;
                while (row >> token) {
                    if (token.starts_with('#'))
                        break;
                    const auto indexText = token.substr(0, token.find('/'));
                    size_t used = 0;
                    const auto index = std::stoll(indexText, &used);
                    if (used != indexText.size() || !index)
                        return false;
                    const auto count = static_cast<int64>(mesh.vertices.size() / 3);
                    const auto resolved = index > 0 ? index - 1 : count + index;
                    if (resolved < 0 || resolved >= count ||
                        resolved > std::numeric_limits<uint32>::max())
                        return false;
                    face.push_back(static_cast<uint32>(resolved));
                }
                if (face.size() < 3)
                    return false;
                for (size_t j = 1; j + 1 < face.size(); ++j)
                    mesh.indices.insert(mesh.indices.end(), {face[0], face[j], face[j + 1]});
            }
        }
    } catch (const std::exception&) {
        return false;
    }
    if (in.bad() || mesh.vertices.empty() || mesh.indices.empty())
        return false;
    s_MeshCache[key] = mesh;
    outMesh = std::move(mesh);
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
