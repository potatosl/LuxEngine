#include "SpriteImageCache.hpp"
#include "Engine/Assets/AssetManager.hpp"
#include "Engine/ECS/ECS.hpp"
#include "Engine/Scene/Scene.hpp"
#include "Project/PathEncoding.hpp"
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace LuxEditor {
QImage SpriteImageCache::Get(const LuxEngine::Scene& scene,
                             const LuxEngine::SpriteRendererComponent& sprite) {
    if (sprite.texturePath.empty())
        return {};
    const std::string resolved = scene.ResolveAssetPath(sprite.texturePath);
    if (resolved.empty())
        return {};
    const QString key = QString::fromUtf8(resolved);
    const auto file = PathFromUtf8(resolved);
    std::error_code error;
    auto modified = std::filesystem::last_write_time(file, error);
    if (error)
        modified = {};
    const auto size = std::filesystem::file_size(file, error);
    const auto fileSize = error ? 0 : size;
    Entry* entry = images_.object(key);
    std::unique_ptr<Entry> replacement;
    const auto now = std::chrono::steady_clock::now();
    if (!entry || entry->modified != modified || entry->size != fileSize ||
        (entry->source.isNull() && now >= entry->retryAfter)) {
        images_.remove(key);
        replacement = std::make_unique<Entry>();
        entry = replacement.get();
        entry->modified = modified;
        entry->size = fileSize;
        int width = 0, height = 0;
        std::vector<LuxEngine::uint8> pixels;
        if (LuxEngine::AssetManager::DecodeTextureCpu(resolved, width, height, pixels))
            entry->source = QImage(pixels.data(), width, height, QImage::Format_RGBA8888).copy();
        if (entry->source.isNull())
            entry->retryAfter = now + std::chrono::seconds(1);
    }
    if (!entry->source.isNull() &&
        (!entry->hasTint || entry->tint.x != sprite.color.x || entry->tint.y != sprite.color.y ||
         entry->tint.z != sprite.color.z || entry->tint.w != sprite.color.w)) {
        const auto channel = [](float value) {
            return std::isfinite(value) ? std::max(value, 0.0f) : 0.0f;
        };
        const float tint[] = {channel(sprite.color.x), channel(sprite.color.y),
                              channel(sprite.color.z), channel(sprite.color.w)};
        entry->tinted = entry->source.copy();
        for (int y = 0; y < entry->tinted.height(); ++y) {
            auto* row = entry->tinted.scanLine(y);
            for (int x = 0; x < entry->tinted.width(); ++x)
                for (int component = 0; component < 4; ++component)
                    row[x * 4 + component] = static_cast<uchar>(
                        std::lround(std::clamp(static_cast<double>(row[x * 4 + component]) *
                                                   tint[component], 0.0, 255.0)));
        }
        entry->tint = sprite.color;
        entry->hasTint = true;
    }
    QImage result = entry->tinted;
    if (replacement) {
        const auto bytes = entry->source.sizeInBytes() + entry->tinted.sizeInBytes();
        const int cost = static_cast<int>(std::max<qint64>(1, (bytes + 1023) / 1024));
        images_.insert(key, replacement.release(), cost);
    }
    return result;
}
}
