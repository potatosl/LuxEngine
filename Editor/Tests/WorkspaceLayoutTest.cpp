#include <LuxEditor/App/EditorWindow.hpp>
#include <LuxEditor/Project/PathEncoding.hpp>
#include <LuxEditor/Viewport/EngineSceneRenderer.hpp>
#include <LuxEngine/Scene/Entity.hpp>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QImage>
#include <QLineEdit>
#include <QListWidget>
#include <QSettings>
#include <QStackedWidget>
#include <QTabBar>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeWidget>
#include <QUndoStack>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("LuxWorkspaceTests"));
    QCoreApplication::setApplicationName(QStringLiteral("Layout"));
    QTemporaryDir temporary;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    LuxEditor::EngineSceneRenderer::SetForceAllowed(qEnvironmentVariableIsSet("LUX_WORKSPACE_GPU"));
    try {
        std::string error;
        const auto directory =
            LuxEditor::PathFromUtf8(temporary.path().toUtf8().toStdString()) / "Game";
        auto project = LuxEditor::EditorProject::Create(directory, "Workspace",
                                                        LuxEditor::ProjectTemplate::Empty, error);
        Check(project.has_value(), error.c_str());
        QSettings().setValue(QStringLiteral("lastProject"),
                             QString::fromUtf8(LuxEditor::PathUtf8(project->GetFile())));
        LuxEditor::EditorWindow window;
        window.resize(1600, 960);
        window.show();
        app.processEvents();
        auto* workspace = window.findChild<QStackedWidget*>(QStringLiteral("editorWorkspace"));
        auto* tabs = window.findChild<QTabBar*>(QStringLiteral("workspaceTabs"));
        auto* contents = window.findChild<QDockWidget*>(QStringLiteral("lux-dock-1"));
        auto* details = window.findChild<QDockWidget*>(QStringLiteral("lux-dock-2"));
        auto* outliner = window.findChild<QDockWidget*>(QStringLiteral("lux-dock-0"));
        auto* contentButton = window.findChild<QToolButton*>(QStringLiteral("contentDrawerButton"));
        Check(workspace && tabs && contents && details && outliner && contentButton,
              "Workspace structure is incomplete");
        Check(!contents->isVisible() && details->isVisible() && outliner->isVisible(),
              "Default level layout is incorrect");
        Check(workspace->width() > window.width() * .6, "Viewport is crowded by default panels");
        contentButton->click();
        app.processEvents();
        Check(contents->isVisible() &&
                  window.findChild<QTreeWidget*>(QStringLiteral("assetFolders"))
                          ->topLevelItemCount() > 0,
              "Content drawer or folder navigation did not open");
        contentButton->click();
        Check(!contents->isVisible(), "Content drawer did not collapse");
        window.ExecuteRecordedEdit(QStringLiteral("Fixture"), [&] {
            auto entity = window.GetDocument().CreateEntity("Animated Cube");
            entity.AddComponent<LuxEngine::MeshRendererComponent>().material.albedoColor = {
                .32f, .56f, .77f, 1};
            auto& clip = entity.AddComponent<LuxEngine::AnimationComponent>().clip;
            clip.duration = 2;
            clip.AddKeyframe({0, {}, {}, {1, 1, 1}});
            clip.AddKeyframe({1, {3, 1, 0}, {}, {1, 1, 1}});
            clip.AddKeyframe({2, {0, 0, 0}, {}, {1, 1, 1}});
        });
        app.processEvents();
        auto* search = window.findChild<QLineEdit*>(QStringLiteral("detailsSearch"));
        Check(search, "Details search is missing");
        search->setText(QStringLiteral("Transform"));
        int visible = 0;
        for (auto* section : window.findChildren<QWidget*>(QStringLiteral("componentSection")))
            visible += section->isVisible() ? 1 : 0;
        Check(visible == 1, "Details search did not filter component sections");
        search->clear();
        auto* cameraEnabled = window.findChild<QCheckBox*>(QStringLiteral("cameraEnabled"));
        auto* fov = window.findChild<QDoubleSpinBox*>(QStringLiteral("cameraFov"));
        Check(cameraEnabled && fov, "Camera Details are missing");
        cameraEnabled->setChecked(true);
        Check(window.GetDocument().GetSelected().HasComponent<LuxEngine::CameraComponent>(),
              "Camera component was not attached");
        fov->setValue(70);
        Check(window.GetDocument().GetSelected().GetComponent<LuxEngine::CameraComponent>().fov ==
                  70,
              "Camera FOV edit did not reach the scene");
        auto* undo = window.findChild<QUndoStack*>();
        undo->undo();
        Check(window.GetDocument().GetSelected().GetComponent<LuxEngine::CameraComponent>().fov ==
                  45,
              "Camera edit undo failed");
        window.findChild<QAction*>(QStringLiteral("focusSelection"))->trigger();
        app.processEvents();
        const QString capture = qEnvironmentVariable("LUX_WORKSPACE_CAPTURE");
        if (!capture.isEmpty())
            Check(window.grab().save(capture + QStringLiteral("-level.png")),
                  "Level screenshot failed");
        tabs->setCurrentIndex(1);
        app.processEvents();
        Check(workspace->currentIndex() == 1 && !details->isVisible(),
              "Animation did not open as a dedicated workspace");
        if (!capture.isEmpty())
            Check(window.grab().save(capture + QStringLiteral("-animation.png")),
                  "Animation screenshot failed");
        tabs->setCurrentIndex(0);
        app.processEvents();
        Check(details->isVisible() && outliner->isVisible(),
              "Level workspace did not restore its panels");
        window.resize(960, 600);
        app.processEvents();
        for (auto* field : window.findChildren<QDoubleSpinBox*>()) {
            if (field->isVisible())
                Check(field->width() >= 56, "Numeric field was clipped below its minimum size");
        }
        if (!capture.isEmpty())
            Check(window.grab().save(capture + QStringLiteral("-small.png")),
                  "Small layout screenshot failed");
        window.GetDocument().SetDirty(false);
        window.close();
        std::cout << "Level layout, content drawer, Details search, animation workspace and "
                     "compact layout passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
