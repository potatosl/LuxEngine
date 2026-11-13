#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace LuxSamples {
// Invalid input consumes exactly one byte, so the caller can resume at the next boundary.
// At end of input, return zero without advancing the offset.
std::uint32_t NextCodepoint(std::string_view text, std::size_t& offset);
} // namespace LuxSamples
