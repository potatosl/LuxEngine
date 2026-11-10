#include "Build/ProjectBuild.hpp"
#include "Project/PathEncoding.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <regex>
#include <set>
#include <system_error>
#include <utility>
#include <zip.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace LuxEditor {
namespace fs = std::filesystem;

namespace {
constexpr uint64_t MaxPackageBytes = 4ull * 1024ull * 1024ull * 1024ull;
constexpr uint64_t MaxPackageFiles = 20000;

#ifdef _WIN32
constexpr const char* ExecutableSuffix = ".exe";
constexpr char PathSeparator = ';';
#else
constexpr const char* ExecutableSuffix = "";
constexpr char PathSeparator = ':';
#endif

bool IsFile(const fs::path& path) {
    std::error_code ec;
    return fs::is_regular_file(path, ec);
}

std::string LowerAscii(std::string text);

// MSYS2, MinGW and Cygwin ship their own cmake builds. They cannot drive Visual Studio and the
// x64-windows vcpkg packages the engine SDK uses, so a game build started with one fails.
bool IsUnixLayerTool(const fs::path& program) {
#ifdef _WIN32
    std::string text = LowerAscii(GenericPathUtf8(program));
    for (const char* marker : {"/msys", "/mingw", "/ucrt64/", "/clang64/", "/cygwin"})
        if (text.find(marker) != std::string::npos)
            return true;
#else
    (void)program;
#endif
    return false;
}

fs::path FindOnPath(const std::string& pathVariable, const std::string& name) {
    size_t start = 0;
    while (start <= pathVariable.size()) {
        size_t end = pathVariable.find(PathSeparator, start);
        if (end == std::string::npos)
            end = pathVariable.size();
        if (end > start) {
            const fs::path candidate = PathFromUtf8(pathVariable.substr(start, end - start)) / name;
            if (IsFile(candidate) && !IsUnixLayerTool(candidate))
                return candidate;
        }
        start = end + 1;
    }
    return {};
}

#ifdef _WIN32
// Visual Studio ships its own cmake; look in every installed version and edition.
fs::path FindVisualStudioCMake() {
    for (const char* base : {"C:/Program Files/Microsoft Visual Studio",
                             "C:/Program Files (x86)/Microsoft Visual Studio"}) {
        std::error_code ec;
        std::vector<fs::path> candidates;
        for (fs::directory_iterator version(base, ec), end; !ec && version != end;
             version.increment(ec))
            for (fs::directory_iterator edition(version->path(), ec), editionEnd;
                 !ec && edition != editionEnd; edition.increment(ec)) {
                const fs::path cmake = edition->path() / "Common7/IDE/CommonExtensions/Microsoft/"
                                                         "CMake/CMake/bin/cmake.exe";
                if (IsFile(cmake))
                    candidates.push_back(cmake);
            }
        if (!candidates.empty()) {
            std::sort(candidates.begin(), candidates.end());
            return candidates.back(); // highest version name
        }
    }
    return {};
}
#endif

bool HasEnginePackage(const fs::path& directory) {
    return IsFile(directory / "LuxEngineConfig.cmake") &&
           IsFile(directory / "LuxEngineTargets.cmake");
}

std::string Trim(std::string text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.pop_back();
    size_t first = 0;
    while (first < text.size() && std::isspace(static_cast<unsigned char>(text[first])))
        ++first;
    return text.substr(first);
}

DiagnosticSeverity SeverityFrom(const std::string& text) {
    if (text == "warning")
        return DiagnosticSeverity::Warning;
    if (text == "note")
        return DiagnosticSeverity::Note;
    return DiagnosticSeverity::Error;
}

std::string LowerAscii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool SkipInPackage(const fs::path& relative) {
    static const std::set<std::string> skippedExtensions = {".pdb",
                                                            ".ilk",
                                                            ".obj",
                                                            ".o",
                                                            ".exp",
                                                            ".lib",
                                                            ".a",
                                                            ".idb",
                                                            ".tlog",
                                                            ".log",
                                                            ".lastbuildstate",
                                                            ".recipe"};
    const std::string extension = LowerAscii(PathUtf8(relative.extension()));
    if (skippedExtensions.contains(extension))
        return true;
    for (const auto& part : relative) {
        const std::string name = PathUtf8(part);
        if (name == "CMakeFiles" || name == ".git" || name == ".lux" || name.ends_with(".dir"))
            return true;
    }
    return false;
}
} // namespace

const char* ToString(BuildConfiguration configuration) {
    return configuration == BuildConfiguration::Release ? "Release" : "Debug";
}

BuildTools DetectBuildTools(const fs::path& applicationDirectory,
                            const EnvironmentLookup& environment) {
    BuildTools tools;
    const auto env = [&](const char* name) {
        return environment ? environment(name) : std::string{};
    };

    if (const std::string explicitCMake = env("LUX_CMAKE"); !explicitCMake.empty()) {
        if (IsFile(PathFromUtf8(explicitCMake)))
            tools.cmake = PathFromUtf8(explicitCMake);
    }
#ifdef _WIN32
    // Visual Studio's own cmake always matches the compiler the SDK was built with. Other cmake
    // copies on PATH (MSYS2, tools that bundle their own) are often old or cannot drive it.
    if (tools.cmake.empty())
        tools.cmake = FindVisualStudioCMake();
#endif
    if (tools.cmake.empty())
        tools.cmake = FindOnPath(env("PATH"), std::string("cmake") + ExecutableSuffix);

    if (const std::string sdk = env("LUX_ENGINE_DIR"); !sdk.empty()) {
        const fs::path root = PathFromUtf8(sdk);
        for (const fs::path& candidate :
             {root, root / "lib/cmake/LuxEngine", root / "cmake/LuxEngine"})
            if (HasEnginePackage(candidate)) {
                tools.enginePackage = candidate;
                break;
            }
    }
    if (tools.enginePackage.empty()) {
        if (const std::string prefix = env("CMAKE_PREFIX_PATH"); !prefix.empty()) {
            size_t start = 0;
            while (start <= prefix.size()) {
                size_t end = prefix.find(PathSeparator, start);
                if (end == std::string::npos)
                    end = prefix.size();
                if (end > start) {
                    const fs::path candRoot = PathFromUtf8(prefix.substr(start, end - start));
                    for (const fs::path& c : {candRoot, candRoot / "lib/cmake/LuxEngine",
                                              candRoot / "cmake/LuxEngine"}) {
                        if (HasEnginePackage(c)) {
                            tools.enginePackage = c;
                            break;
                        }
                    }
                    if (!tools.enginePackage.empty())
                        break;
                }
                start = end + 1;
            }
        }
    }
    if (tools.enginePackage.empty())
        for (const fs::path& relative :
             {fs::path("lib/cmake/LuxEngine"), fs::path("../lib/cmake/LuxEngine"),
              fs::path("../../lib/cmake/LuxEngine"), fs::path("cmake/LuxEngine"),
              fs::path("../cmake/LuxEngine"), fs::path("../../cmake/LuxEngine")}) {
            const fs::path candidate = (applicationDirectory / relative).lexically_normal();
            if (HasEnginePackage(candidate)) {
                tools.enginePackage = candidate;
                break;
            }
        }

    if (const std::string vcpkg = env("VCPKG_ROOT"); !vcpkg.empty()) {
        const fs::path toolchain = PathFromUtf8(vcpkg) / "scripts/buildsystems/vcpkg.cmake";
        if (IsFile(toolchain))
            tools.toolchainFile = toolchain;
    }
    return tools;
}

std::string DescribeMissingTools(const BuildTools& tools) {
    std::string message;
    if (tools.cmake.empty())
        message += "CMake를 찾을 수 없습니다. CMake를 설치하거나 환경 변수 LUX_CMAKE에 cmake "
                   "경로를 지정하세요.\n";
    if (tools.enginePackage.empty())
        message += "설치된 LuxEngine 패키지를 찾을 수 없습니다. 엔진을 `cmake --install`로 "
                   "설치하고 환경 변수 "
                   "LUX_ENGINE_DIR 또는 CMAKE_PREFIX_PATH에 설치 폴더를 지정하세요.\n";
    return message;
}

std::optional<BuildPlan> PlanBuild(const fs::path& projectDirectory,
                                   BuildConfiguration configuration, const BuildTools& tools,
                                   const std::string& executableName, std::string& error) {
    error.clear();
    if (!IsFile(projectDirectory / "CMakeLists.txt")) {
        error = "프로젝트 폴더에 CMakeLists.txt가 없습니다.";
        return std::nullopt;
    }
    if (const std::string missing = DescribeMissingTools(tools); !missing.empty()) {
        error = missing;
        return std::nullopt;
    }
    if (executableName.empty() || executableName.find_first_of("/\\:") != std::string::npos) {
        error = "실행 파일 이름이 올바르지 않습니다.";
        return std::nullopt;
    }
    BuildPlan plan;
    plan.projectDirectory = projectDirectory;
    plan.buildDirectory = projectDirectory / ".lux" / "build";
    plan.configuration = configuration;
    plan.executableName = executableName;

    BuildStep configure;
    configure.title = "CMake 구성";
    configure.program = tools.cmake;
    configure.arguments = {"-S",
                           GenericPathUtf8(projectDirectory),
                           "-B",
                           GenericPathUtf8(plan.buildDirectory),
                           std::string("-DCMAKE_BUILD_TYPE=") + ToString(configuration),
                           "-DLuxEngine_DIR=" + GenericPathUtf8(tools.enginePackage)};
    if (!tools.toolchainFile.empty()) {
        configure.arguments.push_back("-DCMAKE_TOOLCHAIN_FILE=" +
                                      GenericPathUtf8(tools.toolchainFile));
#ifdef _WIN32
        configure.arguments.push_back("-DVCPKG_TARGET_TRIPLET=x64-windows");
#endif
    }
    plan.steps.push_back(std::move(configure));

    BuildStep build;
    build.title = std::string("빌드 (") + ToString(configuration) + ")";
    build.program = tools.cmake;
    build.arguments = {"--build", GenericPathUtf8(plan.buildDirectory), "--config",
                       ToString(configuration), "--parallel"};
    plan.steps.push_back(std::move(build));
    return plan;
}

bool CleanBuildDirectory(const BuildPlan& plan, std::string& error) {
    error.clear();
    const fs::path expected = (plan.projectDirectory / ".lux" / "build").lexically_normal();
    if (plan.buildDirectory.lexically_normal() != expected || plan.projectDirectory.empty()) {
        error = "프로젝트 밖의 폴더는 지울 수 없습니다.";
        return false;
    }
    std::error_code ec;
    // Inspect every component below the project, not just build: .lux can itself be a link.
    fs::path component = fs::canonical(plan.projectDirectory, ec);
    if (ec) {
        error = "프로젝트 경로를 확인할 수 없습니다: " + ec.message();
        return false;
    }
    for (const char* name : {".lux", "build"}) {
        component /= name;
        const auto status = fs::symlink_status(component, ec);
        if (ec == std::errc::no_such_file_or_directory || (!ec && !fs::exists(status)))
            return true;
        if (ec) {
            error = "빌드 폴더를 확인할 수 없습니다: " + ec.message();
            return false;
        }
        bool linked = fs::is_symlink(status);
#ifdef _WIN32
        const DWORD attributes = GetFileAttributesW(component.c_str());
        linked = linked || (attributes != INVALID_FILE_ATTRIBUTES &&
                            (attributes & FILE_ATTRIBUTE_REPARSE_POINT));
#endif
        if (linked || !fs::is_directory(status)) {
            error = "빌드 경로에 링크 또는 일반 폴더가 아닌 항목이 있어 지우지 않습니다.";
            return false;
        }
    }
    const auto requested = fs::canonical(plan.buildDirectory, ec);
    if (ec || !fs::equivalent(requested, component, ec)) {
        error = "빌드 경로의 실제 위치가 프로젝트 내부 경로와 다릅니다.";
        return false;
    }
    // Delete the same physical path that was inspected. Lexically removing '..' before resolving
    // a symlink can change its meaning and select a different project directory.
    fs::remove_all(component, ec);
    if (ec) {
        error = "빌드 폴더를 지울 수 없습니다: " + ec.message();
        return false;
    }
    return true;
}

std::optional<fs::path> FindBuiltExecutable(const BuildPlan& plan) {
    const std::string name = plan.executableName + ExecutableSuffix;
    for (const fs::path& candidate :
         {plan.buildDirectory / ToString(plan.configuration) / name, plan.buildDirectory / name})
        if (IsFile(candidate))
            return candidate;
    return std::nullopt;
}

// MSBuild appends " [C:\\path\\project.vcxproj]" to every diagnostic; it is noise in the issue
// list.
static std::string StripProjectSuffix(std::string message) {
    if (message.size() > 2 && message.back() == ']') {
        const size_t open = message.rfind(" [");
        if (open != std::string::npos && message.find("proj", open) != std::string::npos)
            message.erase(open);
    }
    return message;
}

std::optional<BuildDiagnostic> ParseBuildLine(const std::string& rawLine) {
    const std::string line = Trim(rawLine);
    if (line.empty())
        return std::nullopt;
    std::smatch match;
    BuildDiagnostic diagnostic;

    // MSVC compiler: path(12,5): error C2065: message [project.vcxproj]
    // Localized Visual Studio prints the keyword in its language, e.g. Korean "오류"/"경고".
    static const std::regex msvc(
        R"(^(.+?)\((\d{1,9})(?:,(\d{1,9}))?\)\s*:\s*(fatal error|error|warning|치명적인 오류|오류|경고)\s+([A-Za-z]+\d+)\s*:\s*(.*)$)");
    if (std::regex_match(line, match, msvc)) {
        diagnostic.file = match[1];
        diagnostic.line = std::stoi(match[2]);
        diagnostic.column = match[3].matched ? std::stoi(match[3]) : 0;
        const bool warning = match[4] == "warning" || match[4] == "경고";
        diagnostic.severity = SeverityFrom(warning ? "warning" : "error");
        diagnostic.code = match[5];
        diagnostic.message = StripProjectSuffix(Trim(match[6]));
        return diagnostic;
    }
    // GCC / Clang: path:12:5: error: message
    static const std::regex gcc(
        R"(^(.+?):(\d{1,9}):(?:(\d{1,9}):)?\s*(fatal error|error|warning|note):\s*(.*)$)");
    if (std::regex_match(line, match, gcc)) {
        diagnostic.file = match[1];
        diagnostic.line = std::stoi(match[2]);
        diagnostic.column = match[3].matched ? std::stoi(match[3]) : 0;
        diagnostic.severity = SeverityFrom(match[4] == "fatal error" ? "error" : match[4].str());
        diagnostic.message = Trim(match[5]);
        return diagnostic;
    }
    // Linker: [file : ]fatal error LNK1104: message
    static const std::regex linker(
        R"(^(?:(.+?)\s*:\s*)?(fatal error|error|치명적인 오류|오류)\s+(LNK\d+)\s*:\s*(.*)$)");
    if (std::regex_match(line, match, linker)) {
        diagnostic.file = match[1].matched ? match[1].str() : std::string{};
        diagnostic.code = match[3];
        diagnostic.message = StripProjectSuffix(Trim(match[4]));
        return diagnostic;
    }
    // CMake: CMake Error at CMakeLists.txt:7 (find_package):
    static const std::regex cmakeAt(
        R"(^CMake (Error|Warning)(?: \(dev\))? at (.+?):(\d{1,9}) \(.*\):?\s*$)");
    if (std::regex_match(line, match, cmakeAt)) {
        diagnostic.severity =
            match[1] == "Warning" ? DiagnosticSeverity::Warning : DiagnosticSeverity::Error;
        diagnostic.file = match[2];
        diagnostic.line = std::stoi(match[3]);
        diagnostic.message = line;
        return diagnostic;
    }
    static const std::regex cmakePlain(R"(^CMake (Error|Warning)(?: \(dev\))?: (.*)$)");
    if (std::regex_match(line, match, cmakePlain)) {
        diagnostic.severity =
            match[1] == "Warning" ? DiagnosticSeverity::Warning : DiagnosticSeverity::Error;
        diagnostic.message = Trim(match[2]);
        return diagnostic;
    }
    return std::nullopt;
}

std::optional<int> ParseBuildProgress(const std::string& line) {
    std::smatch match;
    static const std::regex percent(R"(^\s*\[\s*(\d{1,3})%\])");
    if (std::regex_search(line, match, percent)) {
        const int value = std::stoi(match[1]);
        return std::clamp(value, 0, 100);
    }
    static const std::regex ratio(R"(^\s*\[(\d{1,12})/(\d{1,12})\])");
    if (std::regex_search(line, match, ratio)) {
        const long long done = std::stoll(match[1]);
        const long long total = std::stoll(match[2]);
        if (total > 0)
            return static_cast<int>(std::clamp<long long>(done * 100 / total, 0, 100));
    }
    return std::nullopt;
}

std::optional<PackageResult> CreateDistributionArchive(const fs::path& executable,
                                                       const fs::path& archive,
                                                       const std::string& rootName,
                                                       std::string& error) {
    error.clear();
    if (!IsFile(executable)) {
        error = "빌드된 실행 파일이 없습니다. 먼저 빌드하세요.";
        return std::nullopt;
    }
    if (rootName.empty() || rootName.find_first_of("/\\:") != std::string::npos) {
        error = "배포 폴더 이름이 올바르지 않습니다.";
        return std::nullopt;
    }
    const fs::path root = executable.parent_path();
    std::vector<std::pair<fs::path, fs::path>> files; // absolute, relative
    uint64_t total = 0;
    std::error_code ec;
    for (fs::recursive_directory_iterator
             it(root, fs::directory_options::skip_permission_denied, ec),
         end;
         !ec && it != end; it.increment(ec)) {
        const fs::path relative = it->path().lexically_relative(root);
        if (SkipInPackage(relative)) {
            if (it->is_directory(ec))
                it.disable_recursion_pending();
            continue;
        }
        if (it->is_symlink(ec)) {
            error =
                "심볼릭 링크가 있어 배포 패키지를 만들 수 없습니다: " + GenericPathUtf8(relative);
            return std::nullopt;
        }
        if (!it->is_regular_file(ec))
            continue;
        const uint64_t size = it->file_size(ec);
        if (ec || files.size() >= MaxPackageFiles || size > MaxPackageBytes - total) {
            error = "배포 파일 수 또는 크기가 제한을 넘었습니다.";
            return std::nullopt;
        }
        total += size;
        files.emplace_back(it->path(), relative);
    }
    if (ec) {
        error = "빌드 폴더를 읽을 수 없습니다: " + ec.message();
        return std::nullopt;
    }
    std::sort(files.begin(), files.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });

    fs::path temporary = archive;
    temporary +=
        ".tmp-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    fs::create_directories(archive.parent_path(), ec);
    int zipError = 0;
    zip_t* zip = zip_open(PathUtf8(temporary).c_str(), ZIP_CREATE | ZIP_TRUNCATE, &zipError);
    if (!zip) {
        error = "ZIP 파일을 만들 수 없습니다.";
        return std::nullopt;
    }
    bool ok = true;
    for (const auto& [absolute, relative] : files) {
        const std::string source = PathUtf8(absolute);
        zip_source_t* data = zip_source_file(zip, source.c_str(), 0, -1);
        const std::string name = rootName + "/" + GenericPathUtf8(relative);
        if (!data || zip_file_add(zip, name.c_str(), data, ZIP_FL_ENC_UTF_8) < 0) {
            if (data)
                zip_source_free(data);
            error = "파일을 압축하지 못했습니다: " + GenericPathUtf8(relative);
            ok = false;
            break;
        }
    }
    if (ok && zip_close(zip) != 0) {
        error = "ZIP 파일을 저장하지 못했습니다.";
        zip_discard(zip);
        ok = false;
    } else if (!ok) {
        zip_discard(zip);
    }
    if (!ok) {
        fs::remove(temporary, ec);
        return std::nullopt;
    }
    fs::rename(temporary, archive, ec);
    if (ec) { // rename does not replace an existing file on every platform
        fs::remove(archive, ec);
        ec.clear();
        fs::rename(temporary, archive, ec);
    }
    if (ec) {
        fs::remove(temporary, ec);
        error = "ZIP 파일을 최종 위치로 옮길 수 없습니다.";
        return std::nullopt;
    }
    return PackageResult{archive, static_cast<uint64_t>(files.size()), total};
}

} // namespace LuxEditor
