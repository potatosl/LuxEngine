#pragma once

#include <LuxEngine/Core/Core.hpp>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>
#include <string_view>
#include <utility>

union SDL_Event;

namespace LuxEngine {

enum class LuxKey : int32 {
    A = SDLK_A,
    B = SDLK_B,
    C = SDLK_C,
    D = SDLK_D,
    E = SDLK_E,
    F = SDLK_F,
    G = SDLK_G,
    H = SDLK_H,
    I = SDLK_I,
    J = SDLK_J,
    K = SDLK_K,
    L = SDLK_L,
    M = SDLK_M,
    N = SDLK_N,
    O = SDLK_O,
    P = SDLK_P,
    Q = SDLK_Q,
    R = SDLK_R,
    S = SDLK_S,
    T = SDLK_T,
    U = SDLK_U,
    V = SDLK_V,
    W = SDLK_W,
    X = SDLK_X,
    Y = SDLK_Y,
    Z = SDLK_Z,
    Num0 = SDLK_0,
    Num1 = SDLK_1,
    Num2 = SDLK_2,
    Num3 = SDLK_3,
    Num4 = SDLK_4,
    Num5 = SDLK_5,
    Num6 = SDLK_6,
    Num7 = SDLK_7,
    Num8 = SDLK_8,
    Num9 = SDLK_9,
    Numpad0 = SDLK_KP_0,
    Numpad1 = SDLK_KP_1,
    Numpad2 = SDLK_KP_2,
    Numpad3 = SDLK_KP_3,
    Numpad4 = SDLK_KP_4,
    Numpad5 = SDLK_KP_5,
    Numpad6 = SDLK_KP_6,
    Numpad7 = SDLK_KP_7,
    Numpad8 = SDLK_KP_8,
    Numpad9 = SDLK_KP_9,
    Space = SDLK_SPACE,
    Enter = SDLK_RETURN,
    Escape = SDLK_ESCAPE,
    Tab = SDLK_TAB,
    Backspace = SDLK_BACKSPACE,
    Delete = SDLK_DELETE,
    Insert = SDLK_INSERT,
    Home = SDLK_HOME,
    End = SDLK_END,
    PageUp = SDLK_PAGEUP,
    PageDown = SDLK_PAGEDOWN,
    CapsLock = SDLK_CAPSLOCK,
    Comma = SDLK_COMMA,
    Period = SDLK_PERIOD,
    Slash = SDLK_SLASH,
    Semicolon = SDLK_SEMICOLON,
    Apostrophe = SDLK_APOSTROPHE,
    Minus = SDLK_MINUS,
    Equals = SDLK_EQUALS,
    LeftBracket = SDLK_LEFTBRACKET,
    RightBracket = SDLK_RIGHTBRACKET,
    Backslash = SDLK_BACKSLASH,
    Grave = SDLK_GRAVE,
    Up = SDLK_UP,
    Down = SDLK_DOWN,
    Left = SDLK_LEFT,
    Right = SDLK_RIGHT,
    F1 = SDLK_F1,
    F2 = SDLK_F2,
    F3 = SDLK_F3,
    F4 = SDLK_F4,
    F5 = SDLK_F5,
    F6 = SDLK_F6,
    F7 = SDLK_F7,
    F8 = SDLK_F8,
    F9 = SDLK_F9,
    F10 = SDLK_F10,
    F11 = SDLK_F11,
    F12 = SDLK_F12,
    LeftShift = SDLK_LSHIFT,
    RightShift = SDLK_RSHIFT,
    LeftCtrl = SDLK_LCTRL,
    RightCtrl = SDLK_RCTRL,
    LeftAlt = SDLK_LALT,
    RightAlt = SDLK_RALT
};

enum class LuxMouseButton : int32 {
    Left = SDL_BUTTON_LEFT,
    Middle = SDL_BUTTON_MIDDLE,
    Right = SDL_BUTTON_RIGHT,
    X1 = SDL_BUTTON_X1,
    X2 = SDL_BUTTON_X2
};

enum class LuxGamepadButton : int32 {
    South = SDL_GAMEPAD_BUTTON_SOUTH,
    East = SDL_GAMEPAD_BUTTON_EAST,
    West = SDL_GAMEPAD_BUTTON_WEST,
    North = SDL_GAMEPAD_BUTTON_NORTH,
    Back = SDL_GAMEPAD_BUTTON_BACK,
    Guide = SDL_GAMEPAD_BUTTON_GUIDE,
    Start = SDL_GAMEPAD_BUTTON_START,
    LeftStick = SDL_GAMEPAD_BUTTON_LEFT_STICK,
    RightStick = SDL_GAMEPAD_BUTTON_RIGHT_STICK,
    LeftShoulder = SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,
    RightShoulder = SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
    DpadUp = SDL_GAMEPAD_BUTTON_DPAD_UP,
    DpadDown = SDL_GAMEPAD_BUTTON_DPAD_DOWN,
    DpadLeft = SDL_GAMEPAD_BUTTON_DPAD_LEFT,
    DpadRight = SDL_GAMEPAD_BUTTON_DPAD_RIGHT
};

enum class InputDevice { Keyboard, MouseButton, GamepadButton };

struct InputBinding {
    InputDevice device;
    int32 code;

    static InputBinding Key(LuxKey key) {
        return {InputDevice::Keyboard, static_cast<int32>(key)};
    }
    static InputBinding Mouse(LuxMouseButton button) {
        return {InputDevice::MouseButton, static_cast<int32>(button)};
    }
    static InputBinding Gamepad(LuxGamepadButton button) {
        return {InputDevice::GamepadButton, static_cast<int32>(button)};
    }
    bool operator==(const InputBinding&) const = default;
};

class LUX_API Input {
public:
    static void Initialize();
    static void Shutdown();
    static bool BindAction(std::string_view action, InputBinding binding);
    static bool BindAction(std::string_view action, LuxKey key) {
        return BindAction(action, InputBinding::Key(key));
    }
    static bool BindAction(std::string_view action, LuxMouseButton button) {
        return BindAction(action, InputBinding::Mouse(button));
    }
    static bool BindAction(std::string_view action, LuxGamepadButton button) {
        return BindAction(action, InputBinding::Gamepad(button));
    }
    static bool UnbindAction(std::string_view action, InputBinding binding);
    static bool UnbindAction(std::string_view action, LuxKey key) {
        return UnbindAction(action, InputBinding::Key(key));
    }
    static bool UnbindAction(std::string_view action, LuxMouseButton button) {
        return UnbindAction(action, InputBinding::Mouse(button));
    }
    static bool UnbindAction(std::string_view action, LuxGamepadButton button) {
        return UnbindAction(action, InputBinding::Gamepad(button));
    }
    static bool ClearAction(std::string_view action);
    static void ClearBindings();
    static bool IsActionDown(std::string_view action);
    static bool WasActionPressed(std::string_view action);
    static bool WasActionReleased(std::string_view action);
    // Call after each input update. Application calls this after every fixed update.
    static void AdvanceFrame();

    static bool IsKeyPressed(int32 keycode);
    static bool IsKeyPressed(LuxKey key) {
        return IsKeyPressed(static_cast<int32>(key));
    }
    static bool IsMouseButtonPressed(int32 button);
    static bool IsMouseButtonPressed(LuxMouseButton button) {
        return IsMouseButtonPressed(static_cast<int32>(button));
    }
    static bool IsGamepadButtonPressed(LuxGamepadButton button);
    static std::pair<float, float> GetMousePosition();
    static float GetMouseX();
    static float GetMouseY();

private:
    friend class Window;
    static void ProcessEvent(const SDL_Event& event);
};

} // namespace LuxEngine
