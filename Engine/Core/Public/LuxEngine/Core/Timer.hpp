#pragma once

#include <LuxEngine/Core/Core.hpp>

namespace LuxEngine {

class LUX_API Timer {
public:
    Timer();
    void Reset();
    double ElapsedSeconds() const;
    double ElapsedMilliseconds() const;

private:
    uint64 startTicksNs_ = 0;
};

} // namespace LuxEngine
