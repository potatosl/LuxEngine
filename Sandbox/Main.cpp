#include "Engine/LuxEngine.hpp"
#include "Runtime/Runtime.hpp"
#include <SDL3/SDL_main.h>

class SandboxApp : public LuxEngine::Application {
public:
    SandboxApp()
        : LuxEngine::Application(LuxEngine::ApplicationSpecification{
            .name = "LuxEngine | C++20 (SDL3 GPU)",
            .width = 1280,
            .height = 720
        }) {}

protected:
    void OnInit() override {
        LuxEngine::Runtime::PrintEngineInfo();
        LuxEngine::Log::Info("Sandbox application initialized.");
    }

    void OnUpdate(double dt) override {
        // Game logic, ECS updates, physics steps go here.
        (void)dt;
    }

    void OnRender(float elapsedSeconds) override {
        // Custom 2D/3D render passes go here.
        (void)elapsedSeconds;
    }

    void OnShutdown() override {
        LuxEngine::Log::Info("Sandbox application shutting down.");
    }
};

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    SandboxApp app;
    if (!app.Initialize()) {
        return 1;
    }

    return app.Run();
}
