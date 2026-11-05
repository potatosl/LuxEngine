#include "Engine/LuxEngine.hpp"
#include <iostream>
#include <cassert>

int main() {
    std::cout << "[LuxEngineTests] Running basic engine sanity checks..." << std::endl;

    LuxEngine::Timer timer;
    assert(timer.ElapsedSeconds() >= 0.0);

    LuxEngine::TransformComponent transform{ 1.0f, 2.0f, 3.0f };
    assert(transform.x == 1.0f && transform.y == 2.0f && transform.z == 3.0f);

    std::cout << "[LuxEngineTests] All tests passed successfully!" << std::endl;
    return 0;
}
