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

std::optional<ProjectCreationRequest> ShowNewProjectDialog(wxWindow* parent);
std::optional<std::string> ShowProjectNameDialog(wxWindow* parent, const std::string& currentName);

} // namespace LuxEngine
