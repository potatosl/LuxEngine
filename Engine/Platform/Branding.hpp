#pragma once
#include <span>
struct SDL_Surface;
namespace LuxEngine::Branding {
std::span<const unsigned char> WindowIcon(bool dark);
SDL_Surface* CreateWindowIcon(bool dark);
}
