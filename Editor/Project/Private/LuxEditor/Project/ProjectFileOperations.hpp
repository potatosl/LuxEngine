#pragma once
#include <filesystem>
#ifdef _WIN32
#include <chrono>
#include <thread>
#endif

namespace LuxEditor {
void RenameProjectPath(const std::filesystem::path& source,
                       const std::filesystem::path& destination, std::error_code& error);
} // namespace LuxEditor
