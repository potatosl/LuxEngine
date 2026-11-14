#include <LuxEditor/Viewport/EngineSceneRenderer.hpp>
#include <LuxEditor/Viewport/GameViewport.hpp>
#include <LuxEditor/Viewport/ViewportCamera3D.hpp>
#include <LuxEngine/Platform/Input.hpp>
#include <LuxEngine/Scene/Entity.hpp>
#include <LuxEngine/Scene/Scene.hpp>
#include <LuxUI/Widgets.hpp>
#include <cmath>
#include <stdexcept>

namespace {
void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

bool Near(float left, float right) {
    return std::abs(left - right) < .0001f;
}
} // namespace

int main(int argc, char** argv) {
    LuxUI::Application app(argc, argv);
    LuxEngine::Scene scene;
    auto cameraEntity = scene.CreateEntity("Camera");
    cameraEntity.AddComponent<LuxEngine::CameraComponent>().orthoSize = 2;
    auto& transform = cameraEntity.GetComponent<LuxEngine::TransformComponent>();
    transform.position = {10, 3, 0};
    auto sprite = scene.CreateEntity("Sprite");
    sprite.GetComponent<LuxEngine::TransformComponent>().position = {10, 3, 0};
    sprite.AddComponent<LuxEngine::SpriteRendererComponent>().color = {1, 0, 0, 1};
    const auto camera = LuxEditor::ResolveGameViewCamera(scene, 640, 320);
    Check(!camera.perspective && Near(camera.camera2D.GetPosition().x, 10) &&
              Near(camera.camera2D.GetPosition().y, 3),
          "2D Game View ignored its primary camera");
    const auto& projection = camera.camera2D.GetProjectionMatrix();
    Check(Near(projection.elements[0], .25f) && Near(projection.elements[5], .5f),
          "2D camera aspect or orthographic size is incorrect");
    transform.rotation.z = LuxEngine::ToRadians(90.f);
    const auto rotated = LuxEditor::ResolveGameViewCamera(scene, 640, 320);
    Check(Near(rotated.camera2D.GetRotation(), transform.rotation.z),
          "2D camera rotation was lost");
    transform.scale = {0, 0, 0};
    const auto singular = LuxEditor::ResolveGameViewCamera(scene, 640, 320);
    Check(Near(singular.camera2D.GetPosition().x, 10) &&
              Near(singular.camera2D.GetRotation(), transform.rotation.z),
          "zero-scale camera crashed or lost its authored view");
    transform.scale = {1, 1, 1};
    transform.rotation.z = 0;
    LuxEditor::GameViewport view;
    view.resize(640, 320);
    view.SetScene(&scene);
    view.show();
    app.processEvents();
    LuxUI::Image image(view.size(), LuxUI::Image::Format_RGBA8888);
    image.fill(LuxUI::Flags::transparent);
    view.render(&image);
    const auto center = image.pixelColor(320, 160);
    Check(center.red() > 240 && center.green() < 10,
          "Game View did not render the primary camera's centered sprite");
    Check(!view.IsInputEnabled(), "edit-mode camera preview enabled gameplay input");
    LuxUI::MouseEvent editClick(LuxUI::Event::MouseButtonPress, {320, 160},
                                view.mapToGlobal(LuxUI::Point(320, 160)), LuxUI::Flags::LeftButton,
                                LuxUI::Flags::LeftButton, LuxUI::Flags::NoModifier);
    LuxUI::CoreApplication::sendEvent(&view, &editClick);
    Check(!LuxEngine::Input::IsMouseButtonPressed(LuxEngine::LuxMouseButton::Left),
          "edit-mode Game View forwarded a mouse button into the game");
    view.SetPaused(true);
    Check(view.IsPaused() && view.GetScene() == &scene, "pausing discarded the game scene");
    view.SetScene(&scene);
    Check(view.IsPaused(), "refreshing the same scene reset playback state");
    view.SetScene(nullptr);
    Check(!view.GetScene() && !view.IsPaused(), "stopping retained Game View state");

    auto& primary = cameraEntity.GetComponent<LuxEngine::CameraComponent>();
    primary.isPerspective = true;
    primary.fov = 75;
    transform.position = {0, 1, 8};
    auto parent = scene.CreateEntity("Camera rig");
    parent.GetComponent<LuxEngine::TransformComponent>().position = {2, 3, 0};
    Check(scene.SetParent(cameraEntity.GetId(), parent.GetId(), false), "camera parenting failed");
    const auto perspective = LuxEditor::ResolveGameViewCamera(scene, 640, 320);
    const auto position = perspective.camera3D.GetPosition();
    Check(perspective.perspective && Near(position.x, 2) && Near(position.y, 4) &&
              Near(position.z, 8),
          "3D Game View ignored the primary camera's world transform");
    const auto point = LuxEditor::ProjectViewportPoint(perspective.camera3D, {640, 320}, {2, 4, 0});
    Check(point && std::abs(point->x() - 320) < .001 && std::abs(point->y() - 160) < .001,
          "3D Game View did not look along the camera's local -Z direction");
    auto replaceable = std::make_unique<LuxEngine::Scene>();
    view.SetScene(replaceable.get());
    replaceable.reset();
    Check(!view.GetScene(), "Game View retained a destroyed scene pointer");
    view.render(&image);
    replaceable = std::make_unique<LuxEngine::Scene>();
    view.SetSceneProvider([&replaceable] { return replaceable.get(); });
    auto* previous = replaceable.get();
    replaceable = std::make_unique<LuxEngine::Scene>();
    auto replacementCamera = replaceable->CreateEntity("Replacement camera");
    replacementCamera.AddComponent<LuxEngine::CameraComponent>().orthoSize = 2;
    auto replacementSprite = replaceable->CreateEntity("Replacement sprite");
    replacementSprite.AddComponent<LuxEngine::SpriteRendererComponent>().color = {0, 0, 1, 1};
    auto& animation = replacementSprite.AddComponent<LuxEngine::AnimationComponent>();
    animation.playing = true;
    animation.clip.duration = 1;
    animation.clip.AddKeyframe({0, {}, {}, {1, 1, 1}});
    animation.clip.AddKeyframe({1, {3, 0, 0}, {}, {1, 1, 1}});
    Check(view.GetScene() == replaceable.get() && view.GetScene() != previous,
          "Game View provider did not follow a scene replacement");
    std::string before, after;
    Check(replaceable->SerializeToString(before), "preview scene snapshot failed");
    for (int frame = 0; frame < 3; ++frame)
        view.render(&image);
    Check(replaceable->SerializeToString(after) && before == after && animation.currentTime == 0,
          "edit-mode Game View advanced authored animation or mutated scene data");
    Check(image.pixelColor(320, 160).blue() > 240,
          "Game View displayed a stale image after replacing its edit scene");
    return 0;
}
