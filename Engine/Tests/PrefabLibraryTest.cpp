#include <LuxEngine/Scene/Entity.hpp>
#include <LuxEngine/Scene/PrefabLibrary.hpp>
#include <LuxEngine/Scene/SceneManager.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
using namespace LuxEngine;
namespace fs = std::filesystem;

void Check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}

void Check(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}

std::string Utf8(const fs::path& path) {
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

std::string Read(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::string Snapshot(Scene& scene) {
    std::string value;
    Check(scene.SerializeToString(value), "Scene snapshot failed");
    return value;
}

uint32 Local(const Scene& scene, uint32 root, uint32 source) {
    for (const auto& [id, local] : scene.GetComponent<SceneReferenceComponent>(root).entityMap)
        if (id == source)
            return local;
    return 0;
}

struct Files {
    fs::path root = fs::weakly_canonical(fs::temp_directory_path()) /
                    ("lux-prefabs-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

    Files() {
        fs::create_directories(root / "bundle" / "prefabs");
    }

    ~Files() {
        Scene::ClearSaveFaults();
        std::error_code error;
        fs::remove_all(root, error);
    }
};

void RoundTrip(const fs::path& directory) {
    const auto bundle = directory / "bundle";
    const auto assetPath = bundle / "prefabs" / fs::path(u8"나무 공백.luxprefab");
    Scene source;
    source.SetSourcePath(Utf8(bundle / "source.lux"));
    auto outside = source.CreateEntity("Outside");
    outside.GetComponent<TransformComponent>().position.x = 5;
    auto authored = source.CreateEntity("Tree");
    const auto authoredId = authored.GetId();
    authored.GetComponent<TransformComponent>().position.x = 2;
    auto leaf = source.CreateEntity("Leaf");
    const auto leafId = leaf.GetId();
    auto branch = source.CreateEntity("Branch");
    const auto branchId = branch.GetId();
    Check(source.SetParent(authoredId, outside.GetId(), false) &&
              source.SetParent(leafId, authoredId, false) &&
              source.SetParent(branchId, authoredId, false),
          "Source hierarchy setup failed");
    authored.AddComponent<ScriptComponent>().fields["target"] = ScriptEntityReference{leafId};
    authored.GetComponent<ScriptComponent>().fields["speed"] = 1.0f;
    authored.GetComponent<ScriptComponent>().fields["optional"] = ScriptEntityReference{};
    leaf.AddComponent<ScriptComponent>().fields["external"] =
        ScriptEntityReference{outside.GetId()};
    std::string error;
    Check(PrefabLibrary::Save(source, authoredId, assetPath, error), error);
    Scene file;
    Check(file.Deserialize(Utf8(assetPath)) && file.IsAlive(authoredId) && file.IsAlive(branchId) &&
              !file.IsAlive(outside.GetId()) &&
              file.GetChildren(0) == std::vector<uint32>{authoredId},
          "Prefab export must preserve source IDs and include only the selected hierarchy");
    Check(file.GetComponent<TransformComponent>(authoredId).position.x == 7,
          "Exporting below a parent must preserve the root world pose");
    Check(std::get<ScriptEntityReference>(
              file.GetComponent<ScriptComponent>(leafId).fields.at("external"))
                  .id == 0,
          "References outside the exported hierarchy must be cleared");
    const auto original = Read(assetPath);
    Scene::InjectSaveFault(Scene::SaveFault::TempWriteFailure);
    Check(!PrefabLibrary::Save(source, authoredId, assetPath, error) && Read(assetPath) == original,
          "A failed prefab save must retain the original file");
    Scene::ClearSaveFaults();

    Scene target;
    target.SetSourcePath(Utf8(bundle / "main.lux"));
    const auto externalTarget = target.CreateEntity("Existing").GetId();
    const auto first = PrefabLibrary::Instantiate(target, assetPath, error);
    Check(first != 0, error);
    const auto second = PrefabLibrary::Instantiate(target, assetPath, error);
    Check(second && first != second &&
              Local(target, first, authoredId) != Local(target, second, authoredId),
          "Instances must own independent local entities");
    Check(!PrefabLibrary::Save(target, first, assetPath, error) && Read(assetPath) == original,
          "Saving an instance over its connected source must not renumber source identities");
    const auto firstAuthored = Local(target, first, authoredId);
    const auto firstLeaf = Local(target, first, leafId);
    Check(std::get<ScriptEntityReference>(
              target.GetComponent<ScriptComponent>(firstAuthored).fields.at("target"))
                  .id == firstLeaf,
          "Prefab script references must map to their own instance");
    Check(PrefabLibrary::FindInstanceRoot(target, firstLeaf) == first,
          "Selecting a descendant must identify its prefab container");
    target.GetComponent<TagComponent>(firstLeaf).tag = "Local leaf";
    source.GetComponent<TagComponent>(leafId).tag = "Updated leaf";
    source.GetComponent<TagComponent>(branchId).tag = "Updated branch";
    source.GetComponent<ScriptComponent>(authoredId).fields["speed"] = 2.0f;
    Check(PrefabLibrary::Save(source, authoredId, assetPath, error), error);
    Check(PrefabLibrary::Refresh(target, first, error), error);
    const auto firstBranch = Local(target, first, branchId);
    Check(
        std::get<float>(target.GetComponent<ScriptComponent>(firstAuthored).fields.at("speed")) ==
                2.0f &&
            std::get<ScriptEntityReference>(
                target.GetComponent<ScriptComponent>(firstAuthored).fields.at("target"))
                    .id == firstLeaf,
        "Local script entity IDs must not be mistaken for a field override during source refresh");
    Check(target.GetComponent<TagComponent>(firstLeaf).tag == "Local leaf" &&
              target.GetComponent<TagComponent>(firstBranch).tag == "Updated branch",
          "Refresh must preserve local entity overrides and propagate untouched source entities");
    const auto secondScripted = Local(target, second, authoredId);
    target.GetComponent<ScriptComponent>(secondScripted).fields["optional"] =
        ScriptEntityReference{externalTarget};
    Check(PrefabLibrary::Refresh(target, second, error), error);
    Check(
        std::get<ScriptEntityReference>(
            target.GetComponent<ScriptComponent>(secondScripted).fields.at("optional"))
                .id == externalTarget,
        "A scene-external reference override must not be normalized to the source null reference");
    auto local = target.CreateEntity("Local addition");
    const auto localId = local.GetId();
    Check(target.SetParent(localId, first, false), "Local addition setup failed");
    target.GetComponent<TransformComponent>(first).position.z = 123;
    Check(PrefabLibrary::Revert(target, first, error), error);
    Check(Local(target, first, authoredId) == firstAuthored &&
              Local(target, first, leafId) == firstLeaf &&
              Local(target, first, branchId) == firstBranch && !target.IsAlive(localId) &&
              target.GetComponent<TagComponent>(firstLeaf).tag == "Updated leaf" &&
              target.GetComponent<TransformComponent>(first).position.z == 123,
          "Revert must preserve live instance IDs and placement while restoring source data");
    target.DestroyEntity(Entity(firstLeaf, &target));
    Check(PrefabLibrary::Refresh(target, first, error) && !Local(target, first, leafId),
          "An explicit local deletion must survive refresh");
    Check(PrefabLibrary::Revert(target, first, error) && Local(target, first, leafId),
          "Revert must restore a deleted source entity");
    source.DestroyEntity(Entity(leafId, &source));
    Check(PrefabLibrary::Save(source, authoredId, assetPath, error), error);
    Scene stable;
    Check(stable.Deserialize(Utf8(assetPath)) && stable.IsAlive(branchId),
          "Saving after an earlier entity deletion must not renumber surviving source entities");

    const auto beforeFailure = Snapshot(target);
    std::ofstream(assetPath, std::ios::trunc) << "invalid prefab";
    Check(!PrefabLibrary::Refresh(target, first, error) && Snapshot(target) == beforeFailure &&
              !PrefabLibrary::Revert(target, first, error) && Snapshot(target) == beforeFailure &&
              !PrefabLibrary::Instantiate(target, assetPath, error) &&
              Snapshot(target) == beforeFailure,
          "Invalid sources must leave scene data and IDs untouched");
    Check(PrefabLibrary::Save(source, authoredId, assetPath, error), error);
    Scene invalidAsset;
    invalidAsset.CreateEntity("Root A");
    invalidAsset.CreateEntity("Root B");
    const auto invalidPath = bundle / "multi.luxprefab";
    Check(invalidAsset.Serialize(Utf8(invalidPath)), "Invalid fixture write failed");
    SceneManager invalidLoader;
    Check(!invalidLoader.Load(invalidPath),
          "Runtime composition must also reject multiple prefab roots");
    Check(!PrefabLibrary::Instantiate(target, invalidPath, error) &&
              Snapshot(target) == beforeFailure,
          "Multiple prefab roots must be rejected atomically");
    Check(!PrefabLibrary::Instantiate(target, bundle / "wrong.lux", error),
          "Non-prefab paths must be rejected");
    const auto secondAuthored = Local(target, second, authoredId);
    Check(PrefabLibrary::Unpack(target, second, error) &&
              !target.HasComponent<SceneReferenceComponent>(second) &&
              !PrefabLibrary::FindInstanceRoot(target, secondAuthored) &&
              target.IsAlive(secondAuthored),
          "Unpack must retain authored objects and remove their prefab connection");
    Check(PrefabLibrary::Refresh(target, first, error), error);
    Check(SceneManager::Save(target, bundle / "main.lux", {}, error), error);
    const auto relocated = directory / "relocated";
    fs::rename(bundle, relocated);
    SceneManager loader;
    const auto loaded = loader.Load(relocated / "main.lux");
    Check(static_cast<bool>(loaded), loaded.error);
    Check(loaded.scene->GetComponent<SceneReferenceComponent>(first).path.find(Utf8(relocated)) ==
              0,
          "Prefab sources must resolve within a relocated project");
}
} // namespace

int main() {
    try {
        Files files;
        RoundTrip(files.root);
        std::cout
            << "Prefab export, instances, overrides, revert, validation and relocation passed\n";
        return 0;
    } catch (const std::exception& failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
