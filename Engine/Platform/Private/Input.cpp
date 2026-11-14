#include <LuxEngine/Platform/Input.hpp>
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace LuxEngine {
namespace {
struct Action {
    std::vector<InputBinding> bindings;
    bool down = false;
    bool pressed = false;
    bool released = false;
};

struct InputState {
    std::unordered_map<std::string, Action> actions;
    std::unordered_set<int32> keys;
    std::unordered_set<int32> externalKeys;
    std::unordered_set<int32> mouseButtons;
    std::unordered_set<int32> externalMouseButtons;
    std::optional<std::pair<float, float>> externalMousePosition;
    std::unordered_map<SDL_JoystickID, std::unordered_set<int32>> gamepadButtons;
    std::unordered_map<SDL_JoystickID, SDL_Gamepad*> gamepads;
};

InputState& State() {
    static InputState state;
    return state;
}

bool Valid(InputBinding binding) {
    switch (binding.device) {
    case InputDevice::Keyboard:
        return SDL_GetScancodeFromKey(static_cast<SDL_Keycode>(binding.code), nullptr) !=
               SDL_SCANCODE_UNKNOWN;
    case InputDevice::MouseButton:
        return binding.code >= 1 && binding.code <= 32;
    case InputDevice::GamepadButton:
        return binding.code >= 0 && binding.code < SDL_GAMEPAD_BUTTON_COUNT;
    }
    return false;
}

bool PhysicalDown(const InputState& state, InputBinding binding) {
    switch (binding.device) {
    case InputDevice::Keyboard:
        return state.keys.contains(binding.code) || state.externalKeys.contains(binding.code);
    case InputDevice::MouseButton:
        return state.mouseButtons.contains(binding.code) ||
               state.externalMouseButtons.contains(binding.code);
    case InputDevice::GamepadButton:
        for (const auto& [id, buttons] : state.gamepadButtons)
            if (buttons.contains(binding.code))
                return true;
        return false;
    }
    return false;
}

void RefreshActions(InputState& state, bool emitEdges) {
    for (auto& [name, action] : state.actions) {
        const bool down =
            std::any_of(action.bindings.begin(), action.bindings.end(),
                        [&](InputBinding binding) { return PhysicalDown(state, binding); });
        if (emitEdges) {
            if (down && !action.down)
                action.pressed = true;
            if (!down && action.down)
                action.released = true;
        }
        action.down = down;
    }
}

bool OpenGamepad(InputState& state, SDL_JoystickID id) {
    if (state.gamepads.contains(id))
        return false;
    if (SDL_Gamepad* gamepad = SDL_OpenGamepad(id)) {
        state.gamepads.emplace(id, gamepad);
        auto& buttons = state.gamepadButtons[id];
        for (int button = 0; button < SDL_GAMEPAD_BUTTON_COUNT; ++button)
            if (SDL_GetGamepadButton(gamepad, static_cast<SDL_GamepadButton>(button)))
                buttons.insert(button);
        return !buttons.empty();
    }
    return false;
}

Action* FindAction(std::string_view name) {
    auto& actions = State().actions;
    auto it = actions.find(std::string(name));
    return it == actions.end() ? nullptr : &it->second;
}
} // namespace

void Input::Initialize() {
    auto& state = State();
    if ((SDL_WasInit(SDL_INIT_GAMEPAD) & SDL_INIT_GAMEPAD) == 0)
        return;
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    for (int index = 0; index < count; ++index)
        OpenGamepad(state, ids[index]);
    SDL_free(ids);
    RefreshActions(state, false);
}

void Input::Shutdown() {
    auto& state = State();
    for (auto& [id, gamepad] : state.gamepads)
        SDL_CloseGamepad(gamepad);
    state = {};
}

bool Input::BindAction(std::string_view name, InputBinding binding) {
    if (name.empty() || !Valid(binding))
        return false;
    auto& state = State();
    auto& action = state.actions[std::string(name)];
    if (std::find(action.bindings.begin(), action.bindings.end(), binding) != action.bindings.end())
        return false;
    action.bindings.push_back(binding);
    RefreshActions(state, false);
    return true;
}

bool Input::UnbindAction(std::string_view name, InputBinding binding) {
    auto& state = State();
    auto it = state.actions.find(std::string(name));
    if (it == state.actions.end())
        return false;
    auto& bindings = it->second.bindings;
    auto found = std::find(bindings.begin(), bindings.end(), binding);
    if (found == bindings.end())
        return false;
    bindings.erase(found);
    if (bindings.empty())
        state.actions.erase(it);
    else
        RefreshActions(state, false);
    return true;
}

bool Input::ClearAction(std::string_view name) {
    return State().actions.erase(std::string(name)) != 0;
}

void Input::ClearBindings() {
    State().actions.clear();
}

bool Input::IsActionDown(std::string_view name) {
    const Action* action = FindAction(name);
    return action && action->down;
}

bool Input::WasActionPressed(std::string_view name) {
    const Action* action = FindAction(name);
    return action && action->pressed;
}

bool Input::WasActionReleased(std::string_view name) {
    const Action* action = FindAction(name);
    return action && action->released;
}

void Input::AdvanceFrame() {
    for (auto& [name, action] : State().actions) {
        action.pressed = false;
        action.released = false;
    }
}

void Input::ProcessEvent(const SDL_Event& event) {
    auto& state = State();
    bool changed = false;
    switch (event.type) {
    case SDL_EVENT_KEY_DOWN:
        changed = state.keys.insert(static_cast<int32>(event.key.key)).second;
        break;
    case SDL_EVENT_KEY_UP:
        changed = state.keys.erase(static_cast<int32>(event.key.key)) != 0;
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        changed = state.mouseButtons.insert(event.button.button).second;
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        changed = state.mouseButtons.erase(event.button.button) != 0;
        break;
    case SDL_EVENT_GAMEPAD_ADDED:
        changed = OpenGamepad(state, event.gdevice.which);
        break;
    case SDL_EVENT_GAMEPAD_REMOVED:
        if (auto it = state.gamepads.find(event.gdevice.which); it != state.gamepads.end()) {
            SDL_CloseGamepad(it->second);
            state.gamepads.erase(it);
        }
        changed = state.gamepadButtons.erase(event.gdevice.which) != 0;
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        changed = state.gamepadButtons[event.gbutton.which].insert(event.gbutton.button).second;
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        if (auto it = state.gamepadButtons.find(event.gbutton.which);
            it != state.gamepadButtons.end())
            changed = it->second.erase(event.gbutton.button) != 0;
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        changed = !state.keys.empty() || !state.mouseButtons.empty();
        state.keys.clear();
        state.mouseButtons.clear();
        break;
    default:
        break;
    }
    if (changed)
        RefreshActions(state, true);
}

bool Input::IsKeyPressed(int32 keycode) {
    if (State().externalKeys.contains(keycode))
        return true;
    int numKeys = 0;
    const bool* state = SDL_GetKeyboardState(&numKeys);
    if (!state)
        return false;
    const SDL_Scancode scancode =
        SDL_GetScancodeFromKey(static_cast<SDL_Keycode>(keycode), nullptr);
    return scancode >= 0 && scancode < numKeys && state[scancode];
}

bool Input::IsMouseButtonPressed(int32 button) {
    if (button < 1 || button > 32)
        return false;
    if (State().externalMouseButtons.contains(button))
        return true;
    float x = 0.0f;
    float y = 0.0f;
    const SDL_MouseButtonFlags flags = SDL_GetMouseState(&x, &y);
    return (flags & SDL_BUTTON_MASK(button)) != 0;
}

bool Input::IsGamepadButtonPressed(LuxGamepadButton button) {
    const int32 code = static_cast<int32>(button);
    if (code < 0 || code >= SDL_GAMEPAD_BUTTON_COUNT)
        return false;
    for (const auto& [id, gamepad] : State().gamepads)
        if (SDL_GamepadConnected(gamepad) &&
            SDL_GetGamepadButton(gamepad, static_cast<SDL_GamepadButton>(code)))
            return true;
    return false;
}

std::pair<float, float> Input::GetMousePosition() {
    if (State().externalMousePosition)
        return *State().externalMousePosition;
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

InputBinding InputBinding::Key(LuxKey key) {
    return {InputDevice::Keyboard, static_cast<int32>(key)};
}

InputBinding InputBinding::Mouse(LuxMouseButton button) {
    return {InputDevice::MouseButton, static_cast<int32>(button)};
}

InputBinding InputBinding::Gamepad(LuxGamepadButton button) {
    return {InputDevice::GamepadButton, static_cast<int32>(button)};
}

bool Input::BindAction(std::string_view action, LuxKey key) {
    return BindAction(action, InputBinding::Key(key));
}

bool Input::BindAction(std::string_view action, LuxMouseButton button) {
    return BindAction(action, InputBinding::Mouse(button));
}

bool Input::BindAction(std::string_view action, LuxGamepadButton button) {
    return BindAction(action, InputBinding::Gamepad(button));
}

bool Input::UnbindAction(std::string_view action, LuxKey key) {
    return UnbindAction(action, InputBinding::Key(key));
}

bool Input::UnbindAction(std::string_view action, LuxMouseButton button) {
    return UnbindAction(action, InputBinding::Mouse(button));
}

bool Input::UnbindAction(std::string_view action, LuxGamepadButton button) {
    return UnbindAction(action, InputBinding::Gamepad(button));
}

bool Input::IsKeyPressed(LuxKey key) {
    return IsKeyPressed(static_cast<int32>(key));
}

bool Input::IsMouseButtonPressed(LuxMouseButton button) {
    return IsMouseButtonPressed(static_cast<int32>(button));
}

bool InputBinding::operator==(const InputBinding&) const = default;

void Input::SetExternalKey(LuxKey key, bool down) {
    if (!Valid(InputBinding::Key(key)))
        return;
    if (down)
        State().externalKeys.insert(static_cast<int32>(key));
    else
        State().externalKeys.erase(static_cast<int32>(key));
    RefreshActions(State(), true);
}

void Input::ClearExternalKeys() {
    State().externalKeys.clear();
    RefreshActions(State(), true);
}

void Input::SetExternalMouseButton(LuxMouseButton button, bool down) {
    const auto binding = InputBinding::Mouse(button);
    if (!Valid(binding))
        return;
    if (down)
        State().externalMouseButtons.insert(binding.code);
    else
        State().externalMouseButtons.erase(binding.code);
    RefreshActions(State(), true);
}

void Input::SetExternalMousePosition(float x, float y) {
    if (std::isfinite(x) && std::isfinite(y))
        State().externalMousePosition = std::pair{x, y};
}

void Input::ClearExternalMouse() {
    State().externalMouseButtons.clear();
    State().externalMousePosition.reset();
    RefreshActions(State(), true);
}
} // namespace LuxEngine
