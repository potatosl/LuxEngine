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
template <class Entry> int RunEngineMain(Entry&& entry, int argc, char** argv) {
    Log::ConfigureUtf8Output();
    try {
        return std::invoke(std::forward<Entry>(entry), argc, argv);
    } catch (const std::exception& error) {
        Log::Error(std::string("응용 프로그램 실행 중 오류가 발생했습니다: ") + error.what());
    } catch (...) {
        Log::Error("응용 프로그램 실행 중 알 수 없는 오류가 발생했습니다.");
    }
    return 1;
}
} // namespace LuxEngine

// Entry is an int(int, char**) callback. It can parse command-line options before creating an
// Application, then Initialize()/Run() it. The callback's exit code is preserved.
#define LUX_ENGINE_MAIN(Entry)                                                                     \
    int main(int argc, char** argv) {                                                              \
        return ::LuxEngine::RunEngineMain(Entry, argc, argv);                                      \
    }
