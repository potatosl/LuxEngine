#include "Engine/Application.hpp"
#include <SDL3/SDL.h>
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include "Engine/Assets/AssetManager.hpp"
#include "Engine/Audio/AudioEngine.hpp"
#include "Engine/Renderer/Renderer2D/Renderer2D.hpp"
#include "Engine/Renderer/Renderer3D/Renderer3D.hpp"

namespace LuxEngine {

    Application* Application::s_Instance = nullptr;

    Application::Application(const ApplicationSpecification& spec)
        : spec_(spec) {
        if (s_Instance) {
            throw std::logic_error("Application instance already exists");
        }
        s_Instance = this;
    }

    Application::~Application() {
        Shutdown();
        if (s_Instance == this) {
            s_Instance = nullptr;
        }
    }

    bool Application::Initialize() {
        if (sdlInitialized_) return running_;
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
            Shutdown();
            return false;
        }

        window_->SetEventCallback([this](Event& e) {
            OnEvent(e);
        });

        gpuDevice_ = CreateScope<GpuDevice>();
        if (!gpuDevice_->Initialize(window_->GetNativeHandle())) {
            Log::Error("Failed to initialize GPU Device.");
            Shutdown();
            return false;
        }

        window_->SetPresentCallback([this](bool enabled) { return gpuDevice_ && gpuDevice_->SetVSync(enabled); });
        initHookEntered_ = true;
        running_ = true;
        try { OnInit(); }
        catch (...) { Shutdown(); throw; }
        if (!running_) { Shutdown(); return false; }
        return true;
    }

    void Application::OnEvent(Event& e) {
        EventDispatcher dispatcher(e);
        dispatcher.Dispatch<WindowCloseEvent>([this](WindowCloseEvent& ev) { return OnWindowClose(ev); });
        dispatcher.Dispatch<WindowResizeEvent>([this](WindowResizeEvent& ev) { return OnWindowResize(ev); });
    }

    bool Application::OnWindowClose(WindowCloseEvent& /*e*/) {
        running_ = false;
        return true;
    }

    bool Application::OnWindowResize(WindowResizeEvent& e) {
        spec_.width = e.GetWidth();
        spec_.height = e.GetHeight();
        return false;
    }

    int Application::Run() {
        if (!running_) return 1;

        uint64 last = SDL_GetTicksNS();
        constexpr double fixedStep = 1.0 / 60.0;
        double accumulator = 0.0;

        try {
        while (running_ && window_->IsRunning()) {
            window_->PollEvents();
            if (!running_ || !window_->IsRunning()) {
                break;
            }

            const uint64 now = SDL_GetTicksNS();
            const double dt = std::clamp(static_cast<double>(now - last) / 1'000'000'000.0, 0.0, 0.25);
            last = now;
            elapsedSeconds_ += dt;
            accumulator += dt;

            int steps = 0;
            while (running_ && accumulator >= fixedStep && steps < 5) {
                OnUpdate(fixedStep);
                accumulator -= fixedStep;
                ++steps;
            }
            if (steps == 5 && accumulator >= fixedStep) {
                accumulator = 0.0;
            }

            if (!running_) break;
            if (gpuDevice_->BeginFrame()) {
                const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(elapsedSeconds_));
                gpuDevice_->Clear(
                    0.05f + 0.05f * pulse,
                    0.08f + 0.08f * pulse,
                    0.15f + 0.15f * pulse,
                    1.0f
                );

                OnRender(static_cast<float>(elapsedSeconds_));
                gpuDevice_->EndFrame();
            } else {
                SDL_Delay(1);
            }
        }

        } catch (...) { Shutdown(); throw; }
        Shutdown();
        return 0;
    }

    void Application::Shutdown() {
        if (!running_ && !sdlInitialized_) return;

        running_ = false;
        if (initHookEntered_) {
            initHookEntered_ = false;
            try { OnShutdown(); } catch (...) { Log::Error("OnShutdown failed"); }
        }
        AssetManager::Shutdown();
        AudioEngine::Shutdown();
        Renderer3D::Shutdown();
        Renderer2D::Shutdown();

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
