#pragma once
#include <LuxEngine/Scene/Scene.hpp>
#include <LuxUI/Widgets.hpp>
#include <optional>

namespace LuxEditor {
std::optional<LuxEngine::SceneSettings>
ShowSceneSettingsDialog(LuxUI::Widget* parent, const LuxEngine::SceneSettings& settings,
                        const std::string& sourcePath = {});
}
