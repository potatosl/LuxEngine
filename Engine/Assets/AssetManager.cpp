#include "Engine/Assets/AssetManager.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/Renderer/RHI/GpuDevice.hpp"
#include <SDL3/SDL.h>
#include <cstring>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_FAILURE_USERMSG
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <locale>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <stb_image.h>
#include <stdexcept>
#include <tuple>

namespace LuxEngine {
std::unordered_map<std::string, Ref<GpuTexture>> AssetManager::s_TextureCache;
std::unordered_map<std::string, Ref<const RawMeshData>> AssetManager::s_MeshCache;
size_t AssetManager::s_TextureCacheMemoryBytes = 0;
size_t AssetManager::s_MeshCacheMemoryBytes = 0;
size_t AssetManager::s_CacheMemoryBudget = 512ull * 1024ull * 1024ull; // 512 MiB default

namespace {
std::mutex s_AssetMutex, s_AssetKeyMutex;
std::unordered_map<std::string, std::string> s_AssetKeys;
std::unordered_map<std::string, size_t> s_TextureSizes;
size_t MeshBytes(const RawMeshData& mesh) {
    return (mesh.vertices.size() + mesh.normals.size() + mesh.texCoords.size()) * sizeof(float) +
           mesh.indices.size() * sizeof(uint32);
}
std::unordered_map<std::string, uint64> s_MeshVersions, s_TextureVersions;
std::unordered_map<std::string, uint64> s_MeshRequests, s_TextureRequests;
std::unordered_map<std::string, std::string> s_MeshErrors, s_TextureErrors;
uint64 s_AssetSequence = 0, s_AssetEpoch = 0, s_ClearRevision = 0;

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
    const auto input = Path(path);
    const bool absolute = input.is_absolute();
    if (absolute) {
        std::lock_guard lock(s_AssetKeyMutex);
        if (const auto found = s_AssetKeys.find(path); found != s_AssetKeys.end())
            return found->second;
    }
    std::error_code error;
    const auto result = std::filesystem::weakly_canonical(input, error);
    const auto utf8 = result.generic_u8string();
    const auto key =
        error ? path : std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
    if (absolute && !error) {
        std::lock_guard lock(s_AssetKeyMutex);
        if (s_AssetKeys.size() >= 4096)
            s_AssetKeys.erase(s_AssetKeys.begin());
        s_AssetKeys[path] = key;
    }
    return key;
}
bool ReadTextureFile(const std::string& filepath, std::vector<uint8>& data) {
    data.clear();
    if (filepath.empty() || filepath.find('\0') != std::string::npos)
        return false;
    try {
        const auto file = Path(filepath);
        std::error_code ec;
        const auto size = std::filesystem::file_size(file, ec);
        if (ec || size == 0 || size > MaxTextureFileSizeBytes)
            return false;
        const auto time = std::filesystem::last_write_time(file, ec);
        if (ec)
            return false;
        std::ifstream input(file, std::ios::binary);
        if (!input)
            return false;
        data.resize(static_cast<size_t>(size));
        if (!input.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size)) ||
            input.peek() != std::char_traits<char>::eof() ||
            std::filesystem::file_size(file, ec) != size || ec ||
            std::filesystem::last_write_time(file, ec) != time || ec) {
            data.clear();
            return false;
        }
        return true;
    } catch (...) {
        data.clear();
        return false;
    }
}
stbi_uc* DecodeTexture(const std::string& filepath, int& width, int& height) {
    std::vector<uint8> fileData;
    if (!ReadTextureFile(filepath, fileData)) {
        Log::Warn("Image file missing, changing, empty or exceeds size limit: " + filepath);
        return nullptr;
    }
    const auto byteCount = fileData.size();
    // Inspect image header before allocating decoded pixels to prevent decompression bombs
    int testW = 0, testH = 0, testChannels = 0;
    if (!stbi_info_from_memory(fileData.data(), static_cast<int>(byteCount), &testW, &testH,
                               &testChannels)) {
        Log::Warn("Image header validation failed: " + filepath);
        return nullptr;
    }

    if (testW <= 0 || testH <= 0 || testW > MaxTextureDimension || testH > MaxTextureDimension) {
        Log::Warn("Image dimensions exceed allowed maximum (" +
                  std::to_string(MaxTextureDimension) + "): " + filepath);
        return nullptr;
    }

    const uint64 requiredMemory = static_cast<uint64>(testW) * static_cast<uint64>(testH) * 4ull;
    if (requiredMemory > MaxDecodedTextureBytes) {
        Log::Warn("Image uncompressed pixel memory exceeds limit: " + filepath);
        return nullptr;
    }

    // Detect truncated BMP files where declared resolution exceeds actual file payload
    const auto* rawBytes = fileData.data();
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
            Log::Warn("BMP image file truncated before pixel data: " + filepath);
            return nullptr;
        }
        if (compression == 0 && bpp > 0 && bmpW > 0 && bmpH > 0) {
            const uint64 rowBytes = ((static_cast<uint64>(bmpW) * bpp + 31ull) / 32ull) * 4ull;
            const uint64 minExpected = static_cast<uint64>(pixelOffset) + rowBytes * bmpH;
            if (byteCount < minExpected) {
                Log::Warn("BMP image file truncated: declared resolution requires " +
                          std::to_string(minExpected) + " bytes but file has " +
                          std::to_string(byteCount));
                return nullptr;
            }
        }
    }

    int channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(fileData.data(), static_cast<int>(byteCount), &width,
                                            &height, &channels, STBI_rgb_alpha);
    const char* failureReason = stbi_failure_reason();
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
    while (!s_MeshCache.empty() &&
           s_TextureCacheMemoryBytes + s_MeshCacheMemoryBytes + incomingBytes >
               s_CacheMemoryBudget) {
        const auto mesh = s_MeshCache.begin();
        s_MeshCacheMemoryBytes -= MeshBytes(*mesh->second);
        s_MeshCache.erase(mesh);
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
    if (filepath.empty() || filepath.find('\0') != std::string::npos)
        return {};
    std::string key;
    try {
        key = Key(filepath);
    } catch (const std::exception&) {
        return {};
    }
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
    std::vector<uint8> bytes;
    if (!ReadTextureFile(filepath, bytes))
        return false;
    int width = 0, height = 0, channels = 0;
    if (!stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height,
                               &channels) ||
        width <= 0 || height <= 0 || width > MaxTextureDimension || height > MaxTextureDimension ||
        static_cast<uint64>(width) * static_cast<uint64>(height) * 4 > MaxDecodedTextureBytes)
        return false;
    if (outWidth)
        *outWidth = width;
    if (outHeight)
        *outHeight = height;
    return true;
}

bool AssetManager::DecodeTextureCpu(const std::string& filepath, int& outWidth, int& outHeight,
                                    std::vector<uint8>& outRgbaPixels) {
    outWidth = outHeight = 0;
    outRgbaPixels.clear();
    try {
        int width = 0, height = 0;
        auto* decoded = DecodeTexture(filepath, width, height);
        if (!decoded)
            return false;
        std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(decoded, &stbi_image_free);
        const size_t bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
        outRgbaPixels.resize(bytes);
        std::memcpy(outRgbaPixels.data(), pixels.get(), bytes);
        outWidth = width;
        outHeight = height;
        return true;
    } catch (const std::exception&) {
        outRgbaPixels.clear();
        return false;
    }
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
    if (filepath.empty() || filepath.find('\0') != std::string::npos)
        return false;
    std::filesystem::path file;
    try {
        file = Path(filepath);
    } catch (const std::exception&) {
        return false;
    }
    std::ifstream in(file, std::ios::binary | std::ios::ate);
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
namespace {
bool ReadMeshFile(const std::string& filepath, RawMeshData& outMesh) {
    std::error_code ec;
    const auto fileSize = std::filesystem::file_size(Path(filepath), ec);
    if (ec || fileSize == 0 || fileSize > MaxMeshSizeBytes)
        return false;
    const auto writeTime = std::filesystem::last_write_time(Path(filepath), ec);
    if (ec)
        return false;
    std::ifstream in(Path(filepath));
    if (!in)
        return false;
    RawMeshData mesh;
    std::vector<Vec3> positions, normals;
    std::vector<Vec2> uvs;
    std::vector<bool> authoredNormals;
    std::vector<std::pair<int64, int64>> normalGroups;
    std::map<std::tuple<int64, int64, int64, int64>, uint32> corners;
    std::string line;
    int64 smoothing = 0, faceNumber = 0;
    bool firstLine = true, allUvs = true, sawUv = false;
    in.imbue(std::locale::classic());
    try {
        const auto resolve = [](const std::string& text, size_t count) -> int64 {
            size_t used = 0;
            const auto value = std::stoll(text, &used);
            if (used != text.size() || value == 0 || value < -static_cast<int64>(count) ||
                value > static_cast<int64>(count))
                throw std::runtime_error("Invalid OBJ index");
            return value > 0 ? value - 1 : static_cast<int64>(count) + value;
        };
        while (std::getline(in, line)) {
            if (firstLine && line.starts_with("\xef\xbb\xbf"))
                line.erase(0, 3);
            firstLine = false;
            std::istringstream row(line);
            row.imbue(std::locale::classic());
            std::string kind;
            row >> kind;
            if (kind == "v" || kind == "vn") {
                Vec3 v;
                if (!(row >> v.x >> v.y >> v.z) || !std::isfinite(v.x) || !std::isfinite(v.y) ||
                    !std::isfinite(v.z))
                    return false;
                auto& values = kind == "v" ? positions : normals;
                if (values.size() >= MaxMeshVertexElements / 3)
                    return false;
                if (kind == "vn") {
                    if (v.Dot(v) < 1e-20f || !std::isfinite(v.Dot(v)))
                        return false;
                    v = v.Normalized();
                }
                values.push_back(v);
            } else if (kind == "vt") {
                Vec2 uv;
                if (!(row >> uv.x))
                    return false;
                row >> std::ws;
                if (row.peek() != std::char_traits<char>::eof() && row.peek() != '#')
                    if (!(row >> uv.y))
                        return false;
                if (!std::isfinite(uv.x) || !std::isfinite(uv.y) ||
                    uvs.size() >= MaxMeshVertexElements / 3)
                    return false;
                uvs.push_back(uv);
            } else if (kind == "s") {
                std::string group;
                if (!(row >> group))
                    return false;
                size_t used = 0;
                smoothing = group == "off" ? 0 : group == "on" ? 1 : std::stoll(group, &used);
                if (smoothing < 0 || (group != "off" && group != "on" && used != group.size()))
                    return false;
            } else if (kind == "f") {
                ++faceNumber;
                std::vector<uint32> face;
                std::string token;
                while (row >> token) {
                    if (token.starts_with('#'))
                        break;
                    const auto first = token.find('/');
                    const auto second =
                        first == std::string::npos ? first : token.find('/', first + 1);
                    if (second != std::string::npos &&
                        token.find('/', second + 1) != std::string::npos)
                        return false;
                    const auto p = resolve(token.substr(0, first), positions.size());
                    int64 uv = -1, normal = -1;
                    if (first != std::string::npos) {
                        const auto text = token.substr(
                            first + 1, second == std::string::npos ? second : second - first - 1);
                        if (!text.empty())
                            uv = resolve(text, uvs.size());
                        else if (second == std::string::npos)
                            return false;
                    }
                    allUvs &= uv >= 0;
                    sawUv |= uv >= 0;
                    if (second != std::string::npos)
                        normal = resolve(token.substr(second + 1), normals.size());
                    const auto key = std::tuple{p, uv, normal,
                                                normal >= 0 ? int64{0}
                                                : smoothing ? smoothing
                                                            : -faceNumber};
                    auto found = corners.find(key);
                    if (found == corners.end()) {
                        if (mesh.vertices.size() + 3 > MaxMeshVertexElements)
                            return false;
                        const auto index = static_cast<uint32>(mesh.vertices.size() / 3);
                        found = corners.emplace(key, index).first;
                        const auto& position = positions[static_cast<size_t>(p)];
                        const auto n = normal >= 0 ? normals[static_cast<size_t>(normal)] : Vec3{};
                        const auto t = uv >= 0 ? uvs[static_cast<size_t>(uv)] : Vec2{};
                        mesh.vertices.insert(mesh.vertices.end(),
                                             {position.x, position.y, position.z});
                        mesh.normals.insert(mesh.normals.end(), {n.x, n.y, n.z});
                        mesh.texCoords.insert(mesh.texCoords.end(), {t.x, t.y});
                        authoredNormals.push_back(normal >= 0);
                        normalGroups.emplace_back(p, smoothing ? smoothing : -faceNumber);
                    }
                    face.push_back(found->second);
                    if (face.size() > MaxFaceVertices)
                        return false;
                }
                if (face.size() < 3 ||
                    mesh.indices.size() + (face.size() - 2) * 3 > MaxMeshIndexElements)
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
    mesh.hasTexCoords = allUvs && sawUv;
    mesh.boundsMin = mesh.boundsMax = {mesh.vertices[0], mesh.vertices[1], mesh.vertices[2]};
    for (size_t i = 0; i < mesh.vertices.size(); i += 3) {
        mesh.boundsMin.x = std::min(mesh.boundsMin.x, mesh.vertices[i]);
        mesh.boundsMin.y = std::min(mesh.boundsMin.y, mesh.vertices[i + 1]);
        mesh.boundsMin.z = std::min(mesh.boundsMin.z, mesh.vertices[i + 2]);
        mesh.boundsMax.x = std::max(mesh.boundsMax.x, mesh.vertices[i]);
        mesh.boundsMax.y = std::max(mesh.boundsMax.y, mesh.vertices[i + 1]);
        mesh.boundsMax.z = std::max(mesh.boundsMax.z, mesh.vertices[i + 2]);
    }
    const auto position = [&](uint32 index) {
        return Vec3{mesh.vertices[index * 3], mesh.vertices[index * 3 + 1],
                    mesh.vertices[index * 3 + 2]};
    };
    std::map<std::pair<int64, int64>, Vec3> generatedNormals;
    for (size_t i = 0; i < mesh.indices.size(); i += 3) {
        const auto a = mesh.indices[i], b = mesh.indices[i + 1], c = mesh.indices[i + 2];
        const auto n = (position(b) - position(a)).Cross(position(c) - position(a));
        if (!std::isfinite(n.x) || !std::isfinite(n.y) || !std::isfinite(n.z))
            return false;
        for (const auto index : {a, b, c})
            if (!authoredNormals[index]) {
                auto& sum = generatedNormals[normalGroups[index]];
                sum = sum + n;
            }
    }
    for (size_t i = 0; i < mesh.normals.size(); i += 3) {
        const auto n = (authoredNormals[i / 3]
                            ? Vec3{mesh.normals[i], mesh.normals[i + 1], mesh.normals[i + 2]}
                            : generatedNormals[normalGroups[i / 3]])
                           .Normalized();
        if (!std::isfinite(n.x) || !std::isfinite(n.y) || !std::isfinite(n.z))
            return false;
        mesh.normals[i] = n.x;
        mesh.normals[i + 1] = n.y;
        mesh.normals[i + 2] = n.z;
    }
    if (std::filesystem::file_size(Path(filepath), ec) != fileSize || ec ||
        std::filesystem::last_write_time(Path(filepath), ec) != writeTime || ec)
        return false;
    outMesh = std::move(mesh);
    return true;
}
void RecordAssetError(const std::string& key, const std::string& message, bool texture) {
    auto& errors = texture ? s_TextureErrors : s_MeshErrors;
    if (errors.size() >= 128 && !errors.contains(key))
        errors.erase(errors.begin());
    errors[key] = message;
}
void RecordAssetVersion(const std::string& key, uint64 version, bool texture) {
    auto& versions = texture ? s_TextureVersions : s_MeshVersions;
    if (versions.size() >= 4096 && !versions.contains(key))
        versions.erase(versions.begin());
    versions[key] = version;
}
} // namespace

bool AssetManager::ValidateMeshFile(const std::string& filepath, bool* hasTexCoords) {
    if (hasTexCoords)
        *hasTexCoords = false;
    if (filepath.empty() || filepath.find('\0') != std::string::npos)
        return false;
    try {
        RawMeshData data;
        if (!ReadMeshFile(filepath, data))
            return false;
        if (hasTexCoords)
            *hasTexCoords = data.hasTexCoords;
        return true;
    } catch (...) {
        return false;
    }
}

bool AssetManager::LoadMesh(const std::string& filepath, RawMeshData& outMesh, bool reload) {
    std::string error;
    return LoadMeshImpl(filepath, outMesh, reload, error);
}

bool AssetManager::LoadMeshImpl(const std::string& filepath, RawMeshData& outMesh, bool reload,
                                std::string& error, bool requireTexCoords) {
    outMesh = {};
    error.clear();
    if (filepath.empty() || filepath.find('\0') != std::string::npos) {
        error = "메시 경로가 올바르지 않습니다.";
        return false;
    }
    try {
        if (reload) {
            std::lock_guard lock(s_AssetKeyMutex);
            s_AssetKeys.erase(filepath);
        }
        const auto key = Key(filepath);
        uint64 epoch, ticket;
        {
            std::lock_guard lock(s_AssetMutex);
            if (!reload)
                if (const auto found = s_MeshCache.find(key); found != s_MeshCache.end()) {
                    auto copy = *found->second;
                    outMesh = std::move(copy);
                    return true;
                }
            epoch = s_AssetEpoch;
            ticket = ++s_AssetSequence;
            if (s_MeshRequests.size() >= 4096 && !s_MeshRequests.contains(key))
                s_MeshRequests.erase(s_MeshRequests.begin());
            s_MeshRequests[key] = ticket;
        }
        RawMeshData mesh;
        if (!ReadMeshFile(filepath, mesh)) {
            std::lock_guard lock(s_AssetMutex);
            error = "OBJ 메시 파일을 읽을 수 없거나 내용이 올바르지 않습니다: " + filepath;
            const auto request = s_MeshRequests.find(key);
            if (epoch == s_AssetEpoch && request != s_MeshRequests.end() &&
                request->second == ticket)
                RecordAssetError(key, error, false);
            return false;
        }
        if (requireTexCoords && !mesh.hasTexCoords) {
            error = "텍스처를 적용할 UV 좌표가 없는 OBJ입니다: " + filepath;
            std::lock_guard lock(s_AssetMutex);
            const auto request = s_MeshRequests.find(key);
            if (epoch == s_AssetEpoch && request != s_MeshRequests.end() &&
                request->second == ticket)
                RecordAssetError(key, error, false);
            return false;
        }
        auto next = CreateRef<RawMeshData>(std::move(mesh));
        auto result = *next;
        const auto bytes = MeshBytes(*next);
        {
            std::lock_guard lock(s_AssetMutex);
            const auto request = s_MeshRequests.find(key);
            if (epoch != s_AssetEpoch || request == s_MeshRequests.end() ||
                request->second != ticket) {
                error = "더 최근 요청 또는 캐시 초기화로 메시 갱신이 취소되었습니다.";
                return false;
            }
            const auto old = s_MeshCache.find(key);
            const auto oldBytes = old == s_MeshCache.end() ? 0 : MeshBytes(*old->second);
            const auto [version, inserted] = s_MeshVersions.try_emplace(key, 0);
            try {
                if (bytes <= s_CacheMemoryBudget) {
                    s_MeshCache.insert_or_assign(key, next);
                    s_MeshCacheMemoryBytes = s_MeshCacheMemoryBytes - oldBytes + bytes;
                } else if (old != s_MeshCache.end()) {
                    s_MeshCache.erase(old);
                    s_MeshCacheMemoryBytes -= oldBytes;
                }
            } catch (...) {
                if (inserted)
                    s_MeshVersions.erase(version);
                throw;
            }
            next->revision = result.revision = ticket;
            version->second = next->revision;
            if (s_MeshVersions.size() > 4096) {
                auto oldest = std::min_element(
                    s_MeshVersions.begin(), s_MeshVersions.end(),
                    [](const auto& a, const auto& b) { return a.second < b.second; });
                s_MeshVersions.erase(oldest);
            }
            EvictUnusedUnderBudget(0);
            s_MeshErrors.erase(key);
        }
        outMesh = std::move(result);
        return true;
    } catch (const std::bad_alloc&) {
        error = "메시를 갱신할 CPU 메모리가 부족합니다.";
        return false;
    } catch (const std::exception&) {
        error = "메시 경로 또는 데이터를 처리하지 못했습니다.";
        return false;
    }
}

bool AssetManager::ReloadMesh(const std::string& filepath, std::string& error,
                              bool requireTexCoords) {
    RawMeshData mesh;
    return LoadMeshImpl(filepath, mesh, true, error, requireTexCoords);
}

bool AssetManager::IsBaseColorTextureFile(const std::string& filepath) {
    if (filepath.empty() || filepath.find('\0') != std::string::npos)
        return false;
    try {
        auto bytes = Path(filepath).extension().u8string();
        std::string extension(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
               extension == ".bmp" || extension == ".tga";
    } catch (...) {
        return false;
    }
}

bool AssetManager::ReloadTexture(const std::string& filepath, std::string& error) {
    error.clear();
    if (!IsBaseColorTextureFile(filepath)) {
        error = "기본 색상 텍스처는 PNG/JPG/JPEG/BMP/TGA 파일이어야 합니다.";
        return false;
    }
    try {
        const auto key = Key(filepath);
        uint64 epoch, ticket;
        {
            std::lock_guard lock(s_AssetMutex);
            epoch = s_AssetEpoch;
            ticket = ++s_AssetSequence;
            if (s_TextureRequests.size() >= 4096 && !s_TextureRequests.contains(key))
                s_TextureRequests.erase(s_TextureRequests.begin());
            s_TextureRequests[key] = ticket;
        }
        std::error_code ec;
        const auto file = Path(filepath);
        const auto size = std::filesystem::file_size(file, ec);
        const auto time = std::filesystem::last_write_time(file, ec);
        int width = 0, height = 0;
        std::vector<uint8> pixels;
        const bool decoded = !ec && DecodeTextureCpu(filepath, width, height, pixels);
        const bool stable = decoded && std::filesystem::file_size(file, ec) == size && !ec &&
                            std::filesystem::last_write_time(file, ec) == time && !ec;
        std::lock_guard lock(s_AssetMutex);
        const auto request = s_TextureRequests.find(key);
        if (epoch != s_AssetEpoch || request == s_TextureRequests.end() ||
            request->second != ticket) {
            error = "텍스처 캐시가 변경되어 다시 불러오기가 취소되었습니다.";
            return false;
        }
        if (!stable) {
            error = "텍스처 파일을 읽을 수 없거나 내용이 올바르지 않습니다: " + filepath;
            RecordAssetError(key, error, true);
            return false;
        }
        RecordAssetVersion(key, ticket, true);
        s_TextureCache.erase(key);
        if (const auto size = s_TextureSizes.find(key); size != s_TextureSizes.end()) {
            s_TextureCacheMemoryBytes -= size->second;
            s_TextureSizes.erase(size);
        }
        s_TextureErrors.erase(key);
        return true;
    } catch (const std::exception&) {
        error = "텍스처 경로 또는 데이터가 올바르지 않습니다.";
        return false;
    }
}

uint64 AssetManager::GetMeshRevision(const std::string& filepath) {
    try {
        const auto key = Key(filepath);
        std::lock_guard lock(s_AssetMutex);
        const auto found = s_MeshVersions.find(key);
        if (found != s_MeshVersions.end())
            return found->second;
        const auto cached = s_MeshCache.find(key);
        return cached == s_MeshCache.end() ? s_ClearRevision : cached->second->revision;
    } catch (...) {
        return 0;
    }
}
uint64 AssetManager::GetTextureRevision(const std::string& filepath) {
    try {
        const auto key = Key(filepath);
        std::lock_guard lock(s_AssetMutex);
        const auto found = s_TextureVersions.find(key);
        return found == s_TextureVersions.end() ? s_ClearRevision : found->second;
    } catch (...) {
        return 0;
    }
}
std::string AssetManager::GetAssetError(const std::string& filepath, bool texture) {
    try {
        const auto key = Key(filepath);
        std::lock_guard lock(s_AssetMutex);
        const auto& errors = texture ? s_TextureErrors : s_MeshErrors;
        const auto found = errors.find(key);
        return found == errors.end() ? std::string{} : found->second;
    } catch (...) {
        return "자산 경로가 올바르지 않습니다.";
    }
}

bool AssetManager::GetMeshBounds(const std::string& filepath, Vec3& minimum, Vec3& maximum) {
    if (filepath.empty() || filepath.find('\0') != std::string::npos)
        return false;
    const auto key = Key(filepath);
    {
        std::lock_guard lock(s_AssetMutex);
        if (const auto found = s_MeshCache.find(key); found != s_MeshCache.end()) {
            minimum = found->second->boundsMin;
            maximum = found->second->boundsMax;
            return true;
        }
    }
    RawMeshData mesh;
    if (!LoadMesh(filepath, mesh))
        return false;
    minimum = mesh.boundsMin;
    maximum = mesh.boundsMax;
    return true;
}

void AssetManager::Clear() {
    std::lock_guard<std::mutex> lock(s_AssetMutex);
    ++s_AssetEpoch;
    s_ClearRevision = ++s_AssetSequence;
    {
        std::lock_guard keyLock(s_AssetKeyMutex);
        s_AssetKeys.clear();
    }
    s_MeshVersions.clear();
    s_TextureVersions.clear();
    s_MeshRequests.clear();
    s_TextureRequests.clear();
    s_MeshErrors.clear();
    s_TextureErrors.clear();
    s_TextureCache.clear();
    s_MeshCache.clear();
    s_TextureSizes.clear();
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
