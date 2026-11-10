#include "Engine/EntryPoint.hpp"
#include "Engine/Scene/SceneManager.hpp"
#include "Project/EditorProject.hpp"
#include "Project/PathEncoding.hpp"
#include "Scene/EditorDocument.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
namespace fs = std::filesystem;
using namespace LuxEngine;
using namespace LuxEditor;

void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
struct Files {
    fs::path root = fs::temp_directory_path() /
                    ("lux-scene-documents-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Files() {
        fs::create_directories(root);
    }
    ~Files() {
        Scene::ClearSaveFaults();
        std::error_code error;
        fs::remove_all(root, error);
    }
};
std::string Bytes(const fs::path& file) {
    std::ifstream input(file, std::ios::binary);
    Check(input.is_open(), "fixture file can be read");
    return {std::istreambuf_iterator<char>(input), {}};
}
std::string Snapshot(Scene* scene) {
    std::string data;
    Check(scene && scene->SerializeToString(data), "scene snapshot serializes");
    return data;
}

void TestTabsAndHistory() {
    Files files;
    EditorDocument document;
    const auto first = document.GetActiveTabId();
    const auto firstPath = PathUtf8(files.root / PathFromUtf8("첫 번째 씬.lux"));
    const auto secondPath = PathUtf8(files.root / "second.lux");
    auto hero = document.CreateEntity("Hero");
    hero.GetComponent<TransformComponent>().position.x = 3;
    Check(document.SaveAs(firstPath), "first tab saves");
    const auto before = Snapshot(document.GetScene(first));
    document.GetScene().CreateEntity("First local edit");
    document.MarkDirty();
    const auto edited = Snapshot(document.GetScene(first));

    const auto second = document.NewTab();
    Check(first != second && document.ListTabs().size() == 2, "new scenes have stable tab IDs");
    document.CreateEntity("Second");
    Check(document.SaveAs(secondPath), "second tab saves independently");
    Check(document.IsDirty(first) && !document.IsDirty(second), "each tab retains its dirty state");
    Check(document.GetSelectedId(first) == hero.GetId(), "inactive tab retains selection");
    Check(document.GetScene(first)->GetEntityCount() == 2 &&
              document.GetScene(second)->GetEntityCount() == 1,
          "scene contents are independent");
    Check(document.ReplaceSceneFromData(first, before, hero.GetId()),
          "history can restore an inactive scene by stable ID");
    Check(document.GetActiveTabId() == second && document.GetScene(first)->GetEntityCount() == 1 &&
              document.GetScene(second)->GetEntityCount() == 1,
          "inactive history does not switch or overwrite the active scene");
    Check(document.ReplaceSceneFromData(first, edited, hero.GetId()), "redo restores the same tab");
    Check(!document.ReplaceSceneFromData(first, "broken", {}) &&
              Snapshot(document.GetScene(first)) == edited,
          "invalid history preserves the target scene");
    Check(!document.ActivateTab(0) && document.GetActiveTabId() == second,
          "unknown tab activation is harmless");

    const auto count = document.ListTabs().size();
    Check(document.OpenTab(firstPath) == first && document.ListTabs().size() == count &&
              document.IsDirty() && document.GetScene().GetEntityCount() == 2,
          "reopening an existing tab activates its unsaved contents");
    const auto alias = files.root / "hardlink.lux";
    std::error_code error;
    fs::create_hard_link(PathFromUtf8(firstPath), alias, error);
    if (!error)
        Check(document.OpenTab(PathUtf8(alias)) == first && document.ListTabs().size() == count,
              "physical file aliases do not create duplicate editable tabs");
    Check(document.ActivateTab(second), "second tab activates");
    Check(document.Save(first) && document.GetActiveTabId() == second && !document.IsDirty(first),
          "saving an inactive tab preserves the active document");
    const auto savedFirst = Bytes(PathFromUtf8(firstPath));
    const auto savedSecond = Bytes(PathFromUtf8(secondPath));
    document.CreateEntity("Second local edit");
    const auto secondSnapshot = Snapshot(document.GetScene(second));
    Check(!document.SaveAs(firstPath) && document.IsDirty() && document.GetPath() == secondPath &&
              Bytes(PathFromUtf8(firstPath)) == savedFirst,
          "Save As cannot overwrite a scene open in another tab");
    Check(!document.Open(firstPath) && document.GetActiveTabId() == second &&
              Snapshot(document.GetScene(second)) == secondSnapshot,
          "legacy replacement Open cannot silently duplicate another tab's file");
    Check(!document.OpenTab(PathUtf8(files.root / "missing.lux")) &&
              document.GetActiveTabId() == second && document.ListTabs().size() == count,
          "failed tab open leaves the workspace intact");
    Scene::InjectSaveFault(Scene::SaveFault::DiskFull);
    Check(!document.Save() && document.IsDirty() && Bytes(PathFromUtf8(secondPath)) == savedSecond,
          "failed tab save preserves bytes and dirty state");
    Scene::ClearSaveFaults();
    Check(!document.CloseTab(second) && document.GetActiveTabId() == second,
          "dirty tabs require explicit discard authorization");
    Check(document.CloseTab(second, true) && document.GetActiveTabId() == first,
          "closing the active tab chooses a remaining scene");
    Check(!document.GetScene(second) && !document.ReplaceSceneFromData(second, before, {}),
          "closed tab history cannot target a different document");
    const auto lastEntity = document.GetSelected();
    Check(document.CloseTab(first, true) && document.ListTabs().size() == 1 &&
              document.GetActiveTabId() != first && document.GetScene().GetEntityCount() == 0 &&
              !document.IsDirty() && document.GetPath().empty() && !lastEntity,
          "closing the final tab creates a fresh document and invalidates old handles");
    const auto previous = document.GetActiveTabId();
    document.NewTab();
    document.ResetTabs();
    Check(document.ListTabs().size() == 1 && document.GetActiveTabId() != previous &&
              !document.GetScene(previous),
          "project reset retires all tab IDs rather than reusing them");
}

void TestAdoptionAndDuplication() {
    Files files;
    EditorDocument document;
    const auto original = document.GetActiveTabId();
    auto parent = document.CreateEntity("Parent");
    auto child = document.CreateEntity("Child");
    parent.GetComponent<TransformComponent>().position = {5, 6, 0};
    child.GetComponent<TransformComponent>().position = {2, 3, 0};
    Check(document.GetScene().SetParent(child.GetId(), parent.GetId(), false),
          "duplicate fixture hierarchy attaches");
    const auto before = Snapshot(document.GetScene(original));
    const auto copiedPath = PathUtf8(files.root / "copy.lux");
    const auto copied = document.DuplicateTab(original, copiedPath);
    Check(copied && copied != original && document.GetPath() == copiedPath && !document.IsDirty(),
          "scene duplication creates a separately saved tab");
    Check(Snapshot(document.GetScene(copied)) == before && document.IsDirty(original),
          "duplication preserves hierarchy and does not mark its source saved");
    document.GetScene().GetComponent<TagComponent>(child.GetId()).tag = "Copy only";
    document.MarkDirty();
    Check(document.GetScene(original)->GetComponent<TagComponent>(child.GetId()).tag == "Child",
          "copied scene data does not alias its source");
    const auto count = document.ListTabs().size();
    Check(!document.DuplicateTab(original, copiedPath) && document.ListTabs().size() == count,
          "duplicate refuses existing target files");

    const auto lateResult = std::make_shared<Scene>();
    lateResult->CreateEntity("Stale async result");
    Check(document.AdoptTab(copiedPath, lateResult) == copied &&
              document.GetScene().GetComponent<TagComponent>(child.GetId()).tag == "Copy only" &&
              document.IsDirty(),
          "a late asynchronous result never overwrites an already open dirty tab");
    const auto adoptedPath = PathUtf8(files.root / "adopted.lux");
    const auto adopted = document.AdoptTab(adoptedPath, lateResult);
    Check(adopted && document.GetScene().GetEntityCount() == 1,
          "a loaded independent scene can be adopted without a disk reparse");
    Check(!document.AdoptTab(PathUtf8(files.root / "alias.lux"), lateResult),
          "two editable tabs cannot share one mutable scene instance");
    Check(!document.AdoptTab("", {}) && document.GetActiveTabId() == adopted,
          "null asynchronous results do not mutate tabs");
}

void TestExternalAliasesAndNativePaths() {
    Files files;
    EditorDocument document;
    const auto first = document.GetActiveTabId();
    const auto a = files.root / "first.lux", b = files.root / "second.lux";
    document.CreateEntity("First");
    Check(document.SaveAs(PathUtf8(a)), "alias first scene saves");
    const auto second = document.NewTab();
    document.CreateEntity("Second");
    Check(document.SaveAs(PathUtf8(b)), "alias second scene saves");
    fs::remove(b);
    std::error_code error;
    fs::create_hard_link(a, b, error);
    if (!error) {
        Check(document.FindTabByPath(PathUtf8(b)) == second,
              "exact tab paths remain preferred after external file replacement");
        Check(document.ActivateTab(first), "alias source activates");
        document.CreateEntity("Unsaved");
        const auto bytes = Bytes(a);
        Check(!document.SaveAs(PathUtf8(b)) && !document.Save() && Bytes(a) == bytes &&
                  Bytes(b) == bytes && document.IsDirty(),
              "all other tabs are checked even when the first alias match is the current tab");
    }
    const auto dangling = files.root / "dangling.lux";
    error.clear();
    fs::create_symlink(files.root / "absent.lux", dangling, error);
    if (!error)
        Check(!document.DuplicateTab(first, PathUtf8(dangling)) && fs::is_symlink(dangling),
              "duplication preserves an existing dangling symbolic link");

    fs::create_directories(files.root / "inside");
    fs::create_directories(files.root / "outside/child");
    Scene firstScene, secondScene;
    firstScene.CreateEntity("Native first");
    secondScene.CreateEntity("Native second");
    Check(firstScene.Serialize(PathUtf8(files.root / "inside/native.lux")) &&
              secondScene.Serialize(PathUtf8(files.root / "outside/native.lux")),
          "native traversal fixtures save");
    const auto link = files.root / "inside/link";
    error.clear();
    fs::create_directory_symlink(files.root / "outside/child", link, error);
    if (error)
        return;
    struct CurrentDirectory {
        fs::path previous = fs::current_path();
        ~CurrentDirectory() {
            std::error_code error;
            fs::current_path(previous, error);
        }
    } restoreDirectory;
    fs::current_path(files.root);
    const auto requested = fs::path("inside/link/../native.lux");
    Scene expected;
    Check(expected.Deserialize(PathUtf8(requested)), "native path traversal opens its actual file");
    EditorDocument native;
    Check(native.Open(PathUtf8(requested)) && Snapshot(&native.GetScene()) == Snapshot(&expected),
          "relative paths are anchored without collapsing symbolic-link parent traversal");
    const auto originalFile = fs::canonical(requested);
    fs::current_path(files.root / "inside");
    native.CreateEntity("Saved after CWD changed");
    Check(native.Save(), "relative scene save remains anchored after working-directory change");
    Scene saved;
    Check(saved.Deserialize(PathUtf8(originalFile)) && saved.GetEntityCount() == 2,
          "saving uses the document's original physical destination");
}

void TestStartScene() {
    Files files;
    std::string error;
    auto project = EditorProject::Create(files.root / PathFromUtf8("한글 프로젝트"), "씬 검사",
                                         ProjectTemplate::Empty, error);
    Check(project.has_value(), "start-scene project creates");
    const auto original = project->GetSceneFile();
    const auto next = original.parent_path().parent_path() / PathFromUtf8("Levels/첫 시작.lux");
    fs::create_directories(next.parent_path());
    Scene scene;
    scene.CreateEntity("Different entry scene");
    Check(scene.Serialize(PathUtf8(next)), "alternate entry scene saves");
    Check(project->SetStartScene(next.lexically_relative(project->GetFile().parent_path()), error),
          "start scene accepts a valid relative project path");
    auto reopened = EditorProject::Open(project->GetFile(), error);
    Check(reopened && fs::equivalent(reopened->GetSceneFile(), next),
          "start-scene selection survives project reload");
    const auto descriptor = Bytes(project->GetFile());
    const auto outside = files.root / "outside.lux";
    Check(scene.Serialize(PathUtf8(outside)), "external scene fixture saves");
    Check(!project->SetStartScene(outside, error) && Bytes(project->GetFile()) == descriptor &&
              fs::equivalent(project->GetSceneFile(), next),
          "external start-scene rejection preserves the project descriptor");
    const auto invalid = project->GetFile().parent_path() / "broken.lux";
    std::ofstream(invalid) << "invalid scene";
    Check(!project->SetStartScene(invalid, error) && Bytes(project->GetFile()) == descriptor,
          "invalid start-scene rejection preserves metadata");
    Check(!project->SetStartScene("missing.lux", error), "missing start scene is rejected");

    const auto alias = project->GetFile().parent_path() / "outside-link.lux";
    std::error_code ec;
    fs::create_symlink(outside, alias, ec);
    if (!ec)
        Check(!project->SetStartScene(alias, error) && Bytes(project->GetFile()) == descriptor,
              "symlink aliases cannot escape the project start-scene boundary");

    // A failed metadata replacement must not leave the in-memory start path half updated.
    fs::remove(project->GetFile());
    fs::create_directory(project->GetFile());
    Check(!project->SetStartScene(original, error) && fs::equivalent(project->GetSceneFile(), next),
          "failed project save preserves the selected start scene");
}

void TestAutosavePreservation() {
    Files files;
    std::string error;
    auto project =
        EditorProject::Create(files.root / "autosave", "Autosave", ProjectTemplate::Empty, error);
    Check(project.has_value(), "autosave project creates");
    Scene scene;
    auto entity = scene.CreateEntity("Recovery copy");
    const auto source = project->GetSceneFile();
    scene.SetSourcePath(PathUtf8(source));
    Check(project->SaveAutosave(source, scene, error), "initial autosave succeeds");
    Check(scene.GetSourcePath() == PathUtf8(source),
          "autosave does not redirect the live scene's reference context");
    const auto target = project->GetAutosaveFile(source);
    const auto previous = Bytes(target);
    entity.GetComponent<TagComponent>().tag = "Unsaved replacement";
    for (const auto fault : {Scene::SaveFault::DiskFull, Scene::SaveFault::ReplaceAccessDenied}) {
        Scene::InjectSaveFault(fault);
        const bool saved = project->SaveAutosave(source, scene, error);
        Scene::ClearSaveFaults();
        Check(!saved && Bytes(target) == previous,
              "failed autosave replacement retains the last recovery copy");
    }
    const auto missing = source.parent_path() / "DeletedReference.lux";
    auto reference = scene.CreateEntity("Reference under repair");
    reference.AddComponent<SceneReferenceComponent>().path = PathUtf8(missing);
    scene.CreateEntity("Cached authored content");
    const auto beforeMissingAutosave = Snapshot(&scene);
    Check(project->SaveAutosave(source, scene, error) && error.empty() &&
              Snapshot(&scene) == beforeMissingAutosave &&
              scene.GetSourcePath() == PathUtf8(source),
          "missing references do not prevent autosaving authored repair work");
    const auto missingRecovery = Bytes(target);
    Scene::InjectSaveFault(Scene::SaveFault::ReplaceAccessDenied);
    const bool rejectedReplacement = project->SaveAutosave(source, scene, error);
    Scene::ClearSaveFaults();
    Check(!rejectedReplacement && Bytes(target) == missingRecovery,
          "failed authored autosave retains the previous recovery file");
    EditorDocument recovered;
    Check(recovered.Recover(PathUtf8(source), PathUtf8(target)) && recovered.IsDirty() &&
              !recovered.GetLoadWarning().empty() &&
              recovered.GetScene().GetEntityCount() == scene.GetEntityCount() &&
              recovered.GetScene().GetComponent<SceneReferenceComponent>(reference.GetId()).path ==
                  PathUtf8(missing),
          "authored autosave recovers cached content and missing reference paths");
    Check(!SceneManager().Load(target, error),
          "authored autosave does not relax missing-reference runtime validation");

    reference.GetComponent<SceneReferenceComponent>().path = PathUtf8(source);
    Check(scene.Serialize(PathUtf8(source)) && project->SaveAutosave(source, scene, error),
          "cyclic references can be autosaved while awaiting repair");
    Check(recovered.Recover(PathUtf8(source), PathUtf8(target)) &&
              !recovered.GetLoadWarning().empty() && !SceneManager().Load(target, error),
          "cyclic authored autosave recovers in the editor while runtime remains strict");
    const auto outside = files.root / "outside-autosave";
    fs::create_directories(outside / "Scenes");
    const auto victim = outside / "Scenes/Main.lux";
    std::ofstream(victim) << "external data must survive";
    const auto victimBytes = Bytes(victim);
    const auto autosaveDirectory = target.parent_path().parent_path();
    fs::remove_all(autosaveDirectory);
    std::error_code ec;
    fs::create_directory_symlink(outside, autosaveDirectory, ec);
    if (!ec)
        Check(project->GetAutosaveFile(source).empty() &&
                  !project->SaveAutosave(source, scene, error) && !project->ClearAutosave(source) &&
                  Bytes(victim) == victimBytes,
              "autosave metadata links cannot overwrite or delete external files");
}

void TestBrokenReferenceRepair() {
    Files files;
    const auto source = files.root / "Scenes/Source.lux";
    const auto owner = files.root / "Scenes/Owner.lux";
    fs::create_directories(source.parent_path());
    fs::create_directories(files.root / "Copies");
    fs::create_directories(files.root / ".lux/autosave");
    Scene authoredSource;
    authoredSource.CreateEntity("Cached authored child");
    Check(authoredSource.Serialize(PathUtf8(source)), "repair source saves");
    Scene authored;
    authored.CreateEntity("Authored parent");
    SceneManager manager;
    std::string error;
    const auto reference =
        manager.AddSceneInstance(authored, source, SceneReferenceMode::Nested, error);
    Check(reference && SceneManager::Save(authored, owner, owner, error),
          "composed repair fixture saves cached contents");
    const auto cachedCount = authored.GetEntityCount();
    fs::remove(source);
    Check(!SceneManager().Load(owner, error) && !error.empty(),
          "runtime still rejects missing scene dependencies");

    EditorDocument document;
    Check(document.Open(PathUtf8(owner)) && document.GetLastError().empty() &&
              !document.GetLoadWarning().empty() &&
              document.GetScene().GetEntityCount() == cachedCount && !document.IsDirty(),
          "editor opens intact authored cached contents with a separate warning");
    const auto original = document.GetActiveTabId();
    const auto warning = document.GetLoadWarning();
    const auto blank = document.NewTab();
    Check(document.GetLoadWarning().empty() && document.GetLoadWarning(original) == warning &&
              document.GetLoadWarning(0).empty(),
          "load warnings belong to individual tabs");
    Check(document.OpenTab(PathUtf8(owner)) == original && document.GetLoadWarning() == warning &&
              document.GetLastError().empty(),
          "reopening a repairable document preserves its warning");

    const auto copied = files.root / "Copies/Repaired.lux";
    document.CreateEntity("New repair work");
    Check(document.SaveAs(PathUtf8(copied)) && document.GetLastError().empty() &&
              !document.IsDirty() && document.GetLoadWarning() == warning,
          "repair work saves even while a referenced file is missing");
    Scene diskCopy;
    Check(diskCopy.Deserialize(PathUtf8(copied)) && diskCopy.GetEntityCount() == cachedCount + 1,
          "authored save preserves cached entities and new edits");
    const auto diskReference =
        PathFromUtf8(diskCopy.GetComponent<SceneReferenceComponent>(reference).path);
    Check(fs::weakly_canonical(copied.parent_path() / diskReference) ==
              fs::weakly_canonical(source),
          "Save As rebases missing reference paths without retargeting them");
    Check(!SceneManager().Load(copied, error),
          "saving authored repairs does not relax runtime dependency validation");

    const auto autosave = files.root / ".lux/autosave/Owner.lux";
    Check(document.SaveAs(PathUtf8(autosave)), "cached recovery copy saves");
    EditorDocument recovery;
    Check(recovery.Recover(PathUtf8(owner), PathUtf8(autosave)) && recovery.IsDirty() &&
              recovery.GetLastError().empty() && !recovery.GetLoadWarning().empty(),
          "recovery opens cached authored contents when their reference is missing");
    Check(PathFromUtf8(recovery.GetScene().GetComponent<SceneReferenceComponent>(reference).path) ==
              fs::weakly_canonical(source),
          "recovery keeps the autosave reference context after restoring the logical path");

    const auto before = Snapshot(&recovery.GetScene());
    const auto previousPath = recovery.GetPath();
    const auto previousWarning = recovery.GetLoadWarning();
    const auto invalid = files.root / "Invalid.lux";
    std::ofstream(invalid) << "not a scene";
    Check(!recovery.Open(PathUtf8(invalid)) && !recovery.GetLastError().empty() &&
              recovery.GetPath() == previousPath && Snapshot(&recovery.GetScene()) == before &&
              recovery.IsDirty() && recovery.GetLoadWarning() == previousWarning,
          "truly invalid files preserve the current repair document and warning");

    Check(document.ActivateTab(blank), "blank tab activates for successful authored adoption");
    auto adoptedScene = std::make_shared<Scene>();
    const auto adopted = document.AdoptTab(PathUtf8(files.root / "Async.lux"), adoptedScene, false,
                                           "비동기 참조 경고");
    Check(adopted && document.GetLoadWarning(adopted) == "비동기 참조 경고" &&
              document.GetLastError().empty(),
          "asynchronous authored fallback can attach a tab warning");
    document.NewScene();
    Check(document.GetLoadWarning().empty(), "a fresh scene clears its previous load warning");

    const auto cyclic = files.root / "Cyclic.lux";
    Scene cycle;
    auto root = cycle.CreateEntity("Self reference awaiting repair");
    root.AddComponent<SceneReferenceComponent>().path = PathUtf8(cyclic);
    Check(cycle.Serialize(PathUtf8(cyclic)) && !SceneManager().Load(cyclic, error),
          "cycle fixture remains invalid for runtime composition");
    Check(recovery.Open(PathUtf8(cyclic)) && !recovery.GetLoadWarning().empty() && recovery.Save(),
          "cyclic dependencies can be opened and saved as authored repair documents");
}
} // namespace

int RunDocumentChecks(int, char**) {
    try {
        TestTabsAndHistory();
        TestAdoptionAndDuplication();
        TestExternalAliasesAndNativePaths();
        TestStartScene();
        TestAutosavePreservation();
        TestBrokenReferenceRepair();
        std::cout << "씬 탭·저장·복제·시작 씬·자동 저장 검증을 통과했습니다.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "씬 문서 검사 실패: " << error.what() << '\n';
        return 1;
    }
}
LUX_ENGINE_MAIN(RunDocumentChecks)
