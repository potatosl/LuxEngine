#pragma once

#include <LuxEngine/Core/Core.hpp>
#include <cmath>
#include <stdexcept>

namespace LuxEngine {

// A small deterministic generator for procedural scenes and reproducible tests.
class Random {
public:
    explicit Random(uint32 seed) : state_(seed) {}

    uint32 Next() {
        state_ = state_ * 1664525u + 1013904223u;
        return state_;
    }

    float Unit() {
        return static_cast<float>(Next() >> 8) / 16777216.0f;
    }

    float Range(float minimum, float maximum) {
        if (!std::isfinite(minimum) || !std::isfinite(maximum) || maximum < minimum ||
            !std::isfinite(maximum - minimum))
            throw std::invalid_argument("Invalid random range");
        return minimum + (maximum - minimum) * Unit();
    }

private:
    uint32 state_;
};

} // namespace LuxEngine
