#include <LuxEditor/App/EditorWindow.hpp>
#include <LuxEditor/Viewport/SceneViewport2D.hpp>
#include <LuxEditor/Viewport/SceneViewport3D.hpp>
#include <LuxEngine/Scene/Entity.hpp>
#include <LuxUI/Widgets.hpp>
#include <cmath>
#include <stdexcept>

namespace {
using namespace LuxEditor;
using namespace LuxEngine;

void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

bool Near(Vec3 left, Vec3 right) {
    return std::hypot(left.x - right.x, left.y - right.y, left.z - right.z) < .0002;
}

Vec3 Forward(const PerspectiveCamera& camera) {
    return (camera.GetTarget() - camera.GetPosition()).Normalized();
}

Vec3 Up(const PerspectiveCamera& camera) {
    const auto& view = camera.GetViewMatrix().elements;
    return {view[1], view[5], view[9]};
}
} // namespace

int main(int argc, char** argv) {
    LuxUI::Application app(argc, argv);
    LuxUI::CoreApplication::setOrganizationName(LUX_UI_TEXT("LuxCameraTests"));
    LuxUI::CoreApplication::setApplicationName(LUX_UI_TEXT("CameraUi"));
    LuxUI::TemporaryDir files;
    Check(files.isValid(), "camera test settings are unavailable");
    LuxUI::Settings::setDefaultFormat(LuxUI::Settings::IniFormat);
    LuxUI::Settings::setPath(LuxUI::Settings::IniFormat, LuxUI::Settings::UserScope, files.path());
    LuxUI::Settings().clear();
    EditorWindow window;
    window.show();
    app.processEvents();
    window.findChild<LuxUI::Action*>(LUX_UI_TEXT("addCamera"))->trigger();
    const auto cameraId = window.GetDocument().GetSelected().GetId();
    uint32 parentId = 0;
    window.ExecuteRecordedEdit(LUX_UI_TEXT("Camera fixture"), [&] {
        auto& scene = window.GetDocument().GetScene();
        auto parent = scene.CreateEntity("Camera rig");
        parentId = parent.GetId();
        parent.GetComponent<TransformComponent>().position = {2, -1, 3};
        parent.GetComponent<TransformComponent>().rotation = {.2f, -.3f, .4f};
        Check(scene.SetParent(cameraId, parentId, false), "camera fixture parenting failed");
        auto& camera = scene.GetComponent<CameraComponent>(cameraId);
        camera.fov = 61.123456f;
        camera.orthoSize = 12.345678f;
        camera.nearClip = .1234567f;
        camera.farClip = 1234.5678f;
        window.GetDocument().Select(cameraId);
    });
    auto* nearClip = window.findChild<LuxUI::DoubleSpinBox*>(LUX_UI_TEXT("cameraNearClip"));
    auto* farClip = window.findChild<LuxUI::DoubleSpinBox*>(LUX_UI_TEXT("cameraFarClip"));
    auto* projection = window.findChild<LuxUI::ComboBox*>(LUX_UI_TEXT("cameraProjection"));
    auto* align = window.findChild<LuxUI::PushButton*>(LUX_UI_TEXT("cameraAlignToSceneView"));
    auto* through = window.findChild<LuxUI::PushButton*>(LUX_UI_TEXT("cameraViewSelected"));
    auto* history = window.findChild<LuxUI::UndoStack*>();
    auto* scene3D = static_cast<SceneViewport3D*>(
        window.findChild<LuxUI::Widget*>(LUX_UI_TEXT("sceneViewport3D")));
    auto* scene2D = static_cast<SceneViewport2D*>(
        window.findChild<LuxUI::Widget*>(LUX_UI_TEXT("sceneViewport2D")));
    auto* cameraDetails = window.findChild<LuxUI::GroupBox*>(LUX_UI_TEXT("cameraDetails"));
    Check(nearClip && farClip && projection && align && through && history && scene3D && scene2D &&
              cameraDetails && cameraDetails->isVisible() && nearClip->isEnabled() &&
              farClip->isEnabled(),
          "selected camera Details or clipping settings are not visible");
    auto* speed = window.findChild<LuxUI::DoubleSpinBox*>(LUX_UI_TEXT("viewportCameraSpeed"));
    Check(speed, "camera movement speed control is missing");
    speed->setValue(7.5);
    Check(scene3D->GetMoveSpeed() == 7.5, "camera speed field did not update navigation speed");
    Check(scene3D->SetMoveSpeed(12) && speed->value() == 12,
          "navigation speed did not synchronize back to the toolbar");
    window.findChild<LuxUI::PushButton*>(LUX_UI_TEXT("cameraRotateTool"))->click();
    const auto body =
        ProjectViewportPoint(scene3D->GetViewCamera(), scene3D->size(),
                             window.GetDocument().GetScene().GetWorldTransform(cameraId).position);
    Check(body && scene3D->rect().contains(body->toPoint()),
          "camera manipulation did not frame its entity inside the viewport");
    window.ExecuteRecordedEdit(LUX_UI_TEXT("Clear camera selection"),
                               [&] { window.GetDocument().Select(std::nullopt); });
    LuxUI::MouseEvent pickPress(LuxUI::Event::MouseButtonPress, *body,
                                scene3D->mapToGlobal(body->toPoint()), LuxUI::Flags::LeftButton,
                                LuxUI::Flags::LeftButton, LuxUI::Flags::NoModifier);
    LuxUI::MouseEvent pickRelease(LuxUI::Event::MouseButtonRelease, *body,
                                  scene3D->mapToGlobal(body->toPoint()), LuxUI::Flags::LeftButton,
                                  LuxUI::Flags::NoButton, LuxUI::Flags::NoModifier);
    LuxUI::Application::sendEvent(scene3D, &pickPress);
    LuxUI::Application::sendEvent(scene3D, &pickRelease);
    Check(window.GetDocument().GetSelected().GetId() == cameraId,
          "the visible camera body marker could not be selected");
    const auto settings = [&]() -> const CameraComponent& {
        return window.GetDocument().GetScene().GetComponent<CameraComponent>(cameraId);
    };
    const auto original = settings();
    const int beforeNear = history->index();
    nearClip->setValue(.25);
    Check(settings().nearClip == .25f && settings().farClip == original.farClip &&
              settings().fov == original.fov && settings().orthoSize == original.orthoSize &&
              history->index() == beforeNear + 1,
          "near clip edit rounded unrelated camera fields or created multiple undo entries");
    history->undo();
    Check(settings().nearClip == original.nearClip && settings().farClip == original.farClip,
          "camera clipping undo did not restore full precision");
    farClip->setValue(321.125);
    Check(settings().farClip == 321.125f && settings().nearClip == original.nearClip &&
              settings().fov == original.fov,
          "far clip edit changed untouched camera values");

    PerspectiveCamera desired(ToRadians(72),
                              static_cast<float>(scene3D->width()) / scene3D->height(), .2f, 200.f);
    desired.SetPosition({8, 6, 12});
    desired.SetTarget({-1, 2, 0});
    desired.SetUp({.2f, 1, .1f});
    Check(scene3D->SetViewCamera(desired, 72, .2f, 200),
          "custom edit camera could not be configured");
    const int beforeAlign = history->index();
    align->click();
    const auto world = window.GetDocument().GetScene().GetWorldMatrix(cameraId);
    const Vec3 position{world.elements[12], world.elements[13], world.elements[14]};
    const auto forward =
        Vec3{-world.elements[8], -world.elements[9], -world.elements[10]}.Normalized();
    const auto up = Vec3{world.elements[4], world.elements[5], world.elements[6]}.Normalized();
    Check(Near(position, desired.GetPosition()) && Near(forward, Forward(desired)) &&
              Near(up, Up(desired)) && settings().fov == 72 && settings().nearClip == .2f &&
              settings().farClip == 200 && history->index() == beforeAlign + 1,
          "aligning a parented camera lost its world pose, roll or projection");
    history->undo();
    history->redo();
    scene3D->SetViewPreset(SceneViewport3D::ViewPreset::Perspective);
    const int beforeView = history->index();
    const bool dirty = window.GetDocument().IsDirty();
    through->click();
    const auto restored = scene3D->GetViewCamera();
    float fov, nearDistance, farDistance;
    scene3D->GetViewProjection(fov, nearDistance, farDistance);
    Check(Near(restored.GetPosition(), desired.GetPosition()) &&
              Near(Forward(restored), Forward(desired)) && Near(Up(restored), Up(desired)) &&
              fov == 72 && nearDistance == .2f && farDistance == 200 &&
              history->index() == beforeView && window.GetDocument().IsDirty() == dirty,
          "viewing through the selected camera changed authored data or lost camera settings");
    scene3D->FrameAll();
    scene3D->GetViewProjection(fov, nearDistance, farDistance);
    Check(fov == 60 && nearDistance > 0 && farDistance > nearDistance,
          "Frame All did not recover the normal editor projection");

    projection->setCurrentIndex(0);
    Check(!nearClip->isEnabled() && !farClip->isEnabled(),
          "2D camera enabled unsupported 3D depth controls");
    Check(scene2D->SetView(4, -2, 3.5), "2D edit camera setup failed");
    const int before2D = history->index();
    align->click();
    const auto world2D = window.GetDocument().GetScene().GetWorldTransform(cameraId);
    Check(std::abs(world2D.position.x - 4) < .0001 && std::abs(world2D.position.y + 2) < .0001 &&
              settings().orthoSize == 3.5f && history->index() == before2D + 1,
          "2D camera alignment did not copy the edit view center and size");
    Check(scene2D->SetView(0, 0, 10), "2D camera reset failed");
    through->click();
    double x, y, halfHeight;
    scene2D->GetView(x, y, halfHeight);
    Check(std::abs(x - 4) < .0001 && std::abs(y + 2) < .0001 &&
              std::abs(halfHeight - 3.5) < .0001 &&
              through->toolTip().contains(LUX_UI_TEXT("Game View")),
          "2D camera view did not restore center/size or explain where rotation is previewed");

    window.ExecuteRecordedEdit(LUX_UI_TEXT("Singular parent fixture"), [&] {
        window.GetDocument().GetScene().GetComponent<TransformComponent>(parentId).scale.x = 0;
    });
    const int beforeFailure = history->index();
    align->click();
    Check(history->index() == beforeFailure &&
              window.LastLogMessage().contains(LUX_UI_TEXT("부모")),
          "singular parent alignment silently modified the camera or lacked an actionable error");
    window.GetDocument().SetDirty(false);
    window.close();
    return 0;
}
