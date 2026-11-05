#include "Engine/Core/Log.hpp"
#include <SDL3/SDL_log.h>

namespace LuxEngine {

    void Log::Init() {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[LuxEngine::Log] Initialized logger.");
    }

    void Log::Shutdown() {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[LuxEngine::Log] Shutdown logger.");
    }

    void Log::Print(LogLevel level, std::string_view message) {
        const char* msgStr = message.empty() ? "" : message.data();
        const int msgLen = static_cast<int>(message.length());

        switch (level) {
            case LogLevel::Trace:
            case LogLevel::Info:
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[LuxEngine] %.*s", msgLen, msgStr);
                break;
            case LogLevel::Warn:
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[LuxEngine WARN] %.*s", msgLen, msgStr);
                break;
            case LogLevel::Error:
            case LogLevel::Fatal:
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[LuxEngine ERROR] %.*s", msgLen, msgStr);
                break;
        }
    }

    void Log::Info(std::string_view message) {
        Print(LogLevel::Info, message);
    }

    void Log::Warn(std::string_view message) {
        Print(LogLevel::Warn, message);
    }

    void Log::Error(std::string_view message) {
        Print(LogLevel::Error, message);
    }

} // namespace LuxEngine
