#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace LuxEditor {

std::string PathUtf8(const std::filesystem::path& path);

std::string GenericPathUtf8(const std::filesystem::path& path);

std::filesystem::path PathFromUtf8(std::string_view text);

} // namespace LuxEditor
