#pragma once
#include <SDL3/SDL.h>

namespace Aster {
class GpuRenderer final {
public:
    GpuRenderer() = default;
    GpuRenderer(const GpuRenderer&) = delete;
    GpuRenderer& operator=(const GpuRenderer&) = delete;
    ~GpuRenderer();

    bool initialize(SDL_Window* window);
    bool render(float elapsedSeconds);
    void shutdown();

private:
    SDL_Window* window_ = nullptr;   // non-owning
    SDL_GPUDevice* device_ = nullptr;
    bool claimed_ = false;
};
}
