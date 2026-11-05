#include "Engine/Platform/Input.hpp"
#include <SDL3/SDL.h>

namespace LuxEngine {

bool Input::IsKeyPressed(int32 keycode) {
    int numKeys = 0;
    const bool* state = SDL_GetKeyboardState(&numKeys);
    if (!state)
        return false;

    const SDL_Scancode scancode =
        SDL_GetScancodeFromKey(static_cast<SDL_Keycode>(keycode), nullptr);
    if (scancode >= 0 && scancode < numKeys) {
        return state[scancode];
    }
    return false;
}

bool Input::IsMouseButtonPressed(int32 button) {
    if (button < 1 || button > 32)
        return false;
    float x = 0.0f;
    float y = 0.0f;
    const SDL_MouseButtonFlags flags = SDL_GetMouseState(&x, &y);
    return (flags & SDL_BUTTON_MASK(button)) != 0;
}

std::pair<float, float> Input::GetMousePosition() {
    float x = 0.0f;
    float y = 0.0f;
    SDL_GetMouseState(&x, &y);
    return {x, y};
}

float Input::GetMouseX() {
    return GetMousePosition().first;
}

float Input::GetMouseY() {
    return GetMousePosition().second;
}

} // namespace LuxEngine
