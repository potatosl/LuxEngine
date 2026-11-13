#include <LuxEngine/EntryPoint.hpp>
#include <iostream>
#include <stdexcept>

namespace {
int Checks(int argc, char** argv) {
    if (argc < 1 || !argv || !argv[0])
        return 2;
    if (LuxEngine::RunEngineMain(
            [](int count, char** args) { return count == 7 && args == nullptr ? 23 : 24; }, 7,
            nullptr) != 23)
        return 3;
    if (LuxEngine::RunEngineMain([](int, char**) -> int { throw std::runtime_error("expected"); },
                                 0, nullptr) != 1)
        return 4;
    if (LuxEngine::RunEngineMain([](int, char**) -> int { throw 42; }, 0, nullptr) != 1)
        return 5;
    std::cout << "Entry-point callback, argument forwarding and exception checks passed\n";
    return 0;
}
} // namespace
LUX_ENGINE_MAIN(Checks)
