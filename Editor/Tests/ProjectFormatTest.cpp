#include <LuxEditor/Project/EditorProject.hpp>
#include <LuxEditor/Project/PathEncoding.hpp>
#include <LuxEditor/Project/ProjectArchive.hpp>
#include <LuxEngine/Runtime/ProjectManifest.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

using namespace LuxEditor;
using Json = nlohmann::ordered_json;
namespace {
void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
struct Files {
    std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("lux-project-json-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Files() {
        std::filesystem::create_directories(root);
    }
    ~Files() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
};
std::string Read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
void Write(const std::filesystem::path& path, const std::string& data) {
    std::ofstream(path, std::ios::binary | std::ios::trunc) << data;
}
void Test() {
    Files files;
    std::string error;
    auto project = EditorProject::Create(files.root / "project", "한국어 프로젝트",
                                         ProjectTemplate::Empty, error);
    Check(project.has_value(), "project creation");
    Check(project->GetFile().extension() == ".luxproject", "new canonical project extension");
    auto settings = project->GetSettings();
    settings.version = "1.2.3";
    settings.autosaveSeconds = 30;
    settings.backupLimit = 3;
    settings.snapToGrid = false;
    settings.dependencies = {{"ExamplePackage", "1.2"}};
    Check(project->UpdateSettings(settings, error), "settings saved");
    const auto original = Read(project->GetFile());
    const auto json = Json::parse(original);
    Check(json.at("format") == "LuxEngine.Project" && json.at("version") == 1,
          "explicit project schema");
    auto reopened = EditorProject::Open(project->GetFile(), error);
    Check(reopened && reopened->GetName() == project->GetName() &&
              reopened->GetSettings().dependencies == settings.dependencies &&
              reopened->GetSettings().version == settings.version &&
              !reopened->GetSettings().snapToGrid,
          "project fields roundtrip");
    const auto content = project->GetFile().parent_path();
    std::filesystem::path target;
    Check(LuxEngine::ReadProjectStartScene(content, target, error) &&
              target == std::filesystem::canonical(project->GetSceneFile()),
          "runtime resolves JSON start scene");
    const auto reject = [&](const std::string& invalid) {
        Write(project->GetFile(), invalid);
        Check(!EditorProject::Open(project->GetFile(), error), "invalid project rejected");
        Write(project->GetFile(), original);
        Check(EditorProject::Open(project->GetFile(), error).has_value(),
              "valid project remains readable");
    };
    const auto mutate = [&](auto fn) {
        auto bad = json;
        fn(bad);
        reject(bad.dump());
    };
    mutate([](auto& j) { j["version"] = 2; });
    mutate([](auto& j) { j["version"] = true; });
    mutate([](auto& j) { j["engine_version"] = "99.0.0"; });
    mutate([](auto& j) { j["settings"]["autosave_seconds"] = -1; });
    mutate([](auto& j) { j["settings"]["backup_limit"] = 4294967297ull; });
    mutate([](auto& j) { j["settings"]["snap_to_grid"] = 1; });
    mutate([](auto& j) { j["start_scene"] = "../outside.lux"; });
    reject("{\"format\":\"LuxEngine.Project\",\"format\":\"LuxEngine.Project\"}");
    reject(original + " trailing");
    reject(std::string("{\"deep\":") + std::string(30, '[') + "0" + std::string(30, ']') + "}");
    auto invalid = json;
    invalid["start_scene"] = "../outside.lux";
    Write(project->GetFile(), invalid.dump());
    const auto previous = target;
    Check(!LuxEngine::ReadProjectStartScene(content, target, error) && target == previous,
          "runtime rejects traversal without changing caller output");
    Write(project->GetFile(), original);
    const auto old = content / "project.luxproj";
    std::filesystem::rename(project->GetFile(), old);
    Check(EditorProject::Open(old, error).has_value() &&
              LuxEngine::ReadProjectStartScene(content, target, error),
          "old project extension remains readable in editor and runtime");
    // Legacy positional project manifests are still importable; the extracted manifest is
    // canonicalized.
    Write(old, "LUXPROJECT 2\nname \"Legacy\"\nengine_version \"" +
                   EditorProject::CurrentEngineVersion() +
                   "\"\nproject_version \"0.1.0\"\ntemplate \"empty\"\nstart_scene "
                   "\"Scenes/Main.lux\"\nicon \"\"\n"
                   "autosave_seconds 120\nbackup_limit 10\nsnap_to_grid 1\ndependency_count 0\n");
    auto legacy = EditorProject::Open(old, error);
    Check(legacy && LuxEngine::ReadProjectStartScene(content, target, error),
          "legacy v2 manifest opens");
    const auto archive = files.root / "old.luxpack";
    Check(ExportProject(*legacy, archive, error), "legacy export");
    auto imported = ImportProject(archive, files.root / "imported", error);
    Check(imported && imported->GetFile().extension() == ".luxproject",
          "legacy archive imports under canonical name");
    Check(legacy->Rename("이름 변경", error) && Json::parse(Read(old)).at("name") == "이름 변경",
          "legacy next save upgrades its contents without changing the caller's path");
    std::filesystem::path backup;
    Check(CreateProjectBackup(*legacy, backup, error), "legacy-name project backup");
    Check(legacy->Rename("백업 이후 이름", error), "legacy-name project edit");
    Check(RestoreProjectBackup(*legacy, backup, error), "legacy-name project restoration");
    auto restored = EditorProject::Open(old, error);
    Check(restored && restored->GetName() == "이름 변경",
          "restore preserves the original .luxproj path and backed-up metadata");
}
} // namespace
int main() {
    try {
        Test();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
