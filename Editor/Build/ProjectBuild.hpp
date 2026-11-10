#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace LuxEngine {

enum class BuildConfiguration { Debug, Release };

const char* ToString(BuildConfiguration configuration);

// Tools needed to build a game project outside the editor. Nothing is downloaded: the engine must
// already be installed (cmake --install) so the game can find_package(LuxEngine).
struct BuildTools {
    std::filesystem::path cmake;
    std::filesystem::path enginePackage; // folder containing LuxEngineConfig.cmake
    std::filesystem::path toolchainFile; // optional vcpkg toolchain
};

using EnvironmentLookup = std::function<std::string(const char* name)>;

// Looks for cmake and the installed engine package. Order for each: environment variable
// (LUX_CMAKE, LUX_ENGINE_DIR), PATH / Visual Studio's bundled cmake, then next to the editor.
// Missing pieces stay empty; callers report them with DescribeMissingTools.
BuildTools DetectBuildTools(const std::filesystem::path& applicationDirectory,
                            const EnvironmentLookup& environment);
std::string DescribeMissingTools(const BuildTools& tools);

struct BuildStep {
    std::string title;
    std::filesystem::path program;
    std::vector<std::string> arguments;
};

struct BuildPlan {
    std::filesystem::path projectDirectory;
    std::filesystem::path buildDirectory; // always <project>/.lux/build
    BuildConfiguration configuration = BuildConfiguration::Debug;
    std::string executableName;
    std::vector<BuildStep> steps;
};

// Builds the configure and build command lines. Returns nullopt with an error when the project
// folder has no CMakeLists.txt or the tools are incomplete.
std::optional<BuildPlan> PlanBuild(const std::filesystem::path& projectDirectory,
                                   BuildConfiguration configuration, const BuildTools& tools,
                                   const std::string& executableName, std::string& error);

// Deletes the project's build folder for a clean build. Refuses any folder that is not exactly
// <project>/.lux/build.
bool CleanBuildDirectory(const BuildPlan& plan, std::string& error);

// Finds the built executable for single- and multi-configuration generators.
std::optional<std::filesystem::path> FindBuiltExecutable(const BuildPlan& plan);

enum class DiagnosticSeverity { Note, Warning, Error };

struct BuildDiagnostic {
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    std::string file;
    int line = 0;
    int column = 0;
    std::string code;
    std::string message;
};

// Recognizes MSVC, GCC/Clang, linker and CMake error lines. Returns nullopt for ordinary output.
std::optional<BuildDiagnostic> ParseBuildLine(const std::string& line);

// "[ 42%]" (Make) and "[12/40]" (Ninja) give a percentage; other lines return nullopt.
std::optional<int> ParseBuildProgress(const std::string& line);

struct PackageResult {
    std::filesystem::path archive;
    uint64_t fileCount = 0;
    uint64_t uncompressedBytes = 0;
};

// Zips the folder holding the built executable (executable, DLLs, shaders, Scenes, resources)
// into <archive> under a top-level folder named <rootName>. Debug databases and build leftovers
// are skipped. The archive is written to a temporary file first, so a failure never leaves a
// partial ZIP behind.
std::optional<PackageResult> CreateDistributionArchive(const std::filesystem::path& executable,
                                                       const std::filesystem::path& archive,
                                                       const std::string& rootName,
                                                       std::string& error);

} // namespace LuxEngine
