#pragma once
#include <LuxUI/Widgets.hpp>

#include <LuxEditor/Project/EditorProject.hpp>
#include <filesystem>
#include <optional>
#include <string>

namespace LuxEditor {

struct ProjectCreationRequest {
    std::filesystem::path directory;
    std::string name;
    ProjectTemplate projectTemplate = ProjectTemplate::Empty;
};

struct ProjectSettingsRequest {
    ProjectSettings settings;
    std::filesystem::path iconSource;
    bool clearIcon = false;
};

std::optional<ProjectCreationRequest> ShowNewProjectDialog(LuxUI::Widget* parent);
std::optional<ProjectSettingsRequest> ShowProjectSettingsDialog(LuxUI::Widget* parent,
                                                                const ProjectSettings& current);
std::optional<std::filesystem::path> ShowProjectDestinationDialog(LuxUI::Widget* parent,
                                                                  const char* title,
                                                                  const std::string& suggested);

} // namespace LuxEditor
