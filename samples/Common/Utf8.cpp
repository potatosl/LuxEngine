#include <Common/Utf8.hpp>

namespace LuxSamples {
std::uint32_t NextCodepoint(std::string_view text, std::size_t& offset) {
    if (offset >= text.size())
        return 0;
    const std::size_t start = offset++;
    const auto first = static_cast<unsigned char>(text[start]);
    if (first < 0x80)
        return first;
    const std::size_t length = first >= 0xC2 && first <= 0xDF   ? 2
                               : first >= 0xE0 && first <= 0xEF ? 3
                               : first >= 0xF0 && first <= 0xF4 ? 4
                                                                : 0;
    constexpr std::uint32_t replacement = 0xFFFD;
    if (length == 0 || text.size() - start < length)
        return replacement;
    std::uint32_t codepoint = first & ((1u << (7 - length)) - 1);
    for (std::size_t index = 1; index < length; ++index) {
        const auto next = static_cast<unsigned char>(text[start + index]);
        if ((next & 0xC0) != 0x80)
            return replacement;
        codepoint = (codepoint << 6) | (next & 0x3F);
    }
    const std::uint32_t minimum = length == 2 ? 0x80 : length == 3 ? 0x800 : 0x10000;
    if (codepoint < minimum || codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF))
        return replacement;
    offset = start + length;
    return codepoint;
}
} // namespace LuxSamples
