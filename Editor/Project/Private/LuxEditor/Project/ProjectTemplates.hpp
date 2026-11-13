#pragma once

#include <LuxEditor/Project/EditorProject.hpp>
#include <filesystem>
#include <string>

namespace LuxEditor {
bool CreateTemplateFiles(const std::filesystem::path& directory, ProjectTemplate projectTemplate,
                         std::string& error);
} // namespace LuxEditor
