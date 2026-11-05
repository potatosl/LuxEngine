// End-to-end check of what the editor's Build menu does: create a template game, configure and
// build it against the installed engine SDK, run it, and zip it. Skips (exit 77) when no SDK or
// cmake is available, because nothing here is downloaded.
#include "Build/ProjectBuild.hpp"
#include "Project/EditorProject.hpp"
#include "Project/PathEncoding.hpp"

#include <QCoreApplication>
#include <QProcess>
#include <QString>
#include <QStringList>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
using namespace LuxEngine;

namespace {

constexpr int SkipCode = 77;

QString ToQString(const fs::path& path) {
    return QString::fromStdU16String(path.u16string());
}

std::string Env(const char* name) {
#ifdef _WIN32
    // _wgetenv keeps non-ASCII values intact; getenv would convert them to the ANSI code page.
    std::wstring wide(name, name + std::char_traits<char>::length(name));
    const wchar_t* value = _wgetenv(wide.c_str());
    return value ? PathUtf8(fs::path(value)) : std::string();
#else
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
#endif
}

bool Run(const fs::path& program, const QStringList& arguments, const fs::path& workingDirectory,
         int timeoutMs, std::string& output) {
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.setWorkingDirectory(ToQString(workingDirectory));
    process.start(ToQString(program), arguments);
    if (!process.waitForStarted(30000)) {
        output = "failed to start: " + process.errorString().toStdString();
        return false;
    }
    const bool finished = process.waitForFinished(timeoutMs);
    output = QString::fromLocal8Bit(process.readAll()).toStdString();
    if (!finished) {
        process.kill();
        process.waitForFinished(5000);
        output += "\n[timed out]";
        return false;
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

void PrintTail(const std::string& text) {
    constexpr size_t Limit = 6000;
    std::cerr << (text.size() > Limit ? text.substr(text.size() - Limit) : text) << '\n';
}

bool BuildRunAndPackage(const fs::path& projectDir, BuildConfiguration configuration,
                        const BuildTools& tools) {
    const char* config = ToString(configuration);
    std::cout << "== " << config << '\n';
    std::string error;
    auto plan = PlanBuild(projectDir, configuration, tools, "LuxGame", error);
    if (!plan) {
        std::cerr << "PlanBuild failed: " << error << '\n';
        return false;
    }
    for (const BuildStep& step : plan->steps) {
        QStringList arguments;
        for (const std::string& argument : step.arguments)
            arguments << QString::fromUtf8(argument.c_str());
        std::string output;
        std::cout << "-- " << step.title << '\n';
        if (!Run(step.program, arguments, projectDir, 15 * 60 * 1000, output)) {
            std::cerr << step.title << " failed (" << config << "):\n";
            PrintTail(output);
            return false;
        }
    }
    const auto executable = FindBuiltExecutable(*plan);
    if (!executable) {
        std::cerr << "built executable not found in " << PathUtf8(plan->buildDirectory) << '\n';
        return false;
    }
    std::cout << "-- run " << PathUtf8(*executable) << " --smoke-test\n";
    std::string output;
    if (!Run(*executable, {QStringLiteral("--smoke-test")}, executable->parent_path(), 120000,
             output)) {
        std::cerr << "game failed to run (" << config << "):\n";
        PrintTail(output);
        return false;
    }
    const fs::path archive = projectDir / (std::string("LuxGame-") + config + ".zip");
    auto package = CreateDistributionArchive(*executable, archive, "LuxGame", error);
    if (!package) {
        std::cerr << "packaging failed: " << error << '\n';
        return false;
    }
    std::cout << "-- zip " << package->fileCount << " files, " << fs::file_size(archive)
              << " bytes\n";
    return package->fileCount >= 3 && fs::file_size(archive) > 0;
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    const fs::path appDir = fs::path(QCoreApplication::applicationDirPath().toStdU16String());
    const BuildTools tools = DetectBuildTools(appDir, [](const char* name) { return Env(name); });
    if (tools.cmake.empty() || tools.enginePackage.empty()) {
        std::cout << "SKIP: " << DescribeMissingTools(tools) << '\n';
        return SkipCode;
    }
    std::cout << "cmake: " << PathUtf8(tools.cmake) << "\nengine: " << PathUtf8(tools.enginePackage)
              << "\ntoolchain: " << PathUtf8(tools.toolchainFile) << '\n';

    // Spaces and Korean in the path catch quoting and code-page bugs.
    const fs::path root = fs::temp_directory_path() / PathFromUtf8("lux 게임 빌드 테스트");
    std::error_code ec;
    fs::remove_all(root, ec);
    std::string error;
    auto project = EditorProject::Create(root, "빌드 테스트", ProjectTemplate::Platformer, error);
    if (!project) {
        std::cerr << "project creation failed: " << error << '\n';
        return 1;
    }
    bool ok = BuildRunAndPackage(root, BuildConfiguration::Debug, tools);
    ok = BuildRunAndPackage(root, BuildConfiguration::Release, tools) && ok;
    if (ok)
        fs::remove_all(root, ec);
    std::cout << (ok ? "PASS" : "FAIL") << '\n';
    return ok ? 0 : 1;
}
