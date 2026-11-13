#include <LuxEngine/Scene/Entity.hpp>
#include <LuxEngine/Scene/Scene.hpp>
#include <LuxEngine/Scripting/ScriptRuntime.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace LuxEngine;

namespace {
void Check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}

void CheckRuntime(bool success, const ScriptRuntime& runtime) {
    if (!success)
        throw std::runtime_error(runtime.GetLastError());
}
} // namespace

int main(int argc, char** argv) {
    try {
        Check(argc == 2, "Script test assembly is required");
        Scene scene;
        auto entity = scene.CreateEntity("C# regression");
        entity.AddComponent<ScriptComponent>() = {"ScriptTests.Mover", true};
        std::string serialized;
        Check(scene.SerializeToString(serialized), "Script component serialization failed");
        Scene restored;
        Check(restored.DeserializeFromString(serialized), "Script component restore failed");
        Check(restored.GetComponentEntityIds<ScriptComponent>().size() == 1,
              "Script component was lost during serialization");

        struct CustomComponent {
            int value;
        };

        entity.AddComponent<CustomComponent>(42);
        Check(entity.GetComponent<CustomComponent>().value == 42,
              "Custom ECS types were restricted");
        ScriptRuntime runtime;
        ScriptRuntimeConfig config;
        config.assemblyPath = argv[1];
        CheckRuntime(runtime.Initialize(scene, config), runtime);
        CheckRuntime(runtime.Update(.25), runtime);
        auto& transform = entity.GetComponent<TransformComponent>();
        Check(std::abs(transform.position.x - .25f) < .0001f && transform.position.y == 10,
              "Managed lifecycle or native transform bridge failed");
        bool accepted = true;
        std::thread worker([&] { accepted = runtime.Update(.25); });
        worker.join();
        Check(!accepted && transform.position.x == .25f, "Off-thread scene access was accepted");
        entity.GetComponent<ScriptComponent>().enabled = false;
        CheckRuntime(runtime.Update(.25), runtime);
        Check(transform.position.z == 1, "Disabled script was not destroyed");
        entity.GetComponent<ScriptComponent>() = {"ScriptTests.Throwing", true};
        Check(!runtime.Update(.25) &&
                  runtime.GetLastError().find("managed regression exception") != std::string::npos,
              "Managed exception was not contained and reported");
        runtime.Shutdown();
        Check(!runtime.IsRunning(), "Script world was not stopped");
        entity.GetComponent<ScriptComponent>() = {"ScriptTests.Mover", true};
        Check(runtime.Initialize(scene, config) && runtime.Update(.25),
              "Script world did not reload");
        scene.DestroyEntity(entity);
        Check(runtime.Update(.25), "Destroyed entity was retained by a script");
        runtime.Shutdown();
        {
            Scene temporary;
            Check(runtime.Initialize(temporary, config), "Temporary world failed to start");
        }
        Check(!runtime.Update(.25) && !runtime.IsRunning(),
              "Expired scene lifetime was not rejected");
        config.assemblyPath += ".missing";
        Check(!runtime.Initialize(scene, config), "Missing assembly was accepted");
        std::cout
            << "C# lifecycle, transform, exception, lifetime and serialization checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
