#pragma once

#include <LuxEngine/Renderer/Renderer3D/Renderer3D.hpp>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace RainyCreek {

struct ModelPart {
    std::filesystem::path mesh;
    std::filesystem::path albedo;
    float roughness = .9f;
    bool alphaMask = false;
};

struct Model {
    std::vector<ModelPart> parts;
    std::vector<ModelPart> highParts;
    std::vector<ModelPart> farParts;
    LuxEngine::Vec3 minimum;
    LuxEngine::Vec3 maximum;
};

struct Catalog {
    std::map<std::string, Model> models;
    std::filesystem::path ground;
    std::filesystem::path day;
    std::filesystem::path dusk;
};

std::filesystem::path FromUtf8(std::string_view value);
std::string ToUtf8(const std::filesystem::path& value);
Catalog LoadCatalog(const std::filesystem::path& root);
void ValidateCatalog(const Catalog& catalog);

} // namespace RainyCreek
