#pragma once

#include <string>

namespace LuxEngine {
class Scene;
}

namespace LuxEditor {
class EditorProject;

// Validation is read-only, including imported baselines and unloaded streaming snapshots.
bool ValidateProjectSceneReferences(const EditorProject& project, LuxEngine::Scene& scene,
                                    std::string& error);
// Validates authored .lux files and their composed runtime state; metadata/build folders are
// skipped.
bool ValidateProjectScenes(const EditorProject& project, std::string& error);
} // namespace LuxEditor
