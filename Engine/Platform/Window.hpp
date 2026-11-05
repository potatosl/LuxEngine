#pragma once

#include "Engine/Core/Core.hpp"
#include "Engine/Core/Event.hpp"
#include <functional>
#include <string>

struct SDL_Window;

namespace LuxEngine {

enum class ColorTheme { Light, Dark };

struct WindowProps {
    std::string title = "LuxEngine";
    uint32 width = 1280;
    uint32 height = 720;
    bool resizable = true;
    bool vsync = true;
    bool vulkan = false;
    // Creates the window without showing it, e.g. for offscreen rendering in tools.
    bool hidden = false;
};

class LUX_API Window {
public:
    using EventCallbackFn = std::function<void(Event&)>;

    Window() = default;
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&) noexcept = delete;
    Window& operator=(Window&&) noexcept = delete;

    bool Initialize(const WindowProps& props = WindowProps{});
    void Shutdown();
    void PollEvents();

    ColorTheme GetColorTheme() const {
        return colorTheme_;
    }
    bool HasWindowIcon() const {
        return hasWindowIcon_;
    }
    bool IsRunning() const {
        return isRunning_;
    }
    SDL_Window* GetNativeHandle() const {
        return window_;
    }
    uint32 GetWidth() const;
    uint32 GetHeight() const;
    bool GetFramebufferSize(uint32& width, uint32& height) const;
    bool SetSize(uint32 width, uint32 height);
    bool IsMinimized() const;
    bool HasFocus() const;
    float GetDisplayScale() const;

    void SetFullscreen(bool fullscreen);
    bool IsFullscreen() const;

    bool SetVSync(bool enabled);
    void SetPresentCallback(std::function<bool(bool)> callback) {
        presentCallback_ = std::move(callback);
    }
    bool IsVSync() const {
        return props_.vsync;
    }

    void SetEventCallback(const EventCallbackFn& callback) {
        eventCallback_ = callback;
    }

    static constexpr uint32 MaxEventsPerPoll = 4096;

private:
    bool ApplySystemTheme();
    ColorTheme colorTheme_ = ColorTheme::Light;
    bool hasWindowIcon_ = false;
    std::function<bool(bool)> presentCallback_;
    SDL_Window* window_ = nullptr;
    WindowProps props_;
    bool isRunning_ = false;
    EventCallbackFn eventCallback_;
};

} // namespace LuxEngine
