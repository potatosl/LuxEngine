#pragma once
#include "Aster/GpuRenderer.hpp"
#include <SDL3/SDL.h>

namespace Aster {
class Application final {
public:
    Application() = default;
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    ~Application();

    bool initialize();
    int run();
    void shutdown();

private:
    void pollEvents();
    void update(double dt);

    SDL_Window* window_ = nullptr;
    GpuRenderer renderer_;
    bool sdlInitialized_ = false;
    bool running_ = false;
    double elapsedSeconds_ = 0.0;
};
}
