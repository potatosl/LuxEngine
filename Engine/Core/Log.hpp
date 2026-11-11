#pragma once

#include "Engine/Core/Core.hpp"
#include <string_view>

namespace LuxEngine {

enum class LogLevel { Trace, Info, Warn, Error, Fatal };

class LUX_API Log {
public:
    static void ConfigureUtf8Output() noexcept;
    static void Init();
    static void Shutdown();

    static void Print(LogLevel level, std::string_view message);
    static void Info(std::string_view message);
    static void Warn(std::string_view message);
    static void Error(std::string_view message);
};

} // namespace LuxEngine
