#include "Engine/Platform/Window.hpp"
#include "Engine/Core/Log.hpp"
#include <SDL3/SDL.h>

namespace LuxEngine {

    Window::~Window() {
        Shutdown();
    }

    bool Window::Initialize(const WindowProps& props) {
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
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                isRunning_ = false;
            }
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
                isRunning_ = false;
            }

            if (eventCallback_) {
                eventCallback_(event);
            }
        }
    }

} // namespace LuxEngine
