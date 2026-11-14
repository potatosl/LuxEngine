#include <LuxEditor/App/EditorWindow.hpp>
#include <LuxEditor/Viewport/EngineSceneRenderer.hpp>
#include <LuxEditor/Viewport/GameViewport.hpp>
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
    const bool rendered = renderer.RenderGame(scene, 640, 320, image);
    Check(rendered, renderer.LastError().toUtf8().constData());
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

size_t RedPixels(const LuxUI::Image& image) {
    size_t count = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            count += Red(image, x, y);
    return count;
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
    cameraSettings.farClip = 2;
    Check(!Red(Render(renderer, scene), 320, 160), "GPU Game View ignored the camera far plane");
    cameraSettings.farClip = 100;
    cameraSettings.nearClip = 8;
    Check(!Red(Render(renderer, scene), 320, 160), "GPU Game View ignored the camera near plane");
    cameraSettings.nearClip = .1f;
    cameraSettings.fov = 40;
    const auto narrow = RedPixels(Render(renderer, scene));
    cameraSettings.fov = 100;
    image = Render(renderer, scene);
    Check(narrow > RedPixels(image) * 2, "GPU Game View did not update the camera FOV");
    const auto sceneCamera = ResolveGameViewCamera(scene, 640, 320).camera3D;
    LuxUI::Image editorImage;
    Check(
        renderer.Render3D(scene, 640, 320, {}, .65, -.48, 22, editorImage, nullptr, &sceneCamera) &&
            Red(editorImage, 320, 160) &&
            std::abs(static_cast<double>(RedPixels(editorImage)) - RedPixels(image)) < 20,
        "GPU Scene View camera override disagreed with the primary camera projection");
    cameraSettings.fov = 70;
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

void StoppedAndPlaying(LuxUI::Application& app) {
    LuxUI::TemporaryDir settings;
    Check(settings.isValid(), "Game View GPU settings directory is unavailable");
    LuxUI::Settings::setDefaultFormat(LuxUI::Settings::IniFormat);
    LuxUI::Settings::setPath(LuxUI::Settings::IniFormat, LuxUI::Settings::UserScope,
                             settings.path());
    LuxUI::Settings().clear();
    EditorWindow window;
    auto* tabs = window.findChild<LuxUI::TabWidget*>(LUX_UI_TEXT("sceneGameViews"));
    auto* view =
        static_cast<GameViewport*>(window.findChild<LuxUI::Widget*>(LUX_UI_TEXT("gameViewport")));
    Check(tabs && view, "Game View editor surface is unavailable");
    const auto action = [&window](const char* name) {
        auto* value = window.findChild<LuxUI::Action*>(LuxUI::String::fromLatin1(name));
        Check(value, "Game View editor action is missing");
        value->trigger();
    };
    action("addCamera2D");
    uint32 spriteId = 0;
    window.ExecuteRecordedEdit(LUX_UI_TEXT("GPU preview fixture"), [&] {
        auto sprite = window.GetDocument().CreateEntity("Game preview sprite");
        spriteId = sprite.GetId();
        sprite.AddComponent<SpriteRendererComponent>().color = {1, 0, 0, 1};
        auto& animation = sprite.AddComponent<AnimationComponent>();
        animation.playing = true;
        animation.clip.duration = 1;
        animation.clip.AddKeyframe({0, {}, {}, {1, 1, 1}});
        animation.clip.AddKeyframe({1, {1, 0, 0}, {}, {1, 1, 1}});
    });
    tabs->setCurrentWidget(view);
    window.show();
    app.processEvents();
    const auto capture = [&] {
        auto image = view->grab().toImage();
        Check(view->GetRenderer()->IsReady() && view->GetRenderer()->LastError().isEmpty(),
              "Game View widget fell back to software during the GPU regression");
        return image.pixelColor(image.width() / 2, image.height() / 2);
    };
    auto color = capture();
    Check(color.red() > 240 && color.blue() < 10 && !view->IsInputEnabled() &&
              window.GetDocument()
                      .GetScene()
                      .GetComponent<AnimationComponent>(spriteId)
                      .currentTime == 0,
          "stopped Game View did not render the authored camera without advancing animation");
    action("previewStep");
    Check(window.GetRuntimeScene() && view->GetScene() == window.GetRuntimeScene(),
          "playing Game View did not select the runtime scene");
    window.GetRuntimeScene()->GetComponent<SpriteRendererComponent>(spriteId).color = {0, 0, 1, 1};
    color = capture();
    Check(color.blue() > 240 && color.red() < 10,
          "playing Game View did not render runtime changes");
    action("previewStop");
    color = capture();
    Check(color.red() > 240 && color.blue() < 10 && !view->IsInputEnabled() &&
              view->GetScene() == &window.GetDocument().GetScene(),
          "stopping Game View did not return to its authored camera image");
    const auto original = window.GetDocument().GetActiveTabId();
    window.NewSceneDocument();
    action("addCamera2D");
    window.ExecuteRecordedEdit(LUX_UI_TEXT("GPU replacement scene"), [&] {
        window.GetDocument()
            .CreateEntity("Green scene")
            .AddComponent<SpriteRendererComponent>()
            .color = {0, 1, 0, 1};
    });
    color = capture();
    Check(color.green() > 240 && color.red() < 10 && color.blue() < 10,
          "stopped Game View retained the previous scene's GPU frame");
    Check(window.SwitchSceneDocument(original, false), "GPU fixture scene switch failed");
    color = capture();
    Check(color.red() > 240 && color.green() < 10,
          "Game View did not restore the selected scene's camera preview");
    Check(view->GetRenderer()->GetGpuDevice()->GetCounters().validationErrors == 0,
          "edit/play Game View transitions reported GPU validation errors");
    for (const auto& tab : window.GetDocument().ListTabs())
        window.GetDocument().SetDirty(tab.id, false);
    window.close();
}
} // namespace

int main(int argc, char** argv) {
    LuxUI::Application app(argc, argv);
    LuxUI::CoreApplication::setOrganizationName(LUX_UI_TEXT("LuxGameViewGpuTests"));
    LuxUI::CoreApplication::setApplicationName(LUX_UI_TEXT("GameView"));
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
        StoppedAndPlaying(app);
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
