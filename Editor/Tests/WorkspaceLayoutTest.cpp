#include <LuxEditor/App/EditorWindow.hpp>
#include <LuxEditor/Project/PathEncoding.hpp>
#include <LuxEditor/Viewport/EngineSceneRenderer.hpp>
#include <LuxEngine/Scene/Entity.hpp>
#include <LuxUI/Widgets.hpp>
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
    LuxUI::Application app(argc, argv);
    LuxUI::CoreApplication::setOrganizationName(LUX_UI_TEXT("LuxWorkspaceTests"));
    LuxUI::CoreApplication::setApplicationName(LUX_UI_TEXT("Layout"));
    LuxUI::TemporaryDir temporary;
    LuxUI::Settings::setDefaultFormat(LuxUI::Settings::IniFormat);
    LuxUI::Settings::setPath(LuxUI::Settings::IniFormat, LuxUI::Settings::UserScope,
                             temporary.path());
    LuxEditor::EngineSceneRenderer::SetForceAllowed(qEnvironmentVariableIsSet("LUX_WORKSPACE_GPU"));
    try {
        std::string error;
        const auto directory =
            LuxEditor::PathFromUtf8(temporary.path().toUtf8().toStdString()) / "Game";
        auto project = LuxEditor::EditorProject::Create(directory, "Workspace",
                                                        LuxEditor::ProjectTemplate::Empty, error);
        Check(project.has_value(), error.c_str());
        LuxUI::Settings().setValue(
            LUX_UI_TEXT("lastProject"),
            LuxUI::String::fromUtf8(LuxEditor::PathUtf8(project->GetFile())));
        LuxEditor::EditorWindow window;
        window.resize(1600, 960);
        window.show();
        app.processEvents();
        auto* workspace = window.findChild<LuxUI::StackedWidget*>(LUX_UI_TEXT("editorWorkspace"));
        auto* tabs = window.findChild<LuxUI::TabBar*>(LUX_UI_TEXT("workspaceTabs"));
        auto* contents = window.findChild<LuxUI::DockWidget*>(LUX_UI_TEXT("lux-dock-1"));
        auto* details = window.findChild<LuxUI::DockWidget*>(LUX_UI_TEXT("lux-dock-2"));
        auto* outliner = window.findChild<LuxUI::DockWidget*>(LUX_UI_TEXT("lux-dock-0"));
        auto* contentButton =
            window.findChild<LuxUI::ToolButton*>(LUX_UI_TEXT("contentDrawerButton"));
        Check(workspace && tabs && contents && details && outliner && contentButton,
              "Workspace structure is incomplete");
        Check(!contents->isVisible() && details->isVisible() && outliner->isVisible(),
              "Default level layout is incorrect");
        Check(workspace->width() > window.width() * .6, "Viewport is crowded by default panels");
        contentButton->click();
        app.processEvents();
        Check(contents->isVisible() &&
                  window.findChild<LuxUI::TreeWidget*>(LUX_UI_TEXT("assetFolders"))
                          ->topLevelItemCount() > 0,
              "Content drawer or folder navigation did not open");
        contentButton->click();
        Check(!contents->isVisible(), "Content drawer did not collapse");
        window.ExecuteRecordedEdit(LUX_UI_TEXT("Fixture"), [&] {
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
        auto* search = window.findChild<LuxUI::LineEdit*>(LUX_UI_TEXT("detailsSearch"));
        Check(search, "Details search is missing");
        search->setText(LUX_UI_TEXT("Transform"));
        int visible = 0;
        for (auto* section : window.findChildren<LuxUI::Widget*>(LUX_UI_TEXT("componentSection")))
            visible += section->isVisible() ? 1 : 0;
        Check(visible == 1, "Details search did not filter component sections");
        search->clear();
        auto* cameraEnabled = window.findChild<LuxUI::CheckBox*>(LUX_UI_TEXT("cameraEnabled"));
        auto* fov = window.findChild<LuxUI::DoubleSpinBox*>(LUX_UI_TEXT("cameraFov"));
        Check(cameraEnabled && fov, "Camera Details are missing");
        cameraEnabled->setChecked(true);
        Check(window.GetDocument().GetSelected().HasComponent<LuxEngine::CameraComponent>(),
              "Camera component was not attached");
        fov->setValue(70);
        Check(window.GetDocument().GetSelected().GetComponent<LuxEngine::CameraComponent>().fov ==
                  70,
              "Camera FOV edit did not reach the scene");
        auto* undo = window.findChild<LuxUI::UndoStack*>();
        undo->undo();
        Check(window.GetDocument().GetSelected().GetComponent<LuxEngine::CameraComponent>().fov ==
                  45,
              "Camera edit undo failed");
        window.findChild<LuxUI::Action*>(LUX_UI_TEXT("focusSelection"))->trigger();
        app.processEvents();
        const LuxUI::String capture = qEnvironmentVariable("LUX_WORKSPACE_CAPTURE");
        if (!capture.isEmpty())
            Check(window.grab().save(capture + LUX_UI_TEXT("-level.png")),
                  "Level screenshot failed");
        tabs->setCurrentIndex(1);
        app.processEvents();
        Check(workspace->currentIndex() == 1 && !details->isVisible(),
              "Animation did not open as a dedicated workspace");
        if (!capture.isEmpty())
            Check(window.grab().save(capture + LUX_UI_TEXT("-animation.png")),
                  "Animation screenshot failed");
        tabs->setCurrentIndex(0);
        app.processEvents();
        Check(details->isVisible() && outliner->isVisible(),
              "Level workspace did not restore its panels");
        window.resize(960, 600);
        app.processEvents();
        for (auto* field : window.findChildren<LuxUI::DoubleSpinBox*>()) {
            if (field->isVisible())
                Check(field->width() >= 56, "Numeric field was clipped below its minimum size");
        }
        if (!capture.isEmpty())
            Check(window.grab().save(capture + LUX_UI_TEXT("-small.png")),
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
