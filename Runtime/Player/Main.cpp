#include "Engine/Core/Log.hpp"
#include "Runtime/ScenePlayer.hpp"
#include <SDL3/SDL_main.h>
#include <cstdio>
#include <string>
#include <vector>

// SDL_main gives UTF-8 arguments on every platform, so Korean scene paths work on Windows.
int main(int argc, char* argv[]) {
    std::vector<std::string> args(argv + 1, argv + argc);
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
