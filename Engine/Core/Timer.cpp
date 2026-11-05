#include "Engine/Core/Timer.hpp"
#include <SDL3/SDL_timer.h>

namespace LuxEngine {

    Timer::Timer() {
        Reset();
    }

    void Timer::Reset() {
        startTicksNs_ = SDL_GetTicksNS();
    }

    double Timer::ElapsedSeconds() const {
        const uint64 now = SDL_GetTicksNS();
        return static_cast<double>(now - startTicksNs_) / 1'000'000'000.0;
    }

    double Timer::ElapsedMilliseconds() const {
        const uint64 now = SDL_GetTicksNS();
        return static_cast<double>(now - startTicksNs_) / 1'000'000.0;
    }

} // namespace LuxEngine
