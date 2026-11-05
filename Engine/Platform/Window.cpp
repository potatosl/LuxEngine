#include "Engine/Platform/Window.hpp"
#include "Engine/Core/Log.hpp"
#include <SDL3/SDL.h>

namespace LuxEngine {

    Window::~Window() {
        Shutdown();
    }

    bool Window::Initialize(const WindowProps& props) {
        if (window_) {
            Shutdown();
        }

        props_ = props;

        SDL_WindowFlags flags = 0;
        if (props_.resizable) {
            flags |= SDL_WINDOW_RESIZABLE;
        }

        window_ = SDL_CreateWindow(
            props_.title.c_str(),
            static_cast<int>(props_.width),
            static_cast<int>(props_.height),
            flags
        );

        if (!window_) {
            Log::Error(std::string("Failed to create SDL3 window: ") + SDL_GetError());
            return false;
        }

        isRunning_ = true;
        Log::Info("Window initialized successfully (" + props_.title + ")");
        return true;
    }

    void Window::Shutdown() {
        if (window_) {
            SDL_DestroyWindow(window_);
            window_ = nullptr;
        }
        isRunning_ = false;
    }

    void Window::SetFullscreen(bool fullscreen) {
        if (!window_) return;
        SDL_SetWindowFullscreen(window_, fullscreen);
    }

    bool Window::IsFullscreen() const {
        if (!window_) return false;
        return (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0;
    }

    void Window::SetVSync(bool enabled) {
        props_.vsync = enabled;
    }

    void Window::PollEvents() {
        if (!window_) return;

        const SDL_WindowID currentWindowID = SDL_GetWindowID(window_);
        SDL_Event event;

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
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
                if (event.key.key == SDLK_ESCAPE) {
                    isRunning_ = false;
                }
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
            } else if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED || event.type == SDL_EVENT_WINDOW_RESIZED) {
                if (event.window.windowID == currentWindowID) {
                    props_.width = static_cast<uint32>(event.window.data1);
                    props_.height = static_cast<uint32>(event.window.data2);
                    if (eventCallback_) {
                        WindowResizeEvent e(props_.width, props_.height);
                        eventCallback_(e);
                    }
                }
            }
        }
    }

} // namespace LuxEngine
