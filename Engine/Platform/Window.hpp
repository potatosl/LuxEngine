#pragma once

#include "Engine/Core/Core.hpp"
#include <string>
#include <functional>

struct SDL_Window;
union SDL_Event;

namespace LuxEngine {

    struct WindowProps {
        std::string title = "LuxEngine | C++20";
        uint32 width = 1280;
        uint32 height = 720;
        bool resizable = true;
    };

    class Window {
    public:
        using EventCallbackFn = std::function<void(const SDL_Event&)>;

        Window() = default;
        ~Window();

        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;
        Window(Window&&) noexcept = default;
        Window& operator=(Window&&) noexcept = default;

        bool Initialize(const WindowProps& props = WindowProps{});
        void Shutdown();
        void PollEvents();

        bool IsRunning() const { return isRunning_; }
        SDL_Window* GetNativeHandle() const { return window_; }
        uint32 GetWidth() const { return props_.width; }
        uint32 GetHeight() const { return props_.height; }

        void SetEventCallback(const EventCallbackFn& callback) { eventCallback_ = callback; }

    private:
        SDL_Window* window_ = nullptr;
        WindowProps props_;
        bool isRunning_ = false;
        EventCallbackFn eventCallback_;
    };

} // namespace LuxEngine
