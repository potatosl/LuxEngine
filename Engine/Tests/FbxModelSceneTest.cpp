#include <LuxEngine/Assets/ModelAsset.hpp>
#include <LuxEngine/Scene/Entity.hpp>
#include <LuxEngine/Scene/SceneManager.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace LuxEngine;
namespace fs = std::filesystem;

void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

bool Near(double left, double right) {
    return std::abs(left - right) < .001;
}

void Run(const fs::path& fixture, const fs::path& directory) {
    std::string error;
    auto model = ModelAsset::LoadFbx(fixture.string(), error);
    Check(model != nullptr && !model->animations.empty(), error.c_str());
    const auto node = model->submeshes.at(0).bones.at(0).node;
    Scene scene;
    auto entity = scene.CreateEntity("FBX Bone");
    auto& animation = entity.AddComponent<ModelAnimationComponent>();
    animation.modelPath = fixture.string();
    animation.nodeIndex = node;
    animation.clip = 0;
    animation.playing = true;
    auto& mesh = entity.AddComponent<MeshRendererComponent>();
    mesh.meshPath = fixture.string();
    mesh.modelSubmesh = 0;
    scene.OnUpdate(.25);
    Check(Near(animation.time, .25) &&
              Near(entity.GetComponent<TransformComponent>().position.x, .5),
          "FBX scene runtime did not advance the clip and apply its node pose");
    animation.loop = false;
    scene.OnUpdate(2);
    Check(!animation.playing && Near(animation.time, 1) &&
              Near(entity.GetComponent<TransformComponent>().position.x, 2),
          "FBX non-looping animation did not stop at the endpoint");
    animation.loop = true;
    animation.playing = true;
    animation.speed = -1;
    animation.time = .25;
    scene.OnUpdate(.5);
    Check(Near(animation.time, .75), "FBX reverse playback did not wrap correctly");
    const auto position = entity.GetComponent<TransformComponent>().position.x;
    animation.applyNodeTransform = false;
    scene.OnUpdate(.1);
    Check(Near(entity.GetComponent<TransformComponent>().position.x, position),
          "FBX animation overwrote a transform with applyNodeTransform disabled");
    animation.clip = -1;
    animation.applyNodeTransform = true;
    entity.GetComponent<TransformComponent>().position.x = 42;
    scene.OnUpdate(.1);
    Check(Near(entity.GetComponent<TransformComponent>().position.x, 42),
          "Static imported FBX node reset a runtime-authored transform to the bind pose");
    animation.clip = 0;
    animation.applyNodeTransform = false;
    std::string data;
    Check(scene.SerializeToString(data), "FBX model scene could not be serialized");
    Scene restored;
    Check(restored.DeserializeFromString(data), "FBX model scene could not be restored");
    const auto& restoredAnimation = restored.GetComponent<ModelAnimationComponent>(entity.GetId());
    Check(restored.GetComponent<MeshRendererComponent>(entity.GetId()).modelSubmesh == 0 &&
              restoredAnimation.nodeIndex == node && restoredAnimation.clip == 0 &&
              Near(restoredAnimation.time, animation.time) && restoredAnimation.speed == -1 &&
              !restoredAnimation.applyNodeTransform,
          "FBX model selection or animation state was lost in serialization");
    const auto copies = restored.ImportEntities(scene, {entity.GetId()});
    const auto copy = copies.find(entity.GetId());
    Check(copy != copies.end() && restored.HasComponent<ModelAnimationComponent>(copy->second),
          "FBX animation was not copied during entity duplication");
    animation.time = std::numeric_limits<double>::infinity();
    Check(!scene.SerializeToString(data), "Non-finite FBX animation time was serialized");
    animation.time = .5;
    mesh.modelSubmesh = -2;
    Check(!scene.SerializeToString(data), "Invalid FBX submesh selector was serialized");
    mesh.modelSubmesh = 0;
    fs::create_directories(directory);
    const auto saved = directory / "saved.lux";
    Check(SceneManager::Save(scene, saved, {}, error), error.c_str());
    SceneManager manager;
    auto loaded = manager.Load(saved, error);
    Check(loaded != nullptr, error.c_str());
    const auto& loadedAnimation = loaded->GetComponent<ModelAnimationComponent>(entity.GetId());
    Check(fs::equivalent(fs::path(loadedAnimation.modelPath), fixture),
          "Scene Save As/reload changed the FBX model animation asset path");
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Expected the authored AnimatedTriangle.fbx fixture path\n";
        return 1;
    }
    const auto directory =
        std::filesystem::temp_directory_path() /
        ("lux-fbx-scene-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        Run(std::filesystem::absolute(argv[1]), directory);
        std::filesystem::remove_all(directory);
        std::cout << "FBX scene animation/serialization/relocation checks passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::filesystem::remove_all(directory);
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
