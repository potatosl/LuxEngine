#pragma once

#include <LuxEngine/Core/Core.hpp>
#include <filesystem>
#include <span>
#include <string_view>

namespace LuxEngine {

struct BasicModelDescriptor {
    const char* id;
    const char* name;
    const char* fileName;
};

struct BasicModelInstallation {
    std::filesystem::path directory;
    std::filesystem::path modelPath;
    std::filesystem::path baseColorTexture;
};

class LUX_API BasicModelLibrary final {
public:
    static std::span<const BasicModelDescriptor> GetModels() noexcept;
    static const BasicModelDescriptor* FindModel(std::string_view id) noexcept;
    static std::filesystem::path FindLibraryDirectory();
    static bool IdentifyInstalledModel(const std::filesystem::path& modelPath,
                                       BasicModelInstallation& installation);
    // Installs the complete licensed package beneath the project. Identical packages are reused;
    // a different existing package is preserved and a new numbered directory is selected.
    static bool InstallIntoProject(std::string_view id, const std::filesystem::path& projectRoot,
                                   BasicModelInstallation& installation, std::string& error,
                                   const std::filesystem::path& libraryDirectory = {});
};

} // namespace LuxEngine
