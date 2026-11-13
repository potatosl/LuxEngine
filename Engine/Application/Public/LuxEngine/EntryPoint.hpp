#pragma once

#include <LuxEngine/Core/Log.hpp>
#include <exception>
#include <functional>
#include <utility>

// Include only in the translation unit declaring LUX_ENGINE_MAIN. SDL supplies the native
// Windows/macOS entry-point bridge here; including it in the umbrella header would emit that
// bridge in every application source file.
#include <SDL3/SDL_main.h>

namespace LuxEngine {
template <class Entry> int RunEngineMain(Entry&& entry, int argc, char** argv);
} // namespace LuxEngine

// Entry is an int(int, char**) callback. It can parse command-line options before creating an
// Application, then Initialize()/Run() it. The callback's exit code is preserved.
#include <LuxEngine/EntryPoint.inl>
