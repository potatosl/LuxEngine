#include "Aster/Application.hpp"
#include <SDL3/SDL_main.h>

int main(int, char**) {
    Aster::Application app;
    if (!app.initialize()) return 1;
    return app.run();
}
