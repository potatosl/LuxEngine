#pragma once
#include "Engine/Core/Math.hpp"
#include <QCache>
#include <QImage>
#include <chrono>
#include <filesystem>

namespace LuxEngine {
class Scene;
struct SpriteRendererComponent;
}

namespace LuxEditor {
class SpriteImageCache final {
public:
    QImage Get(const LuxEngine::Scene& scene, const LuxEngine::SpriteRendererComponent& sprite);

private:
    struct Entry {
        QImage source;
        QImage tinted;
        LuxEngine::Vec4 tint{};
        std::filesystem::file_time_type modified{};
        std::chrono::steady_clock::time_point retryAfter{};
        std::uintmax_t size = 0;
        bool hasTint = false;
    };
    QCache<QString, Entry> images_{64 * 1024};
};
}
