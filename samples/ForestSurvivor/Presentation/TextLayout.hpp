#pragma once

#include "../../Common/Utf8.hpp"
#include <cmath>
#include <string_view>
#include <vector>

namespace ForestGame {
struct WrappedText {
    std::vector<std::string_view> lines;
    bool complete = true;
};

// Returned views always end at UTF-8 codepoint boundaries, including when a Korean word wraps.
template <typename Measure>
WrappedText WrapText(std::string_view text, float width, int maximumLines, Measure measure);
} // namespace ForestGame

#include <Presentation/TextLayout.inl>
