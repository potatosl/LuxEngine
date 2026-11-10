#include "App/ForestSurvivorApp.hpp"
#include "Engine/EntryPoint.hpp"
#include <SDL3/SDL.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

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
    }
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
    for (int row = 1; row < 100; ++row)
        for (int column = 1; column < 100; ++column) {
            const LuxEngine::Vec2 point{column / 100.0f, row / 100.0f};
            if (app.ActionAt(point.x, point.y) == action)
                return point;
        }
    throw std::runtime_error("Expected visible UI button has no hit target");
}
void Click(ForestSurvivorApp& app, UiAction action) {
    const auto point = Find(app, action);
    Motion(app, point.x, point.y);
    Button(app, true);
    Frames(app, 1);
    Button(app, false);
    Frames(app);
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
    }
    Click(app, UiAction::Pause);
    Click(app, UiAction::Restart);
    Check(app.GetGame().GetPhase() == Phase::Playing && app.GetGame().Elapsed() < 1 &&
              app.GetGame().GetPlayer().level == 1,
          "restart button resets the run");
    Click(app, UiAction::Pause);
    const auto quit = Find(app, UiAction::Quit);
    Motion(app, quit.x, quit.y);
    Button(app, true);
    Frames(app, 1);
    Button(app, false);
    SDL_Delay(18);
    Check(app.RunFrame() == LuxEngine::Application::FrameResult::Stopped,
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
