#include <LuxEngine/Core/Random.hpp>

namespace LuxEngine {
Random::Random(uint32 seed) : state_(seed) {}

uint32 Random::Next() {
    state_ = state_ * 1664525u + 1013904223u;
    return state_;
}

float Random::Unit() {
    return static_cast<float>(Next() >> 8) / 16777216.0f;
}

float Random::Range(float minimum, float maximum) {
    if (!std::isfinite(minimum) || !std::isfinite(maximum) || maximum < minimum ||
        !std::isfinite(maximum - minimum))
        throw std::invalid_argument("Invalid random range");
    return minimum + (maximum - minimum) * Unit();
}
} // namespace LuxEngine
