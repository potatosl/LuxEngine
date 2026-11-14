#include <LuxEditor/Scene/EditorDocument.hpp>
#include <LuxEditor/Viewport/EngineSceneRenderer.hpp>
#include <LuxEditor/Viewport/ModelingViewport.hpp>
#include <LuxEditor/Viewport/SceneViewport3D.hpp>
#include <LuxUI/Widgets.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
using namespace LuxEditor;
using namespace LuxEngine;

void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

Vec3 Point(const Mat4& matrix, Vec3 point) {
    const auto* m = matrix.elements;
    return {m[0] * point.x + m[4] * point.y + m[8] * point.z + m[12],
            m[1] * point.x + m[5] * point.y + m[9] * point.z + m[13],
            m[2] * point.x + m[6] * point.y + m[10] * point.z + m[14]};
}

bool Near(Vec3 a, Vec3 b) {
    const auto delta = a - b;
    return delta.Dot(delta) < 1e-7f;
}

ModelingMesh Plane() {
    ModelingMesh mesh;
    mesh.vertices = {{{-1, -1, 0}, {0, 0, 1}, {0, 0}}, {{1, -1, 0}, {0, 0, 1}, {1, 0}},
                     {{1, 1, 0}, {0, 0, 1}, {1, 1}},   {{-1, -1, 0}, {0, 0, 1}, {0, 0}},
                     {{1, 1, 0}, {0, 0, 1}, {1, 1}},   {{-1, 1, 0}, {0, 0, 1}, {0, 1}}};
    mesh.indices = {0, 1, 2, 3, 4, 5};
    mesh.hasTexCoords = true;
    return mesh;
}

void Geometry() {
    const auto mesh = Plane();
    const auto camera = MakeViewportCamera3D({640, 480}, {}, 0, 0, 8);
    const auto screen = ProjectViewportPoint(camera, {640, 480}, mesh.vertices[0].position);
    Check(screen.has_value(), "Modeling fixture vertex was clipped");
    Check(PickModelingVertex(mesh, {}, camera, {640, 480}, *screen) == 0,
          "Modeling vertex picking did not select the nearest projected vertex");
    const auto ray = MakeViewportRay3D(camera, {640, 480}, {320, 240});
    Check(ray.has_value(), "Modeling fixture ray could not be constructed");
    const auto face = PickModelingFace(mesh, {}, *ray);
    Check(face && CoplanarFaces(mesh, *face).size() == 2,
          "Modeling face picking did not select the full connected planar face");
    ModelingSelection selection;
    selection.faces = {*face};
    selection.vertices = ModelingFaceVertices(mesh, selection.faces);
    Check(selection.vertices.size() >= 4,
          "A selected face did not include seam-coincident vertices");
    selection.vertices = CoincidentVertices(mesh, 0);
    Check(selection.vertices == std::vector<uint32>({0, 3}),
          "Coincident modeling vertex selection lost the UV seam");
    const auto world = Mat4::Translate({3, 2, -1}) * Mat4::RotateY(.3f) * Mat4::Scale({2, 3, 1}) *
                       Mat4::RotateZ(.4f);
    ModelingMesh preview;
    std::string error;
    const Vec3 delta{.7f, -.3f, .2f};
    Check(PreviewModelingTransform(mesh, preview, selection, world, Mat4::Translate(delta), error),
          error.c_str());
    for (size_t id = 0; id < mesh.vertices.size(); ++id) {
        const bool selected = std::find(selection.vertices.begin(), selection.vertices.end(), id) !=
                              selection.vertices.end();
        Check(
            Near(Point(world, preview.vertices[id].position),
                 Point(world, mesh.vertices[id].position) + (selected ? delta : Vec3{})),
            "Modeling delta did not respect parent shear/nonuniform scale or unselected vertices");
    }
    Check(PreviewModelingTransform(mesh, preview, selection, world, Mat4::Translate(delta * 2),
                                   error) &&
              Near(Point(world, preview.vertices[0].position),
                   Point(world, mesh.vertices[0].position) + delta * 2),
          "Modeling previews accumulated previous deltas instead of using the captured mesh");
    const auto checkpoint = preview.vertices[0].position;
    Check(!PreviewModelingTransform(mesh, preview, selection, Mat4::Scale({0, 1, 1}), {}, error) &&
              !error.empty() && Near(preview.vertices[0].position, checkpoint),
          "Invalid modeling inverse partially changed a valid preview");
}

void Mouse(LuxUI::Widget& view, LuxUI::Event::Type type, LuxUI::PointF point,
           LuxUI::Flags::MouseButton button, LuxUI::Flags::MouseButtons buttons,
           LuxUI::Flags::KeyboardModifiers modifiers = LuxUI::Flags::NoModifier) {
    LuxUI::MouseEvent event(type, point, view.mapToGlobal(point.toPoint()), button, buttons,
                            modifiers);
    LuxUI::Application::sendEvent(&view, &event);
}

void Click(LuxUI::Widget& view, LuxUI::PointF point,
           LuxUI::Flags::KeyboardModifiers modifiers = LuxUI::Flags::NoModifier) {
    Mouse(view, LuxUI::Event::MouseButtonPress, point, LuxUI::Flags::LeftButton,
          LuxUI::Flags::LeftButton, modifiers);
    Mouse(view, LuxUI::Event::MouseButtonRelease, point, LuxUI::Flags::LeftButton,
          LuxUI::Flags::NoButton, modifiers);
}

void Gesture(SceneViewport3D& view, LuxUI::PointF start, LuxUI::PointF delta, bool release = true) {
    Mouse(view, LuxUI::Event::MouseButtonPress, start, LuxUI::Flags::LeftButton,
          LuxUI::Flags::LeftButton);
    Mouse(view, LuxUI::Event::MouseMove, start + delta * .5, LuxUI::Flags::NoButton,
          LuxUI::Flags::LeftButton);
    Mouse(view, LuxUI::Event::MouseMove, start + delta, LuxUI::Flags::NoButton,
          LuxUI::Flags::LeftButton);
    if (release)
        Mouse(view, LuxUI::Event::MouseButtonRelease, start + delta, LuxUI::Flags::LeftButton,
              LuxUI::Flags::NoButton);
}

void Interaction() {
    EditorDocument document;
    auto entity = document.GetScene().CreateEntity("Editable plane");
    entity.AddComponent<MeshRendererComponent>();
    document.Select(entity.GetId());
    int objectCallbacks = 0, commits = 0, selections = 0;
    SceneViewport3D view(
        nullptr, document, [&](std::optional<uint32>) { ++objectCallbacks; },
        [&](uint32, const TransformComponent&) { ++objectCallbacks; }, [&] { ++objectCallbacks; },
        [&] { ++objectCallbacks; });
    view.SetCancelEditCallback([&] { ++objectCallbacks; });
    view.SetModelingCommitCallback(
        [&](const ModelingMesh& mesh, const ModelingSelection& selected) {
            ++commits;
            Check(!selected.vertices.empty() && mesh.indices.size() == 6,
                  "Modeling commit lost topology or selection");
        });
    view.SetModelingSelectionCallback([&](const ModelingSelection&) { ++selections; });
    view.resize(640, 480);
    view.SetViewPreset(SceneViewport3D::ViewPreset::Front);
    const auto screen = [&](Vec3 point) {
        const auto projected = ProjectViewportPoint(view.GetViewCamera(), view.size(), point);
        Check(projected.has_value(), "Modeling interaction fixture was clipped");
        return *projected;
    };
    const auto mesh = Plane();
    view.SetModelingMesh(entity.GetId(), mesh);
    view.SetModelingMode(ModelingEditMode::Vertex);
    Click(view, screen(mesh.vertices[0].position));
    Check(view.GetModelingSelection().vertices == std::vector<uint32>({0, 3}) && commits == 0,
          "Vertex click did not select coincident vertices or committed an unchanged mesh");
    view.SetTool(EditorTool::Move);
    Gesture(view, screen(mesh.vertices[0].position), {24, -12});
    Check(commits == 1 && objectCallbacks == 0 &&
              !Near(view.GetModelingMesh()->vertices[0].position, mesh.vertices[0].position) &&
              Near(view.GetModelingMesh()->vertices[0].position,
                   view.GetModelingMesh()->vertices[3].position) &&
              Near(entity.GetComponent<TransformComponent>().position, {}),
          "Vertex gizmo did not produce one mesh commit while preserving the object transform");
    const auto checkpoint = *view.GetModelingMesh();
    Gesture(view, screen(checkpoint.vertices[0].position), {28, 0}, false);
    LuxUI::KeyEvent escape(LuxUI::Event::KeyPress, LuxUI::Flags::Key_Escape,
                           LuxUI::Flags::NoModifier);
    LuxUI::Application::sendEvent(&view, &escape);
    Check(commits == 1 && objectCallbacks == 0 &&
              Near(view.GetModelingMesh()->vertices[0].position, checkpoint.vertices[0].position),
          "Cancelling vertex modeling changed the scene or failed to restore the captured mesh");
    view.SetModelingMesh(entity.GetId(), mesh);
    view.SetModelingMode(ModelingEditMode::Face);
    Click(view, screen({.2f, .1f, 0}));
    Check(view.GetModelingSelection().faces.size() == 2 &&
              view.GetModelingSelection().vertices.size() == 6,
          "Face click selected a single triangle instead of the connected plane patch");
    Click(view, screen({.2f, .1f, 0}), LuxUI::Flags::ControlModifier);
    Check(view.GetModelingSelection().faces.empty() && view.GetModelingSelection().vertices.empty(),
          "Ctrl face selection did not toggle the entire coplanar patch");
    view.SetModelingSelection({{}, {0, 1}});
    view.SetTool(EditorTool::Rotate);
    Gesture(view, screen({}), {18, 0});
    Check(commits == 2 &&
              !Near(view.GetModelingMesh()->vertices[0].position, mesh.vertices[0].position),
          "Face rotation gizmo did not transform the selected vertex set");
    const auto rotated = *view.GetModelingMesh();
    view.SetTool(EditorTool::Scale);
    Gesture(view, screen({}), {18, 0});
    const auto beforeEdge = rotated.vertices[1].position - rotated.vertices[0].position;
    const auto afterEdge =
        view.GetModelingMesh()->vertices[1].position - view.GetModelingMesh()->vertices[0].position;
    Check(commits == 3 && afterEdge.Dot(afterEdge) > beforeEdge.Dot(beforeEdge) &&
              objectCallbacks == 0 && selections >= 5,
          "Face scaling, selection callbacks or modeling gesture isolation failed");
    view.SetTool(EditorTool::Move);
    Gesture(view, screen({}), {18, 0}, false);
    document.NewTab();
    Mouse(view, LuxUI::Event::MouseButtonRelease, screen({}), LuxUI::Flags::LeftButton,
          LuxUI::Flags::NoButton);
    Check(commits == 3 && objectCallbacks == 0,
          "Changing scene tabs committed a modeling gesture into the wrong document");
    view.ClearModelingMesh();
}

void NativeKey(LuxUI::Widget& view, LuxUI::KeySequence::StandardKey standard,
               LuxUI::Event::Type type = LuxUI::Event::KeyPress, bool claimed = false) {
    const auto bindings = LuxUI::KeySequence::keyBindings(standard);
    Check(!bindings.empty() && bindings.front().count() == 1,
          "Native modeling shortcut binding is unavailable");
    const auto combination = bindings.front()[0];
    LuxUI::KeyEvent event(type, combination.key(), combination.keyboardModifiers());
    event.ignore();
    LuxUI::Application::sendEvent(&view, &event);
    if (type == LuxUI::Event::ShortcutOverride)
        Check(event.isAccepted() == claimed,
              "Modeling shortcut override did not claim only sub-element editing commands");
}

void KeyboardRouting() {
    EditorDocument document;
    const auto entity = document.GetScene().CreateEntity("Keyboard editing target");
    document.GetScene().AddComponent<MeshRendererComponent>(entity.GetId());
    document.Select(entity.GetId());
    LuxUI::MainWindow window;
    auto* view = new SceneViewport3D(
        &window, document, [](std::optional<uint32>) {}, [](uint32, const TransformComponent&) {},
        [] {}, [] {});
    window.setCentralWidget(view);
    window.resize(640, 480);
    int selectedObjects = 0, deletedObjects = 0, cutObjects = 0;
    const auto action = [&](LuxUI::KeySequence::StandardKey shortcut, int& invocations) {
        auto* command = new LuxUI::Action(&window);
        command->setShortcut(LuxUI::KeySequence(shortcut));
        window.addAction(command);
        LuxUI::Object::connect(command, &LuxUI::Action::triggered, &window,
                               [&invocations] { ++invocations; });
    };
    action(LuxUI::KeySequence::SelectAll, selectedObjects);
    action(LuxUI::KeySequence::Delete, deletedObjects);
    action(LuxUI::KeySequence::Cut, cutObjects);
    window.show();
    window.activateWindow();
    view->setFocus();
    LuxUI::Application::processEvents();
    NativeKey(*view, LuxUI::KeySequence::SelectAll);
    Check(selectedObjects == 1, "Object mode lost native main-window Select All routing");
    view->SetModelingMesh(entity.GetId(), Plane());
    view->SetModelingMode(ModelingEditMode::Vertex);
    for (const auto standard :
         {LuxUI::KeySequence::SelectAll, LuxUI::KeySequence::Delete, LuxUI::KeySequence::Cut})
        NativeKey(*view, standard, LuxUI::Event::ShortcutOverride, true);
    NativeKey(*view, LuxUI::KeySequence::SelectAll);
    Check(selectedObjects == 1 && view->GetModelingSelection().vertices.size() == 6 &&
              view->GetModelingSelection().faces.empty(),
          "Native Select All reached the object action instead of selecting all mesh vertices");
    view->SetModelingMode(ModelingEditMode::Face);
    NativeKey(*view, LuxUI::KeySequence::SelectAll);
    Check(selectedObjects == 1 && view->GetModelingSelection().faces.size() == 2 &&
              view->GetModelingSelection().vertices.size() == 6,
          "Native Select All did not select every face in face mode");
    NativeKey(*view, LuxUI::KeySequence::Delete);
    NativeKey(*view, LuxUI::KeySequence::Cut);
    Check(deletedObjects == 0 && cutObjects == 0 && document.GetScene().IsAlive(entity.GetId()) &&
              view->toolTip().contains(LUX_UI_TEXT("개발 중")),
          "Unimplemented modeling Delete/Cut reached destructive object actions or gave no notice");
    LuxUI::KeyEvent backspace(LuxUI::Event::KeyPress, LuxUI::Flags::Key_Backspace,
                              LuxUI::Flags::NoModifier);
    backspace.ignore();
    LuxUI::Application::sendEvent(view, &backspace);
    Check(backspace.isAccepted() && deletedObjects == 0,
          "Modeling Backspace leaked to object deletion");
    view->SetModelingMode(ModelingEditMode::Object);
    NativeKey(*view, LuxUI::KeySequence::SelectAll, LuxUI::Event::ShortcutOverride, false);
    NativeKey(*view, LuxUI::KeySequence::Delete);
    NativeKey(*view, LuxUI::KeySequence::Cut);
    Check(deletedObjects == 1 && cutObjects == 1,
          "Returning to Object mode did not restore native Delete/Cut action routing");
    window.close();
    LuxUI::Application::processEvents();
}

double ImageDifference(const LuxUI::Image& first, const LuxUI::Image& second,
                       LuxUI::PointF center) {
    Check(first.size() == second.size() && !first.isNull(), "GPU modeling captures differ in size");
    double difference = 0;
    for (int y = int(center.y()) - 6; y < int(center.y()) + 6; ++y)
        for (int x = int(center.x()) - 6; x < int(center.x()) + 6; ++x) {
            Check(first.rect().contains(x, y), "GPU modeling comparison patch was clipped");
            const auto a = first.pixelColor(x, y), b = second.pixelColor(x, y);
            difference += std::abs(a.red() - b.red()) + std::abs(a.green() - b.green()) +
                          std::abs(a.blue() - b.blue());
        }
    return difference / (144 * 3);
}

int GpuPreview() {
    EngineSceneRenderer::SetForceAllowed(true);
    EngineSceneRenderer renderer;
    if (!renderer.EnsureReady()) {
        std::cout << "SKIP: modeling GPU preview is unavailable: "
                  << renderer.LastError().toStdString() << '\n';
        return 77;
    }
    Scene scene;
    auto entity = scene.CreateEntity("GPU editable mesh");
    entity.AddComponent<MeshRendererComponent>().material.albedoColor = {.8f, .5f, .2f, 1};
    LuxUI::Image baseline, preview, restored;
    Check(renderer.Render3D(scene, 640, 480, {}, 0, 0, 8, baseline),
          "GPU modeling baseline render failed");
    auto mesh = Plane();
    for (auto& vertex : mesh.vertices)
        vertex.position.x += 2;
    Check(renderer.SetMeshPreview(entity.GetId(), mesh.vertices, mesh.indices),
          "GPU modeling preview upload was rejected");
    Check(renderer.Render3D(scene, 640, 480, {}, 0, 0, 8, preview),
          "GPU modeling preview render failed");
    const auto center =
        ProjectViewportPoint(MakeViewportCamera3D({640, 480}, {}, 0, 0, 8), {640, 480}, {2, 0, 0});
    Check(center && ImageDifference(baseline, preview, *center) > 15,
          "Modeling preview did not replace the actual GPU surface geometry");
    renderer.ClearMeshPreview();
    Check(renderer.Render3D(scene, 640, 480, {}, 0, 0, 8, restored) &&
              ImageDifference(baseline, restored, {320, 240}) < 2 &&
              ImageDifference(baseline, restored, *center) < 2,
          "Cancelling GPU modeling preview did not restore the source geometry");
    Check(entity.GetComponent<MeshRendererComponent>().meshPath.empty() &&
              Near(entity.GetComponent<TransformComponent>().position, {}),
          "GPU modeling preview modified the source scene or asset path");
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    const bool gpu = argc == 2 && std::string_view(argv[1]) == "--gpu";
    if (gpu)
        qunsetenv("LUX_EDITOR_SOFTWARE_VIEWPORT");
    else
        qputenv("LUX_EDITOR_SOFTWARE_VIEWPORT", "1");
    LuxUI::Application app(argc, argv);
    try {
        if (gpu)
            return GpuPreview();
        Geometry();
        Interaction();
        KeyboardRouting();
        std::cout << "Modeling vertex/face picking, seam editing, gizmos and rollback passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
