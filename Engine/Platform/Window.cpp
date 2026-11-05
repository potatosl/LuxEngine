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

    void Window::PollEvents() {
        if (!window_) return;

        const SDL_WindowID currentWindowID = SDL_GetWindowID(window_);
        SDL_Event event;

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                isRunning_ = false;
            } else if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                if (event.window.windowID == currentWindowID) {
                    isRunning_ = false;
                }
            } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
                isRunning_ = false;
            } else if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED || event.type == SDL_EVENT_WINDOW_RESIZED) {
                if (event.window.windowID == currentWindowID) {
                    props_.width = static_cast<uint32>(event.window.data1);
                    props_.height = static_cast<uint32>(event.window.data2);
                }
            }

            if (eventCallback_) {
                eventCallback_(event);
            }
        }
    }

} // namespace LuxEngine
