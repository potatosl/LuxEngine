#include "Engine/LuxEngine.hpp"
#include "Engine/Platform/Branding.hpp"
#include <SDL3/SDL.h>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stb_image.h>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <type_traits>

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
    Check(SDL_Init(SDL_INIT_VIDEO), "dummy video initializes");
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
#if LUX_USE_OGRE
        GpuDevice unavailable;
        Check(!unavailable.Initialize(handle) && !unavailable.IsReady(),
              "Ogre rejects a non-X11 window without loading the GL plugin");
#endif
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

        event = {};
        event.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
        event.window.windowID = SDL_GetWindowID(handle);
        Check(SDL_PushEvent(&event), "own close event queued");
        window.PollEvents();
        Check(!window.IsRunning(), "own close event stops window");
    }
    SDL_Quit();
}
struct Graphics {
    Window window;
    GpuDevice gpu;
    Graphics() {
        if (!SDL_Init(SDL_INIT_VIDEO))
            throw std::runtime_error(SDL_GetError());
        WindowProps props;
        props.width = 320;
        props.height = 240;
        props.title = "LuxEngine GPU verification";
#if LUX_USE_OGRE
        props.useOpenGL = true;
#endif
        Check(window.Initialize(props), "GPU test window");
        const bool iconAvailable = window.HasWindowIcon();
        Check(iconAvailable || std::string_view(SDL_GetCurrentVideoDriver()) == "wayland",
              "window icon applied when supported");
        const auto expected =
            SDL_GetSystemTheme() == SDL_SYSTEM_THEME_DARK ? ColorTheme::Dark : ColorTheme::Light;
        if (iconAvailable)
            Check(window.GetColorTheme() == expected, "window icon follows system theme");
        SDL_Event changed{};
        changed.type = SDL_EVENT_SYSTEM_THEME_CHANGED;
        Check(SDL_PushEvent(&changed), "theme event queued");
        window.PollEvents();
        if (iconAvailable)
            Check(window.HasWindowIcon() && window.GetColorTheme() == expected,
                  "theme event preserves correct icon");
        Check(gpu.Initialize(window.GetNativeHandle()), SDL_GetError());
    }
    ~Graphics() {
        AssetManager::Shutdown();
        Renderer2D::Shutdown();
        Renderer3D::Shutdown();
        gpu.Shutdown();
        window.Shutdown();
        SDL_Quit();
    }
};
void Lifecycle() {
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
void Gpu() {
    Graphics graphics;
#if LUX_USE_OGRE
    GpuDevice duplicateOgre;
    Check(!duplicateOgre.Initialize(graphics.window.GetNativeHandle()) && !duplicateOgre.IsReady(),
          "only one Ogre root is active");
#endif
    Files files;
    auto* surface = SDL_CreateSurface(2, 2, SDL_PIXELFORMAT_RGBA32);
    Check(surface != nullptr, "test image surface");
    SDL_FillSurfaceRect(surface, nullptr, SDL_MapSurfaceRGBA(surface, 255, 0, 0, 255));
    Check(SDL_SaveBMP(surface, files.Path("red.bmp").c_str()), "write test image");
    SDL_DestroySurface(surface);
#if LUX_USE_OGRE
    auto texture = AssetManager::LoadTexture(files.Path("red.bmp"), &graphics.gpu);
#else
    auto texture = AssetManager::LoadTexture(files.Path("red.bmp"), graphics.gpu.GetHandle());
#endif
    Check(texture && texture->IsValid() && texture->GetWidth() == 2, "real image upload");
#if LUX_USE_OGRE
    Check(!AssetManager::LoadTexture(files.Path("absent.png"), &graphics.gpu),
          "missing GPU image fails");
#else
    Check(!AssetManager::LoadTexture(files.Path("absent.png"), graphics.gpu.GetHandle()),
          "missing GPU image fails");
#endif
    Renderer2D::Init(&graphics.gpu);
    Renderer3D::Init(&graphics.gpu);
    OrthographicCamera camera(-2, 2, -2, 2);
    PerspectiveCamera perspective(ToRadians(60), 4.0f / 3, 0.1f, 100);
    for (int frame = 0; frame < 3; ++frame) {
        graphics.window.PollEvents();
        Check(graphics.gpu.BeginFrame() == GpuDevice::FrameStart::Ready, "begin frame");
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
    }
#if !LUX_USE_OGRE
    Check(SDL_WaitForGPUIdle(graphics.gpu.GetHandle()), "GPU completion");
#endif
    texture.reset();
    AssetManager::Shutdown();
#if !LUX_USE_OGRE
    GpuBuffer buffer;
    Check(buffer.Initialize(graphics.gpu.GetHandle(), BufferType::Vertex, 4), "buffer init");
    const uint32 value = 42;
    Check(!buffer.SetData(&value, 8), "oversized upload rejected");
    Check(buffer.SetData(&value, 4), "buffer upload");
    GpuBuffer moved = std::move(buffer);
    Check(!buffer.IsValid() && moved.IsValid(), "buffer ownership transfer");
    moved.Release();
#endif
    const uint32 pixel = 0xffffffff;
    GpuTexture delayed;
#if LUX_USE_OGRE
    Check(delayed.Initialize(&graphics.gpu, 1, 1, &pixel), "delayed texture init");
#else
    Check(delayed.Initialize(graphics.gpu.GetHandle(), 1, 1, &pixel), "delayed texture init");
#endif
    graphics.gpu.Shutdown();
    Check(!delayed.IsValid() && !delayed.GetHandle(), "device shutdown invalidates resources");
    delayed.Release();
}
} // namespace
int main(int argc, char** argv) {
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
        else
            throw std::runtime_error("Unknown suite");
        std::cout << suite << ": " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
}
