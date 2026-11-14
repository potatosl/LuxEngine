#include <LuxEditor/App/EditorWindow.hpp>
#include <LuxEditor/Modeling/MeshModeling.hpp>
#include <LuxEditor/Project/EditorProject.hpp>
#include <LuxEditor/Project/PathEncoding.hpp>
#include <LuxEditor/Viewport/SceneViewport3D.hpp>
#include <LuxEngine/Assets/AssetManager.hpp>
#include <LuxEngine/Scene/Entity.hpp>
#include <LuxUI/Widgets.hpp>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace {
using namespace LuxEditor;
using namespace LuxEngine;

void Check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}

std::string Bytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    Check(static_cast<bool>(stream), "model file is missing");
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

std::string MeshPath(EditorWindow& window, uint32 id) {
    return window.GetDocument().GetScene().ResolveAssetPath(
        window.GetDocument().GetScene().GetComponent<MeshRendererComponent>(id).meshPath);
}

void Refresh(EditorWindow& window) {
    window.ExecuteRecordedEdit(LUX_UI_TEXT("Refresh modeling selection"), [] {});
}
} // namespace

int main(int argc, char** argv) {
    LuxUI::Application app(argc, argv);
    LuxUI::CoreApplication::setOrganizationName(LUX_UI_TEXT("LuxModelingTests"));
    LuxUI::CoreApplication::setApplicationName(LUX_UI_TEXT("ModelingWorkspace"));
    LuxUI::TemporaryDir files;
    Check(files.isValid(), "modeling workspace test directory is unavailable");
    LuxUI::Settings::setDefaultFormat(LuxUI::Settings::IniFormat);
    LuxUI::Settings::setPath(LuxUI::Settings::IniFormat, LuxUI::Settings::UserScope, files.path());
    LuxUI::Settings().clear();
    const auto directory = PathFromUtf8(files.path().toUtf8().toStdString());
    std::string error;
    const auto project =
        EditorProject::Create(directory / "Project", "Modeling", ProjectTemplate::Empty, error);
    Check(project.has_value(), "modeling fixture project was not created");
    LuxUI::Settings().setValue(LUX_UI_TEXT("lastProject"),
                               LuxUI::String::fromUtf8(PathUtf8(project->GetFile())));
    EditorWindow window;
    window.show();
    app.processEvents();
    auto* show = window.findChild<LuxUI::Action*>(LUX_UI_TEXT("showModelingTools"));
    auto* mode = window.findChild<LuxUI::ComboBox*>(LUX_UI_TEXT("modelingMode"));
    auto* palette = window.findChild<LuxUI::ComboBox*>(LUX_UI_TEXT("modelingPrimitive"));
    auto* add = window.findChild<LuxUI::PushButton*>(LUX_UI_TEXT("modelingAddPrimitive"));
    auto* extrude = window.findChild<LuxUI::PushButton*>(LUX_UI_TEXT("modelingExtrude"));
    auto* merge = window.findChild<LuxUI::PushButton*>(LUX_UI_TEXT("modelingMerge"));
    auto* bake = window.findChild<LuxUI::PushButton*>(LUX_UI_TEXT("modelingBake"));
    auto* history = window.findChild<LuxUI::UndoStack*>();
    auto* view = static_cast<SceneViewport3D*>(
        window.findChild<LuxUI::Widget*>(LUX_UI_TEXT("sceneViewport3D")));
    Check(show && mode && palette && add && extrude && merge && bake && history && view,
          "modeling workspace controls are missing");
    show->trigger();
    palette->setCurrentIndex(palette->findData(LUX_UI_TEXT("shape-cube")));
    Check(palette->currentIndex() >= 0 && add->isEnabled(), "CC0 cube palette is unavailable");
    add->click();
    const auto first = window.GetDocument().GetSelected().GetId();
    const auto original = MeshPath(window, first);
    const auto originalBytes = Bytes(PathFromUtf8(original));
    mode->setCurrentIndex(2);
    Check(view->GetModelingMode() == ModelingEditMode::Face && view->GetModelingMesh(),
          "face mode did not attach an editable cube snapshot");
    const auto initialTriangles = view->GetModelingMesh()->indices.size() / 3;
    ModelingSelection faceSelection;
    faceSelection.faces = CoplanarFaces(*view->GetModelingMesh(), 0);
    faceSelection.vertices = ModelingFaceVertices(*view->GetModelingMesh(), faceSelection.faces);
    Check(faceSelection.faces.size() >= 2, "cube face did not include its planar triangle patch");
    view->SetModelingSelection(faceSelection);
    Refresh(window);
    Check(extrude->isEnabled(), "selected faces did not enable extrusion");
    const int beforeExtrude = history->index();
    extrude->click();
    const auto generated = MeshPath(window, first);
    Check(generated != original && PathFromUtf8(generated).extension() == ".obj" &&
              std::filesystem::is_regular_file(PathFromUtf8(generated)) &&
              view->GetModelingMesh()->indices.size() / 3 > initialTriangles &&
              history->index() == beforeExtrude + 1,
          "extrusion did not produce an editable generated OBJ and one undo operation");
    Check(Bytes(PathFromUtf8(original)) == originalBytes,
          "modeling modified the original licensed FBX");
    Check(view->GetModelingSelection().faces == faceSelection.faces,
          "extrusion lost the cap selection required for repeated extrusion");
    if (const auto* capture = std::getenv("LUX_EDITOR_MODELING_CAPTURE"); capture && *capture) {
        window.resize(1600, 1000);
        app.processEvents();
        view->FrameAll();
        app.processEvents();
        const auto file = PathFromUtf8(capture);
        if (!file.parent_path().empty())
            std::filesystem::create_directories(file.parent_path());
        Check(window.grab().save(LuxUI::String::fromUtf8(capture), "PNG"),
              "modeling workspace visual capture could not be saved");
    }
    history->undo();
    Check(MeshPath(window, first) == original &&
              std::filesystem::is_regular_file(PathFromUtf8(generated)),
          "undo failed to restore the original mesh reference or removed immutable geometry");
    history->redo();
    Check(MeshPath(window, first) == generated, "redo did not reuse the generated geometry");
    const int afterFirstExtrude = history->index();
    extrude->click();
    Check(history->index() == afterFirstExtrude + 1,
          "the retained face selection could not be extruded again");

    mode->setCurrentIndex(0);
    add->click();
    const auto second = window.GetDocument().GetSelected().GetId();
    uint32 child = 0;
    window.ExecuteRecordedEdit(LUX_UI_TEXT("Merge fixture"), [&] {
        auto& scene = window.GetDocument().GetScene();
        scene.GetComponent<TransformComponent>(second).position = {3, 0, 0};
        auto preserved = scene.CreateEntity("Preserved child");
        child = preserved.GetId();
        preserved.GetComponent<TransformComponent>().position = {0, 1, 0};
        Check(scene.SetParent(child, second, false), "merge fixture parenting failed");
        window.GetDocument().SetSelection({first, second}, first);
    });
    const auto childBefore = window.GetDocument().GetScene().GetWorldTransform(child).position;
    const int beforeMerge = history->index();
    merge->click();
    Check(window.GetDocument().GetScene().IsAlive(first) &&
              !window.GetDocument().GetScene().IsAlive(second) &&
              window.GetDocument().GetScene().IsAlive(child) &&
              window.GetDocument().GetScene().GetParent(child) == 0 &&
              history->index() == beforeMerge + 1,
          "mesh merge lost the active object, discarded a child or created multiple undo steps");
    const auto childAfter = window.GetDocument().GetScene().GetWorldTransform(child).position;
    Check(childAfter.x == childBefore.x && childAfter.y == childBefore.y &&
              childAfter.z == childBefore.z,
          "mesh merge changed the preserved child's world position");
    history->undo();
    Check(window.GetDocument().GetScene().IsAlive(second) &&
              window.GetDocument().GetScene().GetParent(child) == second,
          "merge undo did not restore the source object and hierarchy");
    window.ExecuteRecordedEdit(LUX_UI_TEXT("Protected merge fixture"), [&] {
        window.GetDocument().GetScene().AddComponent<ScriptComponent>(second).typeName =
            "Protected";
        window.GetDocument().SetSelection({first, second}, first);
    });
    const auto beforeRejected = window.GetDocument().GetScene().GetEntityCount();
    const int rejectedIndex = history->index();
    bool rejected = false;
    LuxUI::Timer::singleShot(0, [&] {
        for (auto* widget : LuxUI::Application::topLevelWidgets())
            if (auto* dialog = qobject_cast<LuxUI::MessageBox*>(widget)) {
                rejected = dialog->text().contains(LUX_UI_TEXT("정적"));
                dialog->accept();
            }
    });
    merge->click();
    Check(rejected && window.GetDocument().GetScene().GetEntityCount() == beforeRejected &&
              history->index() == rejectedIndex,
          "merge silently discarded an entity's script component");

    uint32 rig = 0;
    window.ExecuteRecordedEdit(LUX_UI_TEXT("Animated bake fixture"), [&] {
        auto& scene = window.GetDocument().GetScene();
        auto parent = scene.CreateEntity("Animated rig");
        rig = parent.GetId();
        parent.GetComponent<TransformComponent>().position = {5, 1, 0};
        auto& animation = parent.AddComponent<AnimationComponent>();
        animation.clip.duration = 1;
        animation.clip.AddKeyframe({0, {5, 1, 0}, {}, {1, 1, 1}});
        animation.clip.AddKeyframe({1, {6, 1, 0}, {}, {1, 1, 1}});
        Check(scene.SetParent(first, rig, false), "animated bake parenting failed");
        window.GetDocument().Select(first);
    });
    mode->setCurrentIndex(1);
    Check(!mode->isEnabled() && bake->isEnabled() && !view->GetModelingMesh(),
          "an animated ancestor did not require an explicit static bake");
    const auto sourcePosition = window.GetDocument().GetScene().GetWorldTransform(first).position;
    const auto beforeBake = window.GetDocument().GetScene().GetEntityCount();
    const int bakeIndex = history->index();
    bake->click();
    const auto copy = window.GetDocument().GetSelected();
    Check(copy.GetId() != first && !window.GetDocument().GetScene().GetParent(copy.GetId()) &&
              !copy.HasComponent<AnimationComponent>() &&
              !copy.HasComponent<ModelAnimationComponent>() &&
              window.GetDocument().GetScene().IsAlive(first) &&
              window.GetDocument().GetScene().IsAlive(rig) &&
              window.GetDocument().GetScene().HasComponent<AnimationComponent>(rig) &&
              window.GetDocument().GetScene().GetEntityCount() == beforeBake + 1 &&
              history->index() == bakeIndex + 1,
          "static bake changed the original animation hierarchy or failed to create an independent "
          "copy");
    Check(copy.GetComponent<TransformComponent>().position.x == sourcePosition.x &&
              copy.GetComponent<TransformComponent>().position.y == sourcePosition.y &&
              mode->isEnabled(),
          "static bake lost its world position or remained uneditable");
    history->undo();
    Check(window.GetDocument().GetScene().GetEntityCount() == beforeBake &&
              window.GetDocument().GetScene().IsAlive(first) &&
              window.GetDocument().GetScene().IsAlive(rig),
          "bake undo changed the preserved source model");
    for (const auto& tab : window.GetDocument().ListTabs())
        window.GetDocument().SetDirty(tab.id, false);
    window.close();
    return 0;
}
