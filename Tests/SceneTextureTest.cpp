#include "Engine/Scene/Entity.hpp"
#include "Engine/Scene/Scene.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
using namespace LuxEngine;
namespace fs = std::filesystem;

void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

std::string Utf8(const fs::path& path) {
    const auto text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

fs::path Path(const std::string& text) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

struct Files {
    fs::path root = fs::temp_directory_path() /
                    ("lux-scene-textures-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Files() {
        fs::create_directories(root);
    }
    ~Files() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
};

std::string Fixture(unsigned version, const std::string& texture = {}) {
    std::ostringstream out;
    out << "LuxEngineScene " << version << " 1\n";
    if (version >= 2)
        out << "7\n";
    out << "1 \"Legacy\"\n1 0 0 0 0 0 0 1 1 1\n1 0.2 0.4 0.6 0.8 ";
    if (version >= 3)
        out << std::quoted(texture);
    out << "\n0\n0\n0\n0\n0\n0\n";
    if (version >= 2)
        out << "0\n0\n";
    return out.str();
}

void CompatibilityAndValidation() {
    for (unsigned version : {1u, 2u}) {
        Scene legacy;
        Check(legacy.DeserializeFromString(Fixture(version)), "v1/v2 fixture loads");
        auto entity = legacy.GetEntities().front();
        const auto& sprite = entity.GetComponent<SpriteRendererComponent>();
        Check(sprite.texturePath.empty() && sprite.color.x == 0.2f && sprite.color.w == 0.8f,
              "legacy sprites preserve their color without a texture");
        std::string migrated;
        Check(legacy.SerializeToString(migrated) && migrated.starts_with("LuxEngineScene 3 "),
              "legacy scenes save as version 3");
        Scene loaded;
        Check(loaded.DeserializeFromString(migrated), "migrated scene loads");
    }

    Scene scene;
    const std::string texture = "Assets/캐릭터 이미지.png";
    Check(scene.DeserializeFromString(Fixture(3, texture)), "UTF-8 texture fixture loads");
    auto entity = scene.GetEntities().front();
    Check(entity.GetId() == 7, "version 3 preserves stable IDs");
    auto& sprite = entity.GetComponent<SpriteRendererComponent>();
    Check(sprite.texturePath == texture, "UTF-8 texture path preserved");
    std::string snapshot;
    Check(scene.SerializeToString(snapshot), "version 3 sprite saves");
    Scene restored;
    Check(restored.DeserializeFromString(snapshot), "version 3 sprite reloads");
    Check(restored.GetEntities().front().GetComponent<SpriteRendererComponent>().texturePath == texture,
          "texture path survives string roundtrip");

    const std::vector<std::string> invalidPaths = {
        "../outside.png", "Assets/../../outside.png", "/absolute.png", "C:/absolute.png",
        "C:relative.png", "Assets\\sprite.png", "Assets/./sprite.png", "Assets/",
        "Assets/line\nbreak.png", std::string("Assets/null\0byte.png", 20),
        "Assets/\xF5\x80\x80\x80.png", std::string(4097, 'a')};
    for (const auto& path : invalidPaths) {
        sprite.texturePath = path;
        std::string unchanged = "unchanged";
        Check(!scene.SerializeToString(unchanged) && unchanged == "unchanged",
              "unsafe texture path rejected before writing");
        const auto previous = restored.GetEntities().front();
        Check(!restored.DeserializeFromString(Fixture(3, path)) && previous.IsValid(),
              "unsafe texture path rejected without replacing the scene");
    }
    sprite.texturePath.clear();
    Check(scene.SerializeToString(snapshot), "empty texture path remains a color sprite");
}

void RootsAndRelocation() {
    Files files;
    const auto project = files.root / "project";
    const auto sceneFile = project / "Scenes" / "Main.lux";
    fs::create_directories(project / "Assets");
    fs::create_directories(sceneFile.parent_path());
    std::ofstream(project / "project.luxproj") << "fixture";
    std::ofstream(project / "Assets" / "sprite.png") << "fixture";

    Scene scene;
    auto entity = scene.CreateEntity("Sprite");
    entity.AddComponent<SpriteRendererComponent>().texturePath = "Assets/sprite.png";
    std::string snapshot;
    Check(scene.SerializeToString(snapshot) && scene.GetAssetRoot().empty(),
          "string serialization does not invent an asset root");
    Check(scene.Serialize(Utf8(sceneFile)), "textured scene saves to disk");
    Check(scene.GetAssetRoot() == Scene::InferAssetRootForScene(Utf8(sceneFile)),
          "first disk save infers the project root");
    Check(fs::equivalent(Path(scene.ResolveAssetPath("Assets/sprite.png")),
                         project / "Assets" / "sprite.png"),
          "project-relative path resolves under the project root");
    Check(!scene.ResolveAssetPath("Assets/missing.png").empty(),
          "missing safe assets can be resolved for renderer fallback");
    Check(scene.ResolveAssetPath("../outside.png").empty() &&
              scene.ResolveAssetPath(Utf8(project / "Assets/sprite.png")).empty(),
          "resolver rejects traversal and absolute paths");

    const auto explicitRoot = files.root / "explicit";
    fs::create_directory(explicitRoot);
    scene.SetAssetRoot(Utf8(explicitRoot));
    const auto savedRoot = scene.GetAssetRoot();
    Check(scene.DeserializeFromString(snapshot) && scene.GetAssetRoot() == savedRoot,
          "undo snapshot restoration preserves the existing root");
    Check(!scene.DeserializeFromString("bad scene") && scene.GetAssetRoot() == savedRoot,
          "failed undo restoration preserves the existing root");
    Check(scene.Serialize(Utf8(project / "Scenes/Preview.lux")) && scene.GetAssetRoot() == savedRoot,
          "disk serialization preserves an explicit root for temporary previews");
    Check(scene.Deserialize(Utf8(sceneFile)) && scene.GetAssetRoot() != savedRoot,
          "loading a different file infers its own root");
    const auto beforeBadLoad = scene.GetAssetRoot();
    std::ofstream(files.root / "bad.lux") << "bad scene";
    Check(!scene.Deserialize(Utf8(files.root / "bad.lux")) && scene.GetAssetRoot() == beforeBadLoad,
          "failed file loading preserves the existing root");

    const auto relocated = files.root / "relocated";
    fs::copy(project, relocated, fs::copy_options::recursive);
    Scene moved;
    Check(moved.Deserialize(Utf8(relocated / "Scenes/Main.lux")), "relocated project scene loads");
    Check(fs::equivalent(Path(moved.ResolveAssetPath("Assets/sprite.png")),
                         relocated / "Assets/sprite.png"),
          "project relocation resolves textures from the new root");

    const auto standalone = files.root / "standalone";
    fs::create_directory(standalone);
    Scene standaloneScene;
    Check(standaloneScene.Serialize(Utf8(standalone / "Main.lux")), "standalone scene saves");
    Check(standaloneScene.GetAssetRoot() == Utf8(standalone),
          "standalone scenes fall back to the scene directory");
    scene.SetAssetRoot("");
    Check(scene.ResolveAssetPath("Assets/sprite.png").empty(), "unset roots do not use the process directory");
    Check(Scene::InferAssetRootForScene("").empty(), "empty scene paths do not infer a root");
}
void FilePathSafety() {
    Files files;
    const auto file = files.root / "original.lux";
    Scene scene;
    auto entity = scene.CreateEntity("before");
    entity.AddComponent<SpriteRendererComponent>().texturePath = "Assets/sprite.png";
    Check(scene.Serialize(Utf8(file)), "baseline scene saves before path validation checks");
    const auto bytes = [](const fs::path& path) {
        std::ifstream input(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(input), {});
    };
    const auto originalBytes = bytes(file);
    const auto originalRoot = scene.GetAssetRoot();
    entity.GetComponent<TagComponent>().tag = "after";
    const std::vector<std::string> invalidPaths{
        Utf8(file) + std::string(1, '\0') + "suffix.lux",
        Utf8(file) + "\xFF.lux",
        Utf8(file) + "\xF5\x80\x80\x80.lux"};
    for (const auto& path : invalidPaths) {
        Check(!scene.Serialize(path), "invalid scene file paths are rejected before writing");
        Check(fs::is_regular_file(file) && bytes(file) == originalBytes,
              "rejected scene paths cannot overwrite or delete the original file");
        Check(scene.GetAssetRoot() == originalRoot && entity.IsValid(),
              "rejected saves preserve the asset root and entity handles");
        Check(!scene.Deserialize(path) && entity.IsValid() &&
                  scene.GetAssetRoot() == originalRoot &&
                  entity.GetComponent<TagComponent>().tag == "after",
              "rejected scene read paths preserve the current scene");
        Check(Scene::InferAssetRootForScene(path).empty(),
              "root inference uses the same file path validation");
    }
    Scene loaded;
    Check(loaded.Deserialize(Utf8(file)) &&
              loaded.GetEntities().front().GetComponent<TagComponent>().tag == "before",
          "the last valid saved scene remains readable after invalid file operations");
}

void WriteRuntimeFixture(const fs::path& root) {
    fs::create_directories(root / "Assets");
    fs::create_directories(root / "Scenes");
    {
        std::ofstream marker(root / "project.luxproj", std::ios::binary);
        marker << "LuxEngine runtime texture fixture\n";
        marker.close();
        Check(static_cast<bool>(marker), "runtime project marker writes");
    }
    std::vector<unsigned char> bmp(70, 0);
    const auto write32 = [&bmp](size_t offset, unsigned value) {
        for (size_t byte = 0; byte < 4; ++byte)
            bmp[offset + byte] = static_cast<unsigned char>((value >> (byte * 8)) & 255u);
    };
    bmp[0] = 'B';
    bmp[1] = 'M';
    write32(2, static_cast<unsigned>(bmp.size()));
    write32(10, 54);
    write32(14, 40);
    write32(18, 2);
    write32(22, 2);
    bmp[26] = 1;
    bmp[28] = 24;
    write32(34, 16);
    bmp[56] = 255;
    bmp[58] = 255;
    bmp[62] = 255;
    bmp[65] = 255;
    bmp[66] = 255;
    bmp[67] = 255;
    {
        std::ofstream image(root / "Assets/checker.bmp", std::ios::binary);
        image.write(reinterpret_cast<const char*>(bmp.data()), static_cast<std::streamsize>(bmp.size()));
        image.close();
        Check(static_cast<bool>(image), "runtime texture image writes");
    }
    Scene scene;
    auto entity = scene.CreateEntity("Textured sprite");
    entity.GetComponent<TransformComponent>().scale = {2, 2, 1};
    entity.AddComponent<SpriteRendererComponent>().texturePath = "Assets/checker.bmp";
    Check(scene.Serialize(Utf8(root / "Scenes/Texture.lux")), "runtime texture scene writes");
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string_view(argv[1]) == "--write-fixture") {
            WriteRuntimeFixture(Path(argv[2]));
            std::cout << "Runtime texture fixture written\n";
            return 0;
        }
        Check(argc == 1, "expected --write-fixture <directory> or no arguments");
        CompatibilityAndValidation();
        RootsAndRelocation();
        FilePathSafety();
        std::cout << "Scene texture checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Scene texture checks failed: " << error.what() << '\n';
        return 1;
    }
}
