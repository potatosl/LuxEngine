#include <LuxEditor/Scene/EditorDocument.hpp>
#include <LuxEditor/Viewport/SceneViewport2D.hpp>
#include <LuxEditor/Viewport/SceneViewport3D.hpp>
#include <LuxEditor/Viewport/ViewportCamera3D.hpp>
#include <LuxEngine/Scene/Entity.hpp>
#include <QAction>
#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace LuxEditor;
using namespace LuxEngine;

namespace {
void Check(bool success, const char* message) {
    if (!success)
        throw std::runtime_error(message);
}

int DifferentPixels(const QImage& first, const QImage& second) {
    if (first.size() != second.size())
        return 0;
    int different = 0;
    for (int y = 0; y < first.height(); ++y)
        for (int x = 0; x < first.width(); ++x)
            different += first.pixel(x, y) != second.pixel(x, y);
    return different;
}

void SendMouse(QWidget& widget, QEvent::Type type, QPointF point, Qt::MouseButton button,
               Qt::MouseButtons buttons) {
    QMouseEvent event(type, point, widget.mapToGlobal(point.toPoint()), button, buttons,
                      Qt::NoModifier);
    QApplication::sendEvent(&widget, &event);
}

void CameraClipping() {
    const auto camera = MakeViewportCamera3D({640, 480}, {}, 0, 0, 5);
    const auto crossing = ProjectViewportSegment(camera, {640, 480}, {0, 0, 6}, {0, 0, -10});
    Check(crossing && std::isfinite(crossing->p1().x()),
          "A grid segment crossing the near plane disappeared");
    Check(!ProjectViewportSegment(camera, {640, 480}, {0, 0, 6}, {1, 0, 7}),
          "A segment entirely behind the camera was accepted");
    const auto horizontal = ProjectViewportSegment(camera, {640, 480}, {-100, 0, 0}, {100, 0, 0});
    Check(horizontal && std::abs(horizontal->length() - 640) < .01,
          "A grid line crossing the screen was not clipped to its frustum");
    for (double distance : {.1, 1.0, 1000.0, 100000.0}) {
        const auto view = MakeViewportCamera3D({640, 480}, {10000, 0, 10000}, .65, -.48, distance);
        Check(ProjectViewportPoint(view, {640, 480}, {10000, 0, 10000}).has_value(),
              "The orbit focus was clipped while zooming");
        const auto ray = MakeViewportRay3D(view, {640, 480}, {320, 240});
        Check(ray && std::isfinite(ray->direction.x), "Zoom produced an invalid camera ray");
    }
    for (double unitsPerPixel : {.0001, .01, 1.0, 100.0, 10000.0}) {
        const double pixels = ViewportGridSpacing(unitsPerPixel) / unitsPerPixel;
        Check(pixels >= 44 && pixels <= 115, "Adaptive grid density left its readable range");
    }
}

void ViewportInteraction(QApplication& app) {
    EditorDocument document;
    auto entity = document.GetScene().CreateEntity("Gizmo fixture");
    document.Select(entity.GetId());
    int begin = 0, end = 0;
    SceneViewport3D view(
        nullptr, document, [&](std::optional<uint32> id) { document.Select(id); },
        [&](uint32 id, const TransformComponent& value) {
            Check(document.GetScene().SetWorldTransform(id, value),
                  "Gizmo produced an invalid transform");
        },
        [&] { ++begin; }, [&] { ++end; });
    view.resize(640, 480);
    view.SetViewPreset(SceneViewport3D::ViewPreset::Front);
    view.SetTool(EditorTool::Move);
    QAction eraseAction(&view);
    bool eraseTriggered = false;
    QObject::connect(&eraseAction, &QAction::triggered, &view, [&] { eraseTriggered = true; });
    eraseAction.setShortcut(QKeySequence(Qt::Key_X));
    view.addAction(&eraseAction);
    QKeyEvent axisOverride(QEvent::ShortcutOverride, Qt::Key_X, Qt::NoModifier);
    axisOverride.setAccepted(false);
    QApplication::sendEvent(&view, &axisOverride);
    Check(axisOverride.isAccepted(), "A global X shortcut stole the viewport axis lock");
    view.SetSnapToGrid(true);
    view.show();
    app.processEvents();
    view.activateWindow();
    view.setFocus();
    QKeyEvent axisPress(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier);
    QApplication::sendEvent(&view, &axisPress);
    QKeyEvent axisRelease(QEvent::KeyRelease, Qt::Key_X, Qt::NoModifier);
    QApplication::sendEvent(&view, &axisRelease);
    Check(!eraseTriggered, "The 3D axis shortcut activated the global tile erase action");
    SendMouse(view, QEvent::MouseButtonPress, {370, 240}, Qt::LeftButton, Qt::LeftButton);
    SendMouse(view, QEvent::MouseMove, {430, 240}, Qt::NoButton, Qt::LeftButton);
    SendMouse(view, QEvent::MouseButtonRelease, {430, 240}, Qt::LeftButton, Qt::NoButton);
    const auto position = entity.GetComponent<TransformComponent>().position;
    Check(position.x > 0 && position.y == 0 && position.z == 0,
          "The X gizmo moved an unconstrained axis");
    Check(position.x == std::round(position.x), "The gizmo ignored grid snapping");
    Check(begin == 1 && end == 1, "The gizmo did not form one undo gesture");

    entity.GetComponent<TransformComponent>() = {};
    view.SetTool(EditorTool::Rotate);
    SendMouse(view, QEvent::MouseButtonPress, {372, 188}, Qt::LeftButton, Qt::LeftButton);
    SendMouse(view, QEvent::MouseMove, {268, 188}, Qt::NoButton, Qt::LeftButton);
    SendMouse(view, QEvent::MouseButtonRelease, {268, 188}, Qt::LeftButton, Qt::NoButton);
    Check(std::abs(entity.GetComponent<TransformComponent>().rotation.z - ToRadians(90)) < .01f,
          "The Z rotation ring did not follow the mouse arc");
    entity.GetComponent<TransformComponent>() = {};
    entity.GetComponent<TransformComponent>().rotation.z = ToRadians(90);
    document.SetTransformSpace(TransformSpace::Local);
    view.SetTool(EditorTool::Scale);
    SendMouse(view, QEvent::MouseButtonPress, {320, 190}, Qt::LeftButton, Qt::LeftButton);
    SendMouse(view, QEvent::MouseMove, {320, 130}, Qt::NoButton, Qt::LeftButton);
    SendMouse(view, QEvent::MouseButtonRelease, {320, 130}, Qt::LeftButton, Qt::NoButton);
    const auto scale = entity.GetComponent<TransformComponent>().scale;
    Check(scale.x > 1.1f && std::abs(scale.y - 1) < .0001f && std::abs(scale.z - 1) < .0001f,
          "A rotated scale gizmo ignored its local axis or a vertical drag");

    entity.GetComponent<TransformComponent>() = {};
    document.SetTransformSpace(TransformSpace::World);
    view.SetTool(EditorTool::Select);
    view.SetSnapToGrid(false);
    SendMouse(view, QEvent::MouseButtonPress, {320, 240}, Qt::LeftButton, Qt::LeftButton);
    SendMouse(view, QEvent::MouseMove, {350, 210}, Qt::NoButton, Qt::LeftButton);
    SendMouse(view, QEvent::MouseButtonRelease, {350, 210}, Qt::LeftButton, Qt::NoButton);
    const auto freePosition = entity.GetComponent<TransformComponent>().position;
    Check(freePosition.x > 0 && freePosition.y > 0 && std::abs(freePosition.z) < .001f,
          "A completed axis gesture kept a later front-view selection drag constrained");

    document.Select(std::nullopt);
    entity.GetComponent<TransformComponent>().position = {20000, 0, 20000};
    view.FrameAll();
    for (auto preset :
         {SceneViewport3D::ViewPreset::Perspective, SceneViewport3D::ViewPreset::Front,
          SceneViewport3D::ViewPreset::Right, SceneViewport3D::ViewPreset::Top}) {
        view.SetViewPreset(preset);
        view.SetGridVisible(true);
        const auto grid = view.grab().toImage();
        view.SetGridVisible(false);
        const auto plain = view.grab().toImage();
        Check(DifferentPixels(grid, plain) > 100,
              "The 3D grid disappeared outside the origin or in a camera preset");
    }

    SceneViewport2D flat(
        nullptr, document, [](std::optional<uint32>) {}, [](uint32, const TransformComponent&) {},
        [](uint32, uint32, uint32, uint8_t) {}, [] {}, [] {});
    flat.resize(640, 480);
    flat.show();
    flat.FrameAll();
    flat.SetGridVisible(true);
    const auto grid = flat.grab().toImage();
    flat.SetGridVisible(false);
    Check(DifferentPixels(grid, flat.grab().toImage()) > 100,
          "The 2D grid disappeared beyond the old coordinate limit");
}
} // namespace

int main(int argc, char** argv) {
    qputenv("LUX_EDITOR_SOFTWARE_VIEWPORT", "1");
    QApplication app(argc, argv);
    try {
        CameraClipping();
        ViewportInteraction(app);
        std::cout << "Viewport clipping, grid, camera presets and gizmo regressions passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
