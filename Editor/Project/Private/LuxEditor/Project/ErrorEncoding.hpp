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
std::string ErrorMessageUtf8(const std::error_code& error);
} // namespace LuxEditor
