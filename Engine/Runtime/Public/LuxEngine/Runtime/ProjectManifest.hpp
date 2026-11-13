#pragma once
#include <filesystem>
#include <string>

namespace LuxEngine {
// Resolves the declared start scene inside a packaged project's content directory.
// Reads JSON project schema 1 and legacy LUXPROJECT versions 1–2.
bool ReadProjectStartScene(const std::filesystem::path& contentRoot,
                           std::filesystem::path& sceneFile, std::string& error);
} // namespace LuxEngine
