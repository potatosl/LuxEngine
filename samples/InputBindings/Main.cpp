#include "Common/SampleApplication.hpp"
#include "Engine/EntryPoint.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
namespace le = LuxEngine;
namespace {
class InputBindings final : public LuxSamples::SampleApplication {
public:
    static constexpr std::string_view Help = "InputBindings: WASD/Arrows/D-pad move, Space/Mouse "
                                             "left/Gamepad South fires, F rebinds Space/Enter.";
    explicit InputBindings(LuxSamples::Options options)
        : SampleApplication("INPUT BINDINGS", "SPACE / MOUSE LEFT / PAD A FIRE   F REBIND KEY",
                            std::move(options)) {}

private:
    le::Vec2 position_{};
    unsigned presses_ = 0;
    bool alternate_ = false;
    float flash_ = 0;
    void Reset() override {
        position_ = {};
        presses_ = 0;
        flash_ = 0;
        alternate_ = false;
        le::Input::ClearAction("fire");
        le::Input::BindAction("fire", le::LuxKey::Space);
        le::Input::BindAction("fire", le::LuxMouseButton::Left);
        le::Input::BindAction("fire", le::LuxGamepadButton::South);
        le::Input::BindAction("rebind", le::LuxKey::F);
    }
    void Step(float dt) override {
        position_ += LuxSamples::Movement() * (dt * 3.5f);
        position_.x = std::clamp(position_.x, -7.0f, 7.0f);
        position_.y = std::clamp(position_.y, -2.4f, 1.4f);
        if (le::Input::WasActionPressed("rebind")) {
            le::Input::UnbindAction("fire", alternate_ ? le::LuxKey::Enter : le::LuxKey::Space);
            alternate_ = !alternate_;
            le::Input::BindAction("fire", alternate_ ? le::LuxKey::Enter : le::LuxKey::Space);
        }
        if (le::Input::WasActionPressed("fire")) {
            ++presses_;
            flash_ = .28f;
        }
        flash_ = std::max(0.0f, flash_ - dt);
    }
    void Draw(float elapsed) override {
        for (int y = -5; y <= 4; ++y)
            for (int x = -14; x <= 14; ++x)
                LuxSamples::Sprite("TinyTown/Tiles/tile_0043.png", {float(x), float(y)}, {1, 1});
        const std::array<bool, 3> active{
            le::Input::IsKeyPressed(alternate_ ? le::LuxKey::Enter : le::LuxKey::Space),
            le::Input::IsMouseButtonPressed(le::LuxMouseButton::Left),
            le::Input::IsGamepadButtonPressed(le::LuxGamepadButton::South)};
        const char* labels[] = {"KEYBOARD", "MOUSE", "GAMEPAD"};
        for (int index = 0; index < 3; ++index) {
            const float x = -6.0f + float(index) * 6;
            LuxSamples::Panel({x, 3}, {5.2f, 1.7f});
            LuxSamples::Text(labels[index], {x - 1.3f, 3.55f}, .045f);
            LuxSamples::Quad(le::Vec2{x, 2.65f}, {3.8f, .3f},
                             active[index] ? le::Vec4{.87f, .73f, .36f, 1}
                                           : le::Vec4{.2f, .35f, .3f, 1});
        }
        const bool down = le::Input::IsActionDown("fire");
        LuxSamples::Sprite("TinyDungeon/Tiles/tile_0087.png", position_, {1.25f, 1.25f},
                           down ? le::Vec4{1, 1, .75f, 1} : le::Vec4{1, 1, 1, 1});
        if (flash_ > 0) {
            const float size = 1.2f + (1 - flash_ / .28f) * 1.3f;
            LuxSamples::Quad(position_, {size, .06f}, {.96f, .8f, .45f, flash_ / .28f},
                             elapsed * 4);
            LuxSamples::Quad(position_, {.06f, size}, {.96f, .8f, .45f, flash_ / .28f},
                             elapsed * 4);
        }
        LuxSamples::Panel({0, -3.85f}, {14.5f, 2.2f});
        LuxSamples::Text("MOVE WITH WASD / ARROWS / DPAD", {-6, -3.3f}, .038f);
        LuxSamples::Text(std::string("KEY ") + (alternate_ ? "ENTER" : "SPACE") + "   PRESSES " +
                             std::to_string(presses_),
                         {-6, -3.9f}, .045f);
        LuxSamples::Text("REBIND KEEPS MOUSE AND GAMEPAD ACTIVE", {-6, -4.55f}, .032f);
    }
};
} // namespace
LUX_ENGINE_MAIN(LuxSamples::RunSample<InputBindings>)
