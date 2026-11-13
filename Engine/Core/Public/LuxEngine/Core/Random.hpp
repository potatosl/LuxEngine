#pragma once

#include <LuxEngine/Core/Core.hpp>
#include <cmath>
#include <stdexcept>

namespace LuxEngine {

// A small deterministic generator for procedural scenes and reproducible tests.
class Random {
public:
    LUX_API explicit Random(uint32 seed);

    LUX_API uint32 Next();

    LUX_API float Unit();

    LUX_API float Range(float minimum, float maximum);

private:
    uint32 state_;
};

} // namespace LuxEngine
