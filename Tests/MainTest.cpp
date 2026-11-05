#include "Engine/LuxEngine.hpp"
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>

int main() {
    std::cout << "[LuxEngineTests] Running comprehensive engine sanity checks..." << std::endl;

    // 1. Timer verification
    LuxEngine::Timer timer;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(timer.ElapsedSeconds() > 0.0);
    assert(timer.ElapsedMilliseconds() >= 5.0);
    timer.Reset();
    assert(timer.ElapsedSeconds() < 1.0);

    // 2. ECS Component verification
    LuxEngine::TransformComponent transform{ 1.0f, 2.0f, 3.0f };
    assert(transform.x == 1.0f && transform.y == 2.0f && transform.z == 3.0f);
    LuxEngine::TagComponent tag{ "Player" };
    assert(tag.tag == "Player");

    // 3. WindowProps specification verification
    LuxEngine::WindowProps windowProps{};
    assert(!windowProps.title.empty());
    assert(windowProps.width > 0 && windowProps.height > 0);
    assert(windowProps.resizable == true);

    // 4. ApplicationSpecification verification
    LuxEngine::ApplicationSpecification appSpec{};
    assert(!appSpec.name.empty());
    assert(appSpec.width == 1280 && appSpec.height == 720);

    // 5. Scene & Entity lifecycle verification
    {
        LuxEngine::Scene scene;
        LuxEngine::Entity entity{ 42, &scene };
        assert(entity.GetId() == 42);
        scene.OnUpdate(0.016);
        scene.OnRender();
    }

    // 6. Logging safety verification (edge cases like empty string)
    LuxEngine::Log::Init();
    LuxEngine::Log::Print(LuxEngine::LogLevel::Info, "");
    LuxEngine::Log::Print(LuxEngine::LogLevel::Warn, "Test warning message");
    LuxEngine::Log::Shutdown();

    std::cout << "[LuxEngineTests] All tests passed successfully!" << std::endl;
    return 0;
}
