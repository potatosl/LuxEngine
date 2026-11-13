#include <LuxEngine/Core/Log.hpp>
#include <LuxEngine/Platform/Branding.hpp>
#include <LuxEngine/Platform/Input.hpp>
#include <LuxEngine/Platform/Window.hpp>
#include <SDL3/SDL.h>

namespace LuxEngine {

Window::~Window() {
    Shutdown();
}

bool Window::Initialize(const WindowProps& props) {
    if (window_) {
        Log::Error("Window is already initialized");
        return false;
    }

    props_ = props;

    SDL_WindowFlags flags = 0;
    if (props_.resizable) {
        flags |= SDL_WINDOW_RESIZABLE;
    }
    if (props_.vulkan) {
        flags |= SDL_WINDOW_VULKAN;
    }
    if (props_.hidden) {
        flags |= SDL_WINDOW_HIDDEN;
    }

    window_ = SDL_CreateWindow(props_.title.c_str(), static_cast<int>(props_.width),
                               static_cast<int>(props_.height), flags);

    if (!window_) {
        Log::Error(std::string("Failed to create SDL3 window: ") + SDL_GetError());
        return false;
    }

    if (!ApplySystemTheme())
        Log::Warn(std::string("Window icon failed: ") + SDL_GetError());
    isRunning_ = true;
    Log::Info("Window initialized successfully (" + props_.title + ")");
    return true;
}

bool Window::ApplySystemTheme() {
    if (!window_)
        return false;
    const bool dark = SDL_GetSystemTheme() == SDL_SYSTEM_THEME_DARK;
    const auto theme = dark ? ColorTheme::Dark : ColorTheme::Light;
    if (hasWindowIcon_ && theme == colorTheme_)
        return true;
    auto* icon = Branding::CreateWindowIcon(dark);
    const bool applied = icon && SDL_SetWindowIcon(window_, icon);
    SDL_DestroySurface(icon);
    if (applied) {
        colorTheme_ = theme;
        hasWindowIcon_ = true;
    }
    return applied;
}

void Window::Shutdown() {
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    isRunning_ = false;
    presentCallback_ = {};
    hasWindowIcon_ = false;
}

void Window::SetFullscreen(bool fullscreen) {
    if (!window_)
        return;
    SDL_SetWindowFullscreen(window_, fullscreen);
}

bool Window::IsFullscreen() const {
    if (!window_)
        return false;
    return (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0;
}

bool Window::SetVSync(bool enabled) {
    if (!presentCallback_ || !presentCallback_(enabled))
        return false;
    props_.vsync = enabled;
    return true;
}

void Window::PollEvents() {
    if (!window_)
        return;

    const bool dark = SDL_GetSystemTheme() == SDL_SYSTEM_THEME_DARK;
    if (hasWindowIcon_ && (colorTheme_ == ColorTheme::Dark) != dark)
        ApplySystemTheme();
    const SDL_WindowID currentWindowID = SDL_GetWindowID(window_);
    SDL_Event event;
    uint32 processed = 0;

    while (processed < MaxEventsPerPoll && SDL_PollEvent(&event)) {
        ++processed;
        const bool foreignInput =
            ((event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) &&
             event.key.windowID != currentWindowID) ||
            (event.type == SDL_EVENT_MOUSE_MOTION && event.motion.windowID != currentWindowID) ||
            (event.type == SDL_EVENT_MOUSE_WHEEL && event.wheel.windowID != currentWindowID) ||
            ((event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
              event.type == SDL_EVENT_MOUSE_BUTTON_UP) &&
             event.button.windowID != currentWindowID) ||
            (event.type == SDL_EVENT_WINDOW_FOCUS_LOST && event.window.windowID != currentWindowID);
        if (foreignInput)
            continue;
        Input::ProcessEvent(event);
        if (event.type == SDL_EVENT_SYSTEM_THEME_CHANGED) {
            if (!ApplySystemTheme())
                Log::Warn(std::string("Window icon update failed: ") + SDL_GetError());
        } else if (event.type == SDL_EVENT_QUIT) {
            isRunning_ = false;
            if (eventCallback_) {
                WindowCloseEvent e;
                eventCallback_(e);
            }
        } else if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
            if (event.window.windowID == currentWindowID) {
                isRunning_ = false;
                if (eventCallback_) {
                    WindowCloseEvent e;
                    eventCallback_(e);
                }
            }
        } else if (event.type == SDL_EVENT_KEY_DOWN) {
            if (eventCallback_) {
                KeyPressedEvent e(static_cast<int32>(event.key.key), event.key.repeat);
                eventCallback_(e);
            }
        } else if (event.type == SDL_EVENT_KEY_UP) {
            if (eventCallback_) {
                KeyReleasedEvent e(static_cast<int32>(event.key.key));
                eventCallback_(e);
            }
        } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
            if (eventCallback_) {
                MouseMovedEvent e(event.motion.x, event.motion.y);
                eventCallback_(e);
            }
        } else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
            if (eventCallback_) {
                MouseScrolledEvent e(event.wheel.x, event.wheel.y);
                eventCallback_(e);
            }
        } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            if (eventCallback_) {
                MouseButtonPressedEvent e(static_cast<int32>(event.button.button));
                eventCallback_(e);
            }
        } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
            if (eventCallback_) {
                MouseButtonReleasedEvent e(static_cast<int32>(event.button.button));
                eventCallback_(e);
            }
        } else if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
                   event.type == SDL_EVENT_WINDOW_RESIZED ||
                   event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED ||
                   event.type == SDL_EVENT_WINDOW_DISPLAY_CHANGED ||
                   event.type == SDL_EVENT_WINDOW_RESTORED) {
            if (event.window.windowID == currentWindowID) {
                int width = 0, height = 0;
                if (SDL_GetWindowSize(window_, &width, &height)) {
                    props_.width = static_cast<uint32>(width);
                    props_.height = static_cast<uint32>(height);
                }
                if (eventCallback_) {
                    WindowResizeEvent e(props_.width, props_.height);
                    eventCallback_(e);
                }
            }
        }
    }
}

bool Window::GetFramebufferSize(uint32& width, uint32& height) const {
    int pixelWidth = 0, pixelHeight = 0;
    if (!window_ || !SDL_GetWindowSizeInPixels(window_, &pixelWidth, &pixelHeight) ||
        pixelWidth < 0 || pixelHeight < 0)
        return false;
    width = static_cast<uint32>(pixelWidth);
    height = static_cast<uint32>(pixelHeight);
    return true;
}

bool Window::SetSize(uint32 width, uint32 height) {
    if (!window_ || width == 0 || height == 0 || width > 16384 || height > 16384)
        return false;
    if (!SDL_SetWindowSize(window_, static_cast<int>(width), static_cast<int>(height)))
        return false;
    SDL_SyncWindow(window_);
    props_.width = width;
    props_.height = height;
    return true;
}

bool Window::IsMinimized() const {
    return window_ && (SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED) != 0;
}

bool Window::HasFocus() const {
    return window_ && (SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS) != 0;
}

float Window::GetDisplayScale() const {
    const float scale = window_ ? SDL_GetWindowDisplayScale(window_) : 0.0f;
    return scale > 0.0f ? scale : 1.0f;
}

uint32 Window::GetWidth() const {
    int width = 0;
    return window_ && SDL_GetWindowSize(window_, &width, nullptr) ? static_cast<uint32>(width)
                                                                  : props_.width;
}

uint32 Window::GetHeight() const {
    int height = 0;
    return window_ && SDL_GetWindowSize(window_, nullptr, &height) ? static_cast<uint32>(height)
                                                                   : props_.height;
}

} // namespace LuxEngine
