#pragma once

#include <LuxEngine/Core/Core.hpp>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace LuxEngine {

enum class LogLevel { Trace, Info, Warn, Error, Fatal };

struct LogEntry {
    uint64_t sequence = 0;
    LogLevel level = LogLevel::Info;
    std::string message;
};

class LUX_API Log {
public:
    static void ConfigureUtf8Output() noexcept;
    static void Init();
    static void Shutdown();

    static void Print(LogLevel level, std::string_view message);
    static void Info(std::string_view message);
    static void Warn(std::string_view message);
    static void Error(std::string_view message);
    static std::vector<LogEntry> ReadAfter(uint64_t sequence);
};

} // namespace LuxEngine
