#include <LuxEditor/App/EditorWindow.hpp>
#include <LuxEditor/Project/EditorProject.hpp>
#include <LuxEditor/Project/PathEncoding.hpp>
#include <LuxEditor/Scene/EditorDocument.hpp>
#include <LuxEditor/Viewport/EngineSceneRenderer.hpp>
#include <LuxEditor/Viewport/GameViewport.hpp>
#include <LuxEngine/Platform/Input.hpp>
#include <LuxEngine/Scene/Entity.hpp>
#include <LuxUI/Widgets.hpp>
#include <fstream>
#include <stdexcept>

namespace {
void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

void Write(const std::filesystem::path& path, const char* text) {
    std::ofstream stream(path, std::ios::binary);
    stream << text;
    Check(static_cast<bool>(stream), "fixture write failed");
}
} // namespace

int main(int argc, char** argv) {
    LuxUI::Application app(argc, argv);
    LuxUI::CoreApplication::setOrganizationName(LUX_UI_TEXT("LuxEditorDevelopmentTests"));
    LuxUI::CoreApplication::setApplicationName(LUX_UI_TEXT("DevelopmentWorkspace"));
    LuxUI::TemporaryDir settings;
    Check(settings.isValid(), "temporary settings directory missing");
    qputenv("XDG_DATA_HOME", settings.path().toUtf8());
    LuxUI::Settings::setDefaultFormat(LuxUI::Settings::IniFormat);
    LuxUI::Settings::setPath(LuxUI::Settings::IniFormat, LuxUI::Settings::UserScope,
                             settings.path());
    const auto directory = LuxEditor::PathFromUtf8(settings.path().toUtf8().toStdString());
    std::string error;
    const auto project = LuxEditor::EditorProject::Create(directory / "Project", "Development",
                                                          LuxEditor::ProjectTemplate::Empty, error);
    Check(project.has_value(), "fixture project create failed");
    LuxUI::Settings().setValue(LUX_UI_TEXT("lastProject"),
                               LuxUI::String::fromUtf8(LuxEditor::PathUtf8(project->GetFile())));
    LuxUI::Settings().setValue(
        LUX_UI_TEXT("lastScene"),
        LuxUI::String::fromUtf8(LuxEditor::PathUtf8(project->GetSceneFile())));
    LuxEditor::EditorWindow window;
    window.show();
    app.processEvents();
    Check(window.findChild<LuxUI::Widget*>(LUX_UI_TEXT("luxConsolePanel")) &&
              window.findChild<LuxUI::Widget*>(LUX_UI_TEXT("luxProfilerPanel")) &&
              window.findChild<LuxUI::CheckBox*>(LUX_UI_TEXT("body3DEnabled")),
          "development panels missing");
    auto* gameView = static_cast<LuxEditor::GameViewport*>(
        window.findChild<LuxUI::Widget*>(LUX_UI_TEXT("gameViewport")));
    Check(gameView && gameView->GetScene() == &window.GetDocument().GetScene() &&
              !gameView->IsInputEnabled(),
          "stopped Game View did not preview the authored scene");
    auto* history = window.findChild<LuxUI::UndoStack*>();
    auto* add2D = window.findChild<LuxUI::Action*>(LUX_UI_TEXT("addCamera2D"));
    auto* add3D = window.findChild<LuxUI::Action*>(LUX_UI_TEXT("addCamera"));
    Check(history && add2D && add3D, "camera creation commands are missing");
    const int cameraHistory = history->index();
    add2D->trigger();
    const auto camera2D = window.GetDocument().GetSelected().GetId();
    Check(!window.GetDocument()
                  .GetSelected()
                  .GetComponent<LuxEngine::CameraComponent>()
                  .isPerspective &&
              window.GetDocument()
                  .GetSelected()
                  .GetComponent<LuxEngine::CameraComponent>()
                  .isPrimary &&
              history->index() == cameraHistory + 1,
          "first 2D camera was not primary or was not one undo operation");
    add3D->trigger();
    const auto camera3D = window.GetDocument().GetSelected().GetId();
    Check(window.GetDocument()
                  .GetSelected()
                  .GetComponent<LuxEngine::CameraComponent>()
                  .isPerspective &&
              !window.GetDocument()
                   .GetSelected()
                   .GetComponent<LuxEngine::CameraComponent>()
                   .isPrimary &&
              history->index() == cameraHistory + 2,
          "adding a 3D camera replaced the existing primary camera");
    auto* primary = window.findChild<LuxUI::CheckBox*>(LUX_UI_TEXT("cameraPrimary"));
    Check(primary && primary->isEnabled(), "primary camera control is unavailable");
    primary->setChecked(true);
    Check(window.GetDocument()
                  .GetScene()
                  .GetComponent<LuxEngine::CameraComponent>(camera3D)
                  .isPrimary &&
              !window.GetDocument()
                   .GetScene()
                   .GetComponent<LuxEngine::CameraComponent>(camera2D)
                   .isPrimary,
          "primary camera selection did not unset the previous camera");
    primary->setChecked(false);
    add2D->trigger();
    Check(window.GetDocument().GetSelected().GetComponent<LuxEngine::CameraComponent>().isPrimary,
          "adding a camera without an active primary did not restore a primary camera");
    while (history->index() > cameraHistory)
        history->undo();
    Check(window.GetDocument()
                  .GetScene()
                  .GetComponentEntityIds<LuxEngine::CameraComponent>()
                  .empty() &&
              gameView->GetScene() == &window.GetDocument().GetScene(),
          "camera undo left stale authoring data in Game View");
    history->clear();
    const auto initialTab = window.GetDocument().GetActiveTabId();
    const auto freshTab = window.NewSceneDocument();
    Check(freshTab != initialTab && gameView->GetScene() == &window.GetDocument().GetScene(),
          "Game View did not follow a new scene");
    add2D->trigger();
    Check(window.SwitchSceneDocument(initialTab, false) &&
              gameView->GetScene() == &window.GetDocument().GetScene() &&
              window.CloseSceneDocument(freshTab, true),
          "Game View did not follow a scene switch or close");
    const auto cameraFile = project->GetFile().parent_path() / "Scenes/CameraPreview.lux";
    LuxEngine::Scene savedCameraScene;
    auto savedCamera = savedCameraScene.CreateEntity("Saved camera");
    savedCamera.AddComponent<LuxEngine::CameraComponent>();
    savedCamera.GetComponent<LuxEngine::TransformComponent>().position = {20, 2, 0};
    Check(savedCameraScene.Serialize(LuxEditor::PathUtf8(cameraFile)) &&
              window.OpenSceneDocument(cameraFile),
          "saved camera fixture did not open");
    const auto loadedTab = window.GetDocument().GetActiveTabId();
    Check(gameView->GetScene() == &window.GetDocument().GetScene() &&
              LuxEditor::ResolveGameViewCamera(*gameView->GetScene(), 640, 320)
                      .camera2D.GetPosition()
                      .x == 20,
          "Game View did not resolve the newly loaded scene camera");
    Check(window.SwitchSceneDocument(initialTab, false) &&
              window.CloseSceneDocument(loadedTab, true),
          "saved camera fixture did not close");
    bool ran = false;
    Check(window.RegisterTool({"test.tool", "Test tool", [&] { ran = true; }, {}}),
          "native tool registration failed");
    auto* command = window.findChild<LuxUI::Action*>(LUX_UI_TEXT("test.tool"));
    Check(command != nullptr, "native tool menu action missing");
    command->trigger();
    Check(ran, "native tool was not executed");

    const auto tools = directory / "tools.json";
    Write(
        tools,
        R"({"version":1,"tools":[{"id":"duplicate","title":"First","action":"create_boxes"},{"id":"duplicate","title":"Second","action":"create_boxes"}]})");
    Check(!window.LoadProjectToolsFromFile(tools, error), "duplicate tools were accepted");
    Check(!window.findChild<LuxUI::Action*>(LUX_UI_TEXT("project.duplicate")),
          "invalid tool batch was partially registered");
    Write(
        tools,
        R"({"version":1,"tools":[{"id":"boxes","title":"Boxes","action":"create_boxes","count":3,"spacing":2}]})");
    Check(window.LoadProjectToolsFromFile(tools, error), "project tools failed to load");
    const auto before = window.GetDocument().GetScene().GetEntityCount();
    window.findChild<LuxUI::Action*>(LUX_UI_TEXT("project.boxes"))->trigger();
    Check(window.GetDocument().GetScene().GetEntityCount() == before + 3,
          "project tool did not create bodies");
    auto* undo = window.findChild<LuxUI::UndoStack*>();
    Check(undo != nullptr, "undo stack missing");
    undo->undo();
    Check(window.GetDocument().GetScene().GetEntityCount() == before,
          "tool batch is not one undo operation");

    const auto source = directory / "source.txt";
    Write(source, "asset fixture");
    Check(window.ImportAssetFiles({source}, error), "asset import failed");
    Check(std::filesystem::is_regular_file(project->GetFile().parent_path() / "Assets/source.txt"),
          "asset was not copied into the project");
    Check(!window.ImportAssetFiles({source}, error), "asset overwrite was accepted");

    LuxEngine::uint32 bodyId = 0;
    window.ExecuteRecordedEdit(LUX_UI_TEXT("3D fixture"), [&] {
        auto body = window.GetDocument().CreateEntity("3D body");
        bodyId = body.GetId();
        body.GetComponent<LuxEngine::TransformComponent>().position = {0, 4, 0};
        auto& rigid = body.AddComponent<LuxEngine::Rigidbody3DComponent>();
        rigid.mass = 3.1234567f;
        auto& collider = body.AddComponent<LuxEngine::BoxCollider3DComponent>();
        collider.offset.z = .1234567f;
        window.GetDocument().Select(bodyId);
    });
    auto* mass = window.findChild<LuxUI::DoubleSpinBox*>(LUX_UI_TEXT("body3DMass"));
    Check(mass && mass->isEnabled(), "3D body fields not available");
    mass->setValue(2.5);
    LuxUI::MetaObject::invokeMethod(mass, "editingFinished");
    Check(window.GetDocument()
                  .GetScene()
                  .GetComponent<LuxEngine::Rigidbody3DComponent>(bodyId)
                  .mass == 2.5f,
          "3D mass edit failed");
    Check(window.GetDocument()
                  .GetScene()
                  .GetComponent<LuxEngine::BoxCollider3DComponent>(bodyId)
                  .offset.z == .1234567f,
          "3D mass edit changed an untouched collider field");

    const auto position = window.GetDocument().GetScene().GetWorldTransform(bodyId).position;
    const bool wasDirty = window.GetDocument().IsDirty();
    const auto action = [&](const char* name) {
        auto* result = window.findChild<LuxUI::Action*>(LuxUI::String::fromLatin1(name));
        Check(result != nullptr, "play action missing");
        result->trigger();
    };
    for (unsigned repeat = 0; repeat < 2; ++repeat) {
        const auto* authored = &window.GetDocument().GetScene();
        const auto lifetime = authored->Lifetime();
        const int undoIndex = undo->index();
        action("previewStep");
        Check(gameView->GetScene() == window.GetRuntimeScene() && gameView->IsInputEnabled(),
              "playing Game View did not switch to the runtime clone");
        Check(window.GetRuntimeScene() && window.GetRuntimeScene() != authored &&
                  window.GetRuntimeScene()->GetWorldTransform(bodyId).position.y < position.y,
              "immediate play did not advance Jolt");
        Check(authored->GetWorldTransform(bodyId).position.y == position.y &&
                  undo->index() == undoIndex,
              "play mutated authoring data or undo history");
        Check(window.GetRuntimeScene()->GetSourcePath() == authored->GetSourcePath(),
              "play clone lost relative asset resolution");
        action("previewPause");
        auto* viewport = window.findChild<LuxUI::Widget*>(LUX_UI_TEXT("gameViewport"));
        Check(viewport != nullptr, "Game View missing");
        viewport->setFocus();
        app.processEvents();
        LuxUI::KeyEvent press(LuxUI::Event::KeyPress, LuxUI::Flags::Key_W,
                              LuxUI::Flags::NoModifier);
        LuxUI::CoreApplication::sendEvent(viewport, &press);
        Check(LuxEngine::Input::IsKeyPressed(LuxEngine::LuxKey::W),
              "embedded play keyboard bridge failed");
        LuxUI::MouseEvent mouse(
            LuxUI::Event::MouseButtonPress, {42, 64}, viewport->mapToGlobal(LuxUI::Point(42, 64)),
            LuxUI::Flags::LeftButton, LuxUI::Flags::LeftButton, LuxUI::Flags::NoModifier);
        LuxUI::CoreApplication::sendEvent(viewport, &mouse);
        Check(LuxEngine::Input::IsMouseButtonPressed(LuxEngine::LuxMouseButton::Left) &&
                  LuxEngine::Input::GetMousePosition() == std::pair<float, float>{42, 64},
              "Game View mouse bridge failed");
        auto* views = window.findChild<LuxUI::TabWidget*>(LUX_UI_TEXT("sceneGameViews"));
        Check(views && views->count() == 3, "Scene and Game surfaces are not separated");
        views->setCurrentIndex(0);
        auto* sceneView = window.findChild<LuxUI::Widget*>(LUX_UI_TEXT("sceneViewport3D"));
        sceneView->setFocus();
        app.processEvents();
        Check(!LuxEngine::Input::IsKeyPressed(LuxEngine::LuxKey::W) &&
                  !LuxEngine::Input::IsMouseButtonPressed(LuxEngine::LuxMouseButton::Left),
              "leaving Game View retained gameplay input");
        LuxUI::KeyEvent editorKey(LuxUI::Event::KeyPress, LuxUI::Flags::Key_W,
                                  LuxUI::Flags::NoModifier);
        LuxUI::CoreApplication::sendEvent(sceneView, &editorKey);
        Check(!LuxEngine::Input::IsKeyPressed(LuxEngine::LuxKey::W),
              "Scene View forwarded authoring input to the game");
        action("previewStop");
        Check(!LuxEngine::Input::IsKeyPressed(LuxEngine::LuxKey::W),
              "stop left embedded input pressed");
        Check(!window.GetRuntimeScene(), "stop retained the runtime clone");
        Check(gameView->GetScene() == &window.GetDocument().GetScene() &&
                  !gameView->IsInputEnabled(),
              "stopped Game View did not return to the current authored scene");
        Check(!lifetime.expired() && &window.GetDocument().GetScene() == authored &&
                  authored->GetWorldTransform(bodyId).position.y == position.y,
              "stop replaced or mutated the authoring scene");
        Check(window.GetDocument().IsDirty() == wasDirty, "stop changed the edit dirty state");
        Check(undo->index() == undoIndex, "stop changed undo history");
    }
    action("previewStep");
    window.ExecuteRecordedEdit(LUX_UI_TEXT("Edit during play"), [&] {
        window.GetDocument()
            .GetScene()
            .GetComponent<LuxEngine::TransformComponent>(bodyId)
            .position.x = .125f;
        window.GetDocument().MarkDirty();
    });
    Check(window.GetRuntimeScene()->GetWorldTransform(bodyId).position.x == position.x,
          "authoring changes leaked into the running scene");
    action("previewStop");
    Check(window.GetDocument().GetScene().GetWorldTransform(bodyId).position.x == .125f,
          "stopping overwrote an authoring edit made during play");
    window.GetDocument().SetDirty(false);
    window.close();
    return 0;
}
