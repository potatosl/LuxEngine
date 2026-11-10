#include "Engine/EntryPoint.hpp"
#include "Engine/Platform/Window.hpp"
#include "Engine/Renderer/Renderer2D/Renderer2D.hpp"
#include "Engine/Scene/Entity.hpp"
#include <SDL3/SDL.h>
#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <limits>
#include <locale>
#include <stdexcept>

namespace {
using namespace LuxEngine;
void Check(bool value, const char* description) {
    if (!value)
        throw std::runtime_error(description);
}
bool Near(float a, float b, float epsilon = 1e-4f) {
    return std::abs(a - b) < epsilon;
}
void CheckMatrix(const Mat4& a, const Mat4& b, const char* description) {
    for (size_t index = 0; index < 16; ++index)
        Check(Near(a.elements[index], b.elements[index]), description);
}
std::string Data(Scene& scene) {
    std::string data;
    Check(scene.SerializeToString(data), "valid scene serializes");
    return data;
}
void Replace(std::string& data, const std::string& before, const std::string& after) {
    const auto position = data.find(before);
    Check(position != std::string::npos, "malformed fixture marker exists");
    data.replace(position, before.size(), after);
}
void Hierarchy() {
    Scene scene;
    auto parent = scene.CreateEntity("부모"), child = scene.CreateEntity("자식"),
         other = scene.CreateEntity("다른 부모");
    parent.GetComponent<TransformComponent>().position = {3, 4, 2};
    parent.GetComponent<TransformComponent>().rotation = {.2f, .4f, .7f};
    parent.GetComponent<TransformComponent>().scale = {2, 2, 2};
    child.GetComponent<TransformComponent>().position = {1, 2, 3};
    child.GetComponent<TransformComponent>().rotation = {.1f, -.2f, .3f};
    const auto original = scene.GetWorldMatrix(child.GetId());
    Check(scene.SetParent(child.GetId(), parent.GetId()), "reparent keeps world position");
    CheckMatrix(original, scene.GetWorldMatrix(child.GetId()), "keep-world preserves full TRS");
    Check(scene.GetParent(child.GetId()) == parent.GetId() &&
              scene.GetChildren(parent.GetId()) == std::vector<uint32>{child.GetId()},
          "parent-child APIs agree");
    Check(!scene.SetParent(parent.GetId(), child.GetId()) &&
              !scene.SetParent(parent.GetId(), parent.GetId()) &&
              !scene.SetParent(child.GetId(), 999999),
          "self-parent, cycles and missing parents are rejected");
    Check(scene.SetParent(child.GetId(), other.GetId()), "reparent to another root succeeds");
    CheckMatrix(original, scene.GetWorldMatrix(child.GetId()), "second reparent keeps world TRS");
    auto moved = scene.GetWorldTransform(child.GetId());
    moved.position = {-8, 2, 1};
    Check(scene.SetWorldTransform(child.GetId(), moved) &&
              Near(scene.GetWorldTransform(child.GetId()).position.x, -8),
          "world manipulation updates local transform");

    auto sheared = scene.CreateEntity("비균일 부모"), rotated = scene.CreateEntity("회전 자식");
    sheared.GetComponent<TransformComponent>().scale = {3, 1, 1};
    rotated.GetComponent<TransformComponent>().rotation.z = .7f;
    const auto before = Data(scene);
    Check(!scene.SetParent(rotated.GetId(), sheared.GetId()),
          "non-representable keep-world shear is rejected");
    Check(Data(scene) == before, "failed reparent is transactional");
    Check(scene.SetParent(rotated.GetId(), sheared.GetId(), false),
          "authored local transform may intentionally inherit non-uniform affine matrix");
    CheckMatrix(sheared.GetComponent<TransformComponent>().GetTransform() *
                    rotated.GetComponent<TransformComponent>().GetTransform(),
                scene.GetWorldMatrix(rotated.GetId()), "world matrix retains exact affine shear");
    auto translated = scene.GetWorldTransform(rotated.GetId());
    const auto previousLocal = rotated.GetComponent<TransformComponent>();
    translated.position = {6, -2, 3};
    Check(scene.SetWorldTransform(rotated.GetId(), translated) &&
              Near(scene.GetWorldTransform(rotated.GetId()).position.x, 6) &&
              Near(scene.GetWorldTransform(rotated.GetId()).position.y, -2) &&
              rotated.GetComponent<TransformComponent>().rotation.z == previousLocal.rotation.z &&
              rotated.GetComponent<TransformComponent>().scale.x == previousLocal.scale.x,
          "translation under affine shear preserves the authored local rotation and scale");
    scene.DestroyEntity(other);
    Check(!other && !child && scene.IsAlive(parent.GetId()),
          "deleting a group cascades only its descendants");
}
void HierarchyCodecAndImport() {
    Scene source;
    auto root = source.CreateEntity("그룹"), child = source.CreateEntity("자식");
    root.AddComponent<HierarchyComponent>().group = true;
    root.GetComponent<TransformComponent>().position = {4, 0, 0};
    child.GetComponent<TransformComponent>().position = {2, 0, 0};
    child.AddComponent<SpriteRendererComponent>();
    Check(source.GetComponentEntityIds<SpriteRendererComponent>() ==
                  std::vector<uint32>{child.GetId()} &&
              source.GetComponentEntityIds<SceneReferenceComponent>().empty(),
          "typed component queries enumerate only the requested pool");
    child.AddComponent<Rigidbody2DComponent>();
    child.AddComponent<BoxCollider2DComponent>();
    child.AddComponent<TilemapComponent>().Resize(2, 3);
    auto& animation = child.AddComponent<AnimationComponent>();
    animation.clip.name = "이동";
    animation.clip.AddKeyframe({.time = 0, .position = {1, 2, 0}});
    Check(source.SetParent(child.GetId(), root.GetId(), false), "local child added");
    const auto serialized = Data(source);
    Scene restored;
    Check(restored.DeserializeFromString(serialized) && Data(restored) == serialized,
          "hierarchy/group/component v3 roundtrip is deterministic");
    auto stable = restored.CreateEntity("보존");
    const auto stableCount = restored.GetEntityCount();
    auto malformed = serialized;
    Replace(malformed, "\n1 0 1\n0 \n0 \n", "\n1 2 1\n0 \n0 \n");
    Check(!restored.DeserializeFromString(malformed) && stable &&
              restored.GetEntityCount() == stableCount,
          "cyclic file is rejected without replacing the active scene");
    malformed = serialized;
    Replace(malformed, "\n1 1 0\n0 \n0 \n", "\n1 9999 0\n0 \n0 \n");
    Check(!restored.DeserializeFromString(malformed) && stable,
          "dangling parent in a file is rejected transactionally");

    Scene target;
    target.CreateEntity("기존 ID 충돌");
    const auto map = target.ImportEntities(source);
    Check(map.size() == 2 && target.GetEntityCount() == 3 &&
              target.GetParent(map.at(child.GetId())) == map.at(root.GetId()),
          "additive import remaps colliding IDs and child links");
    Check(target.HasComponent<AnimationComponent>(map.at(child.GetId())) &&
              target.HasComponent<TilemapComponent>(map.at(child.GetId())) &&
              target.HasComponent<Rigidbody2DComponent>(map.at(child.GetId())),
          "all authored component families survive import");
    Scene subtree;
    const auto selected = subtree.ImportEntities(source, {child.GetId()});
    Check(selected.size() == 1 &&
              Near(subtree.GetWorldTransform(selected.at(child.GetId())).position.x, 6),
          "moving an isolated subtree preserves its source world position");
    const auto duplicates = target.ImportEntities(target, {map.at(root.GetId())});
    Check(duplicates.size() == 2 && target.GetEntityCount() == 5,
          "same-scene duplication snapshots its source before allocating new IDs");
    struct Unsupported {
        int value = 3;
    };
    source.CreateEntity("미지원").AddComponent<Unsupported>();
    const auto before = Data(target);
    Check(target.ImportEntities(source).empty() && Data(target) == before,
          "failed import preserves every destination component and ID");
}
void ReferenceMetadata() {
    Scene scene;
    auto root = scene.CreateEntity("참조"), child = scene.CreateEntity("인스턴스");
    Check(scene.SetParent(child.GetId(), root.GetId(), false), "instance parent setup");
    auto& ref = root.AddComponent<SceneReferenceComponent>();
    ref.path = "한글/source.lux";
    ref.mode = SceneReferenceMode::Inherited;
    ref.streamCenter = {2, 3};
    ref.streamLoadRadius = 10;
    ref.streamUnloadRadius = 15;
    ref.baseline = "LuxEngineScene 2 0\n";
    ref.suspended = "";
    ref.entityMap.emplace_back(12, child.GetId());
    child.AddComponent<InstanceSourceComponent>(InstanceSourceComponent{root.GetId(), 12});
    Scene loaded;
    const auto saved = Data(scene);
    Check(loaded.DeserializeFromString(saved) && Data(loaded) == saved,
          "reference paths, baselines, streaming settings and provenance roundtrip");
    Scene another;
    another.CreateEntity();
    const auto map = another.ImportEntities(scene);
    const auto* imported = another.TryGetComponent<SceneReferenceComponent>(map.at(root.GetId()));
    Check(imported && imported->entityMap.at(0).second == map.at(child.GetId()) &&
              another.GetComponent<InstanceSourceComponent>(map.at(child.GetId())).instanceRoot ==
                  map.at(root.GetId()),
          "nested provenance and each reference mapping remap to the destination scene");
    Check(!Data(another).empty(), "imported provenance passes strict serialization validation");
    Check(scene.SetParent(child.GetId(), 0), "instance can be detached by reparenting");
    Check(!child.HasComponent<InstanceSourceComponent>() && ref.entityMap.empty(),
          "detaching keeps a local object and removes stale inherited ownership");
    Check(!Data(scene).empty(), "detached scene remains serializable");
    Check(loaded.GetEntityCount() == 2, "roundtripped instance was independent");
    loaded.DestroyEntity({child.GetId(), &loaded});
    Check(loaded.GetComponent<SceneReferenceComponent>(root.GetId()).entityMap.empty() &&
              !Data(loaded).empty(),
          "deleting an inherited child records a valid missing-map deletion override");
}
void SettingsAndPhysics() {
    Scene up, down;
    auto first = up.CreateEntity(), second = down.CreateEntity();
    for (auto entity : {first, second}) {
        entity.GetComponent<TransformComponent>().position = {0, 10, 0};
        entity.AddComponent<Rigidbody2DComponent>();
        entity.AddComponent<BoxCollider2DComponent>();
    }
    auto settings = up.GetSettings();
    settings.clearColor = {.2f, .3f, .4f, 1};
    settings.exposure = 1.7f;
    settings.overrideGravity = true;
    settings.gravity = {0, 6};
    settings.gravity3D = {0, -3, 1};
    settings.fixedStep = .1f;
    settings.physicsSubsteps = 2;
    Check(up.SetSettings(settings), "per-scene settings accepted");
    auto lower = settings;
    lower.gravity.y = -6;
    Check(down.SetSettings(lower), "second scene has independent gravity");
    up.OnUpdate(.05);
    Check(Near(first.GetComponent<TransformComponent>().position.y, 10),
          "physics waits for the scene's selected fixed interval");
    up.OnUpdate(.05);
    down.OnUpdate(.1);
    Check(first.GetComponent<TransformComponent>().position.y > 10 &&
              second.GetComponent<TransformComponent>().position.y < 10,
          "independent worlds use per-scene gravity and fixed substeps");
    Scene restored;
    Check(restored.DeserializeFromString(Data(up)) &&
              Near(restored.GetSettings().clearColor.y, .3f) &&
              Near(restored.GetSettings().exposure, 1.7f) &&
              restored.GetSettings().physicsSubsteps == 2 &&
              Near(restored.GetSettings().gravity3D.z, 1),
          "scene environment/render/2D/3D physics settings persist");
    const auto before = Data(up);
    settings.fixedStep = std::numeric_limits<float>::quiet_NaN();
    Check(!up.SetSettings(settings) && Data(up) == before,
          "invalid settings preserve the previous authored state");

    Scene parented;
    auto group = parented.CreateEntity(), body = parented.CreateEntity();
    group.GetComponent<TransformComponent>().position = {3, 2, 0};
    body.GetComponent<TransformComponent>().position = {0, 2, 0};
    auto& rb = body.AddComponent<Rigidbody2DComponent>();
    rb.type = BodyType::Kinematic;
    rb.velocity = {2, 0};
    body.AddComponent<BoxCollider2DComponent>();
    Check(parented.SetParent(body.GetId(), group.GetId(), false), "parented physics setup");
    for (int step = 0; step < 60; ++step)
        parented.OnUpdate(1.0 / 60);
    Check(Near(parented.GetWorldTransform(body.GetId()).position.x, 5, .01f) &&
              Near(body.GetComponent<TransformComponent>().position.x, 2, .01f),
          "Box2D integrates world coordinates and writes back parent-local coordinates");

    Scene independentBodies;
    auto movingChild = independentBodies.CreateEntity("먼저 생성된 자식");
    auto movingParent = independentBodies.CreateEntity("나중에 생성된 부모");
    movingChild.GetComponent<TransformComponent>().position = {10, 2, 0};
    movingParent.GetComponent<TransformComponent>().position = {0, 2, 0};
    for (auto entity : {movingChild, movingParent}) {
        auto& kinematic = entity.AddComponent<Rigidbody2DComponent>();
        kinematic.type = BodyType::Kinematic;
        kinematic.velocity = {entity.GetId() == movingChild.GetId() ? 3.0f : 2.0f, 0};
        entity.AddComponent<BoxCollider2DComponent>();
    }
    Check(independentBodies.SetParent(movingChild.GetId(), movingParent.GetId()),
          "independent bodies can have a scene relationship");
    for (int frame = 0; frame < 60; ++frame)
        independentBodies.OnUpdate(1.0 / 60);
    Check(Near(independentBodies.GetWorldTransform(movingParent.GetId()).position.x, 2, .01f) &&
              Near(independentBodies.GetWorldTransform(movingChild.GetId()).position.x, 13, .01f),
          "body writeback follows hierarchy rather than entity allocation or hash iteration order");
}
void BackwardCompatibilityAndOrigin() {
    Scene scene;
    Check(scene.DeserializeFromString("LuxEngineScene 1 0\n"), "version 1 remains readable");
    std::string version2 = "LuxEngineScene 2 1\n9\n1 \"legacy\"\n"
                           "1 1 2 3 0 0 0 1 1 1\n";
    for (int index = 0; index < 9; ++index)
        version2 += "0\n";
    Check(scene.DeserializeFromString(version2) && scene.IsAlive(9) && scene.GetParent(9) == 0 &&
              !scene.GetSettings().overrideGravity,
          "version 2 entities retain IDs and inherit legacy global gravity");
    scene.SetSourcePath("한글/origin.lux");
    Check(scene.DeserializeFromString(Data(scene)) && scene.GetSourcePath() == "한글/origin.lux",
          "undo data replacement preserves the non-persisted source path");
    struct CommaNumbers final : std::numpunct<char> {
        char do_decimal_point() const override {
            return ',';
        }
    };
    const auto canonical = Data(scene);
    const auto previousLocale = std::locale();
    std::locale::global(std::locale(previousLocale, new CommaNumbers));
    const bool localeRead = scene.DeserializeFromString(canonical);
    const auto localeWritten = Data(scene);
    std::locale::global(previousLocale);
    Check(localeRead && localeWritten == canonical,
          "saved numeric data is independent of the application's locale");
    const auto file =
        std::filesystem::temp_directory_path() /
        ("lux-scene-core-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".lux");
    const auto u8 = file.u8string();
    const std::string path(reinterpret_cast<const char*>(u8.data()), u8.size());
    Check(scene.Serialize(path), "scene saves atomically");
    Check(scene.GetSourcePath() == "한글/origin.lux",
          "serializing a backup does not retarget the live scene's source origin");
    Scene loaded;
    Check(loaded.Deserialize(path) && loaded.GetSourcePath() == path,
          "file loads retain origin for relative references and cycle checks");
    std::filesystem::remove(file);
}

void RuntimePreservingCommit() {
    Scene scene;
    auto sensor = scene.CreateEntity("트리거"), actor = scene.CreateEntity("기존 물체");
    sensor.AddComponent<BoxCollider2DComponent>().isTrigger = true;
    actor.AddComponent<Rigidbody2DComponent>();
    actor.AddComponent<BoxCollider2DComponent>();
    auto& animation = actor.AddComponent<AnimationComponent>();
    animation.currentTime = .54321f;
    animation.playing = false;
    scene.SetSourcePath("기존 원본.lux");
    scene.OnUpdate(1.0 / 60);
    Check(!scene.GetCollisionEvents2D().empty(), "native physics contact exists before streaming");
    Scene prepared;
    Check(prepared.DeserializeFromString(Data(scene)), "prepared streamed state clones");
    prepared.GetComponent<AnimationComponent>(actor.GetId()).currentTime = animation.currentTime;
    auto added = prepared.CreateEntity("새 청크");
    const auto actorId = actor.GetId(), addedId = added.GetId();
    Check(scene.ApplyState(prepared) && !actor && !added && scene.IsAlive(addedId) &&
              scene.GetComponent<AnimationComponent>(actorId).currentTime == .54321f &&
              scene.GetSourcePath() == "기존 원본.lux",
          "authored commit preserves runtime cursors/origin and invalidates replaced handles");
    scene.OnUpdate(1.0 / 60);
    Check(
        scene.GetCollisionEvents2D().empty(),
        "loading an unrelated chunk preserves native contacts instead of restarting BeginContact");
    Scene withoutCollider;
    Check(withoutCollider.DeserializeFromString(Data(scene)), "collider removal state clones");
    withoutCollider.RemoveComponent<BoxCollider2DComponent>(actorId);
    withoutCollider.GetComponent<Rigidbody2DComponent>(actorId).isGrounded = true;
    Check(scene.ApplyState(withoutCollider) &&
              !scene.GetComponent<Rigidbody2DComponent>(actorId).isGrounded,
          "losing a collider during a state commit clears stale grounded state");
    auto detached = scene.CreateEntity("충돌체 제거");
    detached.AddComponent<Rigidbody2DComponent>().isGrounded = true;
    detached.AddComponent<BoxCollider2DComponent>();
    detached.RemoveComponent<BoxCollider2DComponent>();
    Check(!detached.GetComponent<Rigidbody2DComponent>().isGrounded,
          "direct collider removal immediately clears grounded state");
    const auto before = Data(scene);
    prepared.CreateEntity("유효하지 않은 새 상태").GetComponent<TransformComponent>().position.x =
        std::numeric_limits<float>::infinity();
    Check(!scene.ApplyState(prepared) && Data(scene) == before,
          "invalid prepared state never mutates the live scene or physics world");
}
int RenderHierarchy() {
    struct Graphics {
        Window window;
        GpuDevice gpu;
        ~Graphics() {
            gpu.Shutdown();
            window.Shutdown();
            SDL_Quit();
        }
    } graphics;
    if (!SDL_Init(SDL_INIT_VIDEO) ||
        !graphics.window.Initialize(
            {.title = "씬 계층 렌더링 검사", .width = 320, .height = 240, .vulkan = true}))
        return 77;
    GpuDeviceConfig config;
    config.validation = GpuValidation::Synchronization;
    if (!graphics.gpu.Initialize(graphics.window, config)) {
        const auto error = graphics.gpu.GetLastError();
        if (error.find("No Vulkan physical devices") != std::string::npos ||
            error.find("No suitable Vulkan device") != std::string::npos ||
            error.find("Vulkan 1.1 loader") != std::string::npos ||
            error.find("VK_ERROR_INCOMPATIBLE_DRIVER") != std::string::npos)
            return 77;
        throw std::runtime_error("Vulkan hierarchy initialization failed: " + error);
    }
    Renderer2D::Context renderer(&graphics.gpu);
    const OrthographicCamera camera(-4, 4, -3, 3);
    Scene scene;
    auto settings = scene.GetSettings();
    settings.exposure = 2;
    Check(scene.SetSettings(settings), "exposure configuration accepted");
    auto parent = scene.CreateEntity(), child = scene.CreateEntity();
    parent.GetComponent<TransformComponent>().position = {-1, 0, 0};
    parent.GetComponent<TransformComponent>().rotation.z = ToRadians(90);
    parent.GetComponent<TransformComponent>().scale = {2, 1, 1};
    child.GetComponent<TransformComponent>().position = {1, 0, 5};
    child.GetComponent<TransformComponent>().rotation.z = .3f;
    child.AddComponent<SpriteRendererComponent>().color = {.25f, .1f, 0, 1};
    Check(scene.SetParent(child.GetId(), parent.GetId(), false), "affine render hierarchy setup");
    const auto capture = [&] {
        Check(graphics.gpu.RequestFrameCapture(), "hierarchy capture requests");
        bool ready = false;
        for (int attempt = 0; attempt < 20; ++attempt) {
            graphics.window.PollEvents();
            const auto result = graphics.gpu.BeginFrame();
            if (result == GpuDevice::FrameStart::Ready) {
                ready = true;
                break;
            }
            Check(result != GpuDevice::FrameStart::Error, "hierarchy frame has no device error");
            SDL_Delay(5);
        }
        Check(ready, "hierarchy capture frame begins");
        graphics.gpu.Clear(.01f, .01f, .01f, 1);
        {
            Renderer2D::ScopedContext scope(renderer);
            renderer.BeginScene(camera);
            scene.OnRender();
            renderer.EndScene();
        }
        Check(graphics.gpu.EndFrame(), "hierarchy frame submits");
        GpuFrameCapture frame;
        Check(graphics.gpu.ReadFrameCapture(frame) && !frame.rgba.empty(),
              "hierarchy image reads back");
        return frame;
    };
    const auto pixel = [](const GpuFrameCapture& frame, float worldX, float worldY) {
        const auto x = static_cast<uint32>((worldX + 4) / 8 * frame.width);
        const auto y = static_cast<uint32>((3 - worldY) / 6 * frame.height);
        const auto index = (static_cast<size_t>(y) * frame.width + x) * 4;
        return std::array<uint8, 3>{frame.rgba[index], frame.rgba[index + 1],
                                    frame.rgba[index + 2]};
    };
    const auto original = capture();
    const auto lit = pixel(original, -1, 2);
    Check(lit[0] > 80 && lit[1] > 25 && lit[2] < 20,
          "affine parent/child renders at its world position even with nonzero sorting Z");
    auto moved = scene.GetWorldTransform(child.GetId());
    moved.position = {1, -1, 5};
    Check(scene.SetWorldTransform(child.GetId(), moved),
          "sheared sprite can be moved in world space");
    settings.exposure = .5f;
    Check(scene.SetSettings(settings), "new exposure accepted");
    const auto changed = capture();
    const auto dark = pixel(changed, 1, -1), empty = pixel(changed, -1, 2);
    Check(dark[0] > empty[0] + 10 && lit[0] > dark[0] + 30 && lit[1] > dark[1] + 10,
          "world movement changes pixels and scene exposure changes actual RGB output");
    renderer.Shutdown();
    Check(graphics.gpu.WaitIdle() && graphics.gpu.GetCounters().validationErrors == 0,
          "affine rendering and resource teardown have no validation errors");
    return 0;
}

int RunSceneCoreChecks(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--gpu")
        return RenderHierarchy();
    Hierarchy();
    HierarchyCodecAndImport();
    ReferenceMetadata();
    SettingsAndPhysics();
    BackwardCompatibilityAndOrigin();
    RuntimePreservingCommit();
    std::cout << "Scene hierarchy, versioned data, references and physics checks passed\n";
    return 0;
}
} // namespace
LUX_ENGINE_MAIN(RunSceneCoreChecks)
