#pragma once
#include <filesystem>
#ifdef _WIN32
#include <chrono>
#include <thread>
#endif

namespace LuxEditor {
inline void RenameProjectPath(const std::filesystem::path& source,
                              const std::filesystem::path& destination, std::error_code& error) {
#ifdef _WIN32
    for (const int delay : {10, 20, 40, 80, 160, 200}) {
        std::filesystem::rename(source, destination, error);
        if (!error || (error.value() != 5 && error.value() != 32 && error.value() != 33))
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    }
#endif
    std::filesystem::rename(source, destination, error);
}
} // namespace LuxEditor
