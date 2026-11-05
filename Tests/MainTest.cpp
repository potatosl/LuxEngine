#include "Engine/LuxEngine.hpp"
#include "Engine/Platform/Branding.hpp"
#include "Engine/Renderer/Vulkan/VulkanFault.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stb_image.h>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

namespace {
int checks = 0;
void Check(bool ok, const char* message) {
    ++checks;
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void Throws(F fn, const char* message) {
    bool threw = false;
    try {
        fn();
    } catch (const std::exception&) {
        threw = true;
    }
    Check(threw, message);
}
bool Near(float a, float b) {
    return std::abs(a - b) < 0.0001f;
}
struct Files {
    std::filesystem::path root = std::filesystem::temp_directory_path() /
                                 ("luxengine-tests-" + std::to_string(SDL_GetTicksNS()));
    Files() {
        std::filesystem::create_directories(root);
    }
    ~Files() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
    std::string Path(const char* name) const {
        return (root / name).string();
    }
    void Text(const char* name, const char* data) const {
        std::ofstream(Path(name)) << data;
    }
};
using namespace LuxEngine;
void Core() {
    Random first(42), second(42);
    for (int index = 0; index < 32; ++index) {
        const float value = first.Range(-2.0f, 3.0f);
        Check(value >= -2.0f && value < 3.0f && Near(value, second.Range(-2.0f, 3.0f)),
              "deterministic random range");
    }
    Throws([&] { first.Range(2.0f, 1.0f); }, "reversed random range rejected");
    Throws([&] { first.Range(0.0f, std::numeric_limits<float>::infinity()); },
           "non-finite random range rejected");
    Throws(
        [&] { first.Range(-std::numeric_limits<float>::max(), std::numeric_limits<float>::max()); },
        "overflowing random range rejected");
    auto project = [](const Mat4& m, const Vec3& p) {
        const auto& e = m.elements;
        const float z = e[2] * p.x + e[6] * p.y + e[10] * p.z + e[14];
        const float w = e[3] * p.x + e[7] * p.y + e[11] * p.z + e[15];
        return z / w;
    };
    const Mat4 perspective = Mat4::Perspective(ToRadians(60), 1.0f, 0.5f, 50.0f);
    Check(Near(project(perspective, {0, 0, -0.5f}), 0.0f) &&
              Near(project(perspective, {0, 0, -50.0f}), 1.0f),
          "perspective depth maps near/far to Vulkan's 0..1 range");
    const Mat4 ortho = Mat4::Orthographic(-1, 1, -1, 1, -1, 1);
    Check(Near(project(ortho, {0, 0, 1}), 0.0f) && Near(project(ortho, {0, 0, -1}), 1.0f),
          "orthographic depth maps into Vulkan's 0..1 range");
    static_assert(!std::is_move_constructible_v<Window>);
    static_assert(!std::is_move_constructible_v<GpuDevice>);
    GpuDevice uninitializedGpu;
    Check(uninitializedGpu.BeginFrame() == GpuDevice::FrameStart::Error,
          "uninitialized GPU frame rejected");
    Check(!uninitializedGpu.Clear(0, 0, 0, 1), "uninitialized GPU clear rejected");
    Check(!uninitializedGpu.EndFrame(), "uninitialized GPU submission rejected");
    Timer timer;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    Check(timer.ElapsedSeconds() > 0, "timer");
    Check(Near(Vec3(1, 0, 0).Cross({0, 1, 0}).z, 1), "cross product");
    WindowCloseEvent event;
    EventDispatcher dispatcher(event);
    dispatcher.Dispatch<WindowCloseEvent>([](auto&) { return true; });
    dispatcher.Dispatch<WindowCloseEvent>([](auto&) { return false; });
    Check(event.Handled, "handled event cannot be cleared");
    OrthographicCamera camera(-1, 1, -1, 1);
    camera.SetPosition({2, 3, 0});
    camera.SetRotation(0.7f);
    auto identity = camera.GetViewMatrix() * (Mat4::Translate({2, 3, 0}) * Mat4::RotateZ(0.7f));
    for (int i = 0; i < 16; ++i)
        Check(Near(identity.elements[i], i % 5 == 0 ? 1.0f : 0.0f), "camera inverse");
    TransformComponent transform;
    transform.rotation.x = ToRadians(90);
    Check(Near(transform.GetTransform().elements[6], 1), "X rotation");
    transform.rotation = {0, ToRadians(90), 0};
    Check(Near(transform.GetTransform().elements[2], -1), "Y rotation");
    AnimationClip clip;
    clip.duration = 2;
    clip.loop = false;
    clip.AddKeyframe({1, {10, 0, 0}});
    clip.AddKeyframe({0, {0, 0, 0}});
    Check(Near(clip.Sample(0.5f).position.x, 5), "animation sorted interpolation");
    Check(Near(clip.Sample(1.5f).position.x, 10), "animation last frame held");
    clip.AddKeyframe({1, {20, 0, 0}});
    Check(clip.keyframes.size() == 2 && Near(clip.Sample(1).position.x, 20),
          "duplicate animation time");
    clip.loop = true;
    Check(Near(clip.Sample(-0.5f).position.x, 20), "negative animation time");
    Throws([&] { clip.AddKeyframe({-1}); }, "invalid keyframe rejected");
    PhysicsEngine::Init();
    Vec3 p{0, 0.5f, 0};
    Rigidbody2DComponent body;
    BoxCollider2DComponent collider;
    PhysicsEngine::Step2D(p, body, collider, 1.0 / 60);
    Check(body.isGrounded && Near(p.y, 0.5f), "ground contact");
    body.velocity.y = 2;
    PhysicsEngine::Step2D(p, body, collider, 1.0 / 60);
    Check(p.y > 0.5f && !body.isGrounded, "jump leaves ground");
    p = {0, 0, 0};
    body = {};
    collider.isTrigger = true;
    PhysicsEngine::Step2D(p, body, collider, 1.0 / 60);
    Check(p.y < 0 && !body.isGrounded, "trigger does not block");
    body.type = BodyType::Kinematic;
    body.velocity = {3, 0};
    p = {0, 5, 0};
    PhysicsEngine::Step2D(p, body, collider, 0.1);
    Check(Near(p.x, 0.3f) && Near(p.y, 5), "kinematic motion");
    const auto before = p;
    PhysicsEngine::Step2D(p, body, collider, -1);
    Check(Near(p.x, before.x), "invalid timestep ignored");
    Check(!Input::IsMouseButtonPressed(0) && !Input::IsMouseButtonPressed(33),
          "invalid mouse buttons");
    Check(!Input::IsGamepadButtonPressed(static_cast<LuxGamepadButton>(-1)),
          "invalid gamepad button");
}
void Physics2D() {
    PhysicsEngine::Init();
    PhysicsEngine::SetGravity2D({0, -9.81f});
    Files files;
    {
        Scene scene;
        auto floor = scene.CreateEntity("floor");
        floor.GetComponent<TransformComponent>().position = {0, -0.5f, 0};
        floor.AddComponent<BoxCollider2DComponent>().size = {20, 1};
        auto falling = scene.CreateEntity("falling");
        falling.GetComponent<TransformComponent>().position = {0, 2.5f, 7};
        falling.AddComponent<Rigidbody2DComponent>();
        falling.AddComponent<BoxCollider2DComponent>();
        const auto initial = falling.GetComponent<TransformComponent>().position;
        scene.OnUpdate(-1);
        Check(Near(falling.GetComponent<TransformComponent>().position.y, initial.y),
              "invalid scene timestep ignored");
        bool contact = false;
        for (int i = 0; i < 180; ++i) {
            scene.OnUpdate(1.0 / 60.0);
            for (const auto& event : scene.GetCollisionEvents2D()) {
                if ((event.entityA == floor.GetId() && event.entityB == falling.GetId()) ||
                    (event.entityB == floor.GetId() && event.entityA == falling.GetId())) {
                    Check(!event.isTrigger, "solid contact is not a trigger");
                    contact = true;
                }
            }
        }
        const auto& transform = falling.GetComponent<TransformComponent>();
        const auto& body = falling.GetComponent<Rigidbody2DComponent>();
        Check(contact, "static collider receives falling body contact");
        Check(transform.position.y > 0.4f && transform.position.y < 0.65f,
              "Box2D body rests on static floor");
        Check(Near(transform.position.z, 7), "2D physics preserves Z position");
        Check(body.isGrounded && std::abs(body.velocity.y) < 0.2f,
              "resting body reports grounded state");
        falling.GetComponent<TransformComponent>().position.y = 3;
        for (int i = 0; i < 30; ++i)
            scene.OnUpdate(1.0 / 60.0);
        Check(falling.GetComponent<TransformComponent>().position.y < 2.5f,
              "teleported sleeping body falls again");

        Scene replacement;
        Check(replacement.Serialize(files.Path("empty-physics.lux")),
              "empty replacement scene saved");
        Check(scene.Deserialize(files.Path("empty-physics.lux")), "active physics scene reloads");
        Check(!floor.IsValid() && !falling.IsValid() && scene.GetEntityCount() == 0,
              "reload removes old physics entities");
        Check(scene.GetCollisionEvents2D().empty(), "reload clears contact events");
        auto afterReload = scene.CreateEntity("after reload");
        afterReload.GetComponent<TransformComponent>().position = {0, 1, 0};
        afterReload.AddComponent<Rigidbody2DComponent>();
        afterReload.AddComponent<BoxCollider2DComponent>();
        for (int i = 0; i < 90; ++i)
            scene.OnUpdate(1.0 / 60.0);
        Check(afterReload.GetComponent<TransformComponent>().position.y < -2,
              "reloaded scene has no old floor body");
    }
    {
        Scene scene;
        auto floor = scene.CreateEntity("removable floor");
        floor.GetComponent<TransformComponent>().position = {0, -0.5f, 0};
        floor.AddComponent<BoxCollider2DComponent>().size = {20, 1};
        scene.OnUpdate(1.0 / 60.0);
        scene.DestroyEntity(floor);
        auto faller = scene.CreateEntity("after deletion");
        faller.GetComponent<TransformComponent>().position = {0, 1, 0};
        faller.AddComponent<Rigidbody2DComponent>();
        faller.AddComponent<BoxCollider2DComponent>();
        for (int i = 0; i < 90; ++i)
            scene.OnUpdate(1.0 / 60.0);
        Check(faller.GetComponent<TransformComponent>().position.y < -2,
              "deleted collider no longer blocks bodies");
    }
    {
        Scene scene;
        auto floor = scene.CreateEntity("floor with removable collider");
        floor.GetComponent<TransformComponent>().position = {0, -0.5f, 0};
        floor.AddComponent<BoxCollider2DComponent>().size = {20, 1};
        auto faller = scene.CreateEntity("body above removable collider");
        faller.GetComponent<TransformComponent>().position = {0, 1, 0};
        faller.AddComponent<Rigidbody2DComponent>();
        faller.AddComponent<BoxCollider2DComponent>();
        for (int i = 0; i < 90; ++i)
            scene.OnUpdate(1.0 / 60.0);
        Check(faller.GetComponent<Rigidbody2DComponent>().isGrounded,
              "body rests before collider removal");
        floor.RemoveComponent<BoxCollider2DComponent>();
        for (int i = 0; i < 90; ++i)
            scene.OnUpdate(1.0 / 60.0);
        Check(faller.GetComponent<TransformComponent>().position.y < -2,
              "removed collider no longer blocks bodies");
    }
    {
        Scene scene;
        auto floor = scene.CreateEntity("movable floor");
        floor.GetComponent<TransformComponent>().position = {0, -0.5f, 0};
        floor.AddComponent<BoxCollider2DComponent>().size = {20, 1};
        auto sleeper = scene.CreateEntity("body on movable floor");
        sleeper.GetComponent<TransformComponent>().position = {0, 1, 0};
        sleeper.AddComponent<Rigidbody2DComponent>();
        sleeper.AddComponent<BoxCollider2DComponent>();
        for (int i = 0; i < 180; ++i)
            scene.OnUpdate(1.0 / 60.0);
        Check(sleeper.GetComponent<Rigidbody2DComponent>().isGrounded,
              "body rests before floor moves");
        floor.GetComponent<TransformComponent>().position.y = -10.5f;
        for (int i = 0; i < 60; ++i)
            scene.OnUpdate(1.0 / 60.0);
        Check(sleeper.GetComponent<TransformComponent>().position.y < -2,
              "moving a static floor wakes its resting body");
    }
    {
        Scene scene;
        auto body = scene.CreateEntity("invalid mass and size");
        body.GetComponent<TransformComponent>().position = {0, 3, 0};
        auto& rigidbody = body.AddComponent<Rigidbody2DComponent>();
        auto& collider = body.AddComponent<BoxCollider2DComponent>();
        rigidbody.mass = std::numeric_limits<float>::denorm_min();
        scene.OnUpdate(1.0 / 60.0);
        Check(Near(body.GetComponent<TransformComponent>().position.y, 3),
              "subnormal mass is ignored safely");
        rigidbody.mass = std::numeric_limits<float>::max();
        scene.OnUpdate(1.0 / 60.0);
        Check(Near(body.GetComponent<TransformComponent>().position.y, 3),
              "overflowing density is ignored safely");
        rigidbody.mass = 1;
        collider.size = {0.001f, 0.001f};
        scene.OnUpdate(1.0 / 60.0);
        Check(Near(body.GetComponent<TransformComponent>().position.y, 3),
              "undersized collider is ignored safely");
        collider.size = {1, 1};
        scene.OnUpdate(1.0 / 60.0);
        Check(body.GetComponent<TransformComponent>().position.y < 3,
              "valid body resumes simulation after invalid values");
    }
    {
        PhysicsEngine::SetGravity2D({0, 0});
        Scene scene;
        auto entity = scene.CreateEntity("small body with distant collider center");
        auto& transform = entity.GetComponent<TransformComponent>();
        transform.position = {10, -5, 7};
        transform.rotation.z = ToRadians(45);
        entity.AddComponent<Rigidbody2DComponent>().mass = 0.001f;
        auto& collider = entity.AddComponent<BoxCollider2DComponent>();
        collider.size = {0.02f, 0.02f};
        collider.offset = {10'000, 10'000};
        scene.OnUpdate(1.0 / 60.0);
        Check(std::abs(transform.position.x - 10) < 0.02f &&
                  std::abs(transform.position.y + 5) < 0.02f,
              "large rotated collider offset preserves entity origin");
        Check(Near(transform.position.z, 7), "offset body preserves Z position");
        transform.rotation.z = ToRadians(20);
        scene.OnUpdate(1.0 / 60.0);
        Check(std::abs(transform.position.x - 10) < 0.02f &&
                  std::abs(transform.position.y + 5) < 0.02f,
              "changing rotation recomputes collider center without moving entity origin");
        entity.GetComponent<Rigidbody2DComponent>().velocity = {2, 0};
        for (int i = 0; i < 60; ++i)
            scene.OnUpdate(1.0 / 60.0);
        Check(std::abs(transform.position.x - 12) < 0.05f &&
                  std::abs(transform.position.y + 5) < 0.02f,
              "offset body motion synchronizes back to entity position");
        PhysicsEngine::SetGravity2D({0, -9.81f});
    }
    {
        Scene groundedScene;
        auto floor = groundedScene.CreateEntity("scene A floor");
        floor.GetComponent<TransformComponent>().position = {0, -0.5f, 0};
        floor.AddComponent<BoxCollider2DComponent>().size = {20, 1};
        auto grounded = groundedScene.CreateEntity("scene A body");
        grounded.GetComponent<TransformComponent>().position = {0, 1, 0};
        grounded.AddComponent<Rigidbody2DComponent>();
        grounded.AddComponent<BoxCollider2DComponent>();
        Scene fallingScene;
        auto independent = fallingScene.CreateEntity("scene B body");
        independent.GetComponent<TransformComponent>().position = {0, 1, 0};
        independent.AddComponent<Rigidbody2DComponent>();
        independent.AddComponent<BoxCollider2DComponent>();
        for (int i = 0; i < 90; ++i) {
            groundedScene.OnUpdate(1.0 / 60.0);
            fallingScene.OnUpdate(1.0 / 60.0);
            Check(fallingScene.GetCollisionEvents2D().empty(),
                  "independent scene has no foreign contacts");
        }
        Check(grounded.GetComponent<TransformComponent>().position.y > 0.4f,
              "scene A body rests on its floor");
        Check(independent.GetComponent<TransformComponent>().position.y < -2,
              "scene B body has no scene A floor");
    }
    {
        Scene scene;
        auto suspended = scene.CreateEntity("sleeping body with gravity scale");
        suspended.GetComponent<TransformComponent>().position = {0, 3, 0};
        suspended.AddComponent<Rigidbody2DComponent>().gravityScale = 0;
        suspended.AddComponent<BoxCollider2DComponent>();
        for (int i = 0; i < 120; ++i)
            scene.OnUpdate(1.0 / 60.0);
        Check(Near(suspended.GetComponent<TransformComponent>().position.y, 3),
              "zero gravity scale keeps body suspended");
        suspended.GetComponent<Rigidbody2DComponent>().gravityScale = 1;
        for (int i = 0; i < 30; ++i)
            scene.OnUpdate(1.0 / 60.0);
        Check(suspended.GetComponent<TransformComponent>().position.y < 2.5f,
              "gravity scale change wakes sleeping body");
    }
    {
        Scene scene;
        auto mover = scene.CreateEntity("kinematic");
        mover.GetComponent<TransformComponent>().position = {0, 3, 0};
        auto& body = mover.AddComponent<Rigidbody2DComponent>();
        body.type = BodyType::Kinematic;
        body.velocity = {3, 0};
        mover.AddComponent<BoxCollider2DComponent>();
        for (int i = 0; i < 60; ++i)
            scene.OnUpdate(1.0 / 60.0);
        const auto position = mover.GetComponent<TransformComponent>().position;
        Check(std::abs(position.x - 3.0f) < 0.1f && Near(position.y, 3),
              "kinematic body ignores gravity");
    }
    {
        Scene scene;
        auto entity = scene.CreateEntity("3D body with 2D collider");
        entity.GetComponent<TransformComponent>().position = {0, 3, 0};
        entity.AddComponent<BoxCollider2DComponent>();
        entity.AddComponent<BoxCollider3DComponent>();
        entity.AddComponent<Rigidbody3DComponent>().velocity = {1, 0, 2};
        scene.OnUpdate(0.1);
        const auto position = entity.GetComponent<TransformComponent>().position;
        Check(Near(position.x, 0.1f) && position.y < 3 && Near(position.z, 0.2f),
              "3D physics updates body with 2D collider but no 2D rigidbody");
    }
    PhysicsEngine::SetGravity2D({0, 0});
    {
        Scene scene;
        auto mover = scene.CreateEntity("velocity edited at runtime");
        mover.AddComponent<Rigidbody2DComponent>();
        mover.AddComponent<BoxCollider2DComponent>();
        scene.OnUpdate(1.0 / 60.0);
        mover.GetComponent<Rigidbody2DComponent>().velocity = {2, 0};
        for (int i = 0; i < 60; ++i)
            scene.OnUpdate(1.0 / 60.0);
        Check(std::abs(mover.GetComponent<TransformComponent>().position.x - 2.0f) < 0.1f,
              "runtime velocity change reaches Box2D body");
        const float positionBeforeRemoval = mover.GetComponent<TransformComponent>().position.x;
        mover.RemoveComponent<Rigidbody2DComponent>();
        for (int i = 0; i < 60; ++i)
            scene.OnUpdate(1.0 / 60.0);
        Check(Near(mover.GetComponent<TransformComponent>().position.x, positionBeforeRemoval),
              "removing rigidbody leaves a static collider");
    }
    {
        Scene scene;
        auto trigger = scene.CreateEntity("trigger");
        trigger.AddComponent<BoxCollider2DComponent>().isTrigger = true;
        auto mover = scene.CreateEntity("passing body");
        mover.GetComponent<TransformComponent>().position = {-3, 0, 0};
        mover.AddComponent<Rigidbody2DComponent>().velocity = {4, 0};
        mover.AddComponent<BoxCollider2DComponent>().size = {0.5f, 0.5f};
        int triggerBegins = 0;
        for (int i = 0; i < 90; ++i) {
            scene.OnUpdate(1.0 / 60.0);
            for (const auto& event : scene.GetCollisionEvents2D()) {
                if ((event.entityA == trigger.GetId() && event.entityB == mover.GetId()) ||
                    (event.entityB == trigger.GetId() && event.entityA == mover.GetId())) {
                    Check(event.isTrigger, "sensor contact is marked as trigger");
                    ++triggerBegins;
                }
            }
        }
        Check(triggerBegins == 1, "sensor reports one begin contact");
        Check(mover.GetComponent<TransformComponent>().position.x > 1,
              "trigger does not block movement");
        Check(scene.GetCollisionEvents2D().empty(), "begin contacts clear on later steps");
    }
    {
        Scene scene;
        auto suspended = scene.CreateEntity("sleeping body before world gravity change");
        suspended.GetComponent<TransformComponent>().position = {0, 3, 0};
        suspended.AddComponent<Rigidbody2DComponent>();
        suspended.AddComponent<BoxCollider2DComponent>();
        for (int i = 0; i < 120; ++i)
            scene.OnUpdate(1.0 / 60.0);
        Check(Near(suspended.GetComponent<TransformComponent>().position.y, 3),
              "zero world gravity keeps body suspended");
        PhysicsEngine::SetGravity2D({0, -9.81f});
        for (int i = 0; i < 30; ++i)
            scene.OnUpdate(1.0 / 60.0);
        Check(suspended.GetComponent<TransformComponent>().position.y < 2.5f,
              "world gravity change wakes sleeping body");
    }
    PhysicsEngine::Shutdown();
}
void Scenes() {
    Scene scene;
    auto a = scene.CreateEntity("a"), b = scene.CreateEntity("b");
    struct Custom {
        int value = 7;
    };
    a.AddComponent<Custom>();
    a.AddComponent<Rigidbody2DComponent>();
    scene.DestroyEntity(a);
    Check(!a.IsValid() && !a.HasComponent<Custom>() && !a.HasComponent<Rigidbody2DComponent>(),
          "destroy all components");
    Check(a.TryGetComponent<Custom>() == nullptr, "stale entity lookup is safe");
    scene.DestroyEntity(a);
    Check(scene.GetEntityCount() == 1 && b.IsValid(), "double destruction");
    Scene foreign;
    auto wrong = foreign.CreateEntity();
    scene.DestroyEntity(wrong);
    Check(b.HasComponent<TagComponent>() && scene.GetEntityCount() == 1, "foreign scene protected");
    const Scene& view = scene;
    Throws([&] { view.GetComponent<SpriteRendererComponent>(b.GetId()); },
           "const missing component throws");
    Check(!b.HasComponent<SpriteRendererComponent>(), "query never inserts");
    Check(b.TryGetComponent<SpriteRendererComponent>() == nullptr &&
              view.TryGetComponent<SpriteRendererComponent>(b.GetId()) == nullptr,
          "missing component lookup is safe");
    Check(b.TryGetComponent<TagComponent>() != nullptr &&
              view.TryGetComponent<TagComponent>(b.GetId()) != nullptr,
          "present component lookup returns value");
    Throws([&] { scene.RemoveComponent<TagComponent>(a.GetId()); },
           "stale direct component removal rejected");
    Throws([&] { a.GetComponent<TagComponent>(); }, "stale entity throws");
    Throws([&] { b.AddComponent<TagComponent>(); }, "duplicate component throws");
    Entity orphan;
    {
        Scene shortLived;
        orphan = shortLived.CreateEntity();
    }
    Check(!orphan.IsValid() && !orphan.HasComponent<TagComponent>(), "scene lifetime tracked");
    Files files;
    auto persistent = scene.CreateEntity("spaces and \"quotes\"");
    persistent.GetComponent<TransformComponent>().position = {1.25f, 2, 3};
    persistent.AddComponent<Rigidbody2DComponent>().velocity = {4, 5};
    persistent.AddComponent<BoxCollider3DComponent>();
    persistent.AddComponent<SpriteRendererComponent>().color = {0.2f, 0.4f, 0.6f, 1};
    Check(scene.Serialize(files.Path("scene.lux")), "serialize");
    Check(scene.Deserialize(files.Path("scene.lux")), "deserialize");
    Check(!persistent.IsValid() && !b.IsValid() && scene.GetEntityCount() == 2,
          "load invalidates old handles");
    auto restored = scene.GetEntities();
    Check(restored[1].GetComponent<TagComponent>().tag == "spaces and \"quotes\"", "loaded name");
    Check(Near(restored[1].GetComponent<TransformComponent>().position.x, 1.25f),
          "loaded transform");
    Check(Near(restored[1].GetComponent<Rigidbody2DComponent>().velocity.x, 4), "loaded physics");
    Check(scene.Serialize(files.Path("roundtrip.lux")), "reserialize");
    auto bytes = [](const std::string& path) {
        std::ifstream in(path);
        return std::string(std::istreambuf_iterator<char>(in), {});
    };
    Check(bytes(files.Path("scene.lux")) == bytes(files.Path("roundtrip.lux")),
          "component roundtrip");
    files.Text("bad.lux", "LuxEngineScene 1 1\n1 \"partial\"\n");
    Check(!scene.Deserialize(files.Path("bad.lux")) && scene.GetEntityCount() == 2,
          "failed load is transactional");
    auto custom = scene.CreateEntity();
    custom.AddComponent<Custom>();
    Check(!scene.Serialize(files.Path("unsupported.lux")),
          "custom components never silently discarded");
}
void Assets() {
    double brightness[2]{};
    for (int variant = 0; variant < 2; ++variant) {
        const auto icon = Branding::WindowIcon(variant == 0);
        int width = 0, height = 0, channels = 0;
        auto* pixels = stbi_load_from_memory(icon.data(), static_cast<int>(icon.size()), &width,
                                             &height, &channels, 4);
        Check(pixels && width > 0 && height > 0, "embedded logo PNG decodes");
        double total = 0, weight = 0;
        for (int i = 0; i < width * height; ++i) {
            const double alpha = pixels[i * 4 + 3] / 255.0;
            total += (pixels[i * 4] + pixels[i * 4 + 1] + pixels[i * 4 + 2]) * alpha;
            weight += alpha * 3;
        }
        stbi_image_free(pixels);
        Check(weight > 0, "logo contains visible pixels");
        brightness[variant] = total / weight;
    }
    Check(brightness[0] > brightness[1], "dark theme selects brighter logo");
    for (bool dark : {false, true}) {
        auto* icon = Branding::CreateWindowIcon(dark);
        Check(icon && icon->w == 32 && icon->h == 32, "window icon has native base size");
        Check(SDL_SurfaceHasAlternateImages(icon), "window icon includes high DPI sizes");
        int left = 32, top = 32, right = -1, bottom = -1;
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x) {
                const auto* pixel =
                    static_cast<const uint8*>(icon->pixels) + y * icon->pitch + x * 4;
                if (pixel[3] > 32) {
                    left = std::min(left, x);
                    top = std::min(top, y);
                    right = std::max(right, x);
                    bottom = std::max(bottom, y);
                }
            }
        Check(std::max(right - left + 1, bottom - top + 1) >= 28,
              "logo fills icon area after transparent margin removal");
        SDL_DestroySurface(icon);
    }
    Files files;
    RawMeshData mesh;
    files.Text("quad.obj", "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nf -4 -3 -2 -1\n");
    Check(AssetManager::LoadMesh(files.Path("quad.obj"), mesh), "OBJ load");
    Check(mesh.vertices.size() == 12 && mesh.indices.size() == 6, "OBJ triangulation");
    files.Text("bad.obj", "v 0 0 0\nf 0 1 2\n");
    Check(!AssetManager::LoadMesh(files.Path("bad.obj"), mesh) && mesh.vertices.empty(),
          "invalid mesh fails");
    Check(!AssetManager::LoadMesh(files.Path("missing.obj"), mesh), "missing mesh fails");
    Check(!AssetManager::LoadTexture(files.Path("missing.png")), "missing device fails");
    Check(!AssetManager::HasTexture(files.Path("missing.png")), "failed image not cached");
    std::vector<uint8> bytes{1, 2};
    files.Text("empty", "");
    Check(!AssetManager::LoadShaderBytes(files.Path("empty"), bytes) && bytes.empty(),
          "empty shader fails");
    files.Text("shader", "abc");
    Check(AssetManager::LoadShaderBytes(files.Path("shader"), bytes) && bytes.size() == 3,
          "shader load");
    AssetManager::Shutdown();
}
void Wav(const std::string& file) {
    std::ofstream out(file, std::ios::binary);
    auto u16 = [&](uint16 value) {
        for (int i = 0; i < 2; ++i)
            out.put(static_cast<char>((value >> (i * 8)) & 255));
    };
    auto u32 = [&](uint32 value) {
        for (int i = 0; i < 4; ++i)
            out.put(static_cast<char>((value >> (i * 8)) & 255));
    };
    out.write("RIFF", 4);
    u32(36 + 16000);
    out.write("WAVEfmt ", 8);
    u32(16);
    u16(1);
    u16(1);
    u32(8000);
    u32(16000);
    u16(2);
    u16(16);
    out.write("data", 4);
    u32(16000);
    for (int i = 0; i < 8000; ++i)
        u16(static_cast<uint16>(static_cast<int16>(std::sin(i * 0.2) * 1000)));
}
void Audio() {
    Files files;
    Wav(files.Path("tone.wav"));
    Check(!AudioEngine::PlayMusic(files.Path("tone.wav")), "uninitialized audio fails");
    Check(AudioEngine::Init() && AudioEngine::Init(), "audio initialization idempotent");
    Check(!AudioEngine::PlayMusic(files.Path("missing.wav")), "missing audio fails");
    Check(!AudioEngine::IsMusicPlaying(), "missing audio never plays");
    AudioEngine::SetMasterVolume(2);
    Check(Near(AudioEngine::GetMasterVolume(), 1), "volume clamp");
    Check(AudioEngine::PlayMusic(files.Path("tone.wav"), true, 0.1f), "WAV playback");
    Check(AudioEngine::IsMusicPlaying(), "music is playing");
    Check(AudioEngine::PlaySound(files.Path("tone.wav"), 0.1f), "sound playback");
    AudioEngine::StopMusic();
    Check(!AudioEngine::IsMusicPlaying(), "stop music");
    Check(AudioEngine::PlayMusic(files.Path("tone.wav"), false), "non-loop playback");
    std::this_thread::sleep_for(std::chrono::milliseconds(1400));
    Check(!AudioEngine::IsMusicPlaying(), "music ends naturally");
    AudioEngine::Shutdown();
    AudioEngine::Shutdown();
    Check(!AudioEngine::IsMusicPlaying(), "audio shutdown");
}
void WindowEvents() {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    Check(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD), "dummy video initializes");
    Input::Initialize();
    {
        Window window;
        WindowProps props;
        props.title = "LuxEngine window test";
        props.width = 320;
        props.height = 240;
        Check(window.Initialize(props), "window initializes");
        auto* handle = window.GetNativeHandle();
        Check(!window.Initialize() && window.GetNativeHandle() == handle,
              "duplicate initialization preserves window");
        Check(window.GetWidth() == 320 && window.GetHeight() == 240, "initial window dimensions");
        GpuDevice unavailable;
        Check(!unavailable.Initialize(window) && !unavailable.IsReady(),
              "GPU device rejects a window created without Vulkan support");
        Check(SDL_SetWindowSize(handle, 400, 300), "window resizes");
        Check(window.GetWidth() == 400 && window.GetHeight() == 300,
              "window dimensions stay current");

        SDL_Event event{};
        event.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
        event.window.windowID = SDL_GetWindowID(handle) + 1;
        Check(SDL_PushEvent(&event), "foreign close event queued");
        window.PollEvents();
        Check(window.IsRunning(), "foreign close event ignored");

        event = {};
        event.type = SDL_EVENT_KEY_DOWN;
        event.key.windowID = SDL_GetWindowID(handle) + 1;
        event.key.key = SDLK_ESCAPE;
        Check(SDL_PushEvent(&event), "foreign escape event queued");
        window.PollEvents();
        Check(window.IsRunning(), "foreign escape event ignored");

        Check(!Input::BindAction("", LuxKey::A) &&
                  !Input::BindAction("Jump", static_cast<LuxKey>(-1)),
              "invalid action bindings rejected");
        Check(Input::BindAction("Jump", LuxKey::A) && Input::BindAction("Jump", LuxKey::B) &&
                  Input::BindAction("Jump", LuxMouseButton::Left) &&
                  Input::BindAction("Jump", LuxGamepadButton::South),
              "keyboard mouse and gamepad bindings added");
        Check(!Input::BindAction("Jump", LuxKey::A), "duplicate binding rejected");
        event = {};
        event.type = SDL_EVENT_KEY_DOWN;
        event.key.windowID = SDL_GetWindowID(handle) + 1;
        event.key.key = SDLK_A;
        Check(SDL_PushEvent(&event), "foreign bound key queued");
        window.PollEvents();
        Check(!Input::IsActionDown("Jump"), "foreign key does not trigger action");

        event.key.windowID = SDL_GetWindowID(handle);
        Check(SDL_PushEvent(&event), "bound key queued");
        window.PollEvents();
        Check(Input::IsActionDown("Jump") && Input::WasActionPressed("Jump"), "key action pressed");
        Input::AdvanceFrame();
        Check(Input::IsActionDown("Jump") && !Input::WasActionPressed("Jump"),
              "held action survives frame advance");

        event.key.key = SDLK_B;
        Check(SDL_PushEvent(&event), "alternate key queued");
        event.type = SDL_EVENT_KEY_UP;
        event.key.key = SDLK_A;
        Check(SDL_PushEvent(&event), "first key release queued");
        window.PollEvents();
        Check(Input::IsActionDown("Jump") && !Input::WasActionReleased("Jump"),
              "alternate key holds action");

        event = {};
        event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        event.button.windowID = SDL_GetWindowID(handle);
        event.button.button = SDL_BUTTON_LEFT;
        Check(SDL_PushEvent(&event), "bound mouse queued");
        event.type = SDL_EVENT_KEY_UP;
        event.key.windowID = SDL_GetWindowID(handle);
        event.key.key = SDLK_B;
        Check(SDL_PushEvent(&event), "second key release queued");
        window.PollEvents();
        Check(Input::IsActionDown("Jump") && !Input::WasActionReleased("Jump"),
              "mouse holds action after key release");
        event = {};
        event.type = SDL_EVENT_MOUSE_BUTTON_UP;
        event.button.windowID = SDL_GetWindowID(handle);
        event.button.button = SDL_BUTTON_LEFT;
        Check(SDL_PushEvent(&event), "bound mouse release queued");
        window.PollEvents();
        Check(!Input::IsActionDown("Jump") && Input::WasActionReleased("Jump"),
              "mouse release ends action");
        Input::AdvanceFrame();

        event = {};
        event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        event.gbutton.which = 919;
        event.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
        Check(SDL_PushEvent(&event), "gamepad press queued");
        window.PollEvents();
        Check(Input::IsActionDown("Jump") && Input::WasActionPressed("Jump"),
              "gamepad button triggers action");
        event = {};
        event.type = SDL_EVENT_GAMEPAD_REMOVED;
        event.gdevice.which = 919;
        Check(SDL_PushEvent(&event), "gamepad removal queued");
        window.PollEvents();
        Check(!Input::IsActionDown("Jump") && Input::WasActionReleased("Jump"),
              "gamepad removal releases action");
        Check(Input::UnbindAction("Jump", LuxKey::A) && !Input::UnbindAction("Jump", LuxKey::A) &&
                  Input::ClearAction("Jump"),
              "action bindings can be removed");
        Check(Input::BindAction("Menu", LuxKey::Escape), "escape action bound");
        event = {};
        event.type = SDL_EVENT_KEY_DOWN;
        event.key.windowID = SDL_GetWindowID(handle);
        event.key.key = SDLK_ESCAPE;
        Check(SDL_PushEvent(&event), "own escape event queued");
        window.PollEvents();
        Check(window.IsRunning() && Input::WasActionPressed("Menu"),
              "escape belongs to action instead of closing the window");
        Input::ClearAction("Menu");
        Check(Input::BindAction("Focus", LuxKey::A), "focus action bound");
        event = {};
        event.type = SDL_EVENT_KEY_DOWN;
        event.key.windowID = SDL_GetWindowID(handle);
        event.key.key = SDLK_A;
        Check(SDL_PushEvent(&event), "held key queued");
        window.PollEvents();
        event = {};
        event.type = SDL_EVENT_WINDOW_FOCUS_LOST;
        event.window.windowID = SDL_GetWindowID(handle);
        Check(SDL_PushEvent(&event), "focus loss queued");
        window.PollEvents();
        Check(!Input::IsActionDown("Focus") && Input::WasActionReleased("Focus"),
              "focus loss releases held keyboard action");
        Input::ClearAction("Focus");

        int moves = 0;
        window.SetEventCallback([&](Event& e) {
            if (e.GetEventType() == EventType::MouseMoved)
                ++moves;
        });
        SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
        constexpr int flood = 10000;
        for (int index = 0; index < flood; ++index) {
            event = {};
            event.type = SDL_EVENT_MOUSE_MOTION;
            event.motion.windowID = SDL_GetWindowID(handle);
            event.motion.x = static_cast<float>(index);
            SDL_PushEvent(&event);
        }
        window.PollEvents();
        Check(moves > 0 && moves <= static_cast<int>(Window::MaxEventsPerPoll),
              "event flood is bounded per poll");
        for (int poll = 0; poll < 8 && moves < flood; ++poll)
            window.PollEvents();
        Check(moves == flood, "flooded events are delivered over later polls");
        window.SetEventCallback({});

        event = {};
        event.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
        event.window.windowID = SDL_GetWindowID(handle);
        Check(SDL_PushEvent(&event), "own close event queued");
        window.PollEvents();
        Check(!window.IsRunning(), "own close event stops window");
    }
    Input::Shutdown();
    SDL_Quit();
}
struct SkipTest : std::runtime_error {
    using std::runtime_error::runtime_error;
};
bool MissingVulkanDevice(const std::string& error) {
    return error.find("No Vulkan physical devices") != std::string::npos ||
           error.find("No suitable Vulkan device") != std::string::npos ||
           error.find("Vulkan 1.1 loader") != std::string::npos ||
           error.find("VK_ERROR_INCOMPATIBLE_DRIVER") != std::string::npos;
}
struct Graphics {
    Window window;
    GpuDevice gpu;
    explicit Graphics(const GpuDeviceConfig& config = {}, uint32 width = 320, uint32 height = 240,
                      bool checkTheme = true) {
        if (!SDL_Init(SDL_INIT_VIDEO))
            throw SkipTest(std::string("no video driver: ") + SDL_GetError());
        WindowProps props;
        props.width = width;
        props.height = height;
        props.title = "LuxEngine GPU verification";
        props.vulkan = true;
        if (!window.Initialize(props)) {
            SDL_Quit();
            throw SkipTest("no Vulkan-capable window system");
        }
        if (checkTheme) {
            const bool iconAvailable = window.HasWindowIcon();
            Check(iconAvailable || std::string_view(SDL_GetCurrentVideoDriver()) == "wayland" ||
                      std::string_view(SDL_GetCurrentVideoDriver()) == "offscreen",
                  "window icon applied when supported");
            const auto expected = SDL_GetSystemTheme() == SDL_SYSTEM_THEME_DARK ? ColorTheme::Dark
                                                                                : ColorTheme::Light;
            if (iconAvailable)
                Check(window.GetColorTheme() == expected, "window icon follows system theme");
            SDL_Event changed{};
            changed.type = SDL_EVENT_SYSTEM_THEME_CHANGED;
            Check(SDL_PushEvent(&changed), "theme event queued");
            window.PollEvents();
            if (iconAvailable)
                Check(window.HasWindowIcon() && window.GetColorTheme() == expected,
                      "theme event preserves correct icon");
        }
        if (!gpu.Initialize(window, config)) {
            const std::string error = gpu.GetLastError();
            window.Shutdown();
            SDL_Quit();
            if (MissingVulkanDevice(error))
                throw SkipTest("no Vulkan device: " + error);
            throw std::runtime_error("GPU initialization failed: " + error);
        }
    }
    ~Graphics() {
        AssetManager::Shutdown();
        Renderer2D::Shutdown();
        Renderer3D::Shutdown();
        gpu.Shutdown();
        window.Shutdown();
        SDL_Quit();
    }
    bool Begin(int attempts = 8) {
        for (int attempt = 0; attempt < attempts; ++attempt) {
            window.PollEvents();
            const auto start = gpu.BeginFrame();
            if (start == GpuDevice::FrameStart::Ready)
                return true;
            if (start == GpuDevice::FrameStart::Error)
                return false;
            SDL_Delay(5);
        }
        return false;
    }
    template <class F> GpuFrameCapture Capture(F&& draw) {
        Check(gpu.RequestFrameCapture(), "frame capture requested");
        Check(Begin(), "capture frame begins");
        draw();
        Check(gpu.EndFrame(), "capture frame submitted");
        GpuFrameCapture capture;
        Check(gpu.ReadFrameCapture(capture), "frame capture read back");
        Check(capture.rgba.size() == static_cast<size_t>(capture.width) * capture.height * 4,
              "frame capture size");
        return capture;
    }
    void ValidationClean(const char* stage) {
        const auto counters = gpu.GetCounters();
        if (counters.validationErrors != 0)
            throw std::runtime_error(std::string("Vulkan validation errors after ") + stage + ": " +
                                     std::to_string(counters.validationErrors));
        ++checks;
    }
};
std::array<int, 4> Pixel(const GpuFrameCapture& capture, float x, float y) {
    const auto px = std::min<uint32>(capture.width - 1, static_cast<uint32>(x * capture.width));
    const auto py = std::min<uint32>(capture.height - 1, static_cast<uint32>(y * capture.height));
    const size_t index = (static_cast<size_t>(py) * capture.width + px) * 4;
    return {capture.rgba[index], capture.rgba[index + 1], capture.rgba[index + 2],
            capture.rgba[index + 3]};
}
bool IsColor(const std::array<int, 4>& pixel, int r, int g, int b, int tolerance = 3) {
    return std::abs(pixel[0] - r) <= tolerance && std::abs(pixel[1] - g) <= tolerance &&
           std::abs(pixel[2] - b) <= tolerance;
}
std::array<RenderVertex, 6> Rect(float x0, float y0, float x1, float y1, float z,
                                 const Vec4& color) {
    return {{{{x0, y0, z}, color, {0, 1}},
             {{x1, y0, z}, color, {1, 1}},
             {{x1, y1, z}, color, {1, 0}},
             {{x1, y1, z}, color, {1, 0}},
             {{x0, y1, z}, color, {0, 0}},
             {{x0, y0, z}, color, {0, 1}}}};
}
void ReportValidation(const GpuDevice& gpu) {
    const auto& info = gpu.GetInfo();
    std::cout << "device: " << info.deviceName << " (" << info.deviceType << "), validation "
              << (info.validation ? "on" : "unavailable/off") << ", sync validation "
              << (info.synchronizationValidation ? "on" : "off") << ", timeline "
              << (info.timelineSemaphores ? "on" : "off") << '\n';
}
void Lifecycle() {
    { Graphics probe({}, 64, 64, false); }
    struct Probe : Application {
        int shutdowns = 0;
        bool failInit = false, failRender = false, closeOnUpdate = true;
        void OnInit() override {
            if (failInit)
                throw std::runtime_error("init failure");
        }
        void OnUpdate(double) override {
            if (closeOnUpdate && !failRender)
                Close();
        }
        void OnRender(float) override {
            if (failRender)
                throw std::runtime_error("render failure");
        }
        void OnShutdown() override {
            ++shutdowns;
        }
    };
    Throws([] { Application::Get(); }, "application singleton absent before construction");
    {
        Probe app;
        app.closeOnUpdate = false;
        Check(&Application::Get() == &app, "application singleton available");
        Throws([] { Probe duplicate; }, "duplicate application rejected");
        Check(app.RunFrame() == Application::FrameResult::Error,
              "frame before initialization rejected");
        Throws([&] { app.GetWindow(); }, "window unavailable before initialization");
        Throws([&] { app.GetGpuDevice(); }, "GPU unavailable before initialization");
        Check(app.Initialize(), "single-frame lifecycle init");
        const auto frame = app.RunFrame();
        Check(frame == Application::FrameResult::Continue ||
                  frame == Application::FrameResult::Skipped,
              "single-frame API advances");
        app.Close();
        Check(app.RunFrame() == Application::FrameResult::Stopped && app.shutdowns == 1,
              "single-frame API stops cleanly");
        Throws([&] { app.GetWindow(); }, "window unavailable after shutdown");
    }
    Throws([] { Application::Get(); }, "application singleton cleared after destruction");
    {
        Probe app;
        Check(app.Initialize(), "lifecycle init");
        Check(app.Run() == 0 && app.shutdowns == 1, "Run dispatches derived shutdown");
        app.Shutdown();
        Check(app.shutdowns == 1, "shutdown idempotent");
    }
    {
        Probe app;
        app.failInit = true;
        Throws([&] { app.Initialize(); }, "init failure propagated");
        Check(app.shutdowns == 1, "partial initialization cleaned up");
    }
    {
        Probe app;
        app.failRender = true;
        Check(app.Initialize(), "render failure init");
        Throws([&] { app.Run(); }, "render failure propagated");
        Check(app.shutdowns == 1, "render exception cleanup");
    }
    Check(SDL_Init(SDL_INIT_VIDEO), "host SDL initialization");
    {
        Probe app;
        Check(app.Initialize(), "engine initializes with host SDL");
        app.Shutdown();
        Check(app.shutdowns == 1, "hosted shutdown hook");
        Check((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) != 0,
              "host SDL video stays initialized");
    }
    SDL_Quit();
}
void RenderOutput(Graphics& graphics) {
    const Mat4 identity;
    auto capture = graphics.Capture([&] { Check(graphics.gpu.Clear(1, 0, 0, 1), "clear red"); });
    Check(IsColor(Pixel(capture, 0.5f, 0.5f), 255, 0, 0), "clear color reaches the swapchain");

    capture = graphics.Capture([&] {
        graphics.gpu.Clear(0, 0, 1, 1);
        const auto top = Rect(-1, 0, 1, 1, 0.5f, {0, 1, 0, 1});
        Check(graphics.gpu.DrawTriangles(top, identity), "top half draw");
    });
    Check(IsColor(Pixel(capture, 0.5f, 0.25f), 0, 255, 0) &&
              IsColor(Pixel(capture, 0.5f, 0.75f), 0, 0, 255),
          "positive clip-space Y is the top of the window");

    capture = graphics.Capture([&] {
        graphics.gpu.Clear(1, 0, 0, 1);
        const auto blended = Rect(-1, -1, 1, 1, 0.5f, {0, 0, 1, 0.5f});
        Check(graphics.gpu.DrawTriangles(blended, identity), "blended draw");
    });
    Check(IsColor(Pixel(capture, 0.5f, 0.5f), 128, 0, 128, 3), "2D alpha blending");

    capture = graphics.Capture([&] {
        graphics.gpu.Clear(0, 0, 0, 1);
        const auto nearQuad = Rect(-1, -1, 0, 1, 0.25f, {0, 1, 0, 1});
        const auto farQuad = Rect(-1, -1, 0, 1, 0.75f, {1, 0, 0, 1});
        const auto farFirst = Rect(0, -1, 1, 1, 0.75f, {1, 0, 0, 1});
        const auto nearLast = Rect(0, -1, 1, 1, 0.25f, {0, 1, 0, 1});
        const auto clipped = Rect(-1, -1, 1, 1, 1.5f, {1, 1, 1, 1});
        Check(graphics.gpu.DrawTriangles(nearQuad, identity, nullptr, true) &&
                  graphics.gpu.DrawTriangles(farQuad, identity, nullptr, true) &&
                  graphics.gpu.DrawTriangles(farFirst, identity, nullptr, true) &&
                  graphics.gpu.DrawTriangles(nearLast, identity, nullptr, true) &&
                  graphics.gpu.DrawTriangles(clipped, identity, nullptr, true),
              "depth draws");
    });
    Check(IsColor(Pixel(capture, 0.25f, 0.5f), 0, 255, 0) &&
              IsColor(Pixel(capture, 0.75f, 0.5f), 0, 255, 0),
          "depth test keeps the nearest surface in both draw orders");

    const std::array<uint8, 16> quadrants{255, 0, 0,   255, 0,   255, 0,   255,
                                          0,   0, 255, 255, 255, 255, 255, 255};
    GpuTexture texture;
    Check(texture.Initialize(&graphics.gpu, 2, 2, quadrants.data()), "quadrant texture");
    GpuTexture empty;
    Check(empty.Initialize(&graphics.gpu, 4, 4), "uninitialized texture");
    Renderer2D::Init(&graphics.gpu);
    capture = graphics.Capture([&] {
        graphics.gpu.Clear(0, 0, 0, 1);
        Renderer2D::BeginScene(OrthographicCamera(-1, 1, -1, 1));
        Renderer2D::DrawSprite({0, 0}, {2, 2}, &texture);
        Renderer2D::DrawSprite({0, 0}, {2, 2}, &empty);
        Renderer2D::EndScene();
    });
    Check(IsColor(Pixel(capture, 0.25f, 0.25f), 255, 0, 0) &&
              IsColor(Pixel(capture, 0.75f, 0.25f), 0, 255, 0) &&
              IsColor(Pixel(capture, 0.25f, 0.75f), 0, 0, 255) &&
              IsColor(Pixel(capture, 0.75f, 0.75f), 255, 255, 255),
          "texture rows and columns keep their orientation; empty textures are transparent");

    capture = graphics.Capture([&] {
        graphics.gpu.Clear(0, 0, 0, 1);
        OrthographicCamera camera(-2, 2, -2, 2);
        Renderer2D::BeginScene(camera);
        Renderer2D::DrawQuad(Vec2(1, 1), {1, 1}, {1, 1, 0, 1});
        Renderer2D::EndScene();
    });
    Check(IsColor(Pixel(capture, 0.75f, 0.25f), 255, 255, 0) &&
              IsColor(Pixel(capture, 0.25f, 0.75f), 0, 0, 0),
          "orthographic camera maps +X right and +Y up");

    Renderer3D::Init(&graphics.gpu);
    capture = graphics.Capture([&] {
        graphics.gpu.Clear(0, 0, 0, 1);
        PerspectiveCamera camera(ToRadians(60), 4.0f / 3, 0.1f, 100);
        camera.SetPosition({0, 0, 5});
        DirectionalLight light;
        light.direction = {0, 0, -1};
        Renderer3D::BeginScene(camera, light);
        Renderer3D::DrawCube({0, 0, 0}, {1, 1, 1}, {});
        Renderer3D::EndScene();
    });
    const auto center = Pixel(capture, 0.5f, 0.5f);
    Check(center[0] > 200 && center[1] > 200 && center[2] > 200 &&
              IsColor(Pixel(capture, 0.05f, 0.05f), 0, 0, 0),
          "perspective cube faces the camera and is lit");

    auto pointLit = [&](std::span<const PointLight> points) {
        return graphics.Capture([&] {
            graphics.gpu.Clear(0, 0, 0, 1);
            PerspectiveCamera camera(ToRadians(60), 4.0f / 3, 0.1f, 100);
            camera.SetPosition({0, 0, 5});
            DirectionalLight dark;
            dark.intensity = 0.0f;
            Renderer3D::BeginScene(camera, dark, points);
            Renderer3D::DrawCube({0, 0, 0}, {1, 1, 1}, {});
            Renderer3D::EndScene();
        });
    };
    PointLight red;
    red.position = {0, 0, 2};
    red.color = {1, 0, 0};
    const auto ambient = Pixel(pointLit({}), 0.5f, 0.5f);
    const auto front = Pixel(pointLit(std::span(&red, 1)), 0.5f, 0.5f);
    red.position = {0, 0, -3};
    const auto behind = Pixel(pointLit(std::span(&red, 1)), 0.5f, 0.5f);
    red.position = {0, 0, 2};
    red.radius = 1.0f;
    const auto outOfRange = Pixel(pointLit(std::span(&red, 1)), 0.5f, 0.5f);
    Check(ambient[0] < 60 && front[0] > 200 && front[1] < 60,
          "point light in front of a face lights it with its own color");
    Check(IsColor(behind, ambient[0], ambient[1], ambient[2], 2) &&
              IsColor(outOfRange, ambient[0], ambient[1], ambient[2], 2),
          "point light behind a face or beyond its radius adds nothing");
    texture.Release();
    empty.Release();
    graphics.ValidationClean("render output checks");
}
void Gpu() {
    Graphics graphics;
    ReportValidation(graphics.gpu);
    Check(!graphics.gpu.Initialize(graphics.window), "duplicate device initialization rejected");
    Files files;
    auto* surface = SDL_CreateSurface(2, 2, SDL_PIXELFORMAT_RGBA32);
    Check(surface != nullptr, "test image surface");
    SDL_FillSurfaceRect(surface, nullptr, SDL_MapSurfaceRGBA(surface, 255, 0, 0, 255));
    Check(SDL_SaveBMP(surface, files.Path("red.bmp").c_str()), "write test image");
    SDL_DestroySurface(surface);
    auto texture = AssetManager::LoadTexture(files.Path("red.bmp"), &graphics.gpu);
    Check(texture && texture->IsValid() && texture->GetWidth() == 2, "real image upload");
    Check(AssetManager::LoadTexture(files.Path("red.bmp"), &graphics.gpu) == texture,
          "cached texture reused for the same device");
    Check(!AssetManager::LoadTexture(files.Path("absent.png"), &graphics.gpu),
          "missing GPU image fails");
    files.Text("corrupt.png", "\x89PNG\r\n\x1a\n not really a png");
    Check(!AssetManager::LoadTexture(files.Path("corrupt.png"), &graphics.gpu) &&
              !AssetManager::HasTexture(files.Path("corrupt.png")),
          "corrupted image rejected and not cached");
    Renderer2D::Init(&graphics.gpu);
    Renderer3D::Init(&graphics.gpu);
    OrthographicCamera camera(-2, 2, -2, 2);
    PerspectiveCamera perspective(ToRadians(60), 4.0f / 3, 0.1f, 100);
    for (int frame = 0; frame < 3; ++frame) {
        Check(graphics.Begin(), "begin frame");
        Check(graphics.gpu.Clear(0, 0, 0, 1), "clear frame");
        Renderer2D::BeginScene(camera);
        Renderer2D::DrawSprite({-1, 0}, {1, 1}, texture.get());
        Renderer2D::DrawQuad(Vec2(0, 0), {1, 1}, {0, 1, 0, 1});
        Renderer2D::EndScene();
        Check(Renderer2D::GetStats().drawCalls == 2 && Renderer2D::GetStats().quadCount == 2,
              "real 2D draw calls");
        Renderer3D::BeginScene(perspective, {});
        Renderer3D::DrawCube({1, 0, 0}, {1, 1, 1}, {});
        Renderer3D::EndScene();
        Check(Renderer3D::GetStats().drawCalls == 1 && Renderer3D::GetStats().meshCount == 1,
              "real 3D draw call");
        Check(graphics.gpu.EndFrame(), "submit frame");
        const auto stats = graphics.gpu.GetFrameStats();
        if (frame == 0)
            std::cout << "frame stats: draws " << stats.drawCalls << ", pipelines "
                      << stats.pipelineBinds << ", descriptors " << stats.descriptorBinds
                      << ", push constants " << stats.pushConstantUpdates << '\n';
        Check(stats.drawCalls == 3 && stats.pipelineBinds == 2 && stats.descriptorBinds == 2 &&
                  stats.pushConstantUpdates == 2 && stats.submissions >= 1,
              "frame statistics count recorded GPU commands");
    }
    {
        auto transient = std::make_unique<GpuTexture>();
        const uint32 pixel = 0xff0000ffu;
        Check(transient->Initialize(&graphics.gpu, 1, 1, &pixel), "transient sprite texture");
        Check(graphics.Begin(), "transient sprite frame");
        Renderer2D::BeginScene(camera);
        Renderer2D::DrawSprite({0, 0}, {1, 1}, transient.get());
        transient.reset();
        Renderer2D::DrawQuad(Vec2(1, 0), {1, 1}, {0, 1, 0, 1});
        Renderer2D::EndScene();
        Check(Renderer2D::GetStats().drawCalls == 1 && Renderer2D::GetStats().quadCount == 2,
              "sprite whose texture was destroyed before EndScene is skipped safely");
        Check(graphics.gpu.EndFrame(), "transient sprite frame submitted");
    }
    Check(graphics.gpu.WaitIdle(), "GPU completion");
    texture.reset();
    AssetManager::Shutdown();
    GpuBuffer buffer;
    Check(buffer.Initialize(&graphics.gpu, BufferType::Vertex, 4), "buffer init");
    const uint32 value = 42;
    Check(!buffer.SetData(&value, 8), "oversized upload rejected");
    Check(buffer.SetData(&value, 4), "buffer upload");
    GpuBuffer moved = std::move(buffer);
    Check(!buffer.IsValid() && moved.IsValid(), "buffer ownership transfer");
    moved.Release();
    RenderOutput(graphics);
    const uint32 pixel = 0xffffffff;
    GpuTexture delayed;
    Check(delayed.Initialize(&graphics.gpu, 1, 1, &pixel), "delayed texture init");
    graphics.ValidationClean("GPU suite");
    graphics.gpu.Shutdown();
    Check(!delayed.IsValid() && delayed.GetWidth() == 0, "device shutdown invalidates resources");
    delayed.Release();
}
void VulkanSystems() {
    Graphics graphics;
    ReportValidation(graphics.gpu);
    auto& gpu = graphics.gpu;
    const auto& info = gpu.GetInfo();
    Check(!info.deviceName.empty() && info.apiVersion >= ((1u << 22) | (1u << 12)) &&
              info.maxTextureSize >= 4096 && info.framesInFlight == 2,
          "device capabilities reported");
    Check(gpu.GetStatus() == GpuStatus::Ready && gpu.IsReady(), "device ready");

    Check(!gpu.Clear(0, 0, 0, 1) && !gpu.EndFrame(), "frame commands outside a frame rejected");
    const auto triangle = Rect(-1, -1, 1, 1, 0.5f, {1, 1, 1, 1});
    Check(!gpu.DrawTriangles(triangle, Mat4{}), "draw outside a frame rejected");
    Check(graphics.Begin(), "frame begins");
    Check(gpu.BeginFrame() == GpuDevice::FrameStart::Error, "nested frame rejected");
    Check(!gpu.SetVSync(false) && !gpu.WaitIdle(), "swapchain changes rejected mid-frame");
    Check(!gpu.DrawTriangles(std::span(triangle).first(4), Mat4{}) &&
              !gpu.DrawTriangles({}, Mat4{}),
          "non-triangle vertex counts rejected");
    Check(gpu.EndFrame() && !gpu.EndFrame(), "frame ends once");

    GpuBuffer buffers[4];
    const BufferType types[4] = {BufferType::Vertex, BufferType::Index, BufferType::Uniform,
                                 BufferType::Storage};
    for (int index = 0; index < 4; ++index)
        Check(buffers[index].Initialize(&gpu, types[index], 256) &&
                  buffers[index].GetSize() == 256 && buffers[index].GetType() == types[index],
              "every buffer type allocates");
    std::array<uint8, 256> bytes{};
    Check(buffers[0].SetData(bytes.data(), 128, 128) && !buffers[0].SetData(bytes.data(), 128, 129),
          "buffer offsets are bounds checked");
    Check(!buffers[0].SetData(bytes.data(), 1, std::numeric_limits<uint32>::max()) &&
              !buffers[0].SetData(nullptr, 4) && !buffers[0].SetData(bytes.data(), 0),
          "overflowing, null and empty uploads rejected");
    GpuBuffer zero;
    Check(!zero.Initialize(&gpu, BufferType::Vertex, 0) &&
              !zero.Initialize(nullptr, BufferType::Vertex, 4),
          "invalid buffer creation rejected");
    buffers[1].Release();
    buffers[1].Release();
    Check(!buffers[1].IsValid() && buffers[1].GetSize() == 0, "double release is harmless");

    GpuTexture texture;
    Check(!texture.Initialize(&gpu, 0, 4) && !texture.Initialize(&gpu, info.maxTextureSize + 1, 1),
          "texture dimensions validated against device limits");
    const auto before = gpu.GetMemoryStats();
    Check(texture.Initialize(&gpu, 64, 64) &&
              gpu.GetMemoryStats().liveTextures == before.liveTextures + 1,
          "texture tracked");
    Check(graphics.Begin(), "frame with released texture begins");
    Renderer2D::Init(&gpu);
    Renderer2D::BeginScene(OrthographicCamera(-1, 1, -1, 1));
    Renderer2D::DrawSprite({0, 0}, {1, 1}, &texture);
    Renderer2D::EndScene();
    texture.Release();
    Check(gpu.GetMemoryStats().pendingDestructions > 0, "in-use texture destruction is deferred");
    Check(gpu.EndFrame(), "frame using a released texture submits");
    Check(gpu.WaitIdle() && gpu.GetMemoryStats().pendingDestructions == 0 &&
              gpu.GetMemoryStats().liveTextures == before.liveTextures,
          "deferred destruction completes after the GPU finishes");

    {
        Window other;
        WindowProps props;
        props.vulkan = true;
        props.width = 64;
        props.height = 64;
        Check(other.Initialize(props), "second window");
        GpuDevice otherGpu;
        Check(otherGpu.Initialize(other), "second device");
        GpuTexture foreign;
        Check(foreign.Initialize(&otherGpu, 1, 1), "foreign texture");
        Check(graphics.Begin(), "foreign texture frame");
        Check(!gpu.DrawTriangles(triangle, Mat4{}, &foreign),
              "textures from another device rejected");
        Check(gpu.EndFrame(), "frame after foreign draw");
        Check(!foreign.BelongsTo(gpu) && foreign.BelongsTo(otherGpu), "texture ownership");
        otherGpu.Shutdown();
        Check(!foreign.IsValid(), "foreign texture invalid after its device shuts down");
    }

    const auto recreations = gpu.GetCounters().swapchainRecreations;
    SDL_SetWindowSize(graphics.window.GetNativeHandle(), 400, 300);
    SDL_SyncWindow(graphics.window.GetNativeHandle());
    gpu.NotifyResize();
    auto capture = graphics.Capture([&] { gpu.Clear(0, 1, 0, 1); });
    uint32 width = 0, height = 0;
    graphics.window.GetFramebufferSize(width, height);
    Check(gpu.GetCounters().swapchainRecreations > recreations && capture.width == width &&
              capture.height == height && IsColor(Pixel(capture, 0.9f, 0.9f), 0, 255, 0),
          "swapchain follows window resize");
    if (gpu.SetVSync(false)) {
        Check(graphics.Begin() && gpu.EndFrame(), "frame after disabling vsync");
        Check(gpu.SetVSync(true) && graphics.Begin() && gpu.EndFrame(),
              "frame after enabling vsync");
    } else {
        std::cout << "note: surface offers no non-vsync present mode\n";
    }
    SDL_MinimizeWindow(graphics.window.GetNativeHandle());
    SDL_SyncWindow(graphics.window.GetNativeHandle());
    graphics.window.PollEvents();
    if (graphics.window.IsMinimized()) {
        Check(gpu.BeginFrame() == GpuDevice::FrameStart::Skipped, "minimized window skips frames");
        SDL_RestoreWindow(graphics.window.GetNativeHandle());
        SDL_SyncWindow(graphics.window.GetNativeHandle());
    } else {
        std::cout << "note: display server ignored minimize; minimized-frame check not run\n";
    }
    Check(graphics.Begin(20) && gpu.EndFrame(), "rendering resumes after restore");

    for (int frame = 0; frame < 6; ++frame)
        Check(graphics.Begin() && gpu.EndFrame(), "timestamp frames");
    if (info.gpuTimestamps)
        Check(gpu.GetFrameStats().gpuTimeMs >= 0.0, "GPU timestamps measured");

    gpu.Shutdown();
    for (uint32 frames : {0u, 1u, 3u, 9u}) {
        GpuDeviceConfig config;
        config.framesInFlight = frames;
        Check(gpu.Initialize(graphics.window, config), "device reinitializes");
        Check(gpu.GetInfo().framesInFlight == std::clamp(frames, 1u, 3u),
              "frames in flight clamped");
        for (int frame = 0; frame < 5; ++frame)
            Check(graphics.Begin() && gpu.EndFrame(), "frames with configured depth");
        graphics.ValidationClean("frames-in-flight configuration");
        gpu.Shutdown();
        Check(!gpu.IsReady() && gpu.BeginFrame() == GpuDevice::FrameStart::Error,
              "API calls after shutdown rejected");
    }
    Check(gpu.Initialize(graphics.window), "device available for teardown");
    for (auto& buffer : buffers)
        Check(!buffer.IsValid(), "buffers from a previous device stay invalid");
}
#if defined(LUX_GPU_FAULT_INJECTION) && LUX_GPU_FAULT_INJECTION
struct FaultScope {
    FaultScope() {
        Vk::ClearFaults();
    }
    ~FaultScope() {
        Vk::ClearFaults();
    }
};
void Failures() {
    Graphics graphics;
    ReportValidation(graphics.gpu);
    auto& gpu = graphics.gpu;
    auto& window = graphics.window;
    gpu.Shutdown();
    const Vk::Fault initFaults[] = {
        Vk::Fault::InstanceCreation, Vk::Fault::SurfaceCreation, Vk::Fault::DeviceSelection,
        Vk::Fault::DeviceCreation,   Vk::Fault::SyncObject,      Vk::Fault::ShaderModule,
        Vk::Fault::PipelineCreation, Vk::Fault::CommandBuffer,   Vk::Fault::SwapchainCreation,
        Vk::Fault::ImageAllocation,  Vk::Fault::Submit};
    for (Vk::Fault fault : initFaults) {
        FaultScope scope;
        Vk::InjectFault(fault);
        Check(!gpu.Initialize(window) && !gpu.IsReady() && !gpu.GetLastError().empty(),
              "initialization failure detected and reported");
        Check(Vk::TriggeredFaults(fault) == 1, "injected initialization fault reached");
        Vk::ClearFaults();
        Check(gpu.Initialize(window) && graphics.Begin() && gpu.EndFrame(),
              "device initializes after a failed attempt");
        gpu.Shutdown();
    }
    Files files;
    GpuDeviceConfig config;
    config.shaderDirectory = files.Path("");
    Check(!gpu.Initialize(window, config) &&
              gpu.GetLastError().find("Shader file missing") != std::string::npos,
          "missing shaders reported");
    files.Text("Basic.vert.spv", "not spir-v");
    files.Text("Basic.frag.spv", "12345678");
    Check(!gpu.Initialize(window, config) && !gpu.IsReady(), "corrupted shaders rejected");

    Check(gpu.Initialize(window), "device for runtime failures");
    {
        FaultScope scope;
        Vk::InjectFault(Vk::Fault::BufferAllocation);
        GpuBuffer buffer;
        Check(!buffer.Initialize(&gpu, BufferType::Vertex, 64) && gpu.IsReady() &&
                  gpu.GetLastFailure() == GpuStatus::OutOfMemory,
              "buffer allocation failure is recoverable");
        Check(buffer.Initialize(&gpu, BufferType::Vertex, 64), "buffer allocation retried");
        Vk::InjectFault(Vk::Fault::ImageAllocation);
        GpuTexture texture;
        Check(!texture.Initialize(&gpu, 8, 8) && gpu.IsReady(), "image allocation failure");
        Vk::InjectFault(Vk::Fault::BufferAllocation);
        const uint32 pixels[4]{};
        Check(!texture.Initialize(&gpu, 2, 2, pixels) && gpu.GetMemoryStats().liveTextures == 1,
              "staging failure releases the partial texture");
        Vk::InjectFault(Vk::Fault::ImageAllocation);
        auto* image = SDL_CreateSurface(2, 2, SDL_PIXELFORMAT_RGBA32);
        Check(image && SDL_SaveBMP(image, files.Path("image.bmp").c_str()), "asset image");
        SDL_DestroySurface(image);
        Check(!AssetManager::LoadTexture(files.Path("image.bmp"), &gpu) &&
                  !AssetManager::HasTexture(files.Path("image.bmp")),
              "GPU upload failure is not cached");
        Check(AssetManager::LoadTexture(files.Path("image.bmp"), &gpu) != nullptr,
              "asset load succeeds once memory is available");
        AssetManager::Shutdown();

        Vk::InjectFault(Vk::Fault::Submit);
        Check(graphics.Begin() && !gpu.EndFrame() && gpu.IsReady(),
              "single submission failure keeps the device usable");
        Check(graphics.Begin() && gpu.EndFrame(), "rendering recovers after a failed submission");

        Vk::InjectFault(Vk::Fault::AcquireOutOfDate);
        window.PollEvents();
        const auto recreations = gpu.GetCounters().swapchainRecreations;
        Check(gpu.BeginFrame() == GpuDevice::FrameStart::Skipped, "out-of-date swapchain skips");
        Check(graphics.Begin() && gpu.EndFrame() &&
                  gpu.GetCounters().swapchainRecreations == recreations + 1,
              "out-of-date swapchain recreated once");

        Vk::InjectFault(Vk::Fault::SwapchainCreation);
        gpu.NotifyResize();
        window.PollEvents();
        Check(gpu.BeginFrame() == GpuDevice::FrameStart::Skipped && gpu.IsReady(),
              "swapchain recreation failure skips the frame");
        Check(graphics.Begin() && gpu.EndFrame(), "swapchain recreation retried");

        Vk::InjectFault(Vk::Fault::SurfaceZeroExtent);
        gpu.NotifyResize();
        window.PollEvents();
        Check(gpu.BeginFrame() == GpuDevice::FrameStart::Skipped && gpu.IsReady(),
              "zero surface extent skips instead of acquiring from the stale swapchain");
        Check(graphics.Begin() && gpu.EndFrame(), "swapchain recreated once the surface has size");

        GpuDeviceConfig small;
        small.maxFrameVertexBytes = 1 << 16;
        gpu.Shutdown();
        Check(gpu.Initialize(window, small), "device with a small vertex budget");
        std::vector<RenderVertex> many(3 * 1000, RenderVertex{{0, 0, 0}, {1, 1, 1, 1}, {0, 0}});
        Check(graphics.Begin(), "budget frame");
        Check(!gpu.DrawTriangles(many, Mat4{}) && gpu.GetLastFailure() == GpuStatus::OutOfMemory,
              "per-frame vertex budget enforced");
        Check(gpu.EndFrame() && gpu.IsReady(), "frame survives an over-budget draw");

        GpuDeviceConfig medium;
        medium.maxFrameVertexBytes = 4ull << 20;
        gpu.Shutdown();
        Check(gpu.Initialize(window, medium), "device with a 4 MiB vertex budget");
        Renderer3D::Init(&gpu);
        PerspectiveCamera camera(ToRadians(60), 4.0f / 3, 0.1f, 100);
        Check(graphics.Begin(), "over-budget 3D frame");
        Renderer3D::BeginScene(camera, {});
        for (int cube = 0; cube < 3300; ++cube)
            Renderer3D::DrawCube({0, 0, 0}, {1, 1, 1}, {});
        Renderer3D::EndScene();
        Check(Renderer3D::GetStats().drawCalls == 2 && gpu.IsReady(),
              "3D scene over the vertex budget keeps the chunks that fit");
        Check(gpu.EndFrame(), "over-budget 3D frame submitted");
        Renderer3D::Shutdown();
        graphics.ValidationClean("recoverable failures");

        Vk::InjectFault(Vk::Fault::CommandBuffer, 3);
        for (int frame = 0; frame < 2; ++frame) {
            window.PollEvents();
            Check(gpu.BeginFrame() == GpuDevice::FrameStart::Skipped && gpu.IsReady(),
                  "command recording failure skips the frame");
        }
        window.PollEvents();
        Check(gpu.BeginFrame() == GpuDevice::FrameStart::Error && !gpu.IsReady() &&
                  gpu.GetStatus() == GpuStatus::Error,
              "repeated frame failures stop the device");
        graphics.ValidationClean("repeated frame failures");
        gpu.Shutdown();

        Check(gpu.Initialize(window), "device for unrecoverable submission");
        Vk::InjectFault(Vk::Fault::Submit, 2);
        Check(graphics.Begin() && !gpu.EndFrame() && !gpu.IsReady() &&
                  gpu.GetStatus() == GpuStatus::Error,
              "failed recovery submission is unrecoverable");
        gpu.Shutdown();

        Check(gpu.Initialize(window), "device for timeout");
        Check(graphics.Begin() && gpu.EndFrame(), "timeout setup frame");
        Vk::InjectFault(Vk::Fault::GpuTimeout);
        Check(graphics.Begin() && gpu.EndFrame() &&
                  gpu.BeginFrame() == GpuDevice::FrameStart::Error &&
                  gpu.GetLastFailure() == GpuStatus::Timeout,
              "GPU timeout reported");
        gpu.Shutdown();

        Check(gpu.Initialize(window), "device for device-lost");
        GpuTexture survivor;
        Check(survivor.Initialize(&gpu, 4, 4), "resource before device loss");
        Vk::InjectFault(Vk::Fault::DeviceLost);
        Check(graphics.Begin() && !gpu.EndFrame(), "device loss reported at submission");
        Check(gpu.GetStatus() == GpuStatus::DeviceLost && !gpu.IsReady() &&
                  gpu.BeginFrame() == GpuDevice::FrameStart::Error &&
                  !texture.Initialize(&gpu, 1, 1),
              "lost device rejects further work");
        gpu.Shutdown();
        Check(!survivor.IsValid(), "resources invalid after a lost device is destroyed");
        Check(gpu.Initialize(window) && graphics.Begin() && gpu.EndFrame(),
              "a new device can be created after device loss");
    }
    graphics.ValidationClean("failure injection");
    gpu.Shutdown();
    window.Shutdown();

    struct Probe : Application {
        int shutdowns = 0;
        void OnShutdown() override {
            ++shutdowns;
        }
    };
    {
        FaultScope scope;
        Probe app;
        Vk::InjectFault(Vk::Fault::DeviceCreation);
        Check(!app.Initialize(), "application reports GPU initialization failure");
        Check(app.shutdowns == 0, "application hooks skipped when GPU is unavailable");
    }
    {
        FaultScope scope;
        Probe app;
        Check(app.Initialize(), "application for device loss");
        Vk::InjectFault(Vk::Fault::DeviceLost);
        Application::FrameResult result = Application::FrameResult::Skipped;
        for (int frame = 0; frame < 20 && result != Application::FrameResult::Error; ++frame) {
            result = app.RunFrame();
            SDL_Delay(2);
        }
        Check(result == Application::FrameResult::Error && app.shutdowns == 1,
              "application stops cleanly on device loss");
    }
}
#else
void Failures() {
    throw SkipTest("built without LUX_GPU_FAULT_INJECTION");
}
#endif
template <class F> std::vector<double> TimedFrames(Graphics& graphics, int frames, F&& draw) {
    std::vector<double> times;
    for (int frame = 0; frame < frames; ++frame) {
        const auto start = std::chrono::steady_clock::now();
        Check(graphics.Begin(), "stress frame begins");
        draw(frame);
        Check(graphics.gpu.EndFrame(), "stress frame submitted");
        times.push_back(
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count());
    }
    return times;
}
void PrintTimes(const char* name, std::vector<double> times) {
    std::sort(times.begin(), times.end());
    double sum = 0;
    for (double time : times)
        sum += time;
    auto at = [&](double p) { return times[std::min(times.size() - 1, size_t(p * times.size()))]; };
    std::cout << name << ": avg " << sum / times.size() << " ms, p95 " << at(0.95) << " ms, p99 "
              << at(0.99) << " ms over " << times.size() << " frames\n";
}
void Stress() {
    Graphics graphics({}, 640, 360, false);
    ReportValidation(graphics.gpu);
    auto& gpu = graphics.gpu;
    Renderer2D::Init(&gpu);
    Renderer3D::Init(&gpu);
    std::vector<GpuTexture> textures(8);
    for (size_t index = 0; index < textures.size(); ++index) {
        const uint32 pixel = 0xff000000u | static_cast<uint32>(index * 0x202020u);
        Check(textures[index].Initialize(&gpu, 1, 1, &pixel), "stress texture");
    }
    OrthographicCamera camera(-16, 16, -9, 9);
    auto times = TimedFrames(graphics, 60, [&](int frame) {
        Renderer2D::BeginScene(camera);
        for (int index = 0; index < 20000; ++index)
            Renderer2D::DrawSprite(
                {float(index % 200) * 0.16f - 16, float(index / 200) * 0.18f - 9}, {0.15f, 0.15f},
                &textures[(index / 7 + frame) % textures.size()]);
        Renderer2D::EndScene();
    });
    PrintTimes("20k textured quads", times);
    const auto settled = gpu.GetMemoryStats();
    TimedFrames(graphics, 60, [&](int) {
        Renderer2D::BeginScene(camera);
        for (int index = 0; index < 20000; ++index)
            Renderer2D::DrawQuad(
                Vec2{float(index % 200) * 0.16f - 16, float(index / 200) * 0.18f - 9},
                {0.15f, 0.15f}, {1, 1, 1, 1});
        Renderer2D::EndScene();
    });
    Check(gpu.GetMemoryStats().allocationBytes == settled.allocationBytes,
          "steady-state frames allocate no GPU memory");

    const auto baseline = gpu.GetMemoryStats();
    uint32 peakPools = 0;
    for (int round = 0; round < 10; ++round) {
        std::vector<GpuTexture> churn(300);
        for (auto& texture : churn)
            Check(texture.Initialize(&gpu, 32, 32), "churn texture");
        TimedFrames(graphics, 2, [&](int) {
            Renderer2D::BeginScene(camera);
            for (size_t index = 0; index < churn.size(); ++index)
                Renderer2D::DrawSprite({float(index % 30) - 15, float(index / 30) - 5}, {1, 1},
                                       &churn[index]);
            Renderer2D::EndScene();
        });
        peakPools = std::max(peakPools, gpu.GetMemoryStats().descriptorPools);
    }
    Check(gpu.WaitIdle(), "churn drained");
    TimedFrames(graphics, 3, [](int) {});
    const auto churned = gpu.GetMemoryStats();
    Check(churned.liveTextures == baseline.liveTextures && churned.pendingDestructions == 0 &&
              churned.allocationCount == baseline.allocationCount,
          "texture churn returns every allocation");
    Check(peakPools <= 4, "descriptor pools are reused instead of growing");

    std::vector<uint8> big(512 * 512 * 4, 0x7f);
    std::vector<GpuTexture> uploads(200);
    uint64 peakStaging = 0;
    for (auto& texture : uploads) {
        Check(texture.Initialize(&gpu, 512, 512, big.data()), "bulk upload texture");
        peakStaging = std::max(peakStaging, gpu.GetMemoryStats().pendingStagingBytes);
    }
    Check(peakStaging <= GpuDeviceConfig{}.stagingFlushBytes * 4 + big.size(),
          "staging memory bounded during bulk uploads");
    uploads.clear();
    std::vector<uint8> payload(64 * 1024, 1);
    GpuBuffer buffer;
    Check(buffer.Initialize(&gpu, BufferType::Storage, static_cast<uint32>(payload.size())),
          "upload buffer");
    for (int upload = 0; upload < 3000; ++upload)
        Check(buffer.SetData(payload.data(), static_cast<uint32>(payload.size())),
              "repeated upload");
    Check(gpu.GetMemoryStats().pendingStagingBytes <=
              GpuDeviceConfig{}.stagingFlushBytes * 4 + payload.size(),
          "repeated buffer uploads keep staging bounded");
    buffer.Release();

    PerspectiveCamera perspective(ToRadians(60), 16.0f / 9, 0.1f, 100);
    perspective.SetPosition({0, 4, 14});
    times = TimedFrames(graphics, 30, [&](int) {
        Renderer3D::BeginScene(perspective, {});
        for (int index = 0; index < 1000; ++index)
            Renderer3D::DrawCube(
                {float(index % 25) - 12, float(index / 25) * 0.5f - 4, -float(index % 7)},
                {0.4f, 0.4f, 0.4f}, {});
        Renderer3D::EndScene();
    });
    PrintTimes("1000 cubes", times);

    Random random(7);
    const auto recreations = gpu.GetCounters().swapchainRecreations;
    for (int step = 0; step < 40; ++step) {
        SDL_SetWindowSize(graphics.window.GetNativeHandle(),
                          static_cast<int>(random.Range(64, 900)),
                          static_cast<int>(random.Range(64, 700)));
        graphics.window.PollEvents();
        gpu.NotifyResize();
        Check(graphics.Begin(20), "frame during resize storm");
        Renderer2D::BeginScene(camera);
        Renderer2D::DrawQuad(Vec2{0, 0}, {4, 4}, {1, 0, 0, 1});
        Renderer2D::EndScene();
        Check(gpu.EndFrame(), "resize storm frame submitted");
    }
    Check(gpu.GetCounters().swapchainRecreations >= recreations + 20,
          "swapchain recreated during resize storm");
    for (auto& texture : textures)
        texture.Release();
    Check(gpu.WaitIdle(), "stress drained");
    graphics.ValidationClean("stress");
}
long ResidentKilobytes() {
#if defined(__linux__)
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line))
        if (line.rfind("VmRSS:", 0) == 0)
            return std::stol(line.substr(6));
#endif
    return -1;
}
void LongRun() {
    double seconds = 20.0;
    if (const char* env = std::getenv("LUX_LONGRUN_SECONDS"))
        seconds = std::clamp(std::atof(env), 1.0, 3600.0);
    Graphics graphics({}, 640, 360, false);
    auto& gpu = graphics.gpu;
    Renderer2D::Init(&gpu);
    Renderer3D::Init(&gpu);
    OrthographicCamera camera(-16, 16, -9, 9);
    PerspectiveCamera perspective(ToRadians(60), 16.0f / 9, 0.1f, 100);
    perspective.SetPosition({0, 3, 10});
    std::vector<double> times;
    long startRss = -1;
    uint64 startAllocations = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    int frame = 0;
    GpuTexture rotating;
    while (std::chrono::steady_clock::now() < deadline) {
        if (frame % 50 == 0) {
            const uint32 pixel = 0xff00ff00u + static_cast<uint32>(frame);
            Check(rotating.Initialize(&gpu, 1, 1, &pixel), "rotating texture upload");
        }
        if (frame % 200 == 100) {
            SDL_SetWindowSize(graphics.window.GetNativeHandle(), 600 + frame % 80,
                              340 + frame % 40);
            gpu.NotifyResize();
        }
        const auto start = std::chrono::steady_clock::now();
        Check(graphics.Begin(20), "long-run frame begins");
        Renderer2D::BeginScene(camera);
        for (int index = 0; index < 2000; ++index)
            Renderer2D::DrawRotatedQuad(
                {float(index % 50) * 0.6f - 15, float(index / 50) * 0.4f - 8}, {0.4f, 0.2f},
                frame * 0.02f + index, {0.3f, 0.6f, 0.9f, 1});
        Renderer2D::DrawSprite({0, 0}, {2, 2}, &rotating);
        Renderer2D::EndScene();
        Renderer3D::BeginScene(perspective, {});
        for (int index = 0; index < 100; ++index)
            Renderer3D::DrawCube({float(index % 10) - 5, float(index / 10) * 0.5f - 2, 0},
                                 {0.3f, 0.3f, 0.3f}, {});
        Renderer3D::EndScene();
        Check(gpu.EndFrame(), "long-run frame submitted");
        times.push_back(
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count());
        if (frame == 120) {
            startRss = ResidentKilobytes();
            startAllocations = gpu.GetMemoryStats().allocationBytes;
        }
        ++frame;
    }
    Check(frame > 150, "long run rendered enough frames");
    Check(gpu.WaitIdle(), "long run drained");
    const long endRss = ResidentKilobytes();
    const auto memory = gpu.GetMemoryStats();
    PrintTimes("long run", times);
    std::cout << "long run: " << frame << " frames, RSS " << startRss << " -> " << endRss
              << " KB, GPU allocations " << startAllocations << " -> " << memory.allocationBytes
              << " bytes, swapchain recreations " << gpu.GetCounters().swapchainRecreations << '\n';
    Check(startRss < 0 || endRss - startRss < 32 * 1024, "resident memory stays bounded");
    Check(memory.allocationBytes <= startAllocations + (8ull << 20), "GPU memory stays bounded");
    rotating.Release();
    graphics.ValidationClean("long run");
}
int vulkanValidationMessages = 0;
SDL_LogOutputFunction defaultLogOutput = nullptr;
void* defaultLogData = nullptr;
void SDLCALL CountValidation(void* data, int category, SDL_LogPriority priority,
                             const char* message) {
    if (priority >= SDL_LOG_PRIORITY_ERROR && message && std::strstr(message, "[Vulkan]"))
        ++vulkanValidationMessages;
    if (defaultLogOutput)
        defaultLogOutput(data, category, priority, message);
}
} // namespace
int main(int argc, char** argv) {
    SDL_GetLogOutputFunction(&defaultLogOutput, &defaultLogData);
    SDL_SetLogOutputFunction(CountValidation, defaultLogData);
    try {
        const std::string_view suite = argc > 1 ? argv[1] : "core";
        if (suite == "core")
            Core();
        else if (suite == "physics2d")
            Physics2D();
        else if (suite == "scene")
            Scenes();
        else if (suite == "assets")
            Assets();
        else if (suite == "audio")
            Audio();
        else if (suite == "window")
            WindowEvents();
        else if (suite == "gpu")
            Gpu();
        else if (suite == "lifecycle")
            Lifecycle();
        else if (suite == "vulkan")
            VulkanSystems();
        else if (suite == "failures")
            Failures();
        else if (suite == "stress")
            Stress();
        else if (suite == "longrun")
            LongRun();
        else
            throw std::runtime_error("Unknown suite");
        if (vulkanValidationMessages != 0)
            throw std::runtime_error("Vulkan validation reported " +
                                     std::to_string(vulkanValidationMessages) + " error(s)");
        std::cout << suite << ": " << checks << " checks passed\n";
        return 0;
    } catch (const SkipTest& skipped) {
        std::cout << "SKIPPED: " << skipped.what() << '\n';
        return 77;
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
}
