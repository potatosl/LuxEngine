#include <LuxEngine/Scene/Entity.hpp>
#include <LuxEngine/Scene/Scene.hpp>
#include <LuxEngine/Scripting/ScriptRuntime.hpp>
#include <cmath>
#include <iostream>
#include <limits>
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
        Scene authored;
        auto target = authored.CreateEntity("Target");
        auto scripted = authored.CreateEntity("Fields");
        ScriptComponent component{"ScriptTests.Fields", true};
        component.fields = {
            {"Health", int32{7}},      {"Speed", 3.5f},
            {"Enabled", true},         {"Title", std::string("필드 회귀")},
            {"Offset", Vec3{1, 2, 3}}, {"Tint", Vec4{1, .5f, .25f, 1}},
            {"window", Vec2{8, 9}},    {"Target", ScriptEntityReference{target.GetId()}}};
        scripted.AddComponent<ScriptComponent>(component);
        std::string fieldScene;
        Check(authored.SerializeToString(fieldScene), "Script fields did not serialize");
        Scene restoredFields;
        Check(restoredFields.DeserializeFromString(fieldScene),
              "Script fields did not deserialize");
        CheckRuntime(runtime.Initialize(restoredFields, config), runtime);
        std::vector<ScriptFieldDescription> descriptions;
        CheckRuntime(runtime.DescribeFields("ScriptTests.Fields", descriptions), runtime);
        Check(descriptions.size() == 8, "Public or attributed private fields were not reflected");
        std::vector<ScriptFieldDescription> single;
        CheckRuntime(runtime.DescribeFields("ScriptTests.MetadataOnce", single), runtime);
        Check(single.size() == 1 && std::get<std::string>(single.front().defaultValue) == "x",
              "Metadata inspection must construct the script only once");
        Check(!runtime.DescribeFields("ScriptTests.LargeMetadata", single) &&
                  runtime.GetLastError().find("65536-byte UTF-8 limit") != std::string::npos &&
                  single.size() == 1 && std::get<std::string>(single.front().defaultValue) == "x",
              "Oversized metadata must report its limit and preserve prior metadata");
        Check(!runtime.DescribeFields("ScriptTests.UnsupportedField", descriptions) &&
                  runtime.GetLastError().find("Unsupported serialized script field") !=
                      std::string::npos,
              "Unsupported field type was not rejected");
        CheckRuntime(runtime.Update(0), runtime);
        const auto fieldTransform =
            restoredFields.GetComponent<TransformComponent>(scripted.GetId());
        Check(fieldTransform.position.x == 7 && fieldTransform.position.y == 3.5f &&
                  fieldTransform.position.z == static_cast<float>(target.GetId()),
              "Authored fields or entity reference were not applied before creation");
        runtime.Shutdown();
        Scene imported;
        imported.CreateEntity("Existing");
        const auto mapping = imported.ImportEntities(authored, {});
        Check(mapping.size() == 2 &&
                  std::get<ScriptEntityReference>(
                      imported.GetComponent<ScriptComponent>(mapping.at(scripted.GetId()))
                          .fields.at("Target"))
                          .id == mapping.at(target.GetId()),
              "Imported script entity references were not remapped");
        auto& importedFields =
            imported.GetComponent<ScriptComponent>(mapping.at(scripted.GetId())).fields;
        importedFields["Health"] = std::string("wrong type");
        CheckRuntime(runtime.Initialize(imported, config), runtime);
        Check(!runtime.Update(0) &&
                  runtime.GetLastError().find("type changed") != std::string::npos,
              "A mismatched authored field type reached OnCreate");
        runtime.Shutdown();
        ScriptFieldMap invalidFields;
        Check(!DeserializeScriptFields("{\"bad\":{\"type\":4294967296,\"value\":true}}",
                                       invalidFields),
              "Overflowed field kind was accepted");
        std::string invalidOutput;
        Check(!SerializeScriptFields({{"bad", std::numeric_limits<float>::infinity()}},
                                     invalidOutput),
              "Non-finite authored value was accepted");
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
