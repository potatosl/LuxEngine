#pragma once

#include <LuxEngine/Core/Core.hpp>
#include <filesystem>
#include <string>
#include <vector>

namespace LuxEngine {
class Scene;
class ModelAsset;
} // namespace LuxEngine

namespace LuxEditor {

struct ModelImportResult {
    LuxEngine::uint32 root = 0;
    std::vector<LuxEngine::uint32> entities;
    std::vector<std::string> warnings;
};

// Imports node-local geometry, material slots and animation source links in one scene mutation.
// The caller supplies the undo transaction. Extracted reusable textures survive scene undo.
bool ImportModelAsset(LuxEngine::Scene& scene, const LuxEngine::ModelAsset& model,
                      const std::filesystem::path& projectRoot, ModelImportResult& result,
                      std::string& error);

LuxEngine::uint32 FindImportedModelRoot(const LuxEngine::Scene& scene, LuxEngine::uint32 selected);

// Rebuilds from the source while retaining the wrapper and matching node/submesh entity IDs.
bool ReimportModelAsset(LuxEngine::Scene& scene, LuxEngine::uint32 selected,
                        const std::filesystem::path& projectRoot, ModelImportResult& result,
                        std::string& error, const std::filesystem::path& replacementSource = {});

} // namespace LuxEditor
