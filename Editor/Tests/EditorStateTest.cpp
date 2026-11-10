#include "App/EditorWindow.hpp"
#include "Engine/Scene/Scene.hpp"
#include "Project/PathEncoding.hpp"
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QListWidget>
#include <QMessageBox>
#include <QMimeData>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QUndoStack>
#include <QUrl>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
namespace fs = std::filesystem;
using namespace LuxEditor;
void Check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
QString Q(const fs::path& path) {
    return QString::fromUtf8(PathUtf8(path));
}
struct ProjectFixture {
    QTemporaryDir directory;
    EditorProject project;
    fs::path secondary;
    fs::path external;
    ProjectFixture() {
        Check(directory.isValid(), "temporary project directory exists");
        const auto root = PathFromUtf8(directory.path().toStdString());
        std::string error;
        auto created =
            EditorProject::Create(root / "Project", "Audit", ProjectTemplate::Empty, error);
        Check(created.has_value(), error.c_str());
        project = std::move(*created);
        secondary = project.GetSceneFile().parent_path() / "Secondary.lux";
        external = root / "Outside.lux";
        LuxEngine::Scene scene;
        scene.CreateEntity("Secondary");
        Check(scene.Serialize(PathUtf8(secondary)) && scene.Serialize(PathUtf8(external)),
              "secondary and external scene fixtures save");
    }
    std::unique_ptr<EditorWindow> Open(QApplication& app) {
        QSettings().clear();
        QSettings().setValue("lastProject", Q(project.GetFile()));
        auto window = std::make_unique<EditorWindow>();
        window->show();
        app.processEvents();
        return window;
    }
};
void OpenViaMenu(EditorWindow& window, const fs::path& scene) {
    QTimer chooser;
    QObject::connect(&chooser, &QTimer::timeout, &window, [&] {
        if (auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
            dialog->selectFile(Q(scene));
            QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
        }
    });
    chooser.start(5);
    auto* action = window.findChild<QAction*>("openScene");
    Check(action, "scene open action exists");
    action->trigger();
}
void DropScene(EditorWindow& window, const fs::path& path) {
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(Q(path))});
    QDragEnterEvent enter({50, 50}, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&window, &enter);
    Check(enter.isAccepted(), "scene drag enters the editor");
    QDropEvent drop({50, 50}, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&window, &drop);
}
void TestSceneContext(QApplication& app) {
    for (const int route : {0, 1, 2}) {
        ProjectFixture fixture;
        auto window = fixture.Open(app);
        auto* settings = window->findChild<QAction*>("projectSettings");
        Check(settings && settings->isEnabled(), "project starts connected");
        if (route == 0)
            OpenViaMenu(*window, fixture.secondary);
        else if (route == 1) {
            auto* assets = window->findChild<QListWidget*>("assetGrid");
            Check(assets, "asset browser exists");
            const auto items = assets->findItems("Scenes/Secondary.lux", Qt::MatchExactly);
            Check(items.size() == 1, "secondary scene appears in asset browser");
            assets->setCurrentItem(items.front());
            assets->itemDoubleClicked(items.front());
        } else
            DropScene(*window, fixture.secondary);
        Check(PathFromUtf8(window->GetDocument().GetPath()) == fixture.secondary &&
                  settings->isEnabled(),
              "all scene open routes retain the owning project");
        // Failed opens keep the current document and project connection.
        QTimer dismiss;
        QObject::connect(&dismiss, &QTimer::timeout, &app, [] {
            if (auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
                message->accept();
        });
        dismiss.start(5);
        DropScene(*window, fixture.secondary.parent_path() / "Missing.lux");
        Check(settings->isEnabled() &&
                  PathFromUtf8(window->GetDocument().GetPath()) == fixture.secondary,
              "failed scene open leaves the current document and project intact");
        if (route == 2)
            DropScene(*window, fixture.external);
        else
            OpenViaMenu(*window, fixture.external);
        Check(PathFromUtf8(window->GetDocument().GetPath()) == fixture.external &&
                  !settings->isEnabled(),
              "external scenes detach from the previous project consistently");
    }
}
} // namespace
int main(int argc, char** argv) {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("LuxEngineTests");
    QCoreApplication::setApplicationName("LuxEditorStateTests");
    QTemporaryDir settings;
    if (!settings.isValid())
        return 1;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    try {
        const std::string mode = argc > 1 ? argv[1] : "";
        if (mode == "scene-context")
            TestSceneContext(app);
        else
            throw std::runtime_error("Unknown editor state test mode");
        std::cout << "Editor state checks passed: " << mode << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Editor state checks failed: " << error.what() << '\n';
        return 1;
    }
}
