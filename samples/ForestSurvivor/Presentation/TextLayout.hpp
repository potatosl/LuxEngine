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
WrappedText WrapText(std::string_view text, float width, int maximumLines, Measure measure) {
    WrappedText result;
    if (!std::isfinite(width) || width <= 0 || maximumLines <= 0) {
        result.complete = text.empty();
        return result;
    }
    while (!text.empty() && static_cast<int>(result.lines.size()) < maximumLines) {
        while (!text.empty() && text.front() == ' ')
            text.remove_prefix(1);
        if (text.empty())
            break;
        if (text.front() == '\n') {
            result.lines.emplace_back();
            text.remove_prefix(1);
            continue;
        }
        const auto newline = text.find('\n');
        const size_t end = newline == std::string_view::npos ? text.size() : newline;
        size_t next = 0, count = 0;
        while (next < end) {
            LuxSamples::NextCodepoint(text, next);
            if (measure(text.substr(0, next)) > width)
                break;
            count = next;
        }
        if (count == 0) {
            result.complete = false;
            return result;
        }
        if (count < end) {
            const auto space = text.rfind(' ', count);
            if (space != std::string_view::npos && space > 0)
                count = space;
        }
        result.lines.push_back(text.substr(0, count));
        text.remove_prefix(count);
        if (count == end && !text.empty() && text.front() == '\n')
            text.remove_prefix(1);
    }
    while (!text.empty() && text.front() == ' ')
        text.remove_prefix(1);
    result.complete = text.empty();
    return result;
}
} // namespace ForestGame
