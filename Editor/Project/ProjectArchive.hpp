#pragma once

#include "EditorProject.hpp"
#include <filesystem>
#include <optional>
#include <string>

namespace LuxEditor {

bool ExportProject(const EditorProject& project, const std::filesystem::path& archive,
                   std::string& error);
std::optional<EditorProject> ImportProject(const std::filesystem::path& archive,
                                           const std::filesystem::path& destination,
                                           std::string& error);
std::optional<EditorProject> CloneProject(const EditorProject& project,
                                          const std::filesystem::path& destination,
                                          const std::string& name, std::string& error);
bool CreateProjectBackup(const EditorProject& project, std::filesystem::path& backup,
                         std::string& error);
bool RestoreProjectBackup(const EditorProject& project, const std::filesystem::path& backup,
                          std::string& error);

} // namespace LuxEditor
