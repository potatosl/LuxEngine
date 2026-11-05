#pragma once

#include "EditorProject.hpp"
#include <filesystem>
#include <string>

namespace LuxEngine {
bool CreateTemplateFiles(const std::filesystem::path& directory, ProjectTemplate projectTemplate,
                         std::string& error);
} // namespace LuxEngine
