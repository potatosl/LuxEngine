#include <LuxEditor/Scene/EditorDocument.hpp>
#include <LuxEditor/Viewport/SceneViewport2D.hpp>
#include <LuxEditor/Viewport/SceneViewport3D.hpp>
#include <LuxEditor/Viewport/ViewportCamera3D.hpp>
#include <LuxEngine/Scene/Entity.hpp>
#include <LuxUI/Widgets.hpp>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>

using namespace LuxEditor;
using namespace LuxEngine;

namespace {
void Check(bool success, const char* message) {
    if (!success)
        throw std::runtime_error(message);
}

double Length(Vec3 value) {
    return std::hypot(value.x, value.y, value.z);
}

int DifferentPixels(const LuxUI::Image& first, const LuxUI::Image& second) {
    if (first.size() != second.size())
        return 0;
    int different = 0;
    for (int y = 0; y < first.height(); ++y)
        for (int x = 0; x < first.width(); ++x)
            different += first.pixel(x, y) != second.pixel(x, y);
    return different;
}

void SendMouse(LuxUI::Widget& widget, LuxUI::Event::Type type, LuxUI::PointF point,
               LuxUI::Flags::MouseButton button, LuxUI::Flags::MouseButtons buttons,
               LuxUI::Flags::KeyboardModifiers modifiers = LuxUI::Flags::NoModifier) {
    LuxUI::MouseEvent event(type, point, widget.mapToGlobal(point.toPoint()), button, buttons,
                            modifiers);
    LuxUI::Application::sendEvent(&widget, &event);
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

void FreeLookDirection(LuxUI::Application& app) {
    for (const float fov : {30.f, 60.f, 100.f})
        for (const auto delta : {LuxUI::PointF(30, 0), LuxUI::PointF(-30, 0), LuxUI::PointF(0, 25),
                                 LuxUI::PointF(0, -25)}) {
            EditorDocument document;
            SceneViewport3D view(
                nullptr, document, [](std::optional<uint32>) {},
                [](uint32, const TransformComponent&) {}, [] {}, [] {});
            view.resize(640, 480);
            view.SetViewPreset(SceneViewport3D::ViewPreset::Front);
            Check(view.SetViewCamera(view.GetViewCamera(), fov, .1f, 1000.f),
                  "test projection setup failed");
            view.show();
            app.processEvents();
            const auto before = view.GetViewCamera();
            const LuxUI::PointF start(320, 240);
            SendMouse(view, LuxUI::Event::MouseButtonPress, start, LuxUI::Flags::RightButton,
                      LuxUI::Flags::RightButton);
            SendMouse(view, LuxUI::Event::MouseMove, start + delta, LuxUI::Flags::NoButton,
                      LuxUI::Flags::RightButton);
            SendMouse(view, LuxUI::Event::MouseButtonRelease, start + delta,
                      LuxUI::Flags::RightButton, LuxUI::Flags::NoButton);
            const auto after = view.GetViewCamera();
            const auto eyeDelta = after.GetPosition() - before.GetPosition();
            Check(eyeDelta.Dot(eyeDelta) < .00001f,
                  "Free look moved the camera eye instead of turning it");
            const auto forward = (after.GetTarget() - after.GetPosition()).Normalized();
            if (delta.x() != 0)
                Check(forward.x * delta.x() > 0,
                      "Horizontal free look turned opposite to pointer movement");
            if (delta.y() != 0)
                Check(forward.y * delta.y() < 0,
                      "Vertical free look turned opposite to pointer movement");
            const double yaw =
                -2 * std::atan(delta.x() * std::tan(ToRadians(fov) * .5) / view.height());
            const double pitch =
                -2 * std::atan(delta.y() * std::tan(ToRadians(fov) * .5) / view.height());
            const Vec3 expected{static_cast<float>(-std::sin(yaw) * std::cos(pitch)),
                                static_cast<float>(std::sin(pitch)),
                                static_cast<float>(-std::cos(yaw) * std::cos(pitch))};
            const auto difference = forward - expected;
            Check(difference.Dot(difference) < .000001f,
                  "Free look applied a device-pixel or extra sensitivity multiplier");
        }
}

void RunFor(int milliseconds) {
    LuxUI::EventLoop loop;
    LuxUI::Timer::singleShot(milliseconds, &loop, &LuxUI::EventLoop::quit);
    loop.exec();
}

void FreeNavigation(LuxUI::Application& app) {
    EditorDocument document;
    SceneViewport3D view(
        nullptr, document, [](std::optional<uint32>) {}, [](uint32, const TransformComponent&) {},
        [] {}, [] {});
    view.resize(640, 480);
    view.show();
    view.activateWindow();
    view.setFocus();
    app.processEvents();
    view.SetViewPreset(SceneViewport3D::ViewPreset::Front);
    const auto origin = view.GetViewCamera();
    SendMouse(view, LuxUI::Event::MouseButtonPress, {320, 240}, LuxUI::Flags::LeftButton,
              LuxUI::Flags::LeftButton, LuxUI::Flags::AltModifier);
    SendMouse(view, LuxUI::Event::MouseMove, {350, 250}, LuxUI::Flags::NoButton,
              LuxUI::Flags::LeftButton, LuxUI::Flags::AltModifier);
    SendMouse(view, LuxUI::Event::MouseButtonRelease, {350, 250}, LuxUI::Flags::LeftButton,
              LuxUI::Flags::NoButton, LuxUI::Flags::AltModifier);
    const auto orbit = view.GetViewCamera();
    Check(Length(orbit.GetPosition() - origin.GetPosition()) > .1f &&
              Length(orbit.GetTarget() - origin.GetTarget()) < .001f,
          "Alt+left drag did not orbit around the edit camera focus");
    SendMouse(view, LuxUI::Event::MouseButtonPress, {320, 240}, LuxUI::Flags::MiddleButton,
              LuxUI::Flags::MiddleButton);
    SendMouse(view, LuxUI::Event::MouseMove, {350, 255}, LuxUI::Flags::NoButton,
              LuxUI::Flags::MiddleButton);
    SendMouse(view, LuxUI::Event::MouseButtonRelease, {350, 255}, LuxUI::Flags::MiddleButton,
              LuxUI::Flags::NoButton);
    const auto pan = view.GetViewCamera();
    const auto eyeDelta = pan.GetPosition() - orbit.GetPosition();
    const auto targetDelta = pan.GetTarget() - orbit.GetTarget();
    Check(Length(eyeDelta) > .1f && Length(eyeDelta - targetDelta) < .0001f,
          "middle drag did not pan the camera eye and focus together");
    const auto wheel = [&](LuxUI::Flags::MouseButtons buttons) {
        LuxUI::WheelEvent event({320, 240}, view.mapToGlobal(LuxUI::Point(320, 240)), {}, {0, 120},
                                buttons, LuxUI::Flags::NoModifier, LuxUI::Flags::NoScrollPhase,
                                false);
        LuxUI::Application::sendEvent(&view, &event);
    };
    const auto beforeDolly = view.GetViewCamera();
    wheel(LuxUI::Flags::NoButton);
    Check(Length(view.GetViewCamera().GetPosition() - beforeDolly.GetPosition()) > .1f,
          "wheel did not dolly the camera");
    Check(view.SetMoveSpeed(5), "explicit camera speed was rejected");
    bool speedNotified = false;
    view.SetMoveSpeedChangedCallback([&](double value) { speedNotified = value > 5; });
    SendMouse(view, LuxUI::Event::MouseButtonPress, {320, 240}, LuxUI::Flags::RightButton,
              LuxUI::Flags::RightButton);
    const auto beforeSpeed = view.GetViewCamera().GetPosition();
    wheel(LuxUI::Flags::RightButton);
    Check(view.GetMoveSpeed() > 5 && speedNotified &&
              Length(view.GetViewCamera().GetPosition() - beforeSpeed) < .0001f,
          "right-button wheel did not adjust speed independently of dolly");
    SendMouse(view, LuxUI::Event::MouseButtonRelease, {320, 240}, LuxUI::Flags::RightButton,
              LuxUI::Flags::NoButton);
    const auto fly = [&](int key, bool fast) {
        view.SetViewPreset(SceneViewport3D::ViewPreset::Front);
        Check(view.SetMoveSpeed(5), "fly speed setup failed");
        const auto before = view.GetViewCamera().GetPosition();
        SendMouse(view, LuxUI::Event::MouseButtonPress, {320, 240}, LuxUI::Flags::RightButton,
                  LuxUI::Flags::RightButton);
        LuxUI::KeyEvent press(LuxUI::Event::KeyPress, key,
                              fast ? LuxUI::Flags::ShiftModifier : LuxUI::Flags::NoModifier);
        LuxUI::Application::sendEvent(&view, &press);
        RunFor(90);
        LuxUI::KeyEvent release(LuxUI::Event::KeyRelease, key, LuxUI::Flags::NoModifier);
        LuxUI::Application::sendEvent(&view, &release);
        SendMouse(view, LuxUI::Event::MouseButtonRelease, {320, 240}, LuxUI::Flags::RightButton,
                  LuxUI::Flags::NoButton);
        return view.GetViewCamera().GetPosition() - before;
    };
    const auto normal = fly(LuxUI::Flags::Key_W, false);
    const auto fast = fly(LuxUI::Flags::Key_W, true);
    Check(normal.z < -.05f && Length(fast) > Length(normal) * 1.6f,
          "RMB+W or Shift acceleration did not move the free camera");
    Check(fly(LuxUI::Flags::Key_S, false).z > .05f && fly(LuxUI::Flags::Key_A, false).x < -.05f &&
              fly(LuxUI::Flags::Key_D, false).x > .05f,
          "RMB+S/A/D did not move backward and sideways in camera space");
    Check(fly(LuxUI::Flags::Key_Q, false).y < -.05f && fly(LuxUI::Flags::Key_E, false).y > .05f,
          "RMB+Q/E did not move the camera vertically");
    Check(view.SetMoveSpeed(0) && view.GetEffectiveMoveSpeed() >= 2,
          "automatic camera speed was not restored");
}

void ViewportInteraction(LuxUI::Application& app) {
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
    LuxUI::Action eraseAction(&view);
    bool eraseTriggered = false;
    LuxUI::Object::connect(&eraseAction, &LuxUI::Action::triggered, &view,
                           [&] { eraseTriggered = true; });
    eraseAction.setShortcut(LuxUI::KeySequence(LuxUI::Flags::Key_X));
    view.addAction(&eraseAction);
    LuxUI::KeyEvent axisOverride(LuxUI::Event::ShortcutOverride, LuxUI::Flags::Key_X,
                                 LuxUI::Flags::NoModifier);
    axisOverride.setAccepted(false);
    LuxUI::Application::sendEvent(&view, &axisOverride);
    Check(axisOverride.isAccepted(), "A global X shortcut stole the viewport axis lock");
    view.show();
    app.processEvents();
    view.activateWindow();
    view.setFocus();
    LuxUI::KeyEvent axisPress(LuxUI::Event::KeyPress, LuxUI::Flags::Key_X,
                              LuxUI::Flags::NoModifier);
    LuxUI::Application::sendEvent(&view, &axisPress);
    LuxUI::KeyEvent axisRelease(LuxUI::Event::KeyRelease, LuxUI::Flags::Key_X,
                                LuxUI::Flags::NoModifier);
    LuxUI::Application::sendEvent(&view, &axisRelease);
    Check(!eraseTriggered, "The 3D axis shortcut activated the global tile erase action");
    SendMouse(view, LuxUI::Event::MouseButtonPress, {370, 240}, LuxUI::Flags::LeftButton,
              LuxUI::Flags::LeftButton);
    SendMouse(view, LuxUI::Event::MouseMove, {430, 240}, LuxUI::Flags::NoButton,
              LuxUI::Flags::LeftButton);
    SendMouse(view, LuxUI::Event::MouseButtonRelease, {430, 240}, LuxUI::Flags::LeftButton,
              LuxUI::Flags::NoButton);
    const auto position = entity.GetComponent<TransformComponent>().position;
    Check(position.x > 0 && position.y == 0 && position.z == 0,
          "The X gizmo moved an unconstrained axis");
    Check(std::abs(position.x - std::round(position.x)) > .001f,
          "The 3D position gizmo quantized a free movement to the grid");
    Check(begin == 1 && end == 1, "The gizmo did not form one undo gesture");

    entity.GetComponent<TransformComponent>() = {};
    view.SetTool(EditorTool::Rotate);
    SendMouse(view, LuxUI::Event::MouseButtonPress, {372, 188}, LuxUI::Flags::LeftButton,
              LuxUI::Flags::LeftButton);
    SendMouse(view, LuxUI::Event::MouseMove, {268, 188}, LuxUI::Flags::NoButton,
              LuxUI::Flags::LeftButton);
    SendMouse(view, LuxUI::Event::MouseButtonRelease, {268, 188}, LuxUI::Flags::LeftButton,
              LuxUI::Flags::NoButton);
    Check(std::abs(entity.GetComponent<TransformComponent>().rotation.z - ToRadians(90)) < .01f,
          "The Z rotation ring did not follow the mouse arc");
    entity.GetComponent<TransformComponent>() = {};
    entity.GetComponent<TransformComponent>().rotation.z = ToRadians(90);
    document.SetTransformSpace(TransformSpace::Local);
    view.SetTool(EditorTool::Scale);
    SendMouse(view, LuxUI::Event::MouseButtonPress, {320, 190}, LuxUI::Flags::LeftButton,
              LuxUI::Flags::LeftButton);
    SendMouse(view, LuxUI::Event::MouseMove, {320, 130}, LuxUI::Flags::NoButton,
              LuxUI::Flags::LeftButton);
    SendMouse(view, LuxUI::Event::MouseButtonRelease, {320, 130}, LuxUI::Flags::LeftButton,
              LuxUI::Flags::NoButton);
    const auto scale = entity.GetComponent<TransformComponent>().scale;
    Check(scale.x > 1.1f && std::abs(scale.y - 1) < .0001f && std::abs(scale.z - 1) < .0001f,
          "A rotated scale gizmo ignored its local axis or a vertical drag");

    entity.GetComponent<TransformComponent>() = {};
    document.SetTransformSpace(TransformSpace::World);
    view.SetTool(EditorTool::Select);
    SendMouse(view, LuxUI::Event::MouseButtonPress, {320, 240}, LuxUI::Flags::LeftButton,
              LuxUI::Flags::LeftButton);
    SendMouse(view, LuxUI::Event::MouseMove, {350, 210}, LuxUI::Flags::NoButton,
              LuxUI::Flags::LeftButton);
    SendMouse(view, LuxUI::Event::MouseButtonRelease, {350, 210}, LuxUI::Flags::LeftButton,
              LuxUI::Flags::NoButton);
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
    LuxUI::Application app(argc, argv);
    try {
        CameraClipping();
        FreeLookDirection(app);
        FreeNavigation(app);
        ViewportInteraction(app);
        std::cout << "Viewport clipping, grid, camera presets and gizmo regressions passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
