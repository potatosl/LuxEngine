#include <LuxEditor/Viewport/EngineSceneRenderer.hpp>
#include <LuxEngine/Renderer/RHI/GpuDevice.hpp>
#include <LuxEngine/Scene/Entity.hpp>
#include <LuxEngine/Scene/Scene.hpp>
#include <LuxUI/Widgets.hpp>
#include <iostream>
#include <stdexcept>

namespace {
using namespace LuxEditor;
using namespace LuxEngine;

void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

LuxUI::Image Render(EngineSceneRenderer& renderer, Scene& scene) {
    LuxUI::Image image;
    Check(renderer.RenderGame(scene, 640, 320, image), renderer.LastError().toUtf8().constData());
    Check(!image.isNull() && image.width() == 640 && image.height() == 320,
          "Game View GPU readback returned invalid dimensions");
    return image;
}

bool Red(const LuxUI::Image& image, int x, int y) {
    const auto color = image.pixelColor(x, y);
    return color.red() > 20 && color.red() > color.green() * 3 && color.red() > color.blue() * 3;
}

bool Blue(const LuxUI::Image& image, int x, int y) {
    const auto color = image.pixelColor(x, y);
    return color.blue() > 20 && color.blue() > color.green() * 3 && color.blue() > color.red() * 3;
}

void Primary2D(EngineSceneRenderer& renderer) {
    Scene scene;
    auto settings = scene.GetSettings();
    settings.clearColor = {.02f, .03f, .04f, 1};
    Check(scene.SetSettings(settings), "2D GPU fixture settings are invalid");
    auto inactive = scene.CreateEntity("Inactive camera");
    inactive.AddComponent<CameraComponent>().isPrimary = false;
    auto rig = scene.CreateEntity("Camera rig");
    rig.GetComponent<TransformComponent>().position = {2, -1, 0};
    auto camera = scene.CreateEntity("Primary camera");
    camera.GetComponent<TransformComponent>().position = {10, 3, 0};
    camera.AddComponent<CameraComponent>().orthoSize = 2;
    Check(scene.SetParent(camera.GetId(), rig.GetId(), false), "2D camera parenting failed");
    auto red = scene.CreateEntity("Red center");
    red.GetComponent<TransformComponent>().position = {12, 2, 0};
    red.AddComponent<SpriteRendererComponent>().color = {1, 0, 0, 1};
    auto blue = scene.CreateEntity("Blue right marker");
    blue.GetComponent<TransformComponent>().position = {13, 2, 0};
    blue.GetComponent<TransformComponent>().scale = {.25f, .25f, 1};
    blue.AddComponent<SpriteRendererComponent>().color = {0, 0, 1, 1};
    auto image = Render(renderer, scene);
    Check(Red(image, 320, 160) && Blue(image, 400, 160),
          "GPU Game View ignored the primary 2D camera, parent transform or orthographic size");
    camera.GetComponent<TransformComponent>().rotation.z = ToRadians(90.f);
    image = Render(renderer, scene);
    Check(Red(image, 320, 160) && Blue(image, 320, 240) && !Blue(image, 400, 160),
          "GPU Game View ignored 2D camera rotation");
    camera.GetComponent<TransformComponent>().rotation.z = 0;
    rig.GetComponent<TransformComponent>().position = {4, -2, 0};
    image = Render(renderer, scene);
    Check(Red(image, 160, 80) && Blue(image, 240, 80) && !Red(image, 320, 160),
          "GPU Game View retained a stale 2D camera after its parent moved");
}

void Primary3D(EngineSceneRenderer& renderer) {
    Scene scene;
    auto settings = scene.GetSettings();
    settings.clearColor = {.02f, .03f, .04f, 1};
    settings.render3D.mode = Render3DMode::Legacy;
    Check(scene.SetSettings(settings), "3D GPU fixture settings are invalid");
    auto inactive = scene.CreateEntity("Inactive camera");
    auto& inactiveSettings = inactive.AddComponent<CameraComponent>();
    inactiveSettings.isPrimary = false;
    inactiveSettings.isPerspective = true;
    auto rig = scene.CreateEntity("Camera rig");
    rig.GetComponent<TransformComponent>().position = {3, 2, -1};
    auto camera = scene.CreateEntity("Primary camera");
    camera.GetComponent<TransformComponent>().position = {0, 0, 6};
    auto& cameraSettings = camera.AddComponent<CameraComponent>();
    cameraSettings.isPerspective = true;
    cameraSettings.fov = 70;
    Check(scene.SetParent(camera.GetId(), rig.GetId(), false), "3D camera parenting failed");
    auto red = scene.CreateEntity("Red center");
    red.GetComponent<TransformComponent>().position = {3, 2, 0};
    red.GetComponent<TransformComponent>().scale = {2, 2, 2};
    red.AddComponent<MeshRendererComponent>().material.albedoColor = {1, 0, 0, 1};
    auto green = scene.CreateEntity("Orientation guard");
    green.GetComponent<TransformComponent>().position = {3, 2, 10};
    green.AddComponent<MeshRendererComponent>().material.albedoColor = {0, 1, 0, 1};
    auto image = Render(renderer, scene);
    Check(Red(image, 320, 160),
          "GPU Game View ignored the primary perspective camera or its parent position");
    rig.GetComponent<TransformComponent>().rotation.y = ToRadians(90.f);
    red.GetComponent<TransformComponent>().position = {3, 2, -1};
    green.GetComponent<TransformComponent>().position = {3, 2, 2};
    image = Render(renderer, scene);
    Check(Red(image, 320, 160),
          "GPU Game View ignored the camera rig rotation or retained the editor camera");
    red.GetComponent<TransformComponent>().position.y += 4;
    image = Render(renderer, scene);
    Check(!Red(image, 320, 160), "GPU Game View returned a stale frame after the scene changed");
}
} // namespace

int main(int argc, char** argv) {
    LuxUI::Application app(argc, argv);
    qunsetenv("LUX_EDITOR_SOFTWARE_VIEWPORT");
    EngineSceneRenderer::SetForceAllowed(true);
    EngineSceneRenderer::SetDefaultValidation(static_cast<int>(GpuValidation::Synchronization));
    EngineSceneRenderer renderer;
    if (!renderer.EnsureReady()) {
        std::cerr << "GPU unavailable: " << renderer.LastError().toStdString() << '\n';
        return 77;
    }
    try {
        Primary2D(renderer);
        Primary3D(renderer);
        const auto* gpu = renderer.GetGpuDevice();
        Check(gpu && gpu->IsReady() && gpu->GetCounters().framesSubmitted >= 6,
              "Game View did not submit its GPU frames");
        Check(gpu->GetCounters().validationErrors == 0,
              "Game View GPU rendering reported validation errors");
        std::cout << "GPU Game View primary 2D/3D cameras, transforms and readback passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
