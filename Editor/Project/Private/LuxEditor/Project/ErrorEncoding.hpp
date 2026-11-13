#pragma once

#include <string>
#include <system_error>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace LuxEditor {
inline std::string ErrorMessageUtf8(const std::error_code& error) {
#ifdef _WIN32
    if (error.category() != std::system_category())
        return error.message();
    wchar_t* message = nullptr;
    const auto length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                           FORMAT_MESSAGE_IGNORE_INSERTS,
                                       nullptr, static_cast<DWORD>(error.value()), 0,
                                       reinterpret_cast<wchar_t*>(&message), 0, nullptr);
    if (!length)
        return std::to_string(error.value());
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, message, static_cast<int>(length), nullptr, 0,
                                          nullptr, nullptr);
    std::string utf8(bytes > 0 ? bytes : 0, '\0');
    if (bytes > 0)
        WideCharToMultiByte(CP_UTF8, 0, message, static_cast<int>(length), utf8.data(), bytes,
                            nullptr, nullptr);
    LocalFree(message);
    if (bytes <= 0)
        return std::to_string(error.value());
    while (!utf8.empty() && (utf8.back() == '\r' || utf8.back() == '\n'))
        utf8.pop_back();
    return utf8;
#else
    return error.message();
#endif
}
} // namespace LuxEditor
