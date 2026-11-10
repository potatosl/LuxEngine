#include "Engine/EntryPoint.hpp"
#include <Engine/Core/Math.hpp>
#include <Engine/Scene/Entity.hpp>
#include <Engine/Scene/Scene.hpp>
#include <iostream>

int RunMinimalGame(int, char**) {
    LuxEngine::Scene scene;
    auto entity = scene.CreateEntity("MinimalGameEntity");
    auto& transform = entity.GetComponent<LuxEngine::TransformComponent>();
    transform.position = {1.0f, 2.0f, 3.0f};

    std::cout << "[MinimalGame] Entity created: " << entity.GetId()
              << ", Scene Entity Count: " << scene.GetEntityCount() << std::endl;

    if (scene.GetEntityCount() == 1 && transform.position.x == 1.0f) {
        std::cout << "[MinimalGame] Verification: SUCCESS" << std::endl;
        return 0;
    }
    std::cerr << "[MinimalGame] Verification: FAILED" << std::endl;
    return 1;
}

LUX_ENGINE_MAIN(RunMinimalGame)
