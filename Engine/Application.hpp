#pragma once

#include "Engine/Core/Core.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/Core/Timer.hpp"
#include "Engine/Core/Event.hpp"
#include "Engine/Platform/Window.hpp"
#include "Engine/Platform/Input.hpp"
#include "Engine/Renderer/RHI/GpuDevice.hpp"
#include <string>

namespace LuxEngine {

    struct ApplicationSpecification {
        std::string name = "LuxEngine";
        uint32 width = 1280;
        uint32 height = 720;
    };

    class Application {
    public:
        Application(const ApplicationSpecification& spec = ApplicationSpecification{});
        virtual ~Application();

        Application(const Application&) = delete;
        Application& operator=(const Application&) = delete;
        Application(Application&&) = delete;
        Application& operator=(Application&&) = delete;

        bool Initialize();
        int Run();
        void Shutdown();

        void Close() { running_ = false; }
        virtual void OnEvent(Event& e);

        static Application& Get() { return *s_Instance; }
        Window& GetWindow() { return *window_; }
        GpuDevice& GetGpuDevice() { return *gpuDevice_; }

    protected:
        virtual void OnInit() {}
        virtual void OnUpdate(double /*dt*/) {}
        virtual void OnRender(float /*elapsedSeconds*/) {}
        virtual void OnShutdown() {}

    private:
        bool OnWindowClose(WindowCloseEvent& e);
        bool OnWindowResize(WindowResizeEvent& e);

        static Application* s_Instance;

        ApplicationSpecification spec_;
        Scope<Window> window_;
        Scope<GpuDevice> gpuDevice_;
        bool sdlInitialized_ = false;
        bool running_ = false;
        bool initHookEntered_ = false;
        double elapsedSeconds_ = 0.0;
    };

} // namespace LuxEngine
