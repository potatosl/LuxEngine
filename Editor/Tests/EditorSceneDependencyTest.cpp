#include <LuxEditor/Project/EditorProject.hpp>
#include <LuxEditor/Project/PathEncoding.hpp>
#include <LuxEditor/Scene/SceneDependencies.hpp>
#include <LuxEngine/Assets/AssetManager.hpp>
#include <LuxEngine/EntryPoint.hpp>
#include <LuxEngine/Scene/Entity.hpp>
#include <QColor>
#include <QImage>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
namespace fs = std::filesystem;
using namespace LuxEditor;
using namespace LuxEngine;

void Check(bool condition, const char* description) {
    if (!condition)
        throw std::runtime_error(description);
}

std::string Data(Scene& scene) {
    std::string data;
    Check(scene.SerializeToString(data), "fixture scene serializes");
    return data;
}

std::string Bytes(const fs::path& file) {
    std::ifstream input(file, std::ios::binary);
    Check(input.is_open(), "fixture file opens");
    return {std::istreambuf_iterator<char>(input), {}};
}

struct Fixture {
    fs::path root = fs::temp_directory_path() /
                    ("lux-scene-dependencies-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    EditorProject project;
    fs::path internal, external;

    Fixture() {
        fs::create_directories(root);
        std::string error;
        auto created =
            EditorProject::Create(root / "project", "Dependencies", ProjectTemplate::Empty, error);
        Check(created.has_value(), error.c_str());
        project = std::move(*created);
        internal = project.GetSceneFile().parent_path() / "Internal.lux";
        external = root / "External.lux";
        Scene source;
        source.CreateEntity("Source");
        Check(source.Serialize(PathUtf8(internal)) && source.Serialize(PathUtf8(external)),
              "internal and external sources save");
    }

    ~Fixture() {
        std::error_code error;
        fs::remove_all(root, error);
    }
};

SceneReferenceComponent& Reference(Scene& scene, const fs::path& source) {
    auto entity = scene.CreateEntity("Reference");
    auto& reference = entity.AddComponent<SceneReferenceComponent>();
    reference.path = PathUtf8(source);
    reference.loaded = false;
    return reference;
}

void RejectWithoutMutation(Fixture& fixture, Scene& scene, const char* reason) {
    const auto before = Data(scene);
    const auto origin = scene.GetSourcePath();
    const auto descriptor = Bytes(fixture.project.GetFile());
    const auto source = Bytes(fixture.internal);
    std::string error;
    Check(!ValidateProjectSceneReferences(fixture.project, scene, error) && !error.empty(), reason);
    Check(Data(scene) == before && scene.GetSourcePath() == origin &&
              Bytes(fixture.project.GetFile()) == descriptor && Bytes(fixture.internal) == source,
          "failed dependency validation preserves in-memory data and source files");
}

void TestSourcesAndSnapshots() {
    Fixture fixture;
    std::string error;
    Scene scene;
    auto& reference = Reference(scene, fixture.internal);
    Check(ValidateProjectSceneReferences(fixture.project, scene, error) && error.empty(),
          "valid internal scene dependency is accepted");
    reference.path = PathUtf8(fixture.external);
    RejectWithoutMutation(fixture, scene, "direct external dependency is rejected");
    reference.path = PathUtf8(fixture.internal);
    Scene hidden;
    Reference(hidden, fixture.external);
    reference.baseline = Data(hidden);
    RejectWithoutMutation(fixture, scene, "external dependency hidden in a baseline is rejected");
    reference.baseline.clear();
    reference.suspended = Data(hidden);
    RejectWithoutMutation(fixture, scene,
                          "external dependency hidden in unloaded streaming data is rejected");
    reference.suspended.clear();
    Scene valid;
    Reference(valid, fixture.internal.filename());
    reference.baseline = Data(valid);
    reference.suspended = reference.baseline;
    Check(ValidateProjectSceneReferences(fixture.project, scene, error),
          "relative references in snapshots use the containing scene's directory");
    reference.baseline.clear();
    reference.suspended.clear();
    reference.path = PathUtf8(fixture.internal.parent_path() / "Missing.lux");
    RejectWithoutMutation(fixture, scene, "missing internal dependency is rejected");
    reference.path = PathUtf8(fixture.internal);
    reference.baseline = "broken snapshot";
    RejectWithoutMutation(fixture, scene, "malformed stored dependency state is rejected");
}

void TestEnvironmentDependencies() {
    Fixture fixture;
    Scene scene;
    const auto file = fixture.project.GetFile().parent_path() / "Assets" / PathFromUtf8("환경.HDR");
    fs::create_directories(file.parent_path());
    {
        std::ofstream stream(file, std::ios::binary);
        stream << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 2\n";
        const unsigned char pixels[8] = {128, 128, 128, 129, 64, 64, 64, 129};
        stream.write(reinterpret_cast<const char*>(pixels), 8);
    }
    auto settings = scene.GetSettings();
    settings.render3D.mode = Render3DMode::Pbr;
    settings.render3D.environmentPath = PathUtf8(file);
    Check(scene.SetSettings(settings), "environment settings apply");
    std::string error;
    Check(ValidateProjectSceneReferences(fixture.project, scene, error), "project HDR accepted");
    std::ofstream(file) << "broken";
    RejectWithoutMutation(fixture, scene,
                          "corrupt disk HDR rejected independently of live GPU cache");
    settings.render3D.environmentPath = PathUtf8(fixture.root / "outside.hdr");
    scene.SetSettings(settings);
    RejectWithoutMutation(fixture, scene, "outside environment rejected");
}

void TestMeshDependencies() {
    Fixture fixture;
    Scene scene;
    auto entity = scene.CreateEntity("Mesh");
    auto& mesh = entity.AddComponent<MeshRendererComponent>();
    const auto file = fixture.project.GetFile().parent_path() / "Assets" / PathFromUtf8("한글.obj");
    fs::create_directories(file.parent_path());
    std::ofstream(file) << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    mesh.meshPath = PathUtf8(file);
    std::string error;
    Check(ValidateProjectSceneReferences(fixture.project, scene, error),
          "valid project mesh dependencies are accepted");
    mesh.meshPath = PathUtf8(fixture.root / "Outside.obj");
    std::ofstream(PathFromUtf8(mesh.meshPath)) << Bytes(file);
    RejectWithoutMutation(fixture, scene,
                          "external mesh files cannot silently escape a packaged project");
    mesh.meshPath = PathUtf8(file.parent_path() / "Missing.obj");
    RejectWithoutMutation(fixture, scene,
                          "missing mesh dependencies are rejected before game builds");
}

void TestTextureDependencies() {
    Fixture fixture;
    Scene scene;
    auto entity = scene.CreateEntity("Textured cube");
    auto& material = entity.AddComponent<MeshRendererComponent>().material;
    const auto file =
        fixture.project.GetFile().parent_path() / "Assets" / PathFromUtf8("한글 텍스처.PnG");
    fs::create_directories(file.parent_path());
    QImage image(2, 2, QImage::Format_RGBA8888);
    image.fill(QColor(128, 90, 30));
    Check(image.save(QString::fromUtf8(PathUtf8(file)), "PNG"),
          "dependency texture fixture writes");
    material.baseColorTexture = PathUtf8(file);
    std::string error;
    Check(ValidateProjectSceneReferences(fixture.project, scene, error),
          "valid texture dependencies are accepted");
    const auto model = file.parent_path() / "unmapped.obj";
    std::ofstream(model) << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    entity.GetComponent<MeshRendererComponent>().meshPath = PathUtf8(model);
    RejectWithoutMutation(fixture, scene,
                          "textured models without authored UVs fail before packaging");
    std::ofstream(model, std::ios::trunc)
        << "v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 0 1\nf 1/1 2/2 3/3\n";
    Check(AssetManager::ReloadMesh(PathUtf8(model), error, true) &&
              ValidateProjectSceneReferences(fixture.project, scene, error),
          "UV-complete replacement meshes restore valid material dependencies");
    RawMeshData cached;
    Check(AssetManager::LoadMesh(PathUtf8(model), cached) && cached.hasTexCoords,
          "valid UV geometry is cached before external edits");
    std::ofstream(model, std::ios::trunc) << "corrupt cached OBJ";
    RejectWithoutMutation(
        fixture, scene, "build validation reads disk instead of trusting stale mesh cache entries");
    RawMeshData preserved;
    Check(AssetManager::LoadMesh(PathUtf8(model), preserved) &&
              preserved.revision == cached.revision,
          "failed build validation does not replace a previously valid CPU cache snapshot");
    entity.GetComponent<MeshRendererComponent>().meshPath.clear();
    const auto outside = fixture.root / "outside.png";
    fs::copy_file(file, outside);
    material.baseColorTexture = PathUtf8(outside);
    RejectWithoutMutation(fixture, scene,
                          "external textures cannot silently escape packaged projects");
    material.baseColorTexture = PathUtf8(file.parent_path() / "missing.png");
    RejectWithoutMutation(fixture, scene, "missing textures fail before build");
    material.baseColorTexture = PathUtf8(file);
    std::ofstream(file, std::ios::binary | std::ios::trunc) << "truncated PNG";
    RejectWithoutMutation(fixture, scene, "corrupt images fail full decoding before build");
}

void TestTransitiveSources() {
    Fixture fixture;
    Scene current, nested;
    Reference(current, fixture.internal);
    Reference(nested, fixture.external);
    Check(nested.Serialize(PathUtf8(fixture.internal)), "transitive source fixture saves");
    RejectWithoutMutation(fixture, current,
                          "an internal file cannot conceal a transitive external source");
    std::string error;
    Check(!ValidateProjectScenes(fixture.project, error),
          "whole-project validation catches external sources in currently unloaded scenes");
}

void TestAliasesAndProjectScan() {
    Fixture fixture;
    std::string error;
    Check(ValidateProjectScenes(fixture.project, error), "a valid authored project validates");
    for (const auto* directory : {".git", ".lux/cache", "build", "out", "cmake-build-debug"}) {
        const auto path = fixture.project.GetFile().parent_path() / directory / "Broken.lux";
        fs::create_directories(path.parent_path());
        std::ofstream(path) << "metadata and build products are not authored scenes";
    }
    Check(ValidateProjectScenes(fixture.project, error), "metadata and build folders are excluded");
    const auto bad = fixture.internal.parent_path() / "Broken.lux";
    std::ofstream(bad) << "invalid scene";
    Check(!ValidateProjectScenes(fixture.project, error),
          "invalid unused authored scenes are rejected before packaging");
    fs::remove(bad);
    const auto link = fixture.internal.parent_path() / "ExternalAlias.lux";
    std::error_code ec;
    fs::create_symlink(fixture.external, link, ec);
    if (!ec) {
        Scene scene;
        Reference(scene, link);
        RejectWithoutMutation(fixture, scene, "symbolic links cannot escape the project boundary");
        Check(!ValidateProjectScenes(fixture.project, error),
              "unreferenced external scene aliases are not admitted into Content");
    }
}

void TestDepth() {
    Fixture fixture;
    fs::path next = fixture.internal;
    for (int index = 18; index >= 0; --index) {
        Scene source;
        Reference(source, next);
        next = fixture.internal.parent_path() / ("Depth" + std::to_string(index) + ".lux");
        Check(source.Serialize(PathUtf8(next)), "depth fixture saves");
    }
    Scene scene;
    Reference(scene, next);
    RejectWithoutMutation(fixture, scene, "dependency depth is bounded without loading forever");

    // A shared subtree first visited near the root still needs validation at greater depth.
    Scene shared;
    Reference(shared, fixture.internal.parent_path() / "Depth9.lux");
    Reference(shared, next);
    RejectWithoutMutation(fixture, shared,
                          "cached shallow dependencies cannot bypass the depth limit");
}

void TestFileBudget() {
    Fixture fixture;
    const auto oversized = fixture.internal.parent_path() / "Oversized.lux";
    std::ofstream(oversized) << "LUX_SCENE 3\n";
    fs::resize_file(oversized, 128 * 1024 * 1024 + 1);
    Scene scene;
    Reference(scene, oversized);
    RejectWithoutMutation(fixture, scene,
                          "oversized scene files are rejected before allocating their contents");
}

void TestIndependentSceneBudgets() {
    Fixture fixture;
    Scene scene;
    scene.CreateEntity("Independent graph");
    const auto first = fixture.internal.parent_path() / "Large0.lux";
    Check(scene.Serialize(PathUtf8(first)), "independent graph fixture saves");
    {
        // Trailing whitespace is valid scene data. It exercises real file-byte limits without
        // adding unrelated entity or renderer costs to this dependency-boundary regression.
        std::ofstream padded(first, std::ios::binary | std::ios::app);
        const std::string block(1024 * 1024, ' ');
        for (int index = 0; index < 44; ++index)
            padded.write(block.data(), static_cast<std::streamsize>(block.size()));
        Check(static_cast<bool>(padded), "large independent graph fixture writes");
    }
    for (int index = 1; index < 3; ++index)
        fs::copy_file(first,
                      fixture.internal.parent_path() / ("Large" + std::to_string(index) + ".lux"));
    std::string error;
    Check(ValidateProjectScenes(fixture.project, error) && error.empty(),
          "a project may exceed 128 MiB when each independent scene graph stays within limits");
}
} // namespace

int RunDependencyChecks(int, char**) {
    try {
        TestSourcesAndSnapshots();
        TestMeshDependencies();
        TestEnvironmentDependencies();
        TestTextureDependencies();
        TestTransitiveSources();
        TestAliasesAndProjectScan();
        TestDepth();
        TestFileBudget();
        TestIndependentSceneBudgets();
        std::cout << "프로젝트 씬 참조와 배포 경계 검증을 통과했습니다.\n";
        return 0;
    } catch (const std::exception& failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
LUX_ENGINE_MAIN(RunDependencyChecks)
