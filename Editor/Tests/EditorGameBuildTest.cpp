#include <LuxUI/Widgets.hpp>
// End-to-end check of what the editor's Build menu does: create a template game, configure and
// build it against the installed engine SDK, run it, and zip it. Skips (exit 77) when no SDK or
// cmake is available, because nothing here is downloaded.
#include <LuxEditor/Build/BuildOutput.hpp>
#include <LuxEditor/Build/ProjectBuild.hpp>
#include <LuxEditor/Project/EditorProject.hpp>
#include <LuxEditor/Project/PathEncoding.hpp>
#include <LuxEngine/Scene/Entity.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <zip.h>

namespace fs = std::filesystem;
using namespace LuxEditor;

namespace {

constexpr int SkipCode = 77;

LuxUI::String ToQString(const fs::path& path) {
    return LuxUI::String::fromStdU16String(path.u16string());
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

bool Run(const fs::path& program, const LuxUI::StringList& arguments,
         const fs::path& workingDirectory, int timeoutMs, std::string& output) {
    LuxUI::Process process;
    process.setProcessEnvironment(Utf8BuildEnvironment());
    process.setProcessChannelMode(LuxUI::Process::MergedChannels);
    process.setWorkingDirectory(ToQString(workingDirectory));
    process.start(ToQString(program), arguments);
    if (!process.waitForStarted(30000)) {
        output = "failed to start: " + process.errorString().toStdString();
        return false;
    }
    const bool finished = process.waitForFinished(timeoutMs);
    output = DecodeBuildOutput(process.readAll()).toUtf8().toStdString();
    if (!finished) {
        process.kill();
        process.waitForFinished(5000);
        output += "\n[timed out]";
        return false;
    }
    const bool ok = process.exitStatus() == LuxUI::Process::NormalExit && process.exitCode() == 0;
    if (!ok)
        output += "\n[exit code " + std::to_string(process.exitCode()) + "]";
    return ok;
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
        LuxUI::StringList arguments;
        for (const std::string& argument : step.arguments)
            arguments << LuxUI::String::fromUtf8(argument.c_str());
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
    const auto packagedMesh =
        executable->parent_path() / "Content/Assets" / PathFromUtf8("한글 메시.obj");
    const auto packagedTexture =
        executable->parent_path() / "Content/Assets" / PathFromUtf8("한글 기본색상.PnG");
    if (!fs::is_regular_file(packagedMesh) || !fs::is_regular_file(packagedTexture)) {
        std::cerr << "packaged OBJ mesh is missing\n";
        return false;
    }
    std::string output;
    if (!Run(*executable, {LUX_UI_TEXT("--smoke-test")}, executable->parent_path(), 120000,
             output) ||
        output.find("[LuxEngine WARN]") != std::string::npos) {
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
    int zipError = 0;
    zip_t* packaged = zip_open(PathUtf8(archive).c_str(), ZIP_RDONLY, &zipError);
    const bool hasMesh =
        packaged &&
        zip_name_locate(packaged, "LuxGame/Content/Assets/한글 메시.obj", ZIP_FL_ENC_UTF_8) >= 0;
    const bool hasTexture =
        packaged && zip_name_locate(packaged, "LuxGame/Content/Assets/한글 기본색상.PnG",
                                    ZIP_FL_ENC_UTF_8) >= 0;
    const bool hasEnvironment =
        packaged &&
        zip_name_locate(packaged, "LuxGame/Content/Assets/한글 환경.HdR", ZIP_FL_ENC_UTF_8) >= 0;
    if (packaged)
        zip_close(packaged);
    if (!hasMesh || !hasTexture || !hasEnvironment) {
        std::cerr << "distribution ZIP is missing its OBJ mesh\n";
        return false;
    }
    const auto unpack = projectDir / ".lux" / (std::string("package-check-") + config);
    fs::create_directories(unpack);
    if (!Run(tools.cmake,
             {LUX_UI_TEXT("-E"), LUX_UI_TEXT("tar"), LUX_UI_TEXT("xf"), ToQString(archive)}, unpack,
             120000, output)) {
        PrintTail(output);
        return false;
    }
    const auto unpackedExecutable = unpack / "LuxGame" / executable->filename();
    const auto unpackedScene = unpack / "LuxGame/Content/Levels" / PathFromUtf8("새 시작 씬.lux");
    LuxEngine::Scene portable;
    if (!portable.Deserialize(PathUtf8(unpackedScene))) {
        std::cerr << "packaged scene did not reopen\n";
        return false;
    }
    const auto environment = portable.GetSettings().render3D.environmentPath;
    if (environment.empty() || PathFromUtf8(environment).is_absolute() ||
        !fs::is_regular_file(PathFromUtf8(portable.ResolveAssetPath(environment)))) {
        std::cerr << "packaged environment path is not portable\n";
        return false;
    }
    for (auto entity : portable.GetEntities())
        if (const auto* mesh = entity.TryGetComponent<LuxEngine::MeshRendererComponent>())
            for (const auto* resource : {&mesh->meshPath, &mesh->material.baseColorTexture})
                if (!resource->empty() &&
                    (PathFromUtf8(*resource).is_absolute() ||
                     !fs::is_regular_file(PathFromUtf8(portable.ResolveAssetPath(*resource))))) {
                    std::cerr << "packaged resource paths are not portable\n";
                    return false;
                }
    if (!Run(unpackedExecutable, {LUX_UI_TEXT("--smoke-test")}, unpackedExecutable.parent_path(),
             120000, output) ||
        output.find("[LuxEngine WARN]") != std::string::npos) {
        PrintTail(output);
        return false;
    }
    std::cout << "-- unpacked game runs with portable mesh and texture resources\n";
    std::cout << "-- zip " << package->fileCount << " files, " << fs::file_size(archive)
              << " bytes\n";
    return package->fileCount >= 3 && fs::file_size(archive) > 0;
}

} // namespace

int main(int argc, char* argv[]) {
    LuxUI::CoreApplication app(argc, argv);
    const fs::path appDir = fs::path(LuxUI::CoreApplication::applicationDirPath().toStdU16String());
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
    // The selected start scene must survive building and packaging even outside Scenes/.
    // Removing the default catches a runner that silently keeps using Scenes/Main.lux.
    LuxEngine::Scene alternate;
    alternate.CreateEntity("플레이어"); // A named player without an optional rigidbody is valid.
    const auto meshFile = root / "Assets" / PathFromUtf8("한글 메시.obj");
    fs::create_directories(meshFile.parent_path());
    {
        std::ofstream mesh(meshFile);
        mesh << "v -1 0 0\nv 1 0 0\nv 0 2 0\nvt 0 0\nvt 1 0\nvt .5 1\nvn 0 0 1\nf 1/1/1 2/2/1 "
                "3/3/1\n";
    }
    const auto textureFile = root / "Assets" / PathFromUtf8("한글 기본색상.PnG");
    LuxUI::Image baseColor(4, 4, LuxUI::Image::Format_RGBA8888);
    baseColor.fill(LuxUI::Color(160, 90, 40));
    if (!baseColor.save(ToQString(textureFile), "PNG"))
        return 1;
    const auto environmentFile = root / "Assets" / PathFromUtf8("한글 환경.HdR");
    {
        std::ofstream stream(environmentFile, std::ios::binary);
        stream << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 2\n";
        const unsigned char pixels[8] = {128, 128, 128, 129, 64, 64, 64, 129};
        stream.write(reinterpret_cast<const char*>(pixels), 8);
    }
    auto rendering = alternate.GetSettings();
    rendering.render3D.mode = LuxEngine::Render3DMode::Pbr;
    rendering.render3D.environmentPath = "../Assets/한글 환경.HdR";
    rendering.render3D.showSkybox = true;
    if (!alternate.SetSettings(rendering))
        return 1;
    auto& meshComponent =
        alternate.CreateEntity("Mesh").AddComponent<LuxEngine::MeshRendererComponent>();
    meshComponent.meshPath = "../Assets/한글 메시.obj";
    meshComponent.material.baseColorTexture = "../Assets/한글 기본색상.PnG";
    const auto start = root / PathFromUtf8("Levels/새 시작 씬.lux");
    fs::create_directories(start.parent_path(), ec);
    if (ec || !alternate.Serialize(PathUtf8(start)) || !project->SetStartScene(start, error)) {
        std::cerr << "start scene configuration failed: " << error << '\n';
        return 1;
    }
    fs::remove(root / "Scenes/Main.lux", ec);
    if (ec) {
        std::cerr << "default scene removal failed: " << ec.message() << '\n';
        return 1;
    }
    bool ok = BuildRunAndPackage(root, BuildConfiguration::Debug, tools);
    ok = BuildRunAndPackage(root, BuildConfiguration::Release, tools) && ok;
    if (ok)
        fs::remove_all(root, ec);
    std::cout << (ok ? "PASS" : "FAIL") << '\n';
    return ok ? 0 : 1;
}
