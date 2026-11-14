#include <LuxEngine/Scene/Entity.hpp>
#include <LuxEngine/Scene/Scene.hpp>
#include <cmath>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>

using namespace LuxEngine;
using Json = nlohmann::ordered_json;

namespace {
void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

void Test() {
    Scene scene;
    auto root = scene.CreateEntity("한국어 \"이름\"\n둘째 줄");
    auto child = scene.CreateEntity("자식");
    Check(scene.SetParent(child.GetId(), root.GetId()), "hierarchy setup");
    auto& transform = root.GetComponent<TransformComponent>();
    transform.position = {1.25f, -2.5f, 3};
    transform.rotation = {.1f, .2f, .3f};
    root.AddComponent<SpriteRendererComponent>().color = {.1f, .2f, .3f, .4f};
    auto& mesh = root.AddComponent<MeshRendererComponent>();
    mesh.meshPath = "Assets/메시.obj";
    mesh.material.baseColorTexture = "Assets/색상.png";
    mesh.material.normalTexture = "Assets/normal.png";
    mesh.material.metallicTexture = "Assets/metal.png";
    mesh.material.roughnessTexture = "Assets/rough.png";
    mesh.material.occlusionTexture = "Assets/ao.png";
    mesh.material.normalScale = 2;
    mesh.material.occlusionStrength = .75f;
    mesh.material.alphaMode = MaterialAlphaMode::Blend;
    mesh.material.alphaCutoff = .4f;
    mesh.material.doubleSided = false;
    mesh.material.textureSampling = {true, TextureAddressMode::Repeat, true, false};
    auto& camera = root.AddComponent<CameraComponent>();
    camera.isPerspective = true;
    camera.nearClip = .25f;
    camera.farClip = 2500.f;
    root.AddComponent<Rigidbody2DComponent>().velocity = {2, -3};
    root.AddComponent<BoxCollider2DComponent>().isTrigger = true;
    root.AddComponent<Rigidbody3DComponent>().velocity = {4, 5, 6};
    root.AddComponent<BoxCollider3DComponent>();
    auto& map = root.AddComponent<TilemapComponent>();
    Check(map.Resize(2, 2) && map.Set(1, 1, 5), "tilemap setup");
    auto& animation = root.AddComponent<AnimationComponent>();
    animation.clip.name = "걷기";
    animation.clip.AddKeyframe({.time = .25f, .position = {1, 2, 3}});
    auto& reference = root.AddComponent<SceneReferenceComponent>();
    reference.path = "Scenes/원본.lux";
    reference.baseline = "LuxEngineScene 2 0\n";
    reference.entityMap = {{9, child.GetId()}};
    child.AddComponent<InstanceSourceComponent>() = {root.GetId(), 9};
    auto settings = scene.GetSettings();
    settings.render3D.mode = Render3DMode::Pbr;
    settings.render3D.environmentPath = "Assets/환경.hdr";
    Check(scene.SetSettings(settings), "render settings setup");
    std::string text;
    Check(scene.SerializeToString(text), "JSON serialization succeeds");
    const auto json = Json::parse(text);
    Check(json.at("format") == "LuxEngine.Scene" && json.at("version") == 1,
          "explicit scene schema");
    Check(json["entities"][0]["components"]["transform"]["position"][0] == 1.25,
          "named typed transform field");
    Check(json["entities"][0]["components"]["camera"]["near_clip"] == .25 &&
              json["entities"][0]["components"]["camera"]["far_clip"] == 2500,
          "camera clipping settings are saved as named numeric fields");
    Scene restored;
    Check(restored.DeserializeFromString(text), "all component types load");
    std::string second;
    Check(restored.SerializeToString(second) && second == text, "deterministic lossless roundtrip");
    const auto& loadedCamera = restored.GetComponent<CameraComponent>(root.GetId());
    Check(loadedCamera.nearClip == .25f && loadedCamera.farClip == 2500.f,
          "camera clipping settings survive scene roundtrip");
    auto previousCameraJson = json;
    previousCameraJson["entities"][0]["components"]["camera"].erase("near_clip");
    previousCameraJson["entities"][0]["components"]["camera"].erase("far_clip");
    Scene previousCameraScene;
    Check(previousCameraScene.DeserializeFromString(previousCameraJson.dump()) &&
              previousCameraScene.GetComponent<CameraComponent>(root.GetId()).nearClip == .1f &&
              previousCameraScene.GetComponent<CameraComponent>(root.GetId()).farClip == 1000.f,
          "scenes saved before camera clip settings retain the original projection defaults");
    Check(restored.GetComponent<InstanceSourceComponent>(child.GetId()).sourceEntity == 9,
          "scene instance identity survives");
    Entity retained(root.GetId(), &restored);
    const auto reject = [&](const std::string& invalid) {
        Check(!restored.DeserializeFromString(invalid), "invalid JSON is rejected");
        std::string after;
        Check(retained.IsValid() && restored.SerializeToString(after) && after == text,
              "failed load preserves scene, IDs, and live handles");
    };
    const auto mutate = [&](auto fn) {
        auto bad = json;
        fn(bad);
        reject(bad.dump());
    };

    for (const auto* key : {"alpha_mode", "alpha_cutoff", "normal_scale", "occlusion_strength"})
        mutate([key](auto& j) { j["entities"][0]["components"]["mesh"][key] = -1; });
    mutate([](auto& j) { j["entities"][0]["components"]["mesh"]["alpha_mode"] = 3; });
    for (const auto near : {0.0, -1.0, 2500.0, 3000.0})
        mutate([near](auto& j) { j["entities"][0]["components"]["camera"]["near_clip"] = near; });
    mutate([](auto& j) { j["entities"][0]["components"]["camera"]["far_clip"] = .1; });
    mutate([](auto& j) { j["entities"][0]["components"]["camera"]["far_clip"] = "1000"; });
    mutate([](auto& j) { j["entities"][0]["components"]["camera"]["near_clip"] = nullptr; });
    const auto validFar = camera.farClip;
    camera.farClip = std::numeric_limits<float>::infinity();
    Check(!scene.SerializeToString(second), "non-finite camera clipping values cannot be saved");
    camera.farClip = validFar;
    const auto validNear = camera.nearClip;
    camera.nearClip = 1e20f;
    camera.farClip = 1e21f;
    Check(scene.SerializeToString(second), "large representable camera clip ranges can be saved");
    camera.farClip = std::numeric_limits<float>::max();
    camera.nearClip = std::nextafter(camera.farClip, 0.0f);
    Check(!scene.SerializeToString(second),
          "unrepresentable perspective clip matrices cannot be saved");
    camera.nearClip = validNear;
    camera.farClip = validFar;
    mutate([](auto& j) {
        const float far = std::numeric_limits<float>::max();
        j["entities"][0]["components"]["camera"]["near_clip"] = std::nextafter(far, 0.0f);
        j["entities"][0]["components"]["camera"]["far_clip"] = far;
    });
    mutate([](auto& j) { j["entities"][0]["components"]["mesh"]["sampling"]["address"] = 2; });
    mutate([](auto& j) { j["entities"][0]["components"]["mesh"]["sampling"]["mipmaps"] = 1; });
    mutate([](auto& j) { j["entities"][0]["components"]["mesh"]["double_sided"] = 1; });
    mutate([](auto& j) {
        j["entities"][0]["components"]["mesh"]["normal_texture"] = std::string(4097, 'x');
    });
    mutate([](auto& j) { j["version"] = 2; });
    mutate([](auto& j) { j["version"] = true; });
    mutate([](auto& j) { j["entities"][0]["id"] = 1.5; });
    mutate([](auto& j) { j["entities"][0]["id"] = -1; });
    mutate([](auto& j) { j["entities"][0]["id"] = 4294967297ull; });
    mutate([](auto& j) { j["entities"][1]["id"] = j["entities"][0]["id"]; });
    mutate([](auto& j) { j["entities"][0]["components"]["future_component"] = Json::object(); });
    mutate([](auto& j) {
        j["entities"][0]["components"]["transform"]["scale"] = Json::array({1, 2});
    });
    mutate([](auto& j) { j["settings"]["override_gravity"] = 1; });
    mutate([](auto& j) { j["entities"][0]["components"]["tilemap"]["tiles"][0] = 256; });
    mutate(
        [](auto& j) { j["entities"][0]["components"]["animation"]["keyframes"][0]["time"] = -1; });
    mutate([](auto& j) {
        j["entities"][0]["components"]["hierarchy"]["parent"] = j["entities"][1]["id"];
    });
    reject("{\"format\":\"LuxEngine.Scene\",\"format\":\"LuxEngine.Scene\"}");
    reject(text + " trailing");
    reject(std::string("{\"deep\":") + std::string(100, '[') + "0" + std::string(100, ']') + "}");
    Scene old;
    Check(old.DeserializeFromString("LuxEngineScene 1 0\n"), "legacy v1 reads");
    Check(old.DeserializeFromString("LuxEngineScene 2 0\n"), "legacy v2 reads");
    Check(old.SerializeToString(second) && Json::parse(second).at("format") == "LuxEngine.Scene",
          "legacy scene next save upgrades to JSON");
}
} // namespace

int main() {
    try {
        Test();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
