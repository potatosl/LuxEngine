#pragma once
#include <LuxEngine/Scene/Scene.hpp>
#include <optional>
class QWidget;

namespace LuxEditor {
std::optional<LuxEngine::SceneSettings>
ShowSceneSettingsDialog(QWidget* parent, const LuxEngine::SceneSettings& settings,
                        const std::string& sourcePath = {});
}
