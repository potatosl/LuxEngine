#pragma once
#include <LuxEngine/Core/Core.hpp>
#include <span>
struct SDL_Surface;
namespace LuxEngine::Branding {
LUX_API std::span<const unsigned char> WindowIcon(bool dark);
LUX_API SDL_Surface* CreateWindowIcon(bool dark);
} // namespace LuxEngine::Branding
