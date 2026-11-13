#include <LuxEditor/Project/EditorProject.hpp>
#include <LuxEditor/Project/PathEncoding.hpp>
#include <LuxEditor/Project/ProjectArchive.hpp>
#include <LuxEditor/Project/ProjectFileOperations.hpp>
#include <LuxEditor/Scene/EditorDocument.hpp>

#include <QImage>
#include <QString>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <zip.h>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

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

void TestSceneWriteLimits() {
    using namespace LuxEngine;
    {
        Scene scene;
        for (unsigned i = 0; i < 100001; ++i)
            scene.CreateEntity("Count");
        std::string output = "preserved";
        Check(!scene.SerializeToString(output) && output == "preserved",
              "writer refuses entity counts the reader cannot load");
    }
    {
        Scene scene;
        TilemapComponent map;
        Check(map.Resize(256, 256), "large scene tile fixture creates");
        for (unsigned i = 0; i < 1024; ++i)
            scene.CreateEntity("Map").AddComponent<TilemapComponent>(map);
        std::string output = "preserved";
        Check(!scene.SerializeToString(output) && output == "preserved",
              "writer refuses scene bytes above the reader limit");
    }
}

void TestDocument() {
    using namespace LuxEngine;
    using namespace LuxEditor;

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
    using namespace LuxEditor;
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
    using namespace LuxEditor;
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
    // A failed save must leave the previous file untouched and no temporary file behind.
    {
        std::ifstream before(path, std::ios::binary);
        const std::string original((std::istreambuf_iterator<char>(before)),
                                   std::istreambuf_iterator<char>());
        before.close();
        Check(!original.empty() && !document.SaveAs(path), "invalid scene does not save");
        std::ifstream after(path, std::ios::binary);
        const std::string kept((std::istreambuf_iterator<char>(after)),
                               std::istreambuf_iterator<char>());
        size_t files_in_folder = 0;
        for ([[maybe_unused]] const auto& entry :
             std::filesystem::directory_iterator(std::filesystem::path(path).parent_path()))
            ++files_in_folder;
        Check(kept == original, "failed save keeps the previous scene file");
        size_t leftovers = 0;
        for (const auto& entry :
             std::filesystem::directory_iterator(std::filesystem::path(path).parent_path()))
            if (entry.path().string().find(".saving-") != std::string::npos)
                ++leftovers;
        Check(leftovers == 0 && files_in_folder > 0, "failed save leaves no temporary file");
    }
}

void TestProjectManagement() {
    using namespace LuxEngine;
    using namespace LuxEditor;
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
    using namespace LuxEditor;
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
    Check(migrated.has_value() &&
              std::filesystem::exists(legacy / (std::string(EditorProject::FileName) + ".v1.bak")),
          "v1 project migrates with original manifest backup");
    std::ifstream migratedFile(migrated->GetFile());
    std::string firstLine;
    std::getline(migratedFile, firstLine);
    Check(firstLine == "{", "migration writes current project format");
    Check(std::filesystem::is_regular_file(legacy / "Source" / "Main.cpp") &&
              std::filesystem::is_regular_file(legacy / "CMakeLists.txt"),
          "migration supplies template source to legacy projects");
    Check(EditorProject::Open(migrated->GetFile(), error).has_value(), "migrated project reopens");

    const auto incompatible = files.path / "incompatible.luxproj";
    std::filesystem::copy_file(project->GetFile(), incompatible);
    std::ifstream source(incompatible);
    std::string text((std::istreambuf_iterator<char>(source)), std::istreambuf_iterator<char>());
    source.close();
    const auto engineVersion =
        "\"engine_version\": \"" + EditorProject::CurrentEngineVersion() + "\"";
    const auto location = text.find(engineVersion);
    Check(location != std::string::npos, "fixture engine version exists");
    text.replace(location, engineVersion.size(), "\"engine_version\": \"99.0.0\"");
    std::ofstream(incompatible) << text;
    Check(!EditorProject::Open(incompatible, error), "incompatible engine version is rejected");

    const auto malicious = files.path / "malicious.luxpack";
    {
        int code = 0;
        zip_t* zip = zip_open(PathUtf8(malicious).c_str(), ZIP_CREATE | ZIP_TRUNCATE, &code);
        Check(zip != nullptr, "malicious archive fixture creates");
        zip_source_t* zipSource = zip_source_buffer(zip, "bad", 3, 0);
        Check(zipSource && zip_file_add(zip, "../outside.txt", zipSource, 0) >= 0,
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
    using namespace LuxEditor;
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

void TestArchiveSecurityAndFuzzing() {
    using namespace LuxEngine;
    using namespace LuxEditor;
    TempDirectory files;
    std::string error;

    auto makeValidProj = [](const std::string& name, const std::string& sceneRel) {
        return "LUXPROJECT 2\nname \"" + name + "\"\nengine_version \"" +
               std::string(EditorProject::CurrentEngineVersion()) +
               "\"\nproject_version \"1.0.0\"\ntemplate \"empty\"\nstart_scene \"" + sceneRel +
               "\"\nicon \"\"\nautosave_seconds 300\nbackup_limit 5\nsnap_to_grid "
               "0\ndependency_count 0\n";
    };
    const std::string validSceneData = "LuxEngineScene 2 0\n";

    // 1. Corrupted / truncated ZIP archive
    const auto corruptZip = files.path / "corrupt.luxpack";
    {
        std::ofstream out(corruptZip, std::ios::binary);
        out << "PK\x03\x04truncated_and_corrupt_data";
    }
    Check(!ImportProject(corruptZip, files.path / "corrupt_imported", error),
          "corrupted archive safely rejected");

    // 2. Non-existent archive
    Check(!ImportProject(files.path / "does_not_exist.luxpack", files.path / "none", error),
          "non-existent archive fails safely");

    // 3. Zip with malicious path traversal (leading slash / root escape)
    const auto escapeZip = files.path / "escape.luxpack";
    int zipErr = 0;
    zip_t* z = zip_open(PathUtf8(escapeZip).c_str(), ZIP_CREATE | ZIP_TRUNCATE, &zipErr);
    Check(z != nullptr, "fixture zip created");
    zip_source_t* src1 = zip_source_buffer(z, "malicious", 9, 0);
    Check(zip_file_add(z, "/escape.txt", src1, ZIP_FL_ENC_UTF_8) >= 0, "root escape entry added");
    zip_source_t* src2 = zip_source_buffer(
        z, "LUXPROJECT 2\nname \"Escape\"\nengine_version \"0.1.0\"\nstart_scene \"Scene.lux\"\n",
        71, 0);
    Check(zip_file_add(z, EditorProject::FileName, src2, ZIP_FL_ENC_UTF_8) >= 0,
          "project file added");
    Check(zip_close(z) == 0, "escape fixture zip finalized");

    Check(!ImportProject(escapeZip, files.path / "escape_imported", error),
          "root escape / Zip Slip attempt safely rejected");

    // 4. Zip with Windows reserved name (e.g. CON.txt, NUL, COM1, AUX, CONIN$)
    const auto conZip = files.path / "reserved.luxpack";
    z = zip_open(PathUtf8(conZip).c_str(), ZIP_CREATE | ZIP_TRUNCATE, &zipErr);
    Check(z != nullptr, "reserved zip created");
    zip_source_t* src3 = zip_source_buffer(z, "reserved", 8, 0);
    Check(zip_file_add(z, "CON.txt", src3, ZIP_FL_ENC_UTF_8) >= 0, "CON entry added");
    zip_source_t* src4 = zip_source_buffer(
        z, "LUXPROJECT 2\nname \"Reserved\"\nengine_version \"0.1.0\"\nstart_scene \"Scene.lux\"\n",
        73, 0);
    Check(zip_file_add(z, EditorProject::FileName, src4, ZIP_FL_ENC_UTF_8) >= 0,
          "project file added");
    Check(zip_close(z) == 0, "reserved fixture zip finalized");

    Check(!ImportProject(conZip, files.path / "reserved_imported", error),
          "Windows reserved device name (CON) in zip safely rejected");

    // 5. Zip with Alternate Data Stream (:)
    const auto adsZip = files.path / "ads.luxpack";
    z = zip_open(PathUtf8(adsZip).c_str(), ZIP_CREATE | ZIP_TRUNCATE, &zipErr);
    Check(z != nullptr, "ads zip created");
    zip_source_t* srcAds = zip_source_buffer(z, "stream data", 11, 0);
    Check(zip_file_add(z, "file.txt:hidden", srcAds, ZIP_FL_ENC_UTF_8) >= 0, "ADS entry added");
    zip_source_t* srcProjAds = zip_source_buffer(
        z, "LUXPROJECT 2\nname \"ADS\"\nengine_version \"0.1.0\"\nstart_scene \"Scene.lux\"\n", 68,
        0);
    Check(zip_file_add(z, EditorProject::FileName, srcProjAds, ZIP_FL_ENC_UTF_8) >= 0,
          "project file added");
    Check(zip_close(z) == 0, "ads fixture zip finalized");
    Check(!ImportProject(adsZip, files.path / "ads_imported", error),
          "Alternate Data Stream (:) in zip safely rejected");

    // 6. Zip with Trailing Dot and Trailing Space
    const auto trailingZip = files.path / "trailing.luxpack";
    z = zip_open(PathUtf8(trailingZip).c_str(), ZIP_CREATE | ZIP_TRUNCATE, &zipErr);
    Check(z != nullptr, "trailing zip created");
    zip_source_t* srcDot = zip_source_buffer(z, "dot data", 8, 0);
    Check(zip_file_add(z, "bad_name.", srcDot, ZIP_FL_ENC_UTF_8) >= 0, "trailing dot entry added");
    zip_source_t* srcProjTrailing = zip_source_buffer(
        z, "LUXPROJECT 2\nname \"Trailing\"\nengine_version \"0.1.0\"\nstart_scene \"Scene.lux\"\n",
        73, 0);
    Check(zip_file_add(z, EditorProject::FileName, srcProjTrailing, ZIP_FL_ENC_UTF_8) >= 0,
          "project file added");
    Check(zip_close(z) == 0, "trailing fixture zip finalized");
    Check(!ImportProject(trailingZip, files.path / "trailing_imported", error),
          "Trailing dot/space in zip path safely rejected");

    // 7. Zip with case-insensitive collision on Windows (e.g. Asset.txt vs asset.txt)
    const auto caseZip = files.path / "case_collision.luxpack";
    z = zip_open(PathUtf8(caseZip).c_str(), ZIP_CREATE | ZIP_TRUNCATE, &zipErr);
    Check(z != nullptr, "case collision zip created");
    zip_source_t* srcCase1 = zip_source_buffer(z, "case1", 5, 0);
    Check(zip_file_add(z, "Asset.txt", srcCase1, ZIP_FL_ENC_UTF_8) >= 0, "Asset.txt entry added");
    zip_source_t* srcCase2 = zip_source_buffer(z, "case2", 5, 0);
    Check(zip_file_add(z, "asset.txt", srcCase2, ZIP_FL_ENC_UTF_8) >= 0, "asset.txt entry added");
    zip_source_t* srcProjCase = zip_source_buffer(
        z, "LUXPROJECT 2\nname \"Case\"\nengine_version \"0.1.0\"\nstart_scene \"Scene.lux\"\n", 69,
        0);
    Check(zip_file_add(z, EditorProject::FileName, srcProjCase, ZIP_FL_ENC_UTF_8) >= 0,
          "project file added");
    Check(zip_close(z) == 0, "case fixture zip finalized");
    Check(!ImportProject(caseZip, files.path / "case_imported", error),
          "Case-collision entries in zip safely rejected");

    // 8. Zip with file-directory collision (e.g. "dir" is a file, and "dir/child.txt" is inside it)
    const auto collZip = files.path / "file_dir_collision.luxpack";
    z = zip_open(PathUtf8(collZip).c_str(), ZIP_CREATE | ZIP_TRUNCATE, &zipErr);
    Check(z != nullptr, "collision zip created");
    zip_source_t* srcFile = zip_source_buffer(z, "file content", 12, 0);
    Check(zip_file_add(z, "conflict", srcFile, ZIP_FL_ENC_UTF_8) >= 0, "conflict file entry added");
    zip_source_t* srcChild = zip_source_buffer(z, "child content", 13, 0);
    Check(zip_file_add(z, "conflict/child.txt", srcChild, ZIP_FL_ENC_UTF_8) >= 0,
          "conflict/child.txt entry added");
    zip_source_t* srcProjColl = zip_source_buffer(
        z, "LUXPROJECT 2\nname \"Coll\"\nengine_version \"0.1.0\"\nstart_scene \"Scene.lux\"\n", 69,
        0);
    Check(zip_file_add(z, EditorProject::FileName, srcProjColl, ZIP_FL_ENC_UTF_8) >= 0,
          "project file added");
    Check(zip_close(z) == 0, "collision fixture zip finalized");
    Check(!ImportProject(collZip, files.path / "coll_imported", error),
          "File-directory collision in zip safely rejected");

    // 9. Zip missing project.luxproj manifest file
    const auto noProjZip = files.path / "no_manifest.luxpack";
    z = zip_open(PathUtf8(noProjZip).c_str(), ZIP_CREATE | ZIP_TRUNCATE, &zipErr);
    Check(z != nullptr, "no proj zip created");
    zip_source_t* srcOrphan = zip_source_buffer(z, "orphan data", 11, 0);
    Check(zip_file_add(z, "orphan.txt", srcOrphan, ZIP_FL_ENC_UTF_8) >= 0, "orphan entry added");
    Check(zip_close(z) == 0, "no proj fixture zip finalized");
    Check(!ImportProject(noProjZip, files.path / "no_manifest_imported", error),
          "Archive missing project.luxproj safely rejected");

    // 10. Zip with case-insensitive .git / .lux attempt (.GIT/config, .Lux/cache)
    const auto gitZip = files.path / "git_blocked.luxpack";
    z = zip_open(PathUtf8(gitZip).c_str(), ZIP_CREATE | ZIP_TRUNCATE, &zipErr);
    Check(z != nullptr, "git zip created");
    zip_source_t* srcGit = zip_source_buffer(z, "[core]\n", 7, 0);
    Check(zip_file_add(z, ".GIT/config", srcGit, ZIP_FL_ENC_UTF_8) >= 0, ".GIT/config entry added");
    zip_source_t* srcProjGit = zip_source_buffer(
        z, "LUXPROJECT 2\nname \"Git\"\nengine_version \"0.1.0\"\nstart_scene \"Scene.lux\"\n", 68,
        0);
    Check(zip_file_add(z, EditorProject::FileName, srcProjGit, ZIP_FL_ENC_UTF_8) >= 0,
          "project file added");
    Check(zip_close(z) == 0, "git fixture zip finalized");
    Check(!ImportProject(gitZip, files.path / "git_imported", error),
          "Case-insensitive .GIT directory in zip safely rejected");

    const auto luxDirZip = files.path / "lux_blocked.luxpack";
    z = zip_open(PathUtf8(luxDirZip).c_str(), ZIP_CREATE | ZIP_TRUNCATE, &zipErr);
    Check(z != nullptr, "lux dir zip created");
    zip_source_t* srcLux = zip_source_buffer(z, "cache data", 10, 0);
    Check(zip_file_add(z, ".Lux/internal", srcLux, ZIP_FL_ENC_UTF_8) >= 0,
          ".Lux/internal entry added");
    zip_source_t* srcProjLux = zip_source_buffer(
        z, "LUXPROJECT 2\nname \"Lux\"\nengine_version \"0.1.0\"\nstart_scene \"Scene.lux\"\n", 68,
        0);
    Check(zip_file_add(z, EditorProject::FileName, srcProjLux, ZIP_FL_ENC_UTF_8) >= 0,
          "project file added");
    Check(zip_close(z) == 0, "lux fixture zip finalized");
    Check(!ImportProject(luxDirZip, files.path / "lux_imported", error),
          "Case-insensitive .Lux directory in zip safely rejected");

    // 11. Order-reversed file-directory collision ("folder/child.txt" FIRST, "folder" SECOND as
    // file)
    const auto revCollZip = files.path / "rev_file_dir_collision.luxpack";
    z = zip_open(PathUtf8(revCollZip).c_str(), ZIP_CREATE | ZIP_TRUNCATE, &zipErr);
    Check(z != nullptr, "rev collision zip created");
    zip_source_t* srcRevChild = zip_source_buffer(z, "child content", 13, 0);
    Check(zip_file_add(z, "rev_dir/nested.txt", srcRevChild, ZIP_FL_ENC_UTF_8) >= 0,
          "rev_dir/nested.txt entry added first");
    zip_source_t* srcRevFile = zip_source_buffer(z, "file content", 12, 0);
    Check(zip_file_add(z, "rev_dir", srcRevFile, ZIP_FL_ENC_UTF_8) >= 0,
          "rev_dir file entry added second");
    zip_source_t* srcProjRev = zip_source_buffer(
        z, "LUXPROJECT 2\nname \"RevColl\"\nengine_version \"0.1.0\"\nstart_scene \"Scene.lux\"\n",
        72, 0);
    Check(zip_file_add(z, EditorProject::FileName, srcProjRev, ZIP_FL_ENC_UTF_8) >= 0,
          "project file added");
    Check(zip_close(z) == 0, "rev collision fixture zip finalized");
    Check(!ImportProject(revCollZip, files.path / "rev_coll_imported", error),
          "Order-reversed file-directory collision in zip safely rejected");

#if defined(_WIN32)
    // 12. Windows Directory Junction rejection in project export
    const auto juncProjDir = files.path / "junc_proj";
    auto juncProj = EditorProject::Create(juncProjDir, "JuncProj", ProjectTemplate::Empty, error);
    Check(juncProj.has_value(), "junction test project created");
    const auto externalTarget = files.path / "secret_external";
    std::filesystem::create_directories(externalTarget);
    std::ofstream(externalTarget / "secret.txt") << "sensitive system data";

    const auto junctionLink = juncProjDir / "Assets" / "LinkedJunction";
    std::string cmd = "cmd /c mklink /J \"" + junctionLink.string() + "\" \"" +
                      externalTarget.string() + "\" >nul 2>nul";
    system(cmd.c_str());

    const auto juncArchive = files.path / "junc_test.luxpack";
    Check(!ExportProject(*juncProj, juncArchive, error),
          "Project containing directory junction safely rejected during export");

    std::string rmCmd = "cmd /c rmdir \"" + junctionLink.string() + "\" >nul 2>nul";
    system(rmCmd.c_str());
#endif

    // 13. Valid ZIP directory entries (Assets/, Scenes/) and traversal rejection (BUG-01)
    const auto dirEntriesZip = files.path / "valid_dir_entries.luxpack";
    z = zip_open(PathUtf8(dirEntriesZip).c_str(), ZIP_CREATE | ZIP_TRUNCATE, &zipErr);
    Check(z != nullptr, "dir entries zip created");
    Check(zip_dir_add(z, "Assets", ZIP_FL_ENC_UTF_8) >= 0, "Assets/ dir entry added");
    Check(zip_dir_add(z, "Scenes", ZIP_FL_ENC_UTF_8) >= 0, "Scenes/ dir entry added");
    zip_source_t* srcTex = zip_source_buffer(z, "texture data", 12, 0);
    Check(zip_file_add(z, "Assets/texture.txt", srcTex, ZIP_FL_ENC_UTF_8) >= 0,
          "Assets/texture.txt added");
    zip_source_t* srcScene = zip_source_buffer(z, validSceneData.c_str(), validSceneData.size(), 0);
    Check(zip_file_add(z, "Scenes/Main.lux", srcScene, ZIP_FL_ENC_UTF_8) >= 0,
          "Scenes/Main.lux added");
    const std::string projStr = makeValidProj("DirSuccess", "Scenes/Main.lux");
    zip_source_t* srcProjDir = zip_source_buffer(z, projStr.c_str(), projStr.size(), 0);
    Check(zip_file_add(z, EditorProject::FileName, srcProjDir, ZIP_FL_ENC_UTF_8) >= 0,
          "project file added");
    Check(zip_close(z) == 0, "dir entries zip finalized");

    auto dirImported = ImportProject(dirEntriesZip, files.path / "dir_success_imported", error);
    Check(dirImported.has_value(),
          "Archive with standard directory entries (Assets/, Scenes/) imported successfully");
    Check(std::filesystem::is_directory(files.path / "dir_success_imported" / "Assets"),
          "Assets directory created");
    Check(std::filesystem::is_directory(files.path / "dir_success_imported" / "Scenes"),
          "Scenes directory created");
    Check(std::filesystem::is_regular_file(files.path / "dir_success_imported" / "Assets" /
                                           "texture.txt"),
          "nested asset file preserved");

    // Attack ZIP with leading slash directory entry
    const auto slashDirZip = files.path / "slash_dir.luxpack";
    z = zip_open(PathUtf8(slashDirZip).c_str(), ZIP_CREATE | ZIP_TRUNCATE, &zipErr);
    zip_source_t* srcSlashDir = zip_source_buffer(z, "data", 4, 0);
    zip_file_add(z, "/absolute.txt", srcSlashDir, ZIP_FL_ENC_UTF_8);
    zip_close(z);
    Check(!ImportProject(slashDirZip, files.path / "slash_dir_imported", error),
          "leading slash entry rejected");

    // Attack ZIP with drive letter entry
    const auto driveZip = files.path / "drive.luxpack";
    z = zip_open(PathUtf8(driveZip).c_str(), ZIP_CREATE | ZIP_TRUNCATE, &zipErr);
    zip_source_t* srcDrive = zip_source_buffer(z, "data", 4, 0);
    zip_file_add(z, "C:/Windows/test.txt", srcDrive, ZIP_FL_ENC_UTF_8);
    zip_close(z);
    Check(!ImportProject(driveZip, files.path / "drive_imported", error),
          "drive letter entry rejected");

    // Attack ZIP with double slash
    const auto doubleSlashZip = files.path / "double_slash.luxpack";
    z = zip_open(PathUtf8(doubleSlashZip).c_str(), ZIP_CREATE | ZIP_TRUNCATE, &zipErr);
    zip_source_t* srcDoubleSlash = zip_source_buffer(z, "data", 4, 0);
    zip_file_add(z, "folder//unexpected.txt", srcDoubleSlash, ZIP_FL_ENC_UTF_8);
    zip_close(z);
    Check(!ImportProject(doubleSlashZip, files.path / "double_slash_imported", error),
          "double slash entry rejected");

    // 14. UTF-8 Boundary and Code Point Validation (BUG-04)
    // 14.1. Invalid 4-byte sequence > U+10FFFF (e.g. \xF5\x80\x80\x80)
    const auto overUnicodeZip = files.path / "over_unicode.luxpack";
    z = zip_open(PathUtf8(overUnicodeZip).c_str(), ZIP_CREATE | ZIP_TRUNCATE, &zipErr);
    zip_source_t* srcOver = zip_source_buffer(z, "bad", 3, 0);
    zip_file_add(z, "\xF5\x80\x80\x80.txt", srcOver, ZIP_FL_ENC_UTF_8);
    zip_close(z);
    Check(!ImportProject(overUnicodeZip, files.path / "over_unicode_imported", error),
          "UTF-8 code point > U+10FFFF (0xF5 lead byte) strictly rejected");

    // 14.2. Overlong UTF-8 sequence (e.g. 0xC0 0xAF)
    const auto overlongZip = files.path / "overlong.luxpack";
    z = zip_open(PathUtf8(overlongZip).c_str(), ZIP_CREATE | ZIP_TRUNCATE, &zipErr);
    zip_source_t* srcOverlong = zip_source_buffer(z, "bad", 3, 0);
    zip_file_add(z, "\xC0\xAF.txt", srcOverlong, ZIP_FL_ENC_UTF_8);
    zip_close(z);
    Check(!ImportProject(overlongZip, files.path / "overlong_imported", error),
          "Overlong UTF-8 sequence strictly rejected");

    // 14.3. UTF-16 surrogate in UTF-8 (0xED 0xA0 0x80)
    const auto surrogateZip = files.path / "surrogate.luxpack";
    z = zip_open(PathUtf8(surrogateZip).c_str(), ZIP_CREATE | ZIP_TRUNCATE, &zipErr);
    zip_source_t* srcSurrogate = zip_source_buffer(z, "bad", 3, 0);
    zip_file_add(z, "\xED\xA0\x80.txt", srcSurrogate, ZIP_FL_ENC_UTF_8);
    zip_close(z);
    Check(!ImportProject(surrogateZip, files.path / "surrogate_imported", error),
          "UTF-16 surrogate in UTF-8 strictly rejected");

    // 14.4. Valid 4-byte Unicode U+10FFFF (\xF4\x8F\xBF\xBF) - allowed
    const auto maxUnicodeZip = files.path / "max_unicode.luxpack";
    z = zip_open(PathUtf8(maxUnicodeZip).c_str(), ZIP_CREATE | ZIP_TRUNCATE, &zipErr);
    zip_source_t* srcMax = zip_source_buffer(z, "good", 4, 0);
    zip_file_add(z, "max_\xF4\x8F\xBF\xBF.txt", srcMax, ZIP_FL_ENC_UTF_8);
    zip_source_t* srcSceneMax =
        zip_source_buffer(z, validSceneData.c_str(), validSceneData.size(), 0);
    zip_file_add(z, "Scene.lux", srcSceneMax, ZIP_FL_ENC_UTF_8);
    const std::string projMaxStr = makeValidProj("MaxU", "Scene.lux");
    zip_source_t* srcProjMax = zip_source_buffer(z, projMaxStr.c_str(), projMaxStr.size(), 0);
    zip_file_add(z, EditorProject::FileName, srcProjMax, ZIP_FL_ENC_UTF_8);
    zip_close(z);
    Check(ImportProject(maxUnicodeZip, files.path / "max_unicode_imported", error).has_value(),
          "Maximum valid Unicode U+10FFFF accepted");
}

void TestArchiveSizeLimits() {
    using namespace LuxEngine;
    using namespace LuxEditor;
    constexpr uint64_t limit = 256ull * 1024 * 1024;
    for (const uint64_t size : {limit + 1, limit - 1, limit}) {
        TempDirectory files;
        std::string error;
        auto project = EditorProject::Create(files.path / "original", "SizeLimit",
                                             ProjectTemplate::Empty, error);
        Check(project.has_value(), "size-limit project creates");
        std::filesystem::path originalBackup;
        if (size > limit) {
            auto settings = project->GetSettings();
            settings.backupLimit = 1;
            Check(project->UpdateSettings(settings, error) &&
                      CreateProjectBackup(*project, originalBackup, error),
                  "valid backup exists before an oversized asset is added");
        }
        const auto asset = project->GetFile().parent_path() / "Assets" / "large.bin";
        std::ofstream(asset, std::ios::binary).close();
        std::filesystem::resize_file(asset, size);
        const auto archive = files.path / "export.luxpack";
        if (size > limit) {
            std::ofstream(archive, std::ios::binary) << "existing archive";
            Check(!ExportProject(*project, archive, error) && !error.empty(),
                  "export rejects an asset above the import limit");
            std::ifstream previous(archive, std::ios::binary);
            Check(std::string(std::istreambuf_iterator<char>(previous), {}) == "existing archive",
                  "rejected export preserves the previous archive");
            std::filesystem::path backup;
            Check(!CreateProjectBackup(*project, backup, error) && !std::filesystem::exists(backup),
                  "backup rejects an unrestorable project without leaving an archive");
            Check(std::filesystem::is_regular_file(originalBackup),
                  "failed backup does not prune the last valid backup");
            Check(!CloneProject(*project, files.path / "clone", "Clone", error) &&
                      !std::filesystem::exists(files.path / "clone"),
                  "clone rejects an oversized asset without leaving a project");
            continue;
        }
        Check(ExportProject(*project, archive, error), "boundary-size asset exports");
        auto imported = ImportProject(archive, files.path / "imported", error);
        Check(imported.has_value() &&
                  std::filesystem::file_size(files.path / "imported/Assets/large.bin") == size,
              "successful boundary-size export imports without truncation");
        std::filesystem::remove_all(files.path / "imported");
        std::filesystem::path backup;
        Check(CreateProjectBackup(*project, backup, error), "boundary-size backup succeeds");
        Check(RestoreProjectBackup(*project, backup, error) &&
                  std::filesystem::file_size(asset) == size,
              "successful boundary-size backup restores without truncation");
    }
}

void TestEmptyProjectDirectories() {
    using namespace LuxEngine;
    using namespace LuxEditor;
    TempDirectory files;
    std::string error;
    auto original =
        EditorProject::Create(files.path / "original", "Empty", ProjectTemplate::Empty, error);
    Check(original.has_value(), "empty project creates");
    std::filesystem::create_directories(files.path / "original/Assets/Empty/Nested");
    const auto archive = files.path / "empty.luxpack";
    Check(ExportProject(*original, archive, error), "empty project exports");
    auto imported = ImportProject(archive, files.path / "imported", error);
    auto cloned = CloneProject(*original, files.path / "cloned", "Clone", error);
    Check(imported && cloned, "empty project imports and clones");
    QImage image(2, 2, QImage::Format_RGB32);
    image.fill(Qt::green);
    const auto icon = files.path / "icon.png";
    Check(image.save(QString::fromUtf8(PathUtf8(icon))), "icon fixture saves");
    for (EditorProject* project : {&*imported, &*cloned}) {
        const auto root = project->GetFile().parent_path();
        Check(std::filesystem::is_directory(root / "Assets/Empty/Nested"),
              "export preserves nested empty directories");
        Check(project->SetIcon(icon, error) &&
                  std::filesystem::is_regular_file(root / project->GetIcon()),
              "imported and cloned empty projects accept an icon");
        std::filesystem::remove_all(root / "Assets");
        Check(project->SetIcon(icon, error) &&
                  std::filesystem::is_regular_file(root / project->GetIcon()),
              "icon storage recreates a missing Assets directory");
    }
    std::filesystem::remove_all(files.path / "original/Assets");
    Check(ExportProject(*original, files.path / "legacy.luxpack", error),
          "package without an Assets directory exports");
    auto legacy = ImportProject(files.path / "legacy.luxpack", files.path / "legacy", error);
    Check(legacy && std::filesystem::is_directory(files.path / "legacy/Assets") &&
              legacy->SetIcon(icon, error),
          "import repairs the basic layout of older packages without empty directory entries");
}

void TestBackupRetentionAfterRestore() {
    using namespace LuxEditor;
    TempDirectory files;
    for (const unsigned limit : {1u, 2u}) {
        std::string error;
        auto project = EditorProject::Create(files.path / std::to_string(limit), "Retention",
                                             ProjectTemplate::Empty, error);
        Check(project.has_value(), "retention project creates");
        auto settings = project->GetSettings();
        settings.backupLimit = limit;
        Check(project->UpdateSettings(settings, error), "retention limit saves");
        std::filesystem::path initial;
        Check(CreateProjectBackup(*project, initial, error), "initial backup exists");
        Check(RestoreProjectBackup(*project, initial, error), "backup restores with safety copy");
        for (int iteration = 0; iteration < 3; ++iteration) {
            std::filesystem::path newest;
            Check(CreateProjectBackup(*project, newest, error) && std::filesystem::exists(newest),
                  "new backup survives pruning mixed backup prefixes");
            size_t count = 0;
            for (const auto& entry : std::filesystem::directory_iterator(newest.parent_path()))
                count += entry.path().extension() == ".luxbackup";
            Check(count == limit, "all backup kinds share the configured retention limit");
            auto restored = ImportProject(
                newest,
                files.path / (std::to_string(limit) + "-restored-" + std::to_string(iteration)),
                error);
            Check(restored.has_value(), "retained backup remains readable");
        }
    }
}

} // namespace

void TestProjectSharingLocks() {
#ifdef _WIN32
    TempDirectory files;
    const auto source = files.path / "sharing-source", target = files.path / "sharing-target";
    std::filesystem::create_directory(source);
    const auto child = source / "data.txt";
    std::ofstream(child) << "preserved";
    HANDLE locked = CreateFileW(child.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(locked != INVALID_HANDLE_VALUE, "sharing-lock fixture opens");
    std::error_code error;
    {
        std::jthread release([locked] {
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
            CloseHandle(locked);
        });
        LuxEditor::RenameProjectPath(source, target, error);
    }
    Check(!error && std::filesystem::is_regular_file(target / "data.txt"),
          "project folder rename recovers from a temporary Windows sharing lock");
    locked =
        CreateFileW((target / "data.txt").c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(locked != INVALID_HANDLE_VALUE, "persistent sharing-lock fixture opens");
    LuxEditor::RenameProjectPath(target, source, error);
    CloseHandle(locked);
    Check(error && std::filesystem::is_regular_file(target / "data.txt") &&
              !std::filesystem::exists(source),
          "persistent sharing locks fail safely and preserve the project folder");
#endif
}

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "archive-limits") {
            TestArchiveSizeLimits();
            std::cout << "Archive size-limit checks passed\n";
            return 0;
        }
        TestDocument();
        TestSceneWriteLimits();
        Test3DEditorEntity();
        Test2DEditorComponents();
        TestProjectManagement();
        TestProjectOperations();
        TestBackupRetentionAfterRestore();
        TestProjectSharingLocks();
        TestUnicodeProjectPaths();
        TestArchiveSecurityAndFuzzing();
        TestEmptyProjectDirectories();
        std::cout << "Editor document checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Editor document checks failed: " << error.what() << '\n';
        return 1;
    }
}
