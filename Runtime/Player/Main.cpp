#include "Engine/Core/Log.hpp"
#include "Engine/EntryPoint.hpp"
#include "Runtime/ScenePlayer.hpp"
#include <cstdio>
#include <string>
#include <vector>

#ifndef LUX_RUNTIME_ENGINE_VERSION
#define LUX_RUNTIME_ENGINE_VERSION "0.1.0"
#endif

// SDL_main gives UTF-8 arguments on every platform, so Korean scene paths work on Windows.
int RunPlayer(int argc, char* argv[]) {
    std::vector<std::string> args(argv + 1, argv + argc);
    for (const auto& arg : args) {
        if (arg == "--version" || arg == "-v") {
            std::printf("LuxPlayer version %s\n", LUX_RUNTIME_ENGINE_VERSION);
            return 0;
        }
        if (arg == "--help" || arg == "-h") {
            std::printf("usage: LuxPlayer <scene.lux> [--title T] [--size W H] "
                        "[--fullscreen] [--view X Y HALF_HEIGHT] [--frames N]\n");
            return 0;
        }
    }

    std::string error;
    auto options = LuxEngine::ParseScenePlayerArguments(args, error);
    if (!options) {
        std::fprintf(stderr,
                     "LuxPlayer: %s\nusage: LuxPlayer <scene.lux> [--title T] [--size W H] "
                     "[--fullscreen] [--view X Y HALF_HEIGHT] [--frames N]\n",
                     error.c_str());
        return 2;
    }
    LuxEngine::ScenePlayer player(std::move(*options));
    if (!player.Initialize())
        return player.LoadedScene() ? 1 : 3;
    const int result = player.Run();
    player.Shutdown();
    return result;
}

LUX_ENGINE_MAIN(RunPlayer)
