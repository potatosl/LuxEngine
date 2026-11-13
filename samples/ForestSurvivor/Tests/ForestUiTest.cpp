#include "App/ForestSurvivorApp.hpp"
#include <LuxEngine/Audio/AudioEngine.hpp>
#include <LuxEngine/EntryPoint.hpp>
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
using namespace ForestGame;
void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
void Frames(ForestSurvivorApp& app, int count = 3) {
    for (int frame = 0; frame < count; ++frame) {
        SDL_Delay(18);
        const auto result = app.RunFrame();
        Check(result == LuxEngine::Application::FrameResult::Continue ||
                  result == LuxEngine::Application::FrameResult::Skipped,
              "UI scenario keeps rendering");
        Check(app.UiTextFits(),
              "all Korean labels fit the current viewport and wrapped containers");
    }
}
void Capture(ForestSurvivorApp& app, const std::string& name) {
    const char* directory = SDL_getenv("LUX_FOREST_CAPTURE_DIR");
    if (!directory)
        return;
    std::filesystem::create_directories(directory);
    Check(app.GetGpuDevice().RequestFrameCapture(), "localized UI capture queues");
    Frames(app, 2);
    LuxEngine::GpuFrameCapture capture;
    Check(app.GetGpuDevice().ReadFrameCapture(capture), "localized UI capture completes");
    SDL_Surface* surface = SDL_CreateSurfaceFrom(
        static_cast<int>(capture.width), static_cast<int>(capture.height), SDL_PIXELFORMAT_RGBA32,
        capture.rgba.data(), static_cast<int>(capture.width * 4));
    Check(surface != nullptr, "localized UI capture surface is created");
    const bool saved =
        SDL_SaveBMP(surface, (std::filesystem::path(directory) / (name + ".bmp")).string().c_str());
    SDL_DestroySurface(surface);
    Check(saved, "localized UI capture is saved");
}
void Motion(ForestSurvivorApp& app, float x, float y) {
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.windowID = SDL_GetWindowID(app.GetWindow().GetNativeHandle());
    event.motion.x = x * app.GetWindow().GetWidth();
    event.motion.y = y * app.GetWindow().GetHeight();
    Check(SDL_PushEvent(&event), "mouse movement queues");
}
void Button(ForestSurvivorApp& app, bool pressed) {
    SDL_Event event{};
    event.type = pressed ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.windowID = SDL_GetWindowID(app.GetWindow().GetNativeHandle());
    event.button.button = SDL_BUTTON_LEFT;
    Check(SDL_PushEvent(&event), "mouse button queues");
}
void Key(ForestSurvivorApp& app, SDL_Keycode key, bool pressed) {
    SDL_Event event{};
    event.type = pressed ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.windowID = SDL_GetWindowID(app.GetWindow().GetNativeHandle());
    event.key.key = key;
    Check(SDL_PushEvent(&event), "keyboard event queues");
}
LuxEngine::Vec2 Find(ForestSurvivorApp& app, UiAction action) {
    LuxEngine::Vec2 low{1, 1}, high{0, 0};
    bool found = false;
    for (int row = 1; row < 100; ++row)
        for (int column = 1; column < 100; ++column) {
            const LuxEngine::Vec2 point{column / 100.0f, row / 100.0f};
            if (app.ActionAt(point.x, point.y) == action) {
                found = true;
                low.x = std::min(low.x, point.x);
                low.y = std::min(low.y, point.y);
                high.x = std::max(high.x, point.x);
                high.y = std::max(high.y, point.y);
            }
        }
    Check(found, "Expected visible UI button has no hit target");
    const auto center = (low + high) * .5f;
    Check(app.ActionAt(center.x, center.y) == action, "button center belongs to its hit target");
    return center;
}
void QueueClick(ForestSurvivorApp& app, UiAction action) {
    const auto point = Find(app, action);
    // Queue a complete gesture together so native pointer motion cannot split the synthetic click.
    Motion(app, point.x, point.y);
    Button(app, true);
    Motion(app, point.x, point.y);
    Button(app, false);
}
void Click(ForestSurvivorApp& app, UiAction action) {
    QueueClick(app, action);
    const auto expected = action == UiAction::Pause ? Phase::Paused : Phase::Playing;
    const auto deadline = SDL_GetTicks() + 2000;
    // UI actions are consumed by the next fixed update, which need not run in every rendered frame.
    do {
        Frames(app, 1);
        if (app.GetGame().GetPhase() == expected)
            return;
    } while (SDL_GetTicks() < deadline);
    Check(false, "a confirmed button click reaches the expected phase within two seconds");
}
} // namespace

int TestMenuButtons() {
    RunOptions options;
    options.pauseOnFocusLoss = false;
    ForestSurvivorApp app(options);
    if (!app.Initialize())
        return 77;
    Frames(app);
    Check(app.GetGame().GetPhase() == Phase::Title, "normal startup stays at the title");
    for (const auto size : {LuxEngine::Vec2{960, 720}, LuxEngine::Vec2{1600, 900}}) {
        Check(app.GetWindow().SetSize(static_cast<unsigned>(size.x), static_cast<unsigned>(size.y)),
              "title resizes for Korean text layout checks");
        Frames(app, 5);
        Capture(app, "title-" + std::to_string(static_cast<int>(size.x)));
    }
    for (const auto key : {SDLK_RETURN, SDLK_SPACE, SDLK_ESCAPE}) {
        Key(app, key, true);
        Frames(app, 1);
        Key(app, key, false);
        Frames(app, 1);
        Check(app.GetGame().GetPhase() == Phase::Title,
              "keyboard shortcuts cannot bypass the title start button");
    }
    Motion(app, .01f, .01f);
    Button(app, true);
    Button(app, false);
    Frames(app);
    Check(app.GetGame().GetPhase() == Phase::Title, "blank-area clicks do not start the game");
    const auto start = Find(app, UiAction::Start);
    Motion(app, start.x, start.y);
    Button(app, true);
    Frames(app, 1);
    Motion(app, .01f, .01f);
    Button(app, false);
    Frames(app);
    Check(app.GetGame().GetPhase() == Phase::Title,
          "dragging out of a pressed button cancels activation");
    Click(app, UiAction::Start);
    Check(app.GetGame().GetPhase() == Phase::Playing, "releasing the start button starts play");
    if (std::string_view(SDL_getenv("SDL_AUDIO_DRIVER") ? SDL_getenv("SDL_AUDIO_DRIVER") : "") ==
        "dummy")
        Check(LuxEngine::AudioEngine::GetCachedFileCount() > 0,
              "a confirmed menu click loads and plays the bundled CC0 sound");
    Click(app, UiAction::Pause);
    const auto stoppedAt = app.GetGame().Elapsed();
    Frames(app, 5);
    Check(app.GetGame().GetPhase() == Phase::Paused && app.GetGame().Elapsed() == stoppedAt,
          "the visible pause button freezes the run");
    Click(app, UiAction::Resume);
    Check(app.GetGame().GetPhase() == Phase::Playing, "resume button returns to play");
    for (const auto size : {LuxEngine::Vec2{960, 720}, LuxEngine::Vec2{1600, 700}}) {
        Check(app.GetWindow().SetSize(static_cast<unsigned>(size.x), static_cast<unsigned>(size.y)),
              "window resizes for pointer mapping checks");
        Frames(app, 5);
        Click(app, UiAction::Pause);
        Check(app.GetGame().GetPhase() == Phase::Paused,
              "pause hit target follows the resized viewport");
        Click(app, UiAction::Resume);
        Check(app.GetGame().GetPhase() == Phase::Playing,
              "resume hit target follows the resized viewport");
        Capture(app, "playing-" + std::to_string(static_cast<int>(size.x)));
    }
    Click(app, UiAction::Pause);
    Click(app, UiAction::Restart);
    Check(app.GetGame().GetPhase() == Phase::Playing && app.GetGame().Elapsed() < 1 &&
              app.GetGame().GetPlayer().level == 1,
          "restart button resets the run");
    Click(app, UiAction::Pause);
    QueueClick(app, UiAction::Quit);
    auto result = LuxEngine::Application::FrameResult::Continue;
    const auto deadline = SDL_GetTicks() + 2000;
    while (result != LuxEngine::Application::FrameResult::Stopped && SDL_GetTicks() < deadline) {
        SDL_Delay(18);
        result = app.RunFrame();
        Check(result != LuxEngine::Application::FrameResult::Error,
              "quit gesture has no GPU error");
    }
    Check(result == LuxEngine::Application::FrameResult::Stopped,
          "quit button closes the application");
    std::cout << "Forest Survivor UI interaction checks passed\n";
    return 0;
}

int RunForestSurvivorUiTests(int, char**) {
    const int menuResult = TestMenuButtons();
    if (menuResult != 0)
        return menuResult;
    for (int choice = 0; choice < 3; ++choice) {
        RunOptions preview;
        preview.pauseOnFocusLoss = false;
        preview.showcaseSeconds = 60;
        preview.stopAtUpgrade = true;
        ForestSurvivorApp app(preview);
        Check(app.Initialize(), "upgrade UI initializes after the previous app shuts down");
        Frames(app);
        Check(app.GetGame().GetPhase() == Phase::Upgrade,
              "an explicit test preview stops at a genuine level-up");
        const auto elapsed = app.GetGame().Elapsed();
        for (int index = 0; index < 3; ++index)
            Find(app, static_cast<UiAction>(static_cast<int>(UiAction::Choice0) + index));
        Frames(app, 3);
        Check(app.GetWindow().SetSize(choice == 0 ? 960u : 1600u, choice == 0 ? 720u : 900u),
              "Korean upgrade cards resize with the viewport");
        Frames(app, 5);
        Capture(app, "upgrade-" + std::to_string(choice == 0 ? 960 : 1600));
        Check(app.GetGame().Elapsed() == elapsed, "upgrade cards pause the actual run clock");
        Click(app, static_cast<UiAction>(static_cast<int>(UiAction::Choice0) + choice));
        Check(app.GetGame().GetPhase() == Phase::Playing,
              "each real upgrade card applies a choice and resumes play");
        app.Close();
        app.RunFrame();
    }
    std::cout << "Forest Survivor upgrade-button checks passed\n";
    return 0;
}

LUX_ENGINE_MAIN(RunForestSurvivorUiTests)
