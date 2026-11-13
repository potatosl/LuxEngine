#include <LuxEngine/Assets/AssetManager.hpp>
#include <LuxEngine/Environment/EnvironmentMap.hpp>
#include <RainyCreek/Catalog.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace RainyCreek {
namespace {
namespace le = LuxEngine;
namespace fs = std::filesystem;
using Json = nlohmann::json;

fs::path Resolve(const fs::path& root, const Json& entry) {
    const auto relative = FromUtf8(entry.get<std::string>());
    if (relative.empty() || relative.is_absolute() || relative.has_root_name())
        throw std::runtime_error("에셋 경로는 상대 경로여야 합니다.");
    const auto base = fs::canonical(root);
    const auto resolved = fs::canonical(base / relative);
    const auto inside = resolved.lexically_relative(base);
    if (inside.empty() || *inside.begin() == ".." || !fs::is_regular_file(resolved))
        throw std::runtime_error("에셋 경로가 샘플 디렉터리를 벗어났습니다.");
    return resolved;
}

std::vector<ModelPart> ReadParts(const fs::path& root, const Json& entries) {
    if (!entries.is_array() || entries.empty() || entries.size() > 16)
        throw std::runtime_error("모델의 메시 목록이 올바르지 않습니다.");
    std::vector<ModelPart> result;
    for (const auto& entry : entries) {
        ModelPart part;
        part.mesh = Resolve(root, entry.at("mesh"));
        part.albedo = Resolve(root, entry.at("albedo"));
        part.roughness = entry.value("roughness", .9f);
        part.alphaMask = entry.value("alphaMask", false);
        if (!std::isfinite(part.roughness) || part.roughness < 0 || part.roughness > 1)
            throw std::runtime_error("모델의 거칠기 값이 올바르지 않습니다.");
        result.push_back(std::move(part));
    }
    return result;
}

le::RawMeshData ReadMesh(const ModelPart& part) {
    le::RawMeshData raw;
    if (!le::AssetManager::LoadMesh(ToUtf8(part.mesh), raw) || raw.vertices.empty() ||
        raw.vertices.size() % 3 != 0 || raw.normals.size() != raw.vertices.size() ||
        raw.texCoords.size() != raw.vertices.size() / 3 * 2 || raw.indices.size() % 3 != 0 ||
        raw.indices.empty())
        throw std::runtime_error("모델을 읽지 못했습니다: " + ToUtf8(part.mesh));
    for (const auto index : raw.indices)
        if (index >= raw.vertices.size() / 3)
            throw std::runtime_error("모델의 인덱스 범위가 올바르지 않습니다.");
    for (const auto* values : {&raw.vertices, &raw.normals, &raw.texCoords})
        for (const float value : *values)
            if (!std::isfinite(value))
                throw std::runtime_error("모델에 유효하지 않은 좌표가 있습니다.");
    for (const float value : raw.vertices)
        if (std::abs(value) > 10000.f)
            throw std::runtime_error("샘플 모델 좌표는 10km 이내여야 합니다.");
    return raw;
}

void ValidateTexture(const fs::path& path, bool alphaMask) {
    std::vector<le::uint8> pixels;
    int width = 0, height = 0;
    if (!le::AssetManager::DecodeTextureCpu(ToUtf8(path), width, height, pixels) || width <= 0 ||
        height <= 0 || pixels.size() != static_cast<size_t>(width) * height * 4)
        throw std::runtime_error("텍스처를 읽지 못했습니다: " + ToUtf8(path));
    // 현재 Renderer3D는 alpha == 0만 버립니다. 변환 시 이진 마스크를 만들어 사용합니다.
    if (alphaMask)
        for (size_t index = 3; index < pixels.size(); index += 4)
            if (pixels[index] != 0 && pixels[index] != 255)
                throw std::runtime_error("식생 알파 마스크는 0 또는 255여야 합니다.");
}
} // namespace

fs::path FromUtf8(std::string_view value) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(value.data()), value.size()));
}

std::string ToUtf8(const fs::path& value) {
    const auto text = value.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

Catalog LoadCatalog(const fs::path& root) {
    std::ifstream stream(root / "catalog.json");
    if (!stream)
        throw std::runtime_error("Rainy Creek 에셋 목록을 열지 못했습니다.");
    const auto data = Json::parse(stream);
    if (data.at("schema") != "luxengine.rainy_creek.catalog.v1")
        throw std::runtime_error("지원하지 않는 Rainy Creek 에셋 형식입니다.");
    Catalog result;
    result.ground = Resolve(root, data.at("textures").at("ground"));
    result.day = Resolve(root, data.at("environments").at("day"));
    result.dusk = Resolve(root, data.at("environments").at("dusk"));
    for (const char* id : {"rock_a", "rock_b", "rock_c", "fern_a", "fern_b", "trunk", "pine"}) {
        const auto& entry = data.at("models").at(id);
        Model model;
        model.parts = ReadParts(root, entry.at("parts"));
        if (entry.contains("highParts"))
            model.highParts = ReadParts(root, entry.at("highParts"));
        if (entry.contains("farParts"))
            model.farParts = ReadParts(root, entry.at("farParts"));
        const auto limit = std::numeric_limits<float>::max();
        model.minimum = {limit, limit, limit};
        model.maximum = {-limit, -limit, -limit};
        for (const auto& part : model.parts) {
            const auto raw = ReadMesh(part);
            for (size_t i = 0; i < raw.vertices.size(); i += 3) {
                model.minimum.x = std::min(model.minimum.x, raw.vertices[i]);
                model.minimum.y = std::min(model.minimum.y, raw.vertices[i + 1]);
                model.minimum.z = std::min(model.minimum.z, raw.vertices[i + 2]);
                model.maximum.x = std::max(model.maximum.x, raw.vertices[i]);
                model.maximum.y = std::max(model.maximum.y, raw.vertices[i + 1]);
                model.maximum.z = std::max(model.maximum.z, raw.vertices[i + 2]);
            }
        }
        if ((model.maximum - model.minimum).Dot(model.maximum - model.minimum) < .000001f)
            throw std::runtime_error("모델의 크기가 올바르지 않습니다.");
        result.models.emplace(id, std::move(model));
    }
    le::AssetManager::Clear();
    return result;
}

void ValidateCatalog(const Catalog& catalog) {
    ValidateTexture(catalog.ground, false);
    for (const auto& [id, model] : catalog.models) {
        for (const auto* parts : {&model.parts, &model.highParts, &model.farParts})
            for (const auto& part : *parts) {
                ReadMesh(part);
                ValidateTexture(part.albedo, part.alphaMask);
            }
    }
    for (const auto& path : {catalog.day, catalog.dusk}) {
        std::string error;
        if (!le::EnvironmentData::ValidateFile(ToUtf8(path), error))
            throw std::runtime_error("HDR 환경맵의 형식이 올바르지 않습니다: " + error);
    }
    le::AssetManager::Clear();
}

} // namespace RainyCreek
