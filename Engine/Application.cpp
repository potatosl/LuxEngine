#include "Engine/Application.hpp"
#include <SDL3/SDL.h>
#include <cmath>
#include <algorithm>

namespace LuxEngine {

    Application* Application::s_Instance = nullptr;

    Application::Application(const ApplicationSpecification& spec)
        : spec_(spec) {
        s_Instance = this;
    }

    Application::~Application() {
        Shutdown();
        s_Instance = nullptr;
    }

    bool Application::Initialize() {
        Log::Init();

        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
            Log::Error(std::string("SDL_Init failed: ") + SDL_GetError());
            return false;
        }
        sdlInitialized_ = true;

        window_ = CreateScope<Window>();
        WindowProps props;
        props.title = spec_.name;
        props.width = spec_.width;
        props.height = spec_.height;

        if (!window_->Initialize(props)) {
            Log::Error("Failed to initialize window.");
            return false;
        }

        gpuDevice_ = CreateScope<GpuDevice>();
        if (!gpuDevice_->Initialize(window_->GetNativeHandle())) {
            Log::Error("Failed to initialize GPU Device.");
            return false;
        }

        OnInit();
        running_ = true;
        return true;
    }

    int Application::Run() {
        if (!running_) return 1;

        uint64 last = SDL_GetTicksNS();
        constexpr double fixedStep = 1.0 / 60.0;
        double accumulator = 0.0;

        while (running_ && window_->IsRunning()) {
            window_->PollEvents();
            if (!running_ || !window_->IsRunning()) {
                break;
            }

            const uint64 now = SDL_GetTicksNS();
            const double dt = std::min(static_cast<double>(now - last) / 1'000'000'000.0, 0.25);
            last = now;
            elapsedSeconds_ += dt;
            accumulator += dt;

            int steps = 0;
            while (accumulator >= fixedStep && steps < 5) {
                OnUpdate(fixedStep);
                accumulator -= fixedStep;
                ++steps;
            }
            if (steps == 5 && accumulator >= fixedStep) {
                accumulator = 0.0;
            }

            if (gpuDevice_->BeginFrame()) {
                const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(elapsedSeconds_));
                // LuxEngine theme clear color (Deep Indigo / Teal highlight)
                gpuDevice_->Clear(
                    0.05f + 0.05f * pulse,
                    0.08f + 0.08f * pulse,
                    0.15f + 0.15f * pulse,
                    1.0f
                );

                OnRender(static_cast<float>(elapsedSeconds_));
                gpuDevice_->EndFrame();
            }
        }

        return 0;
    }

    void Application::Shutdown() {
        if (!running_ && !sdlInitialized_) return;

        running_ = false;
        OnShutdown();

        if (gpuDevice_) {
            gpuDevice_->Shutdown();
            gpuDevice_.reset();
        }

        if (window_) {
            window_->Shutdown();
            window_.reset();
        }

        if (sdlInitialized_) {
            SDL_Quit();
            sdlInitialized_ = false;
        }

        Log::Shutdown();
    }

} // namespace LuxEngine
