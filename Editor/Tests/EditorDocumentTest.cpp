#include "Scene/EditorDocument.hpp"
#include "Project/EditorProject.hpp"
#include "Project/PathEncoding.hpp"
#include "Project/ProjectArchive.hpp"

#include <QImage>
#include <QString>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <zip.h>

namespace {

void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

struct TempDirectory {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("lux-editor-document-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

    TempDirectory() {
        std::filesystem::create_directories(path);
    }
    ~TempDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

void TestDocument() {
    using namespace LuxEngine;

    TempDirectory files;
    const std::string savedPath = (files.path / "scene.lux").string();
    const std::string invalidPath = (files.path / "invalid.lux").string();

    EditorDocument document;
    Check(!document.IsDirty() && document.GetPath().empty(), "new document state");
    Check(document.GetScene().GetEntityCount() == 0 && !document.GetSelected(),
          "new scene is empty");

    auto hero = document.CreateEntity("Hero");
    Check(hero.IsValid() && document.GetSelected() == hero, "created entity is selected");
    Check(document.IsDirty() && document.GetScene().GetEntityCount() == 1,
          "creation marks document dirty");
    Check(!document.Save() && document.IsDirty(), "save without a path fails safely");
    hero.GetComponent<TransformComponent>().position = {3.0f, 4.0f, 0.0f};
    Check(document.SaveAs(savedPath), "save as succeeds");
    Check(!document.IsDirty() && document.GetPath() == savedPath,
          "save as records path and clears dirty state");

    hero.GetComponent<TagComponent>().tag = "Renamed";
    document.MarkDirty();
    Check(document.IsDirty(), "component edit marks document dirty");
    Check(document.Save() && !document.IsDirty(), "save updates current path");

    auto transient = document.CreateEntity("Transient");
    Check(document.GetScene().GetEntityCount() == 2 && document.GetSelected() == transient,
          "second entity is selected");
    document.Select(hero.GetId());
    Check(document.GetSelected() == hero, "entity selection by ID");
    Check(document.DeleteSelected(), "selected entity is deleted");
    Check(!hero.IsValid() && !document.GetSelected(), "deleted entity clears selection");
    Check(document.GetScene().GetEntityCount() == 1 && document.IsDirty(),
          "deletion marks document dirty");
    document.Select(999999u);
    Check(!document.GetSelected() && !document.DeleteSelected(),
          "invalid selection cannot delete an entity");

    std::ofstream(invalidPath) << "invalid scene";
    document.Select(transient.GetId());
    Check(!document.Open(invalidPath), "invalid scene fails to open");
    Check(document.IsDirty() && document.GetPath() == savedPath,
          "failed open preserves document state");
    Check(document.GetScene().GetEntityCount() == 1 && document.GetSelected() == transient,
          "failed open preserves scene and selection");
    Check(!document.Open((files.path / "missing.lux").string()), "missing scene fails to open");
    Check(document.GetSelected() == transient, "missing scene preserves selection");

    Check(document.Open(savedPath), "saved scene reopens");
    Check(!document.IsDirty() && document.GetPath() == savedPath, "open clears dirty state");
    Check(document.GetScene().GetEntityCount() == 1 && !document.GetSelected(),
          "open replaces scene and clears selection");
    Check(!transient.IsValid(), "old entity handles expire after open");
    auto entities = document.GetScene().GetEntities();
    Check(entities.size() == 1 && entities[0].GetComponent<TagComponent>().tag == "Renamed",
          "saved entity name survives roundtrip");
    Check(entities[0].GetComponent<TransformComponent>().position.x == 3.0f &&
              entities[0].GetComponent<TransformComponent>().position.y == 4.0f,
          "saved transform survives roundtrip");

    document.Select(entities[0].GetId());
    const auto recovery = (files.path / "recovery.lux").string();
    document.CreateEntity("RecoveryOnly");
    Check(document.GetScene().Serialize(recovery), "autosave fixture serializes");
    Check(document.Open(savedPath), "original scene opens before recovery");
    Check(document.Recover(savedPath, recovery), "autosave recovers into original document");
    Check(document.IsDirty() && document.GetPath() == savedPath &&
              document.GetScene().GetEntityCount() == 2,
          "recovered scene keeps original save path and dirty state");
    document.NewScene();
    Check(document.GetScene().GetEntityCount() == 0 && !document.GetSelected(),
          "new scene clears entities and selection");
    Check(document.GetPath().empty() && !document.IsDirty(),
          "new scene clears path and dirty state");
}

void Test3DEditorEntity() {
    using namespace LuxEngine;
    TempDirectory files;
    EditorDocument document;
    auto entity = document.CreateEntity("3D Mesh");
    auto& transform = entity.GetComponent<TransformComponent>();
    transform.position = {2.0f, 3.0f, 4.0f};
    auto& material = entity.AddComponent<MeshRendererComponent>().material;
    material.metallic = 0.75f;
    material.roughness = 0.25f;
    document.MarkDirty();
    Check(document.SaveAs((files.path / "mesh-scene.lux").string()), "3D editor entity saves");
    Check(document.Open((files.path / "mesh-scene.lux").string()), "3D editor entity reopens");
    const auto entities = document.GetScene().GetEntities();
    Check(entities.size() == 1 && entities[0].HasComponent<MeshRendererComponent>(),
          "3D mesh component survives scene roundtrip");
    const auto& restored = entities[0].GetComponent<MeshRendererComponent>().material;
    Check(restored.metallic == 0.75f && restored.roughness == 0.25f &&
              entities[0].GetComponent<TransformComponent>().position.z == 4.0f,
          "3D transform and material values survive scene roundtrip");
}

void Test2DEditorComponents() {
    using namespace LuxEngine;
    TempDirectory files;
    EditorDocument document;
    auto entity = document.CreateEntity("Editable World");
    auto& map = entity.AddComponent<TilemapComponent>();
    Check(map.Resize(4, 3) && map.Set(2, 1, 4), "tilemap can be painted");
    map.tileSize = .5f;
    auto& body = entity.AddComponent<Rigidbody2DComponent>();
    body.type = BodyType::Kinematic;
    body.gravityScale = .25f;
    auto& collider = entity.AddComponent<BoxCollider2DComponent>();
    collider.size = {2.0f, 3.0f};
    collider.offset = {.25f, -.5f};
    collider.isTrigger = true;
    auto& animation = entity.AddComponent<AnimationComponent>();
    animation.clip.name = "Move";
    animation.clip.duration = 2.0f;
    animation.clip.loop = false;
    animation.clip.AddKeyframe({0.0f, {0, 0, 0}, {0, 0, 0}, {1, 1, 1}});
    animation.clip.AddKeyframe({2.0f, {2, 3, 0}, {0, 0, 0}, {1, 1, 1}});
    const auto path = (files.path / "world.lux").string();
    Check(document.SaveAs(path), "scene saves editor components");
    const auto id = entity.GetId();
    Check(document.Open(path), "scene with editor components reopens");
    auto restored = document.GetScene().GetEntities();
    Check(restored.size() == 1 && restored[0].GetId() == id,
          "scene roundtrip preserves stable entity IDs");
    const auto& restoredMap = restored[0].GetComponent<TilemapComponent>();
    Check(restoredMap.width == 4 && restoredMap.height == 3 && restoredMap.Get(2, 1) == 4 &&
              restoredMap.tileSize == .5f,
          "tilemap cells survive scene roundtrip");
    const auto& restoredBody = restored[0].GetComponent<Rigidbody2DComponent>();
    const auto& restoredCollider = restored[0].GetComponent<BoxCollider2DComponent>();
    Check(restoredBody.type == BodyType::Kinematic && restoredBody.gravityScale == .25f &&
              restoredCollider.isTrigger && restoredCollider.size.y == 3.0f &&
              restoredCollider.offset.x == .25f,
          "physics settings survive scene roundtrip");
    const auto& restoredClip = restored[0].GetComponent<AnimationComponent>().clip;
    Check(restoredClip.name == "Move" && !restoredClip.loop && restoredClip.keyframes.size() == 2 &&
              restoredClip.keyframes[1].position.y == 3.0f,
          "animation keyframes survive scene roundtrip");
    restored[0].GetComponent<TilemapComponent>().tiles[0] = 255;
    Check(!document.GetScene().Serialize((files.path / "invalid-world.lux").string()),
          "invalid tilemap is rejected before serialization");
}

void TestProjectManagement() {
    using namespace LuxEngine;
    TempDirectory files;
    std::string error;
    const std::array templates{ProjectTemplate::Empty, ProjectTemplate::Platformer,
                               ProjectTemplate::TopDown, ProjectTemplate::Rpg,
                               ProjectTemplate::Puzzle};
    for (size_t index = 0; index < templates.size(); ++index) {
        const auto directory = files.path / ("project-" + std::to_string(index));
        const std::string name = "Project " + std::to_string(index);
        auto created = EditorProject::Create(directory, name, templates[index], error);
        Check(created.has_value(), "project template creates successfully");
        Check(std::filesystem::is_directory(directory / "Assets") &&
                  std::filesystem::is_regular_file(created->GetSceneFile()),
              "project has assets and start scene");
        auto opened = EditorProject::Open(created->GetFile(), error);
        Check(opened.has_value() && opened->GetName() == name &&
                  opened->GetTemplate() == templates[index],
              "project metadata survives roundtrip");
        EditorDocument document;
        Check(document.Open(opened->GetSceneFile().string()), "template scene opens in editor");
        Check(document.GetScene().GetEntityCount() == (index == 0                 ? 0u
                                                       : index == 3 || index == 4 ? 3u
                                                                                  : 2u),
              "template creates expected starter entities");
        Check(!EditorProject::Create(directory, "Duplicate", templates[index], error),
              "existing project is preserved");
    }
    const auto first = files.path / "project-0" / EditorProject::FileName;
    auto project = EditorProject::Open(first, error);
    Check(project->Rename("Renamed", error), "project can be renamed");
    Check(EditorProject::Open(first, error)->GetName() == "Renamed", "rename persists");
    Check(!project->Rename("../invalid", error), "invalid project name is rejected");
    Check(!EditorProject::Create(files.path / "invalid", "", ProjectTemplate::Empty, error),
          "empty project name is rejected");

    RecentProjects recent(files.path / "preferences" / "recent-projects.txt");
    recent.Load();
    Check(recent.Get().empty(), "missing recent list starts empty");
    Check(recent.Add(first), "recent project is saved");
    Check(recent.Add(files.path / "project-1" / EditorProject::FileName),
          "second recent project is saved");
    Check(recent.Add(first) && recent.Get().size() == 2 && recent.Get().front() == first,
          "recent projects move to front without duplicates");
    RecentProjects restored(files.path / "preferences" / "recent-projects.txt");
    restored.Load();
    Check(restored.Get() == recent.Get(), "recent projects survive restart");

    const auto malicious = files.path / "malicious.luxproj";
    std::ofstream(malicious) << "LUXPROJECT 1\nname \"Bad\"\nengine_version \"0.1.0\"\n"
                                "template \"empty\"\nstart_scene \"../outside.lux\"\n";
    Check(!EditorProject::Open(malicious, error), "parent traversal is rejected");
    std::ofstream(first) << "invalid";
    Check(!EditorProject::Open(first, error), "corrupt project is rejected");
    restored.Load();
    Check(restored.Get().size() == 1, "missing or corrupt recent projects are pruned");
}

void TestProjectOperations() {
    using namespace LuxEngine;
    TempDirectory files;
    std::string error;
    auto project = EditorProject::Create(files.path / "original", "Original",
                                         ProjectTemplate::Platformer, error);
    Check(project.has_value(), "project operations fixture creates");
    Check(std::filesystem::is_regular_file(files.path / "original" / "Source" / "Main.cpp") &&
              std::filesystem::is_regular_file(files.path / "original" / "CMakeLists.txt"),
          "template includes buildable source layout");
    auto settings = project->GetSettings();
    settings.version = "1.2.3";
    settings.autosaveSeconds = 30;
    settings.backupLimit = 2;
    settings.snapToGrid = false;
    settings.dependencies.push_back({"ExamplePackage", "1.2"});
    Check(project->UpdateSettings(settings, error), "project settings save");
    auto invalidSettings = settings;
    invalidSettings.version = "invalid";
    Check(!project->UpdateSettings(invalidSettings, error) &&
              project->GetSettings().version == "1.2.3",
          "invalid project version preserves settings");
    invalidSettings = settings;
    invalidSettings.dependencies = {{"Bad;Command", "1.0"}};
    Check(!project->UpdateSettings(invalidSettings, error),
          "dependency package cannot inject CMake commands");
    Check(EditorProject::Open(project->GetFile(), error)->GetSettings().version == "1.2.3",
          "project settings survive reopen");
    Check(std::filesystem::exists(files.path / "original" / "Dependencies.cmake"),
          "dependency declarations are generated");
    Check(project->BumpVersion(2, error) && project->GetSettings().version == "1.2.4",
          "patch version increments");
    Check(project->BumpVersion(1, error) && project->GetSettings().version == "1.3.0",
          "minor version resets patch");
    const auto icon = files.path / "icon.png";
    QImage iconImage(1, 1, QImage::Format_RGB32);
    iconImage.fill(QColor(255, 100, 50));
    Check(iconImage.save(QString::fromUtf8(PathUtf8(icon)), "PNG"), "icon fixture saves as PNG");
    Check(project->SetIcon(icon, error) &&
              std::filesystem::is_regular_file(files.path / "original" / project->GetIcon()),
          "project icon is copied into assets");
    const auto previousIcon = project->GetIcon();
    const auto badIcon = files.path / "invalid-icon.png";
    std::ofstream(badIcon, std::ios::binary) << "not an image";
    Check(!project->SetIcon(badIcon, error) && project->GetIcon() == previousIcon,
          "invalid icon leaves existing icon unchanged");
    Check(project->RescanResources(error) == 1 && error.empty(), "assets are rescanned");
    std::ofstream(files.path / "original" / "Assets" / "new.txt") << "new";
    Check(project->RescanResources(error) == 2, "resource rescan detects added assets");
    Check(std::filesystem::is_regular_file(files.path / "original" / ".lux" / "assets.index"),
          "resource index is persisted");

    Scene autosaved;
    Check(autosaved.Deserialize(project->GetSceneFile().string()), "scene loads for autosave");
    autosaved.CreateEntity("Unsaved");
    Check(project->SaveAutosave(project->GetSceneFile(), autosaved, error),
          "dirty scene can be autosaved");
    std::filesystem::last_write_time(project->GetAutosaveFile(project->GetSceneFile()),
                                     std::filesystem::file_time_type::clock::now() +
                                         std::chrono::seconds(5));
    Check(project->HasNewerAutosave(project->GetSceneFile()), "newer autosave is detected");
    Check(project->ClearAutosave(project->GetSceneFile()), "autosave can be cleared");
    Check(!project->HasNewerAutosave(project->GetSceneFile()), "cleared autosave is ignored");
    Check(!project->SaveAutosave(files.path / "outside.lux", autosaved, error),
          "autosave refuses a scene outside the project");

    std::filesystem::create_directory(files.path / "original" / ".git");
    std::ofstream(files.path / "original" / ".git" / "config") << "local git data";
    const auto archive = files.path / "export.luxpack";
    Check(ExportProject(*project, archive, error), "project exports to package");
    Check(ExportProject(*project, archive, error), "project export safely replaces package");
    auto imported = ImportProject(archive, files.path / "imported", error);
    Check(imported.has_value() && imported->GetName() == "Original" &&
              imported->GetSettings().dependencies.size() == 1 &&
              !std::filesystem::exists(files.path / "imported" / ".git"),
          "project package imports with metadata");
    Check(!ImportProject(archive, files.path / "imported", error),
          "import does not overwrite existing project");
    auto cloned = CloneProject(*project, files.path / "clone", "Clone", error);
    Check(cloned.has_value() && cloned->GetName() == "Clone", "project clone changes name");
    Check(!CloneProject(*project, files.path / "invalid-clone", "../Bad", error) &&
              !std::filesystem::exists(files.path / "invalid-clone"),
          "invalid clone does not leave project folder");

    std::filesystem::path backup;
    Check(CreateProjectBackup(*project, backup, error) && std::filesystem::exists(backup),
          "project backup is created");
    Check(project->Rename("Changed", error), "project changes after backup");
    Check(RestoreProjectBackup(*project, backup, error), "project backup restores");
    auto restored = EditorProject::Open(project->GetFile(), error);
    Check(restored.has_value() && restored->GetName() == "Original",
          "restore recovers earlier project metadata");
    Check(std::filesystem::exists(backup), "restore preserves backup history");
    bool hasPreRestoreBackup = false;
    for (const auto& entry :
         std::filesystem::directory_iterator(files.path / "original" / ".lux" / "backups"))
        hasPreRestoreBackup |= entry.path().filename().string().starts_with("pre-restore-");
    Check(hasPreRestoreBackup, "restore keeps pre-restore safety backup in project history");
    std::filesystem::path secondBackup;
    std::filesystem::path thirdBackup;
    Check(CreateProjectBackup(*restored, secondBackup, error) &&
              CreateProjectBackup(*restored, thirdBackup, error),
          "multiple backups can be created");
    size_t backupCount = 0;
    for (const auto& entry :
         std::filesystem::directory_iterator(files.path / "original" / ".lux" / "backups"))
        if (entry.path().extension() == ".luxbackup")
            ++backupCount;
    Check(backupCount == 2, "project backup retention limit is applied");

    const auto legacy = files.path / "legacy";
    std::filesystem::create_directories(legacy / "Scenes");
    std::filesystem::copy_file(project->GetSceneFile(), legacy / "Scenes" / "Main.lux");
    std::ofstream(legacy / EditorProject::FileName)
        << "LUXPROJECT 1\nname \"Legacy\"\nengine_version \""
        << EditorProject::CurrentEngineVersion()
        << "\"\ntemplate \"empty\"\nstart_scene \"Scenes/Main.lux\"\n";
    auto migrated = EditorProject::Open(legacy / EditorProject::FileName, error);
    Check(migrated.has_value() && std::filesystem::exists(legacy / "project.luxproj.v1.bak"),
          "v1 project migrates with original manifest backup");
    std::ifstream migratedFile(migrated->GetFile());
    std::string firstLine;
    std::getline(migratedFile, firstLine);
    Check(firstLine == "LUXPROJECT 2", "migration writes current project format");
    Check(std::filesystem::is_regular_file(legacy / "Source" / "Main.cpp") &&
              std::filesystem::is_regular_file(legacy / "CMakeLists.txt"),
          "migration supplies template source to legacy projects");
    Check(EditorProject::Open(migrated->GetFile(), error).has_value(), "migrated project reopens");

    const auto incompatible = files.path / "incompatible.luxproj";
    std::filesystem::copy_file(project->GetFile(), incompatible);
    std::ifstream source(incompatible);
    std::string text((std::istreambuf_iterator<char>(source)), std::istreambuf_iterator<char>());
    source.close();
    const auto engineVersion = "engine_version \"" + EditorProject::CurrentEngineVersion() + "\"";
    const auto location = text.find(engineVersion);
    Check(location != std::string::npos, "fixture engine version exists");
    text.replace(location, engineVersion.size(), "engine_version \"99.0.0\"");
    std::ofstream(incompatible) << text;
    Check(!EditorProject::Open(incompatible, error), "incompatible engine version is rejected");

    const auto malicious = files.path / "malicious.luxpack";
    {
        int code = 0;
        zip_t* zip = zip_open(PathUtf8(malicious).c_str(), ZIP_CREATE | ZIP_TRUNCATE, &code);
        Check(zip != nullptr, "malicious archive fixture creates");
        zip_source_t* source = zip_source_buffer(zip, "bad", 3, 0);
        Check(source && zip_file_add(zip, "../outside.txt", source, 0) >= 0,
              "malicious archive entry creates");
        Check(zip_close(zip) == 0, "malicious archive fixture closes");
    }
    Check(!ImportProject(malicious, files.path / "unsafe", error) &&
              !std::filesystem::exists(files.path / "outside.txt") &&
              !std::filesystem::exists(files.path / "unsafe"),
          "archive path traversal is rejected without writing outside destination");
}

void TestUnicodeProjectPaths() {
    using namespace LuxEngine;
    TempDirectory files;
    std::string error;
    const auto directory = files.path / PathFromUtf8("한글 프로젝트");
    auto project = EditorProject::Create(directory, "숲속 게임", ProjectTemplate::Empty, error);
    Check(project.has_value(), "unicode project creates");
    EditorDocument document;
    Check(document.Open(PathUtf8(project->GetSceneFile())), "unicode scene opens");
    document.CreateEntity("한글 객체");
    Check(document.Save(), "unicode scene saves");
    const auto resource = directory / "Assets" / PathFromUtf8("나무.txt");
    std::ofstream(resource) << "tree";
    Check(project->RescanResources(error) == 1 && error.empty(), "unicode resource rescans");
    const auto archive = files.path / PathFromUtf8("한글 패키지.luxpack");
    Check(ExportProject(*project, archive, error), "unicode project exports");
    const auto importedDirectory = files.path / PathFromUtf8("가져온 프로젝트");
    auto imported = ImportProject(archive, importedDirectory, error);
    Check(imported.has_value() && imported->GetName() == "숲속 게임" &&
              std::filesystem::exists(importedDirectory / "Assets" / PathFromUtf8("나무.txt")),
          "unicode project and resource import");
    Check(document.Open(PathUtf8(imported->GetSceneFile())) &&
              document.GetScene().GetEntityCount() == 1,
          "imported unicode scene opens");
    RecentProjects recent(files.path / PathFromUtf8("설정") / "recent.txt");
    Check(recent.Add(imported->GetFile()), "unicode recent project saves");
    RecentProjects reloaded(files.path / PathFromUtf8("설정") / "recent.txt");
    reloaded.Load();
    Check(reloaded.Get().size() == 1 && reloaded.Get()[0] == imported->GetFile(),
          "unicode recent project reloads");
}

} // namespace

int main() {
    try {
        TestDocument();
        Test3DEditorEntity();
        Test2DEditorComponents();
        TestProjectManagement();
        TestProjectOperations();
        TestUnicodeProjectPaths();
        std::cout << "Editor document checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Editor document checks failed: " << error.what() << '\n';
        return 1;
    }
}
