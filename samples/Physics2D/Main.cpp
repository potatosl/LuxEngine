#include "Common/SampleApplication.hpp"
#include "Engine/EntryPoint.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
namespace le = LuxEngine;
namespace {
class Physics2D final : public LuxSamples::SampleApplication {
public:
    static constexpr std::string_view Help =
        "2D 물리: Space·마우스 왼쪽 버튼·게임패드 아래쪽 버튼으로 상자를 떨어뜨립니다.";
    explicit Physics2D(LuxSamples::Options options)
        : SampleApplication("2D 물리", "Space / 마우스 왼쪽 / 게임패드 A로 상자 추가",
                            std::move(options)) {}

private:
    le::Scene scene_;
    le::Random random_{42};
    unsigned spawned_ = 0;
    le::Entity Box(le::Vec2 position, le::Vec2 size, le::Vec4 color, bool dynamic) {
        auto entity = scene_.CreateEntity(dynamic ? "동적 상자" : "고정 바닥");
        auto& transform = entity.GetComponent<le::TransformComponent>();
        transform.position = {position.x, position.y, 0};
        transform.scale = {size.x, size.y, 1};
        entity.AddComponent<le::SpriteRendererComponent>().color = color;
        entity.AddComponent<le::BoxCollider2DComponent>();
        if (dynamic)
            entity.AddComponent<le::Rigidbody2DComponent>();
        return entity;
    }
    void Spawn() {
        if (spawned_ >= 64)
            return;
        const float size = random_.Range(.45f, .85f);
        auto box =
            Box({random_.Range(-2.0f, 2.0f), 3.5f}, {size, size}, {.78f, .57f, .33f, 1}, true);
        box.GetComponent<le::TransformComponent>().rotation.z = random_.Range(-.3f, .3f);
        ++spawned_;
    }
    void Reset() override {
        for (auto entity : scene_.GetEntities())
            scene_.DestroyEntity(entity);
        random_ = le::Random(42);
        spawned_ = 0;
        Box({0, -3.8f}, {16, 1}, {.24f, .39f, .28f, 1}, false);
        Box({-8, -.5f}, {.5f, 7.2f}, {.2f, .31f, .26f, 1}, false);
        Box({8, -.5f}, {.5f, 7.2f}, {.2f, .31f, .26f, 1}, false);
        auto slope = Box({3, -1.5f}, {4, .3f}, {.36f, .49f, .33f, 1}, false);
        slope.GetComponent<le::TransformComponent>().rotation.z = .23f;
        le::Input::BindAction("drop", le::LuxKey::Space);
        le::Input::BindAction("drop", le::LuxMouseButton::Left);
        le::Input::BindAction("drop", le::LuxGamepadButton::South);
        for (int index = 0; index < 8; ++index) {
            auto entity = Box({-2 + float(index % 3) * .9f, -2.9f + float(index / 3) * .9f},
                              {.75f, .75f}, {.75f, .52f, .32f, 1}, true);
            entity.GetComponent<le::TransformComponent>().rotation.z = float(index) * .04f;
            ++spawned_;
        }
    }
    void Step(float dt) override {
        if (le::Input::WasActionPressed("drop"))
            Spawn();
        scene_.OnUpdate(dt);
    }
    void Draw(float) override {
        for (int y = -5; y <= 4; ++y)
            for (int x = -12; x <= 12; ++x)
                LuxSamples::Sprite("TinyDungeon/Tiles/tile_0000.png", {float(x), float(y)}, {1, 1});
        for (auto entity : scene_.GetEntities()) {
            const auto& t = entity.GetComponent<le::TransformComponent>();
            const bool dynamic = entity.HasComponent<le::Rigidbody2DComponent>();
            LuxSamples::Sprite(
                dynamic ? "TinyDungeon/Tiles/tile_0075.png" : "TinyDungeon/Tiles/tile_0014.png",
                {t.position.x, t.position.y}, {t.scale.x, t.scale.y}, {1, 1, 1, 1}, t.rotation.z);
        }
        LuxSamples::Panel({4.8f, 3.7f}, {5.3f, 1.3f});
        LuxSamples::Text("Box2D 물리 시뮬레이션", {2.5f, 4.03f}, .04f);
        LuxSamples::Text("상자 " + std::to_string(spawned_) + " / 64", {2.5f, 3.54f}, .04f);
    }
};
} // namespace
LUX_ENGINE_MAIN(LuxSamples::RunSample<Physics2D>)
