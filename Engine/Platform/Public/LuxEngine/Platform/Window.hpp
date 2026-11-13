#pragma once

#include <LuxEngine/Core/Core.hpp>
#include <LuxEngine/Core/Event.hpp>
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
    // Legacy name: request the platform rendering surface (Vulkan on Linux, Metal on macOS).
    // Windows uses the native window directly for The Forge Direct3D 12.
    bool vulkan = false;
    // Creates the window without showing it, e.g. for offscreen rendering in tools.
    bool hidden = false;
};

class LUX_API Window {
public:
    using EventCallbackFn = std::function<void(Event&)>;

    Window();
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&) noexcept = delete;
    Window& operator=(Window&&) noexcept = delete;

    bool Initialize(const WindowProps& props = WindowProps{});
    void Shutdown();
    void PollEvents();

    ColorTheme GetColorTheme() const;

    bool HasWindowIcon() const;

    bool IsRunning() const;

    SDL_Window* GetNativeHandle() const;

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

    void SetPresentCallback(std::function<bool(bool)> callback);

    bool IsVSync() const;

    void SetEventCallback(const EventCallbackFn& callback);

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
