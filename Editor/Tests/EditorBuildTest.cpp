#include "Build/ProjectBuild.hpp"
#include "Project/PathEncoding.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <system_error>
#include <zip.h>

namespace {
namespace fs = std::filesystem;
using namespace LuxEditor;

void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

struct TempDirectory {
    fs::path path = fs::temp_directory_path() /
                    ("lux-editor-build-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    TempDirectory() {
        fs::create_directories(path);
    }
    ~TempDirectory() {
        std::error_code error;
        fs::remove_all(path, error);
    }
};

void Write(const fs::path& file, const std::string& text = "x") {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

void TestDiagnostics() {
    auto d = ParseBuildLine(
        R"(C:\proj\Source\Main.cpp(42,17): error C2065: 'x': undeclared identifier [C:\p.vcxproj])");
    Check(d && d->severity == DiagnosticSeverity::Error &&
              d->file == R"(C:\proj\Source\Main.cpp)" && d->line == 42 && d->column == 17 &&
              d->code == "C2065",
          "msvc error");
    Check(d && d->message == "'x': undeclared identifier", "msbuild project suffix stripped");
    d = ParseBuildLine("C:\\프로젝트\\Main.cpp(5,3): 오류 C2065: 'x': 선언되지 않은 식별자입니다.");
    Check(d && d->severity == DiagnosticSeverity::Error && d->line == 5 && d->code == "C2065",
          "korean msvc error");
    d = ParseBuildLine("Main.cpp(8): 경고 C4100: 'a': 참조되지 않은 매개 변수");
    Check(d && d->severity == DiagnosticSeverity::Warning && d->code == "C4100",
          "korean msvc warning");
    d = ParseBuildLine("LINK : 치명적인 오류 LNK1104: 'x.lib' 파일을 열 수 없습니다.");
    Check(d && d->severity == DiagnosticSeverity::Error && d->code == "LNK1104",
          "korean linker fatal error");
    d = ParseBuildLine("Main.cpp(7): warning C4100: 'a': unreferenced formal parameter");
    Check(d && d->severity == DiagnosticSeverity::Warning && d->line == 7 && d->column == 0,
          "msvc warning without column");
    d = ParseBuildLine("/src/main.cpp:12:5: error: 'y' was not declared in this scope");
    Check(d && d->severity == DiagnosticSeverity::Error && d->file == "/src/main.cpp" &&
              d->line == 12 && d->column == 5,
          "gcc error");
    d = ParseBuildLine(R"(C:\src\a.cpp:3:1: fatal error: missing.h: No such file or directory)");
    Check(d && d->severity == DiagnosticSeverity::Error && d->file == R"(C:\src\a.cpp)" &&
              d->line == 3,
          "gcc fatal error with drive letter");
    d = ParseBuildLine("a.cpp:9: note: declared here");
    Check(d && d->severity == DiagnosticSeverity::Note && d->line == 9, "gcc note");
    d = ParseBuildLine("main.obj : error LNK2019: unresolved external symbol foo");
    Check(d && d->code == "LNK2019" && d->file == "main.obj", "linker error");
    d = ParseBuildLine("LINK : fatal error LNK1104: cannot open file 'x.lib'");
    Check(d && d->code == "LNK1104", "linker fatal error");
    d = ParseBuildLine("CMake Error at CMakeLists.txt:7 (find_package):");
    Check(d && d->file == "CMakeLists.txt" && d->line == 7, "cmake error at");
    d = ParseBuildLine("CMake Error: The source directory does not exist.");
    Check(d && d->severity == DiagnosticSeverity::Error && d->line == 0, "cmake plain error");
    Check(!ParseBuildLine("") && !ParseBuildLine("-- Configuring done") &&
              !ParseBuildLine("In file included from a.cpp:3:") &&
              !ParseBuildLine("Build succeeded."),
          "ordinary output is not a diagnostic");
    // Numbers too large for int must not throw.
    Check(!ParseBuildLine("a.cpp(99999999999999999999): error C1: x") ||
              ParseBuildLine("a.cpp(99999999999999999999): error C1: x")->line == 0,
          "oversized line number is ignored");

    Check(ParseBuildProgress("[ 42%] Building CXX object a.o") == 42, "make progress");
    Check(ParseBuildProgress("[100%] Linking") == 100, "make progress 100");
    Check(ParseBuildProgress("[12/40] Building a.o") == 30, "ninja progress");
    Check(ParseBuildProgress("[0/0] none") == std::nullopt, "ninja zero total");
    Check(ParseBuildProgress("  Compiling...") == std::nullopt, "no progress");
}

void TestDetection() {
    TempDirectory files;
    const fs::path bin = files.path / "bin";
    const fs::path sdk = files.path / "sdk";
    Write(bin / ("cmake"
#ifdef _WIN32
                 ".exe"
#endif
                 ));
    Write(sdk / "lib/cmake/LuxEngine/LuxEngineConfig.cmake");
    Write(sdk / "lib/cmake/LuxEngine/LuxEngineTargets.cmake");
    Write(files.path / "vcpkg/scripts/buildsystems/vcpkg.cmake");

    std::map<std::string, std::string> env;
    auto lookup = [&env](const char* name) {
        const auto it = env.find(name);
        return it == env.end() ? std::string{} : it->second;
    };
    BuildTools tools = DetectBuildTools(files.path / "app", lookup);
    Check(tools.cmake.empty() || fs::exists(tools.cmake),
          "no environment: cmake only if installed");
    Check(tools.enginePackage.empty(), "no environment: no engine package");
    Check(!DescribeMissingTools(BuildTools{}).empty(), "missing tools are described");

#ifdef _WIN32
    env["PATH"] = PathUtf8(bin) + ";C:/nowhere";
#else
    env["PATH"] = PathUtf8(bin) + ":/nowhere";
#endif
    env["LUX_ENGINE_DIR"] = PathUtf8(sdk);
    env["VCPKG_ROOT"] = PathUtf8(files.path / "vcpkg");
    tools = DetectBuildTools(files.path / "app", lookup);
#ifdef _WIN32
    // Visual Studio's bundled cmake wins when installed; otherwise PATH is used.
    Check(!tools.cmake.empty(), "cmake found");
#else
    Check(tools.cmake.parent_path() == bin, "cmake found on PATH");
#endif
    Check(tools.enginePackage == sdk / "lib/cmake/LuxEngine", "engine package found from SDK root");
    Check(!tools.toolchainFile.empty(), "vcpkg toolchain found");
    Check(DescribeMissingTools(tools).empty(), "complete tools");
#ifdef _WIN32
    // An MSYS2 cmake earlier on PATH cannot build with Visual Studio and must be skipped.
    const fs::path msys = files.path / "msys64/ucrt64/bin";
    Write(msys / "cmake.exe");
    env["PATH"] = PathUtf8(msys) + ";" + PathUtf8(bin);
    tools = DetectBuildTools(files.path / "app", lookup);
    Check(!tools.cmake.empty() && tools.cmake.parent_path() != msys,
          "msys2 cmake on PATH is skipped");
#endif

    env["LUX_ENGINE_DIR"] = PathUtf8(files.path / "missing");
    env.erase("PATH");
    // The editor's own folder is searched when the variable points nowhere.
    Write(files.path / "app/lib/cmake/LuxEngine/LuxEngineConfig.cmake");
    Write(files.path / "app/lib/cmake/LuxEngine/LuxEngineTargets.cmake");
    tools = DetectBuildTools(files.path / "app", lookup);
    Check(tools.enginePackage == files.path / "app/lib/cmake/LuxEngine",
          "engine package next to the editor");
}

void TestPlan() {
    TempDirectory files;
    const fs::path project = files.path / PathFromUtf8("게임 프로젝트");
    BuildTools tools;
    tools.cmake = "cmake";
    tools.enginePackage = files.path / "sdk";
    std::string error;
    Check(!PlanBuild(project, BuildConfiguration::Debug, tools, "LuxGame", error) && !error.empty(),
          "plan needs CMakeLists.txt");
    Write(project / "CMakeLists.txt");
    Check(!PlanBuild(project, BuildConfiguration::Debug, BuildTools{}, "LuxGame", error) &&
              !error.empty(),
          "plan needs tools");
    Check(!PlanBuild(project, BuildConfiguration::Debug, tools, "../x", error) && !error.empty(),
          "executable name cannot escape");

    auto plan = PlanBuild(project, BuildConfiguration::Release, tools, "LuxGame", error);
    Check(plan && plan->steps.size() == 2, "plan has configure and build steps");
    Check(plan->buildDirectory == project / ".lux" / "build", "build folder is inside .lux");
    const auto& configure = plan->steps[0].arguments;
    Check(std::find(configure.begin(), configure.end(), "-DCMAKE_BUILD_TYPE=Release") !=
              configure.end(),
          "configure has build type");
    bool hasEngineDir = false;
    for (const auto& argument : configure)
        hasEngineDir = hasEngineDir || argument.starts_with("-DLuxEngine_DIR=");
    Check(hasEngineDir, "configure passes the engine package");
    const auto& build = plan->steps[1].arguments;
    Check(build.size() >= 4 && build[2] == "--config" && build[3] == "Release",
          "build step selects the configuration");

    // Clean only removes <project>/.lux/build.
    Write(plan->buildDirectory / "CMakeCache.txt");
    Write(project / "Scenes/Main.lux");
    Check(CleanBuildDirectory(*plan, error) && !fs::exists(plan->buildDirectory), "clean build");
    Check(fs::exists(project / "Scenes/Main.lux"), "clean keeps project files");
    Check(CleanBuildDirectory(*plan, error), "clean without a build folder succeeds");
    BuildPlan hostile = *plan;
    hostile.buildDirectory = project;
    Check(!CleanBuildDirectory(hostile, error) && fs::exists(project / "CMakeLists.txt"),
          "clean refuses other folders");
    hostile.buildDirectory = project / ".lux" / "build" / ".." / "..";
    Check(!CleanBuildDirectory(hostile, error), "clean refuses parent paths");

    Check(!FindBuiltExecutable(*plan), "no executable before a build");
    const std::string exe = std::string("LuxGame")
#ifdef _WIN32
                            + ".exe"
#endif
        ;
    Write(plan->buildDirectory / "Release" / exe);
    Check(FindBuiltExecutable(*plan) == plan->buildDirectory / "Release" / exe,
          "multi-config executable");
    fs::remove(plan->buildDirectory / "Release" / exe);
    Write(plan->buildDirectory / exe);
    Check(FindBuiltExecutable(*plan) == plan->buildDirectory / exe, "single-config executable");
}

void TestPackage() {
    TempDirectory files;
    const fs::path out = files.path / "out";
    const std::string exe = std::string("LuxGame")
#ifdef _WIN32
                            + ".exe"
#endif
        ;
    Write(out / exe, "binary");
    Write(out / "SDL3.dll", "dll");
    Write(out / "shaders/Basic.spv", "spv");
    Write(out / "Scenes/Main.lux", "scene");
    Write(out / PathFromUtf8("한글/자원.png"), "png");
    Write(out / "LuxGame.pdb", "debug");
    Write(out / "CMakeFiles/x.txt", "skip");
    Write(out / "a.dir/b.obj", "skip");

    std::string error;
    const fs::path archive = files.path / "dist" / "game.zip";
    auto result = CreateDistributionArchive(out / exe, archive, "MyGame", error);
    Check(result && result->fileCount == 5 && fs::exists(archive), "archive created");
    Check(result->uncompressedBytes == 6 + 3 + 3 + 5 + 3, "archive size counts files");

    int zipError = 0;
    zip_t* zip = zip_open(PathUtf8(archive).c_str(), ZIP_RDONLY, &zipError);
    Check(zip != nullptr, "archive opens");
    bool hasExe = false, hasScene = false, hasKorean = false, hasPdb = false, hasSkipped = false;
    for (zip_int64_t i = 0; i < zip_get_num_entries(zip, 0); ++i) {
        const std::string name = zip_get_name(zip, static_cast<zip_uint64_t>(i), ZIP_FL_ENC_UTF_8);
        Check(name.starts_with("MyGame/"), "entries live under the root folder");
        hasExe = hasExe || name == "MyGame/" + exe;
        hasScene = hasScene || name == "MyGame/Scenes/Main.lux";
        hasKorean = hasKorean || name == "MyGame/한글/자원.png";
        hasPdb = hasPdb || name.ends_with(".pdb");
        hasSkipped = hasSkipped || name.find("CMakeFiles") != std::string::npos ||
                     name.find(".dir") != std::string::npos;
    }
    zip_close(zip);
    Check(hasExe && hasScene && hasKorean, "archive has the game files");
    Check(!hasPdb && !hasSkipped, "archive skips debug and build leftovers");

    Check(CreateDistributionArchive(out / exe, archive, "MyGame", error) && fs::exists(archive),
          "archive can be replaced");
    bool leftover = false;
    for (const auto& entry : fs::directory_iterator(files.path / "dist"))
        leftover = leftover || entry.path().filename().string().find(".tmp-") != std::string::npos;
    Check(!leftover, "no temporary files remain");

    Check(!CreateDistributionArchive(out / "missing", archive, "MyGame", error) && !error.empty(),
          "missing executable is an error");
    Check(!CreateDistributionArchive(out / exe, archive, "bad/name", error) && !error.empty(),
          "bad root name is an error");
}
} // namespace

int main() {
    try {
        TestDiagnostics();
        TestDetection();
        TestPlan();
        TestPackage();
        std::cout << "Editor build checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Editor build checks failed: " << error.what() << '\n';
        return 1;
    }
}
