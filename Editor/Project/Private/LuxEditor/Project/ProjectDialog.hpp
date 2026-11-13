#pragma once

#include <LuxEditor/Project/EditorProject.hpp>
#include <filesystem>
#include <optional>
#include <string>

class QWidget;

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

std::optional<ProjectCreationRequest> ShowNewProjectDialog(QWidget* parent);
std::optional<ProjectSettingsRequest> ShowProjectSettingsDialog(QWidget* parent,
                                                                const ProjectSettings& current);
std::optional<std::filesystem::path>
ShowProjectDestinationDialog(QWidget* parent, const char* title, const std::string& suggested);

} // namespace LuxEditor
