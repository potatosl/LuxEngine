#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace LuxEditor {

inline std::string PathUtf8(const std::filesystem::path& path) {
    const auto bytes = path.u8string();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

inline std::string GenericPathUtf8(const std::filesystem::path& path) {
    const auto bytes = path.generic_u8string();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

inline std::filesystem::path PathFromUtf8(std::string_view text) {
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

} // namespace LuxEditor
