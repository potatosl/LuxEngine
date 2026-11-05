#pragma once

#include "Engine/Core/Core.hpp"

namespace LuxEngine {

    class Timer {
    public:
        Timer();
        void Reset();
        double ElapsedSeconds() const;
        double ElapsedMilliseconds() const;

    private:
        uint64 startTicksNs_ = 0;
    };

} // namespace LuxEngine
