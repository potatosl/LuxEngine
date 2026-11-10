#include "Common/SampleApplication.hpp"
#include "Engine/EntryPoint.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
namespace le = LuxEngine;
namespace {
class Basic2D final : public LuxSamples::SampleApplication {
public:
    static constexpr std::string_view Help = "Basic2D: WASD/Arrows/D-pad move the sprite.";
    explicit Basic2D(LuxSamples::Options options)
        : SampleApplication("BASIC 2D", "WASD / ARROWS / DPAD MOVE", std::move(options)) {}

private:
    le::Scene scene_;
    le::Entity player_;
    void Reset() override {
        for (auto entity : scene_.GetEntities())
            scene_.DestroyEntity(entity);
        player_ = scene_.CreateEntity("플레이어");
        player_.GetComponent<le::TransformComponent>().scale = {.8f, 1.1f, 1};
        player_.AddComponent<le::SpriteRendererComponent>().color = {.96f, .76f, .37f, 1};
    }
    void Step(float dt) override {
        const auto move = LuxSamples::Movement();
        auto& transform = player_.GetComponent<le::TransformComponent>();
        transform.position.x =
            std::clamp(transform.position.x + move.x * dt * 4, -HalfWidth() + 1, HalfWidth() - 1);
        transform.position.y = std::clamp(transform.position.y + move.y * dt * 4, -4.5f, 3.5f);
        transform.rotation.z = move.x * .12f;
    }
    void Draw(float elapsed) override {
        for (int y = -5; y <= 4; ++y)
            for (int x = -14; x <= 14; ++x)
                LuxSamples::Sprite("TinyTown/Tiles/tile_0000.png", {float(x), float(y)}, {1, 1});
        for (int y = -5; y <= 4; ++y)
            for (int x = -1; x <= 1; ++x)
                LuxSamples::Sprite("TinyTown/Tiles/tile_0025.png", {float(x), float(y)}, {1, 1});
        for (const auto& p : std::array<le::Vec2, 6>{
                 {{-6, 2}, {-4, -2}, {5, 1.5f}, {7, -2.5f}, {-8, -1}, {3.5f, -3}}})
            LuxSamples::Sprite("TinyTown/Tiles/tile_0016.png", p, {2, 2});
        for (int i = 0; i < 7; ++i)
            LuxSamples::Sprite("TinyTown/Tiles/tile_0002.png", {-6 + float(i) * 1.7f, -4.3f},
                               {.7f, .7f});
        const auto& t = player_.GetComponent<le::TransformComponent>();
        // Scene sprites currently carry color only; textured assets use the renderer's public API.
        LuxSamples::Sprite("TinyDungeon/Tiles/tile_0096.png",
                           {t.position.x, t.position.y + .04f * std::sin(elapsed * 6)},
                           {1.2f, 1.2f}, {1, 1, 1, 1}, t.rotation.z);
        LuxSamples::Panel({5, -3.5f}, {6.7f, 1.4f});
        LuxSamples::Text("SCENE / ENTITY / TRANSFORM", {2.1f, -3.16f}, .04f);
        LuxSamples::Text("CC0 TILES AND SPRITES", {2.1f, -3.65f}, .036f);
    }
};
} // namespace
LUX_ENGINE_MAIN(LuxSamples::RunSample<Basic2D>)
