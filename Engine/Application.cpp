#include "Engine/Application.hpp"
#include "Engine/Assets/AssetManager.hpp"
#include "Engine/Audio/AudioEngine.hpp"
#include "Engine/Renderer/Renderer2D/Renderer2D.hpp"
#include "Engine/Physics/PhysicsEngine.hpp"
#include "Engine/Renderer/Renderer3D/Renderer3D.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace LuxEngine {

namespace {
template <typename Fn> void AttemptShutdown(Fn&& operation, const char* error) noexcept {
    try {
        operation();
    } catch (...) {
        try {
            Log::Error(error);
        } catch (...) {
        }
    }
}
} // namespace

Application* Application::s_Instance = nullptr;

Application::Application(const ApplicationSpecification& spec) : spec_(spec) {
    if (s_Instance) {
        throw std::logic_error("Application instance already exists");
    }
    s_Instance = this;
}

Application& Application::Get() {
    if (!s_Instance)
        throw std::logic_error("Application instance does not exist");
    return *s_Instance;
}

Window& Application::GetWindow() {
    if (!window_ || !window_->GetNativeHandle())
        throw std::logic_error("Window is not initialized");
    return *window_;
}

GpuDevice& Application::GetGpuDevice() {
    if (!gpuDevice_ || !gpuDevice_->IsReady())
        throw std::logic_error("GPU device is not initialized");
    return *gpuDevice_;
}

Application::~Application() {
    Shutdown();
    if (s_Instance == this) {
        s_Instance = nullptr;
    }
}

bool Application::Initialize() {
    if (shuttingDown_)
        return false;
    if (sdlInitialized_)
        return running_;
    Log::Init();

    ownsSdlRoot_ = SDL_WasInit(0) == 0;
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        Log::Error(std::string("SDL_Init failed: ") + SDL_GetError());
        if (ownsSdlRoot_)
            SDL_Quit();
        ownsSdlRoot_ = false;
        Log::Shutdown();
        return false;
    }
    sdlInitialized_ = true;
    Input::Initialize();

    window_ = CreateScope<Window>();
    WindowProps props;
    props.title = spec_.name;
    props.width = spec_.width;
    props.height = spec_.height;
    props.vulkan = true;

    if (!window_->Initialize(props)) {
        Log::Error("Failed to initialize window.");
        Shutdown();
        return false;
    }

    window_->SetEventCallback([this](Event& e) { OnEvent(e); });

    gpuDevice_ = CreateScope<GpuDevice>();
    if (!gpuDevice_->Initialize(*window_)) {
        Log::Error("Failed to initialize GPU Device.");
        Shutdown();
        return false;
    }

    window_->SetPresentCallback(
        [this](bool enabled) { return gpuDevice_ && gpuDevice_->SetVSync(enabled); });
    initHookEntered_ = true;
    running_ = true;
    try {
        OnInit();
    } catch (...) {
        Shutdown();
        throw;
    }
    if (!running_) {
        Shutdown();
        return false;
    }
    lastTicksNs_ = SDL_GetTicksNS();
    return true;
}

void Application::OnEvent(Event& e) {
    EventDispatcher dispatcher(e);
    dispatcher.Dispatch<WindowCloseEvent>(
        [this](WindowCloseEvent& ev) { return OnWindowClose(ev); });
    dispatcher.Dispatch<WindowResizeEvent>(
        [this](WindowResizeEvent& ev) { return OnWindowResize(ev); });
}

bool Application::OnWindowClose(WindowCloseEvent& /*e*/) {
    running_ = false;
    return true;
}

bool Application::OnWindowResize(WindowResizeEvent& e) {
    spec_.width = e.GetWidth();
    spec_.height = e.GetHeight();
    if (gpuDevice_)
        gpuDevice_->NotifyResize();
    return false;
}

int Application::Run() {
    if (!sdlInitialized_)
        return 1;
    while (true) {
        const FrameResult result = RunFrame();
        if (result == FrameResult::Continue)
            continue;
        if (result == FrameResult::Skipped) {
            SDL_Delay(1);
            continue;
        }
        return result == FrameResult::Stopped ? 0 : 1;
    }
}

Application::FrameResult Application::RunFrame() {
    if (!sdlInitialized_)
        return FrameResult::Error;
    constexpr double fixedStep = 1.0 / 60.0;

    try {
        if (!running_ || !window_->IsRunning()) {
            Shutdown();
            return FrameResult::Stopped;
        }

        window_->PollEvents();
        if (!running_ || !window_->IsRunning()) {
            Shutdown();
            return FrameResult::Stopped;
        }

        const uint64 now = SDL_GetTicksNS();
        const double dt =
            std::clamp(static_cast<double>(now - lastTicksNs_) / 1'000'000'000.0, 0.0, 0.25);
        lastTicksNs_ = now;
        elapsedSeconds_ += dt;
        accumulator_ += dt;

        int steps = 0;
        while (running_ && accumulator_ >= fixedStep && steps < 5) {
            OnUpdate(fixedStep);
            Input::AdvanceFrame();
            accumulator_ -= fixedStep;
            ++steps;
        }
        if (!running_) {
            Shutdown();
            return FrameResult::Stopped;
        }
        if (steps == 5 && accumulator_ >= fixedStep)
            accumulator_ = 0.0;

        const auto frameStart = gpuDevice_->BeginFrame();
        if (frameStart == GpuDevice::FrameStart::Error) {
            Log::Error(std::string("GPU frame failed (") + ToString(gpuDevice_->GetStatus()) +
                       "): " + gpuDevice_->GetLastError());
            Shutdown();
            return FrameResult::Error;
        }
        if (frameStart == GpuDevice::FrameStart::Skipped) {
            return FrameResult::Skipped;
        }

        const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(elapsedSeconds_));
        const bool cleared = gpuDevice_->Clear(0.05f + 0.05f * pulse, 0.08f + 0.08f * pulse,
                                               0.15f + 0.15f * pulse, 1.0f);
        if (!cleared) {
            gpuDevice_->EndFrame();
            Shutdown();
            return FrameResult::Error;
        }

        OnRender(static_cast<float>(elapsedSeconds_));
        if (!sdlInitialized_)
            return FrameResult::Stopped;
        if (!gpuDevice_->EndFrame() && !gpuDevice_->IsReady()) {
            Log::Error(std::string("GPU frame submission failed (") +
                       ToString(gpuDevice_->GetStatus()) + "): " + gpuDevice_->GetLastError());
            Shutdown();
            return FrameResult::Error;
        }
        if (!running_) {
            Shutdown();
            return FrameResult::Stopped;
        }
        return FrameResult::Continue;
    } catch (...) {
        Shutdown();
        throw;
    }
}

void Application::Shutdown() noexcept {
    if (shuttingDown_)
        return;
    if (!running_ && !sdlInitialized_ && !window_ && !gpuDevice_)
        return;
    shuttingDown_ = true;

    running_ = false;
    lastTicksNs_ = 0;
    accumulator_ = 0.0;
    elapsedSeconds_ = 0.0;
    if (initHookEntered_) {
        initHookEntered_ = false;
        AttemptShutdown([this] { OnShutdown(); }, "OnShutdown failed");
    }
    AttemptShutdown([] { AssetManager::Shutdown(); }, "AssetManager shutdown failed");
    AttemptShutdown([] { AudioEngine::Shutdown(); }, "AudioEngine shutdown failed");
    AttemptShutdown([] { PhysicsEngine::Shutdown(); }, "PhysicsEngine shutdown failed");
    AttemptShutdown([] { Renderer3D::Shutdown(); }, "Renderer3D shutdown failed");
    AttemptShutdown([] { Renderer2D::Shutdown(); }, "Renderer2D shutdown failed");

    if (gpuDevice_) {
        AttemptShutdown([this] { gpuDevice_->Shutdown(); }, "GpuDevice shutdown failed");
        AttemptShutdown([this] { gpuDevice_.reset(); }, "GpuDevice release failed");
    }

    if (window_) {
        AttemptShutdown([this] { window_->Shutdown(); }, "Window shutdown failed");
        AttemptShutdown([this] { window_.reset(); }, "Window release failed");
    }

    if (sdlInitialized_) {
        Input::Shutdown();
        SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD);
        if (ownsSdlRoot_)
            SDL_Quit();
        sdlInitialized_ = false;
        ownsSdlRoot_ = false;
    }

    AttemptShutdown([] { Log::Shutdown(); }, "Log shutdown failed");
    shuttingDown_ = false;
}

} // namespace LuxEngine
