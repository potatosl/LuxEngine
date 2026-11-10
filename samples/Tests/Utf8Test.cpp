#include "Common/Utf8.hpp"
#include "Engine/EntryPoint.hpp"
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
std::vector<std::uint32_t> Decode(std::string_view text) {
    std::vector<std::uint32_t> result;
    std::size_t offset = 0;
    while (offset < text.size()) {
        const auto before = offset;
        result.push_back(LuxSamples::NextCodepoint(text, offset));
        Check(offset > before && offset <= text.size(), "decoder always advances inside input");
    }
    Check(LuxSamples::NextCodepoint(text, offset) == 0 && offset == text.size(),
          "end of input does not advance");
    return result;
}
} // namespace

int RunUtf8Checks(int, char**) {
    try {
        Check(Decode("가나다") == std::vector<std::uint32_t>{0xAC00, 0xB098, 0xB2E4},
              "Korean syllables decode as codepoints rather than bytes");
        Check(Decode("A가🙂Z") == std::vector<std::uint32_t>{'A', 0xAC00, 0x1F642, 'Z'},
              "ASCII, Korean and supplementary-plane characters coexist");
        const std::string mixed = "A가🙂Z";
        std::size_t offset = 0;
        for (const auto [expected, boundary] : std::array<std::pair<std::uint32_t, std::size_t>, 4>{
                 {{'A', 1}, {0xAC00, 4}, {0x1F642, 8}, {'Z', 9}}}) {
            Check(LuxSamples::NextCodepoint(mixed, offset) == expected && offset == boundary,
                  "byte offsets stay on complete character boundaries");
        }
        Check(Decode("\x7F\xC2\x80\xDF\xBF\xE0\xA0\x80\xED\x9F\xBF\xEE\x80\x80"
                     "\xEF\xBF\xBF\xF0\x90\x80\x80\xF4\x8F\xBF\xBF") ==
                  std::vector<std::uint32_t>{0x7F, 0x80, 0x7FF, 0x800, 0xD7FF, 0xE000, 0xFFFF,
                                             0x10000, 0x10FFFF},
              "UTF-8 scalar range boundaries decode correctly");
        constexpr std::uint32_t replacement = 0xFFFD;
        for (const std::string invalid :
             {"\x80", "\xBF", "\xC0\xAF", "\xC1\xBF", "\xE0\x80\x80", "\xED\xA0\x80",
              "\xF0\x80\x80\x80", "\xF4\x90\x80\x80", "\xF5\x80\x80\x80", "\xFF", "\xC2",
              "\xE2\x82", "\xF0\x9F\x99"}) {
            Check(Decode(invalid) == std::vector<std::uint32_t>(invalid.size(), replacement),
                  "invalid, overlong, surrogate and truncated sequences consume one byte");
        }
        Check(Decode("\xE2"
                     "A가") == std::vector<std::uint32_t>{replacement, 'A', 0xAC00},
              "invalid continuation preserves the following valid text");
        Check(Decode(std::string("A\0B", 3)) == std::vector<std::uint32_t>{'A', 0, 'B'},
              "embedded NUL consumes one byte inside a bounded string view");
        Check(Decode("").empty(), "empty input is safe");
        offset = std::numeric_limits<std::size_t>::max();
        Check(LuxSamples::NextCodepoint("가", offset) == 0 &&
                  offset == std::numeric_limits<std::size_t>::max(),
              "out-of-range offset is safe and unchanged");
        std::cout << "UTF-8 Korean, emoji, invalid-sequence and boundary checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

LUX_ENGINE_MAIN(RunUtf8Checks)
