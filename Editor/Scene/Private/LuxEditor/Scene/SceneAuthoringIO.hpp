#pragma once

#include <filesystem>
#include <memory>
#include <string>

namespace LuxEngine {
class Scene;
}

namespace LuxEditor {

// Reads cached authored data without requiring referenced files to exist or compose successfully.
std::shared_ptr<LuxEngine::Scene> ReadAuthoredScene(const std::filesystem::path& path,
                                                    std::string& error);
// Rebases references in a temporary copy and atomically saves it, preserving the live scene.
bool SaveAuthoredScene(LuxEngine::Scene& scene, const std::filesystem::path& destination,
                       const std::filesystem::path& previous, std::string& error);

} // namespace LuxEditor
