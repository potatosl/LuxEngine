#include "Aster/Application.hpp"
#include <algorithm>

namespace Aster {
Application::~Application() { shutdown(); }

bool Application::initialize() {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "SDL init: %s", SDL_GetError());
        return false;
    }
    sdlInitialized_ = true;
    window_ = SDL_CreateWindow("Aster Engine | C++20", 1280, 720, SDL_WINDOW_RESIZABLE);
    if (!window_) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Create window: %s", SDL_GetError());
        return false;
    }
    if (!renderer_.initialize(window_)) return false;
    running_ = true;
    return true;
}

void Application::pollEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
            running_ = false;
        }
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
            running_ = false;
        }
    }
}

void Application::update(double /*dt*/) {
    // Scene::update, ECS systems and gameplay will be added here.
}

int Application::run() {
    if (!running_) return 1;
    Uint64 last = SDL_GetTicksNS();
    constexpr double fixedStep = 1.0 / 60.0;
    double accumulator = 0.0;
    while (running_) {
        pollEvents();
        if (!running_) break;
        const Uint64 now = SDL_GetTicksNS();
        const double dt = std::min(static_cast<double>(now - last) / 1'000'000'000.0, 0.25);
        last = now;
        elapsedSeconds_ += dt;
        accumulator += dt;
        int steps = 0;
        while (accumulator >= fixedStep && steps < 5) {
            update(fixedStep);
            accumulator -= fixedStep;
            ++steps;
        }
        if (steps == 5 && accumulator >= fixedStep) accumulator = 0.0;
        if (!renderer_.render(static_cast<float>(elapsedSeconds_))) return 1;
    }
    return 0;
}

void Application::shutdown() {
    running_ = false;
    renderer_.shutdown();
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    if (sdlInitialized_) {
        SDL_Quit();
        sdlInitialized_ = false;
    }
}
}
