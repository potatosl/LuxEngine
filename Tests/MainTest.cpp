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

    // 2. Smart pointer wrappers (CreateScope, CreateRef)
    {
        auto scopeInt = LuxEngine::CreateScope<int>(42);
        assert(*scopeInt == 42);

        auto refString = LuxEngine::CreateRef<std::string>("LuxEngine");
        assert(*refString == "LuxEngine");
        assert(refString.use_count() == 1);
    }

    // 3. ECS Component verification
    LuxEngine::TransformComponent transform{ 1.0f, 2.0f, 3.0f };
    assert(transform.x == 1.0f && transform.y == 2.0f && transform.z == 3.0f);
    LuxEngine::TagComponent tag{ "Player" };
    assert(tag.tag == "Player");

    // 4. WindowProps specification verification
    LuxEngine::WindowProps windowProps{};
    assert(!windowProps.title.empty());
    assert(windowProps.width > 0 && windowProps.height > 0);
    assert(windowProps.resizable == true);

    // 5. ApplicationSpecification verification
    LuxEngine::ApplicationSpecification appSpec{};
    assert(!appSpec.name.empty());
    assert(appSpec.width == 1280 && appSpec.height == 720);

    // 6. Scene & Entity lifecycle and validity verification
    {
        LuxEngine::Scene scene;
        LuxEngine::Entity nullEntity;
        assert(!nullEntity.IsValid());
        assert(!nullEntity);

        LuxEngine::Entity entityA{ 42, &scene };
        LuxEngine::Entity entityB{ 42, &scene };
        LuxEngine::Entity entityC{ 99, &scene };

        assert(entityA.IsValid());
        assert(static_cast<bool>(entityA));
        assert(entityA.GetId() == 42);
        assert(entityA == entityB);
        assert(entityA != entityC);
        assert(entityA != nullEntity);

        scene.OnUpdate(0.016);
        scene.OnRender();
    }

    // 7. Logging safety verification (edge cases like empty string)
    LuxEngine::Log::Init();
    LuxEngine::Log::Print(LuxEngine::LogLevel::Info, "");
    LuxEngine::Log::Print(LuxEngine::LogLevel::Warn, "Test warning message");
    LuxEngine::Log::Shutdown();

    std::cout << "[LuxEngineTests] All tests passed successfully!" << std::endl;
    return 0;
}
