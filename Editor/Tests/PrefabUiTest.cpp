#include <LuxEditor/App/EditorWindow.hpp>
#include <LuxEditor/Project/EditorProject.hpp>
#include <LuxEditor/Project/PathEncoding.hpp>
#include <LuxEditor/Scene/SceneDependencies.hpp>
#include <LuxEngine/Scene/PrefabLibrary.hpp>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QListWidget>
#include <QSettings>
#include <QTemporaryDir>
#include <QUndoStack>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
using namespace LuxEngine;
using namespace LuxEditor;
namespace fs = std::filesystem;

void Check(bool value, const std::string& error) {
    if (!value)
        throw std::runtime_error(error);
}

QAction* Action(EditorWindow& window, const char* name) {
    auto* action = window.findChild<QAction*>(QString::fromLatin1(name));
    Check(action != nullptr, std::string("Missing prefab command: ") + name);
    return action;
}

uint32 Local(const Scene& scene, uint32 root, uint32 source) {
    for (const auto& [id, local] : scene.GetComponent<SceneReferenceComponent>(root).entityMap)
        if (id == source)
            return local;
    return 0;
}

void Run(const fs::path& directory) {
    std::string error;
    auto project = EditorProject::Create(directory / PathFromUtf8("프리팹 프로젝트"), "Prefabs",
                                         ProjectTemplate::Empty, error);
    Check(project.has_value(), error);
    const auto root = project->GetFile().parent_path();
    const auto file = root / "Assets" / "Prefabs" / "Tree.luxprefab";
    fs::create_directories(file.parent_path());
    Scene source;
    source.SetSourcePath(GenericPathUtf8(root / "source.lux"));
    const auto authored = source.CreateEntity("Tree").GetId();
    const auto leaf = source.CreateEntity("Leaf").GetId();
    Check(source.SetParent(leaf, authored, false), "Source hierarchy setup failed");
    Check(PrefabLibrary::Save(source, authored, file, error), error);
    QSettings().clear();
    QSettings().setValue(QStringLiteral("lastProject"),
                         QString::fromUtf8(PathUtf8(project->GetFile())));
    EditorWindow window;
    window.show();
    QApplication::processEvents();
    for (const char* name :
         {"savePrefab", "instantiatePrefab", "refreshPrefab", "revertPrefab", "unpackPrefab"})
        Action(window, name);
    auto& document = window.GetDocument();
    auto* browser = window.findChild<QListWidget*>(QStringLiteral("assetGrid"));
    auto* filter = window.findChild<QComboBox*>(QStringLiteral("assetTypeFilter"));
    auto* history = window.findChild<QUndoStack*>();
    Check(browser && filter && history, "Prefab editor controls are unavailable");
    filter->setCurrentIndex(filter->findText(QStringLiteral("장면")));
    const auto matches = browser->findItems(QStringLiteral("Tree.luxprefab"), Qt::MatchContains);
    Check(matches.size() == 1,
          "Prefab must be discoverable through the Content Browser scene filter");
    browser->setCurrentItem(matches.front());
    browser->itemDoubleClicked(matches.front());
    const auto instance = document.GetSelected().GetId();
    Check(instance && document.GetScene().GetEntityCount() == 3 && history->count() == 1,
          "Double-clicking a prefab must instantiate its hierarchy in one undo command");
    history->undo();
    Check(document.GetScene().GetEntityCount() == 0 && fs::exists(file),
          "Undo must remove the placed instance while preserving its source asset");
    history->redo();
    Check(document.GetSelected().GetId() == instance && document.GetScene().GetEntityCount() == 3,
          "Redo must restore the same prefab instance and selection");
    Check(ValidateProjectSceneReferences(*project, document.GetScene(), error), error);
    const auto localLeaf = Local(document.GetScene(), instance, leaf);
    document.GetScene().GetComponent<TagComponent>(localLeaf).tag = "Local leaf";
    document.MarkDirty();
    source.GetComponent<TagComponent>(authored).tag = "Updated tree";
    source.GetComponent<TagComponent>(leaf).tag = "Updated leaf";
    Check(PrefabLibrary::Save(source, authored, file, error), error);
    document.Select(localLeaf);
    Action(window, "refreshPrefab")->trigger();
    Check(document.GetScene().GetComponent<TagComponent>(localLeaf).tag == "Local leaf" &&
              document.GetScene()
                      .GetComponent<TagComponent>(Local(document.GetScene(), instance, authored))
                      .tag == "Updated tree",
          "Refreshing from a selected descendant must preserve local overrides and update other "
          "objects");
    document.Select(localLeaf);
    Action(window, "revertPrefab")->trigger();
    Check(Local(document.GetScene(), instance, leaf) == localLeaf &&
              document.GetScene().GetComponent<TagComponent>(localLeaf).tag == "Updated leaf",
          "Reverting must restore prefab values without renumbering live objects");
    history->undo();
    Check(document.GetScene().GetComponent<TagComponent>(localLeaf).tag == "Local leaf",
          "Undo must restore the overridden prefab value");
    history->redo();
    Action(window, "unpackPrefab")->trigger();
    Check(!document.GetScene().HasComponent<SceneReferenceComponent>(instance) &&
              document.GetScene().IsAlive(localLeaf),
          "Unpack must preserve authored objects");
    history->undo();
    Check(document.GetScene().HasComponent<SceneReferenceComponent>(instance),
          "Undo must restore the prefab source connection");
    const auto external = directory / "Outside.luxprefab";
    Check(PrefabLibrary::Save(source, authored, external, error), error);
    Scene outside;
    outside.SetSourcePath(GenericPathUtf8(project->GetSceneFile()));
    Check(PrefabLibrary::Instantiate(outside, external, error) != 0, error);
    Check(!ValidateProjectSceneReferences(*project, outside, error),
          "Project packaging validation must reject a prefab outside the project");
    for (const auto& tab : document.ListTabs())
        document.SetDirty(tab.id, false);
    window.close();
    QApplication::processEvents();
    QSettings().clear();
}
} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("LuxEnginePrefabTests"));
    QCoreApplication::setApplicationName(QStringLiteral("PrefabUi"));
    try {
        QTemporaryDir temporary;
        Check(temporary.isValid(), "Temporary project creation failed");
        Run(PathFromUtf8(temporary.path().toUtf8().toStdString()));
        std::cout << "Prefab Content Browser, commands, overrides and undo passed\n";
        return 0;
    } catch (const std::exception& failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
