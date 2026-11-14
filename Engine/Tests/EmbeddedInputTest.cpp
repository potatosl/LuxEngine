#include <LuxEngine/Core/Log.hpp>
#include <LuxEngine/EntryPoint.hpp>
#include <LuxEngine/Platform/Input.hpp>
#include <SDL3/SDL.h>
#include <stdexcept>
#include <thread>

namespace {
int Run(int, char**) {
    using namespace LuxEngine;
    if (!SDL_Init(SDL_INIT_EVENTS))
        return 1;
    Input::Initialize();
    if (!Input::BindAction("editor-play", LuxKey::A))
        return 2;
    Input::SetExternalKey(LuxKey::A, true);
    if (!Input::IsKeyPressed(LuxKey::A) || !Input::WasActionPressed("editor-play"))
        return 3;
    Input::AdvanceFrame();
    if (Input::WasActionPressed("editor-play") || !Input::IsActionDown("editor-play"))
        return 4;
    Input::ClearExternalKeys();
    if (Input::IsActionDown("editor-play") || !Input::WasActionReleased("editor-play"))
        return 5;
    if (!Input::BindAction("game-click", LuxMouseButton::Left))
        return 7;
    Input::SetExternalMousePosition(125.5f, 42.25f);
    Input::SetExternalMouseButton(LuxMouseButton::Left, true);
    if (!Input::IsMouseButtonPressed(LuxMouseButton::Left) ||
        !Input::WasActionPressed("game-click") || Input::GetMouseX() != 125.5f ||
        Input::GetMouseY() != 42.25f)
        return 8;
    Input::AdvanceFrame();
    Input::ClearExternalMouse();
    if (Input::IsActionDown("game-click") || !Input::WasActionReleased("game-click"))
        return 9;
    const auto before = Log::ReadAfter(0);
    const auto cursor = before.empty() ? 0 : before.back().sequence;
    std::thread writer([] { Log::Warn("백그라운드 로그 수집 검사입니다."); });
    writer.join();
    const auto entries = Log::ReadAfter(cursor);
    if (entries.size() != 1 || entries[0].level != LogLevel::Warn ||
        !Log::ReadAfter(entries[0].sequence).empty())
        return 6;
    Input::Shutdown();
    SDL_Quit();
    return 0;
}
} // namespace
LUX_ENGINE_MAIN(Run)
