#include <LuxEditor/App/EditorWindow.hpp>
#include <LuxEditor/Project/EditorProject.hpp>
#include <LuxEditor/Project/PathEncoding.hpp>
#include <LuxEngine/Assets/BasicModelLibrary.hpp>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QUndoStack>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
namespace fs = std::filesystem;
using namespace LuxEditor;
using namespace LuxEngine;

void Check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}

QAction* Action(EditorWindow& window, const char* name) {
    auto* action = window.findChild<QAction*>(QString::fromLatin1(name));
    Check(action, "Basic model command is missing from the editor");
    return action;
}

void Close(EditorWindow& window) {
    for (const auto& tab : window.GetDocument().ListTabs())
        window.GetDocument().SetDirty(tab.id, false);
    window.close();
    QApplication::processEvents();
}

void RequiresProject() {
    QSettings().clear();
    EditorWindow window;
    window.show();
    bool notified = false;
    QTimer::singleShot(0, [&] {
        for (auto* widget : QApplication::topLevelWidgets())
            if (auto* message = qobject_cast<QMessageBox*>(widget)) {
                notified = message->text().contains(QStringLiteral("프로젝트"));
                message->accept();
            }
    });
    Action(window, "addBasicModel.shape-cube")->trigger();
    Check(notified && window.GetDocument().GetScene().GetEntityCount() == 0,
          "Basic model placement without a project must explain the missing destination");
    Close(window);
}

void PlacementAndContentBrowser(const fs::path& root) {
    std::string error;
    auto project = EditorProject::Create(root / PathFromUtf8("모델 프로젝트"), "Models",
                                         ProjectTemplate::Empty, error);
    Check(project.has_value(), error.c_str());
    QSettings().clear();
    QSettings().setValue(QStringLiteral("lastProject"),
                         QString::fromUtf8(PathUtf8(project->GetFile())));
    EditorWindow window;
    window.show();
    QApplication::processEvents();
    auto& document = window.GetDocument();
    auto* history = window.findChild<QUndoStack*>();
    auto* inspector = window.findChild<QLineEdit*>(QStringLiteral("meshAssetPath"));
    auto* filter = window.findChild<QComboBox*>(QStringLiteral("assetTypeFilter"));
    auto* browser = window.findChild<QListWidget*>(QStringLiteral("assetGrid"));
    Check(history && inspector && filter && browser, "Model editor controls are unavailable");
    Check(document.GetScene().GetEntityCount() == 0, "Empty project fixture contains objects");
    Action(window, "addBasicModel.shape-cube")->trigger();
    Check(document.GetScene().GetEntityCount() == 1 && history->count() == 1,
          "Basic model creation must produce one object and one undo command");
    const auto id = document.GetSelected().GetId();
    const auto& mesh = document.GetSelected().GetComponent<MeshRendererComponent>();
    const auto model = PathFromUtf8(mesh.meshPath);
    const auto texture = PathFromUtf8(mesh.material.baseColorTexture);
    const auto projectRoot = fs::canonical(project->GetFile().parent_path());
    Check(model.filename() == "shape-cube.fbx" && !model.lexically_relative(projectRoot).empty() &&
              *model.lexically_relative(projectRoot).begin() != ".." &&
              fs::is_regular_file(model) && fs::is_regular_file(texture) &&
              fs::is_regular_file(model.parent_path() / "License.txt") &&
              fs::is_regular_file(model.parent_path() / "Provenance.json") &&
              inspector->text() == QString::fromUtf8(mesh.meshPath),
          "Basic model placement must use project-owned FBX, palette texture and license data");
    history->undo();
    Check(document.GetScene().GetEntityCount() == 0 && fs::is_regular_file(model),
          "Undo must remove the placed object while preserving reusable project assets");
    history->redo();
    Check(document.GetSelected().GetId() == id &&
              document.GetSelected().GetComponent<MeshRendererComponent>().meshPath ==
                  PathUtf8(model),
          "Redo must restore the same model reference and selection");
    Action(window, "addBasicModel.shape-cylinder")->trigger();
    Check(document.GetScene().GetEntityCount() == 2 &&
              PathFromUtf8(document.GetSelected().GetComponent<MeshRendererComponent>().meshPath)
                      .parent_path() == model.parent_path(),
          "Additional basic models must reuse the installed package");
    filter->setCurrentIndex(filter->findText(QStringLiteral("모델")));
    Check(browser->count() == 10, "Content Browser model filter must list the ten FBX models");
    const auto slopes = browser->findItems(QStringLiteral("shape-slope.fbx"), Qt::MatchContains);
    Check(slopes.size() == 1, "Packaged FBX was not discoverable in the Content Browser");
    browser->setCurrentItem(slopes.front());
    browser->itemDoubleClicked(slopes.front());
    Check(document.GetScene().GetEntityCount() == 3 &&
              PathFromUtf8(document.GetSelected().GetComponent<MeshRendererComponent>().meshPath)
                      .filename() == "shape-slope.fbx" &&
              !document.GetSelected()
                   .GetComponent<MeshRendererComponent>()
                   .material.baseColorTexture.empty(),
          "Double-clicking an installed FBX must place it with its verified palette texture");
    const auto obj = model.parent_path() / "custom.OBJ";
    std::ofstream(obj) << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    filter->setCurrentIndex(0);
    filter->setCurrentIndex(filter->findText(QStringLiteral("모델")));
    const auto objects = browser->findItems(QStringLiteral("custom.OBJ"), Qt::MatchContains);
    Check(objects.size() == 1 && browser->count() == 11,
          "Model filtering must include case-insensitive existing OBJ assets");
    browser->setCurrentItem(objects.front());
    browser->itemDoubleClicked(objects.front());
    Check(document.GetScene().GetEntityCount() == 4 &&
              document.GetSelected().GetComponent<MeshRendererComponent>().meshPath ==
                  PathUtf8(obj) &&
              document.GetSelected()
                  .GetComponent<MeshRendererComponent>()
                  .material.baseColorTexture.empty(),
          "Generic OBJ placement must not inherit a catalog material by accident");
    const auto sceneFile = projectRoot / "Scenes" / "Placed.lux";
    fs::create_directories(sceneFile.parent_path());
    Check(document.SaveAs(PathUtf8(sceneFile)), "Placed model scene did not save");
    Scene reopened;
    Check(reopened.Deserialize(PathUtf8(sceneFile)) && reopened.GetEntityCount() == 4,
          "Saved model scene did not reopen");
    const auto& reopenedMesh = reopened.GetComponent<MeshRendererComponent>(id);
    Check(fs::equivalent(PathFromUtf8(reopened.ResolveAssetPath(reopenedMesh.meshPath)), model) &&
              fs::equivalent(
                  PathFromUtf8(reopened.ResolveAssetPath(reopenedMesh.material.baseColorTexture)),
                  texture),
          "Saving and reopening must preserve project-relative FBX and palette references");
    Close(window);
}
} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("LuxModelTests"));
    QCoreApplication::setApplicationName(QStringLiteral("BasicModelUi"));
    QTemporaryDir settings;
    QTemporaryDir files;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    try {
        Check(settings.isValid() && files.isValid(),
              "Temporary model test directories are invalid");
        RequiresProject();
        PlacementAndContentBrowser(PathFromUtf8(files.path().toUtf8().toStdString()));
        std::cout
            << "Basic model menu, content placement, material, undo and scene persistence passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
