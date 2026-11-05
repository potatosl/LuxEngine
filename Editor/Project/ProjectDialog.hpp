#pragma once

#include "EditorProject.hpp"
#include <filesystem>
#include <optional>
#include <string>

class wxWindow;

namespace LuxEngine {

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

std::optional<ProjectCreationRequest> ShowNewProjectDialog(wxWindow* parent);
std::optional<ProjectSettingsRequest> ShowProjectSettingsDialog(wxWindow* parent,
                                                                const ProjectSettings& current);
std::optional<std::filesystem::path>
ShowProjectDestinationDialog(wxWindow* parent, const char* title, const std::string& suggested);

} // namespace LuxEngine
