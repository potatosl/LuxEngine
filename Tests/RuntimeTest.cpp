// Tests for the Runtime library: LuxPlayer argument parsing, view fitting and a scene file for
// the LuxPlayer smoke test.
#include "Engine/ECS/ECS.hpp"
#include "Engine/Physics/PhysicsEngine.hpp"
#include "Engine/Scene/Entity.hpp"
#include "Runtime/ScenePlayer.hpp"
#include <cmath>
#include <iostream>
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
    const std::vector<Args> invalid = {
        {},
        {"--title", "x"},
        {"a.lux", "b.lux"},
        {"a.lux", "--size", "10", "600"},
        {"a.lux", "--size", "800"},
        {"a.lux", "--view", "0", "0", "0"},
        {"a.lux", "--view", "0", "0", "nan"},
        {"a.lux", "--frames", "-1"},
        {"a.lux", "--unknown"},
    };
    for (const auto& args : invalid) {
        error.clear();
        Check(!ParseScenePlayerArguments(args, error) && !error.empty(),
              "invalid arguments rejected");
    }
}

void Fit() {
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
        else
            throw std::runtime_error("Unknown suite");
        std::cout << suite << ": " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
}
