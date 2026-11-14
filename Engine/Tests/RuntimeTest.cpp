// Tests for the Runtime library: LuxPlayer argument parsing, view fitting and a scene file for
// the LuxPlayer smoke test.
#include <LuxEngine/ECS/ECS.hpp>
#include <LuxEngine/Physics/PhysicsEngine.hpp>
#include <LuxEngine/Runtime/ScenePlayer.hpp>
#include <LuxEngine/Scene/Entity.hpp>
#include <SDL3/SDL_timer.h>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
int checks = 0;

void Check(bool ok, const char* message) {
    ++checks;
    if (!ok)
        throw std::runtime_error(message);
}

bool Near(float a, float b) {
    return std::abs(a - b) < 0.001f;
}

using Args = std::vector<std::string>;
using LuxEngine::ParseScenePlayerArguments;

void Parse() {
    std::string error;
    auto options = ParseScenePlayerArguments({"scene.lux"}, error);
    Check(options && options->scenePath == "scene.lux" && !options->view && !options->fullscreen &&
              options->maxFrames == 0,
          "scene path alone");
    options =
        ParseScenePlayerArguments({"a.lux", "--title", "게임", "--size", "800", "600",
                                   "--fullscreen", "--view", "1.5", "-2", "3", "--frames", "10"},
                                  error);
    Check(options && options->title == "게임" && options->width == 800 && options->height == 600 &&
              options->fullscreen && options->maxFrames == 10,
          "all options");
    Check(options && options->view && Near(options->view->centerX, 1.5f) &&
              Near(options->view->centerY, -2.0f) && Near(options->view->halfHeight, 3.0f),
          "view option");
    options = ParseScenePlayerArguments({"a.lux", "--view", "1.5e1", "-.5", "3e-1"}, error);
    Check(options && options->view && Near(options->view->centerX, 15.0f) &&
              Near(options->view->centerY, -.5f) && Near(options->view->halfHeight, .3f),
          "view accepts decimal and scientific notation");
    const std::vector<Args> invalid = {
        {},
        {"--title", "x"},
        {"a.lux", "b.lux"},
        {"a.lux", "--size", "10", "600"},
        {"a.lux", "--size", "800"},
        {"a.lux", "--view", "0", "0", "0"},
        {"a.lux", "--view", "0", "0", "nan"},
        {"a.lux", "--view", "0", "0", "inf"},
        {"a.lux", "--view", "0", "0", "1e999"},
        {"a.lux", "--view", " 1", "0", "3"},
        {"a.lux", "--view", "1 ", "0", "3"},
        {"a.lux", "--view", "+1", "0", "3"},
        {"a.lux", "--view", "1x", "0", "3"},
        {"a.lux", "--view", "1,5", "0", "3"},
        {"a.lux", "--frames", "-1"},
        {"a.lux", "--unknown"},
    };
    for (const auto& args : invalid) {
        error.clear();
        Check(!ParseScenePlayerArguments(args, error) && !error.empty(),
              "invalid arguments rejected");
    }
    Check(ParseScenePlayerArguments({"scene.lux"}, error).has_value() && error.empty(),
          "successful parsing clears an earlier error");
}

void Fit() {
    const LuxEngine::ScenePlayerCameraView aggregateCamera{{0, 3, 9}, {}, {0, 1, 0}, 60};
    Check(Near(aggregateCamera.nearClip, .1f) && Near(aggregateCamera.farClip, 1000.f),
          "public runtime camera aggregate preserves original clipping defaults");
    LuxEngine::Scene empty;
    auto view = LuxEngine::FitSceneView(empty);
    Check(Near(view.centerX, 0) && Near(view.centerY, 0) && view.halfHeight > 0,
          "empty scene view");
    LuxEngine::Scene scene;
    auto a = scene.CreateEntity("a");
    a.GetComponent<LuxEngine::TransformComponent>().position = {-10, 0, 0};
    auto b = scene.CreateEntity("b");
    b.GetComponent<LuxEngine::TransformComponent>().position = {10, 4, 0};
    view = LuxEngine::FitSceneView(scene);
    Check(Near(view.centerX, 0) && Near(view.centerY, 2), "view centred on entities");
    // 21 units wide at 16:9 needs a half height of at least 21/2*9/16.
    Check(view.halfHeight >= 21.0f * 0.5f * 9.0f / 16.0f, "view fits width");
    auto map = scene.CreateEntity("map");
    map.GetComponent<LuxEngine::TransformComponent>().position = {0, -50, 0};
    map.AddComponent<LuxEngine::TilemapComponent>();
    view = LuxEngine::FitSceneView(scene);
    Check(view.centerY < 0 && view.halfHeight > 25, "tilemap extends view");
    LuxEngine::Scene rotated;
    auto sprite = rotated.CreateEntity("rotated");
    auto& transform = sprite.GetComponent<LuxEngine::TransformComponent>();
    transform.scale = {4, 2, 1};
    transform.rotation.z = LuxEngine::ToRadians(90);
    view = LuxEngine::FitSceneView(rotated);
    Check(view.halfHeight >= 2.19f, "rotated bounds fit the visible frame");
    LuxEngine::Scene parented;
    auto parent = parented.CreateEntity("parent");
    parent.GetComponent<LuxEngine::TransformComponent>().position = {20, 10, 0};
    auto child = parented.CreateEntity("child");
    child.GetComponent<LuxEngine::TransformComponent>().position = {4, 2, 0};
    Check(parented.SetParent(child.GetId(), parent.GetId(), false), "fit hierarchy initializes");
    view = LuxEngine::FitSceneView(parented);
    Check(Near(view.centerX, 22) && Near(view.centerY, 11),
          "view uses world transforms for parented scene objects");
    LuxEngine::Scene cameraScene;
    auto camera = cameraScene.CreateEntity("primary camera");
    auto& cameraComponent = camera.AddComponent<LuxEngine::CameraComponent>();
    cameraComponent.isPerspective = true;
    cameraComponent.fov = 70;
    cameraComponent.nearClip = .75f;
    cameraComponent.farClip = 75.f;
    auto cameraView = LuxEngine::ResolveScenePlayerCamera(cameraScene, {});
    Check(Near(cameraView.position.x, 0) && Near(cameraView.position.z, 0) &&
              Near(cameraView.target.z, -1) && Near(cameraView.up.y, 1) &&
              Near(cameraView.fovDegrees, 70) && Near(cameraView.nearClip, .75f) &&
              Near(cameraView.farClip, 75.f),
          "an origin camera looks forward rather than degenerating into eye == target");
    const auto projection =
        LuxEngine::Mat4::Perspective(LuxEngine::ToRadians(cameraView.fovDegrees), 4.f / 3.f,
                                     cameraView.nearClip, cameraView.farClip);
    const auto depth = [&](float z) {
        return (projection.elements[10] * z + projection.elements[14]) / -z;
    };
    Check(Near(depth(-.75f), 0) && Near(depth(-75.f), 1) && depth(-.5f) < 0 && depth(-100.f) > 1,
          "resolved runtime camera clip settings map the custom near/far planes and reject outside "
          "depth");
    cameraComponent.nearClip = 1e20f;
    cameraComponent.farClip = 1e21f;
    const auto largeCamera = LuxEngine::ResolveScenePlayerCamera(cameraScene, {});
    Check(largeCamera.nearClip == 1e20f && largeCamera.farClip == 1e21f,
          "runtime accepts large clipping ranges whose projection fits in float");
    const float extremeFar = std::numeric_limits<float>::max();
    const std::array<std::pair<float, float>, 7> invalidClips{
        {{0, 75},
         {-1, 75},
         {75, 75},
         {80, 75},
         {std::numeric_limits<float>::quiet_NaN(), 75},
         {1, std::numeric_limits<float>::infinity()},
         {std::nextafter(extremeFar, 0.0f), extremeFar}}};
    for (const auto [near, far] : invalidClips) {
        cameraComponent.nearClip = near;
        cameraComponent.farClip = far;
        const auto fallback = LuxEngine::ResolveScenePlayerCamera(cameraScene, {});
        Check(Near(fallback.nearClip, .1f) && Near(fallback.farClip, 1000.f),
              "invalid camera clipping ranges fall back to a finite runtime projection");
    }
    cameraComponent.nearClip = .75f;
    cameraComponent.farClip = 75.f;
    auto cameraParent = cameraScene.CreateEntity("camera parent");
    auto& parentTransform = cameraParent.GetComponent<LuxEngine::TransformComponent>();
    parentTransform.position = {10, 2, 0};
    parentTransform.rotation.y = LuxEngine::ToRadians(90);
    parentTransform.scale = {-2, 3, -4};
    camera.GetComponent<LuxEngine::TransformComponent>().position.z = 5;
    Check(cameraScene.SetParent(camera.GetId(), cameraParent.GetId(), false),
          "parented camera hierarchy configures");
    cameraView = LuxEngine::ResolveScenePlayerCamera(cameraScene, {});
    Check(Near(cameraView.position.x, -10) && Near(cameraView.position.y, 2) &&
              cameraView.target.x > cameraView.position.x + .99f && Near(cameraView.up.y, 1),
          "primary camera uses world -Z/+Y axes under a rotated, negatively scaled parent");
    camera.GetComponent<LuxEngine::TransformComponent>().rotation.z = LuxEngine::ToRadians(90);
    cameraView = LuxEngine::ResolveScenePlayerCamera(cameraScene, {});
    Check(cameraView.up.z < -.99f, "camera roll changes its actual view up axis");
    parentTransform.scale = {0, 0, 0};
    cameraView = LuxEngine::ResolveScenePlayerCamera(cameraScene, {});
    const auto finite = [](LuxEngine::Vec3 value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    };
    const auto forward = cameraView.target - cameraView.position;
    Check(finite(cameraView.position) && finite(cameraView.target) && finite(cameraView.up) &&
              forward.Dot(forward) > .9f &&
              forward.Cross(cameraView.up).Dot(forward.Cross(cameraView.up)) > .9f,
          "collapsed camera axes receive a finite, non-parallel view basis");
}

// Writes a small platformer scene for the LuxPlayer smoke test.
void WriteScene(const std::string& path) {
    LuxEngine::Scene scene;
    auto ground = scene.CreateEntity("땅");
    auto& groundTransform = ground.GetComponent<LuxEngine::TransformComponent>();
    groundTransform.position = {0, -3, 0};
    groundTransform.scale = {12, 1, 1};
    ground.AddComponent<LuxEngine::SpriteRendererComponent>().color = {0.3f, 0.6f, 0.3f, 1};
    ground.AddComponent<LuxEngine::Rigidbody2DComponent>().type = LuxEngine::BodyType::Static;
    ground.AddComponent<LuxEngine::BoxCollider2DComponent>();
    auto player = scene.CreateEntity("플레이어");
    player.GetComponent<LuxEngine::TransformComponent>().position = {0, 1, 0};
    player.AddComponent<LuxEngine::SpriteRendererComponent>().color = {0.9f, 0.7f, 0.2f, 1};
    player.AddComponent<LuxEngine::Rigidbody2DComponent>();
    player.AddComponent<LuxEngine::BoxCollider2DComponent>();
    auto map = scene.CreateEntity("타일맵");
    map.GetComponent<LuxEngine::TransformComponent>().position = {-6, -6, 0};
    auto& tiles = map.AddComponent<LuxEngine::TilemapComponent>();
    tiles.Resize(12, 2);
    for (LuxEngine::uint32 x = 0; x < 12; ++x)
        tiles.Set(x, 0, 2);
    auto cube3D = scene.CreateEntity("3D 배경 큐브");
    cube3D.GetComponent<LuxEngine::TransformComponent>().position = {2.5f, 1.5f, -2.0f};
    cube3D.AddComponent<LuxEngine::MeshRendererComponent>().material.albedoColor = {0.4f, 0.7f,
                                                                                    0.9f, 1.0f};
    Check(scene.Serialize(path), "smoke scene written");
    LuxEngine::Scene loaded;
    Check(loaded.Deserialize(path) && loaded.GetEntityCount() == 4, "smoke scene reloads");
}

int CameraClippingGpu(const std::filesystem::path& directory) {
    LuxEngine::Scene scene;
    auto settings = scene.GetSettings();
    settings.clearColor = {.01f, .02f, .04f, 1};
    Check(scene.SetSettings(settings), "camera clip GPU scene settings configure");
    auto camera = scene.CreateEntity("Custom clipping camera");
    auto& component = camera.AddComponent<LuxEngine::CameraComponent>();
    component.isPerspective = true;
    component.fov = 60;
    component.nearClip = 3;
    component.farClip = 5;
    const std::array<LuxEngine::Vec3, 3> positions{{{-.85f, 0, -2}, {0, 0, -4}, {3.4f, 0, -8}}};
    for (size_t index = 0; index < positions.size(); ++index) {
        auto object = scene.CreateEntity("Clip test cube");
        auto& transform = object.GetComponent<LuxEngine::TransformComponent>();
        transform.position = positions[index];
        transform.scale = {.5f, .5f, .5f};
        object.AddComponent<LuxEngine::MeshRendererComponent>().material.albedoColor =
            index == 1 ? LuxEngine::Vec4{.15f, .85f, .2f, 1} : LuxEngine::Vec4{.9f, .1f, .1f, 1};
    }
    const auto file = directory / "camera-clipping.lux";
    const auto encoded = file.u8string();
    const std::string path(reinterpret_cast<const char*>(encoded.data()), encoded.size());
    Check(scene.Serialize(path), "camera clipping scene saves");
    LuxEngine::ScenePlayerOptions options;
    options.scenePath = path;
    options.width = 320;
    options.height = 240;
    LuxEngine::ScenePlayer player(options);
    if (!player.Initialize()) {
        std::cout << "SKIP: scene player camera GPU/window backend is unavailable\n";
        return 77;
    }
    Check(player.LoadedScene(), "runtime custom camera scene loads");
    const auto frame = [&] {
        SDL_Delay(18);
        const auto status = player.RunFrame();
        Check(status == LuxEngine::Application::FrameResult::Continue ||
                  status == LuxEngine::Application::FrameResult::Skipped,
              "runtime custom camera keeps rendering");
    };
    frame();
    Check(player.GetGpuDevice().RequestFrameCapture(), "runtime camera capture queues");
    frame();
    LuxEngine::GpuFrameCapture image;
    Check(player.GetGpuDevice().ReadFrameCapture(image) && image.width >= 32 && image.height >= 24,
          "runtime camera capture completes");
    const auto pixel = [&](float x) {
        const auto offset = (static_cast<size_t>(image.height / 2) * image.width +
                             static_cast<size_t>(x * image.width)) *
                            4;
        return std::array<int, 3>{image.rgba.at(offset), image.rgba.at(offset + 1),
                                  image.rgba.at(offset + 2)};
    };
    const auto center = pixel(.5f), near = pixel(.224f), far = pixel(.776f);
    Check(center[1] > center[0] + 20 && center[1] > center[2] + 20,
          "runtime custom camera renders geometry between the configured clipping planes");
    Check(near[0] < near[1] + 15 && far[0] < far[1] + 15,
          "actual runtime projection ignores custom near/far clipping settings");
    player.Close();
    player.RunFrame();
    return 0;
}

int SceneManagementGpu() {
    namespace fs = std::filesystem;

    struct Files {
        fs::path root = fs::temp_directory_path() / fs::path(u8"lux-runtime-씬") /
                        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());

        Files() {
            fs::create_directories(root);
        }

        ~Files() {
            std::error_code error;
            fs::remove_all(root, error);
        }
    } files;

    if (const int result = CameraClippingGpu(files.root); result != 0)
        return result;

    const auto text = [](const fs::path& path) {
        const auto bytes = path.u8string();
        return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    };
    LuxEngine::Scene source;
    auto sprite = source.CreateEntity("스트리밍 스프라이트");
    sprite.GetComponent<LuxEngine::TransformComponent>().scale = {2, 2, 1};
    sprite.AddComponent<LuxEngine::SpriteRendererComponent>().color = {.2f, .8f, .3f, 1};
    auto settings = source.GetSettings();
    settings.clearColor = {.04f, .08f, .16f, 1};
    settings.exposure = .5f;
    Check(source.SetSettings(settings), "runtime source settings are valid");
    const auto base = files.root / "base.lux", composed = files.root / "composed.lux";
    Check(source.Serialize(text(base)), "runtime source file serializes");
    LuxEngine::SceneManager manager;
    LuxEngine::Scene target;
    std::string error;
    const auto root =
        manager.AddSceneInstance(target, base, LuxEngine::SceneReferenceMode::Inherited, error);
    Check(root != 0, "runtime inherited scene composes");
    Check(manager.ConfigureStreaming(target, root, {0, 0}, 5, 8, error) &&
              manager.UnloadInstance(target, root, error),
          "runtime source starts as an unloaded streaming region");
    Check(LuxEngine::SceneManager::Save(target, composed, {}, error),
          "runtime composed file saves with portable paths");
    Check(manager.Load(composed).scene != nullptr,
          "runtime composed fixture is valid before GPU startup");
    LuxEngine::ScenePlayerOptions options;
    options.scenePath = text(composed);
    options.width = 320;
    options.height = 240;
    options.view = LuxEngine::ScenePlayerView{0, 0, 4.5f};
    LuxEngine::ScenePlayer player(options);
    if (!player.Initialize()) {
        std::cout << "SKIP: scene player GPU/window backend is unavailable\n";
        return 77;
    }
    Check(player.LoadedScene(), "runtime player opens composed scenes");
    const auto frame = [&] {
        SDL_Delay(18);
        const auto result = player.RunFrame();
        Check(result == LuxEngine::Application::FrameResult::Continue ||
                  result == LuxEngine::Application::FrameResult::Skipped,
              "runtime composed scene keeps rendering");
    };
    bool sawSprite = false;
    const auto deadline = SDL_GetTicks() + 5000;
    LuxEngine::GpuFrameCapture image;
    do {
        frame();
        Check(player.GetGpuDevice().RequestFrameCapture(), "runtime frame capture queues");
        frame();
        Check(player.GetGpuDevice().ReadFrameCapture(image), "runtime frame capture completes");
        Check(image.width > 2 && image.height > 2 &&
                  image.rgba.size() == static_cast<size_t>(image.width) * image.height * 4,
              "runtime frame capture contains a complete image");
        const auto middle =
            (static_cast<size_t>(image.height / 2) * image.width + image.width / 2) * 4;
        sawSprite = image.rgba[middle + 1] > image.rgba[middle] + 20 &&
                    image.rgba[middle + 1] > image.rgba[middle + 2] + 20;
    } while (!sawSprite && SDL_GetTicks() < deadline);
    Check(sawSprite, "runtime player streams inherited scene entities into the actual GPU frame");
    const size_t edge = (static_cast<size_t>(image.width) + 1) * 4;
    Check(image.rgba[edge + 2] > image.rgba[edge + 1] + 10 &&
              image.rgba[edge + 1] > image.rgba[edge] + 3,
          "scene-specific environment color reaches the actual runtime framebuffer");
    player.Close();
    player.RunFrame();
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    try {
        const std::string_view suite = argc > 1 ? argv[1] : "parse";
        if (suite == "parse")
            Parse();
        else if (suite == "fit")
            Fit();
        else if (suite == "write-scene" && argc > 2)
            WriteScene(argv[2]);
        else if (suite == "scene-management-gpu")
            return SceneManagementGpu();
        else
            throw std::runtime_error("Unknown suite");
        std::cout << suite << ": " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
}
