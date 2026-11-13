#pragma once
#include <LuxEngine/LuxEngine.hpp>
#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace LuxSamples {
class Artwork {
public:
    Artwork(LuxEngine::GpuDevice& device, std::filesystem::path root);
    ~Artwork();
    void Sprite(const std::string& relative, LuxEngine::Vec2 position, LuxEngine::Vec2 size,
                LuxEngine::Vec4 tint = {1, 1, 1, 1}, float angle = 0);
    void Panel(LuxEngine::Vec2 position, LuxEngine::Vec2 size);
    void Quad(LuxEngine::Vec2 position, LuxEngine::Vec2 size, LuxEngine::Vec4 tint,
              float angle = 0);
    void Flush(const LuxEngine::Mat4& projection);
    void Text(std::string_view text, LuxEngine::Vec2 position, float pixel, LuxEngine::Vec4 color);

private:
    struct Glyph {
        LuxEngine::Ref<LuxEngine::GpuTexture> texture;
        float width = 0, height = 0, offsetX = 0, offsetY = 0, advance = 0;
    };
    struct Draw {
        LuxEngine::GpuTexture* texture;
        std::array<LuxEngine::RenderVertex, 6> vertices;
    };
    void Queue(LuxEngine::GpuTexture* texture, LuxEngine::Vec2 position, LuxEngine::Vec2 size,
               LuxEngine::Vec4 tint, float angle, LuxEngine::Vec4 uv = {0, 0, 1, 1});
    LuxEngine::Ref<LuxEngine::GpuTexture> white_;
    std::vector<Draw> draws_;
    LuxEngine::GpuTexture* Texture(const std::string& relative);
    Glyph& GetGlyph(std::uint32_t character);
    LuxEngine::GpuDevice& device_;
    std::filesystem::path root_;
    std::map<std::string, LuxEngine::Ref<LuxEngine::GpuTexture>> textures_;
    std::map<std::uint32_t, Glyph> glyphs_;
    std::vector<unsigned char> fontBytes_;
    float fontAscent_ = 0;
    float lineHeight_ = 32;
};
} // namespace LuxSamples
