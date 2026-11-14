#include <LuxEditor/Viewport/CameraGizmos.hpp>
#include <LuxEditor/Viewport/ViewportCamera3D.hpp>
#include <LuxEngine/Runtime/ScenePlayer.hpp>
#include <LuxEngine/Scene/Entity.hpp>
#include <LuxEngine/Scene/Scene.hpp>
#include <LuxUI/Widgets.hpp>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace LuxEditor;
using namespace LuxEngine;

void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

bool Near(Vec3 first, Vec3 second) {
    return std::hypot(first.x - second.x, first.y - second.y, first.z - second.z) < .001;
}

Vec3 Center(const std::array<Vec3, 4>& corners) {
    return corners[0] * .25f + corners[1] * .25f + corners[2] * .25f + corners[3] * .25f;
}

void Geometry() {
    Scene scene;
    auto entity = scene.CreateEntity("Perspective camera");
    auto& camera = entity.AddComponent<CameraComponent>();
    camera.isPerspective = true;
    camera.fov = 90;
    camera.nearClip = .5f;
    camera.farClip = 10;
    auto geometry = BuildCameraGizmoGeometry(scene, entity.GetId(), 2);
    Check(geometry && geometry->perspective && Near(geometry->forward, {0, 0, -1}) &&
              Near(Center(geometry->nearCorners), {0, 0, -.5f}) &&
              Near(Center(geometry->farCorners), {0, 0, -10}) &&
              Near(geometry->nearCorners[0], {-1, -.5f, -.5f}) &&
              Near(geometry->farCorners[2], {20, 10, -10}),
          "Perspective camera gizmo did not use vertical FOV, aspect, near and far planes");
    auto parent = scene.CreateEntity("Parent camera rig");
    parent.GetComponent<TransformComponent>() = {{3, 4, 5}, {.2f, .4f, -.1f}, {2, 3, 4}};
    entity.GetComponent<TransformComponent>() = {{1, 2, 3}, {-.1f, .3f, .5f}, {3, 2, 1}};
    Check(scene.SetParent(entity.GetId(), parent.GetId(), false), "Camera rig fixture failed");
    geometry = BuildCameraGizmoGeometry(scene, entity.GetId(), 2);
    const auto runtime = ResolveScenePlayerCamera(scene, {});
    Check(geometry && Near(geometry->position, runtime.position) &&
              Near(geometry->forward, (runtime.target - runtime.position).Normalized()) &&
              Near(geometry->up, runtime.up),
          "Camera gizmo parent rotation/scale or roll disagrees with the runtime camera");
    const auto nearEdge = geometry->nearCorners[3] - geometry->nearCorners[0];
    Check(std::abs(std::sqrt(nearEdge.Dot(nearEdge)) - 1) < .001,
          "Camera object scale incorrectly scaled its lens/frustum");
    Check(scene.SetParent(entity.GetId(), 0, false), "Camera rig could not be detached");
    entity.GetComponent<TransformComponent>() = {{3, 4, 5}, {0, 0, ToRadians(90)}, {4, 5, 6}};
    camera.isPerspective = false;
    camera.orthoSize = 2;
    geometry = BuildCameraGizmoGeometry(scene, entity.GetId(), 2);
    Check(geometry && !geometry->perspective && Near(geometry->position, {3, 4, 5}) &&
              Near(geometry->up, {-1, 0, 0}) && Near(geometry->nearCorners[0], {5, 0, 5}) &&
              Near(geometry->nearCorners[2], {1, 8, 5}) &&
              Near(geometry->farCorners[0], geometry->nearCorners[0]),
          "2D camera rectangle did not use XY position, Z rotation and ortho half-height");
    camera.orthoSize = 0;
    Check(!BuildCameraGizmoGeometry(scene, entity.GetId(), 2),
          "Invalid 2D camera size produced a misleading gizmo");
    camera.isPerspective = true;
    camera.fov = 60;
    camera.nearClip = .1f;
    camera.farClip = 100;
    const CameraComponent valid = camera;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    for (const auto fov : {0.f, 180.f, nan, infinity}) {
        camera = valid;
        camera.fov = fov;
        Check(!BuildCameraGizmoGeometry(scene, entity.GetId(), 2),
              "Invalid camera FOV was accepted");
    }
    for (const auto clips : std::array<std::array<float, 2>, 5>{
             {{{0, 100}}, {{-1, 100}}, {{10, 10}}, {{100, 1}}, {{nan, infinity}}}}) {
        camera = valid;
        camera.nearClip = clips[0];
        camera.farClip = clips[1];
        Check(!BuildCameraGizmoGeometry(scene, entity.GetId(), 2),
              "Invalid camera clip range was accepted");
    }
    camera = valid;
    for (const auto aspect : {0., -1., double(nan), double(infinity), 1e-300})
        Check(!BuildCameraGizmoGeometry(scene, entity.GetId(), aspect),
              "Invalid camera gizmo viewport aspect was accepted");
    entity.GetComponent<TransformComponent>().position.x = nan;
    Check(!BuildCameraGizmoGeometry(scene, entity.GetId(), 2),
          "Non-finite camera transform generated a gizmo");
}

void ProjectionAndPicking() {
    Scene scene;
    auto front = scene.CreateEntity("Front camera");
    auto& camera = front.AddComponent<CameraComponent>();
    camera.isPerspective = true;
    camera.fov = 40;
    camera.nearClip = .5f;
    camera.farClip = 6;
    auto back = scene.CreateEntity("Rear camera");
    back.AddComponent<CameraComponent>(camera);
    back.GetComponent<TransformComponent>().position.z = -4;
    auto invalid = scene.CreateEntity("Invalid camera");
    invalid.AddComponent<CameraComponent>(camera).fov = 180;
    invalid.GetComponent<TransformComponent>().position.z = 5;
    const auto view = MakeViewportCamera3D({640, 480}, {}, 0, 0, 10);
    Check(PickCameraGizmo(scene, view, {640, 480}, {320, 240}) == front.GetId(),
          "Camera marker picking did not choose the closest valid body icon");
    Check(!PickCameraGizmo(scene, view, {640, 480}, {340, 240}) &&
              !PickCameraGizmo(scene, view, {640, 480}, {320, 260}) &&
              !PickCameraGizmo(scene, view, {0, 480}, {320, 240}),
          "Camera frustum/large object bounds stole selection outside the small body icon");
    front.GetComponent<TransformComponent>().position = {0, 0, 20};
    back.GetComponent<TransformComponent>().position = {100, 0, 0};
    Check(!PickCameraGizmo(scene, view, {640, 480}, {320, 240}),
          "Camera body behind the viewer or outside the viewport remained pickable");

    PerspectiveCamera clipping(ToRadians(90), 1, 1, 10);
    clipping.SetPosition({});
    clipping.SetTarget({0, 0, -1});
    const auto bounded = [](const LuxUI::LineF& line) {
        for (const auto point : {line.p1(), line.p2()})
            if (!std::isfinite(point.x()) || !std::isfinite(point.y()) || point.x() < -.001 ||
                point.y() < -.001 || point.x() > 600.001 || point.y() > 600.001)
                return false;
        return true;
    };
    const auto crossing = ProjectViewportSegment(clipping, {600, 600}, {-2, 0, 1}, {2, 0, -5});
    const auto sides = ProjectViewportSegment(clipping, {600, 600}, {-100, 0, -5}, {100, 0, -5});
    Check(crossing && bounded(*crossing) && sides && bounded(*sides) &&
              !ProjectViewportSegment(clipping, {600, 600}, {0, 0, 1}, {0, 1, 2}) &&
              !ProjectViewportSegment(clipping, {600, 600}, {0, 0, -11}, {1, 0, -20}),
          "Camera gizmo segments did not clip behind/near/far/side planes before division");
    front.GetComponent<TransformComponent>().position = {0, 0, 1};
    const auto geometry = BuildCameraGizmoGeometry(scene, front.GetId(), 1);
    Check(geometry.has_value(), "Near-plane crossing camera fixture was invalid");
    for (size_t corner = 0; corner < 4; ++corner) {
        const auto line = ProjectViewportSegment(
            clipping, {600, 600}, geometry->nearCorners[corner], geometry->farCorners[corner]);
        Check(line && bounded(*line),
              "Selected camera frustum crossing the Scene View near plane disappeared or exploded");
    }
    LuxUI::Image image(600, 600, LuxUI::Image::Format_ARGB32_Premultiplied);
    image.fill(LuxUI::Flags::transparent);
    LuxUI::Painter painter(&image);
    const std::array<uint32, 1> selected{front.GetId()};
    DrawCameraGizmos(painter, scene, clipping, {600, 600}, selected);
    painter.end();
    bool visible = false;
    for (int y = 0; y < image.height() && !visible; ++y)
        for (int x = 0; x < image.width(); ++x)
            if (image.pixelColor(x, y).alpha()) {
                visible = true;
                break;
            }
    Check(visible, "Clipped selected camera frustum produced no viewport overlay");
}
} // namespace

int main(int argc, char** argv) {
    LuxUI::Application app(argc, argv);
    try {
        Geometry();
        ProjectionAndPicking();
        std::cout << "Camera gizmo lens, runtime orientation, clipping and body picking passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
