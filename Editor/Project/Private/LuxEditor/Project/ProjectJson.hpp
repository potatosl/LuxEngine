#pragma once
#include <LuxEditor/Project/EditorProject.hpp>
#include <string_view>

namespace LuxEditor {
std::string SerializeProjectJson(const EditorProject& project, const std::filesystem::path& scene);
bool DeserializeProjectJson(std::string_view data, ProjectSettings& settings,
                            std::string& engineVersion, std::string& templateName,
                            std::string& scene);
} // namespace LuxEditor
