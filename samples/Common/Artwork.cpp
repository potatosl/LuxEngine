#include "Artwork.hpp"
#include "Utf8.hpp"
#define STB_TRUETYPE_IMPLEMENTATION
#include <fstream>
#include <iterator>
#include <memory>
#include <stb_truetype.h>
#include <stdexcept>

namespace LuxSamples {
namespace le = LuxEngine;

namespace {
std::string Utf8(const std::filesystem::path& p) {
    auto bytes = p.u8string();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
} // namespace

Artwork::Artwork(le::GpuDevice& device, std::filesystem::path root)
    : device_(device), root_(std::move(root)) {
    std::ifstream stream(root_ / "DungGeunMo/DungGeunMo.ttf", std::ios::binary);
    fontBytes_ = {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    if (fontBytes_.empty())
        throw std::runtime_error("한글 폰트 파일을 읽지 못했습니다.");
    stbtt_fontinfo info{};
    if (!stbtt_InitFont(&info, fontBytes_.data(), 0))
        throw std::runtime_error("폰트 형식이 올바르지 않습니다.");
    int ascent = 0, descent = 0, lineGap = 0;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);
    const float fontScale = stbtt_ScaleForPixelHeight(&info, 32);
    fontAscent_ = float(ascent) * fontScale;
    lineHeight_ = float(ascent - descent + lineGap) * fontScale;
    white_ = le::CreateRef<le::GpuTexture>();
    const unsigned char pixel[] = {255, 255, 255, 255};
    if (!white_->Initialize(&device_, 1, 1, pixel))
        throw std::runtime_error("샘플 렌더링을 초기화하지 못했습니다.");
}

Artwork::~Artwork() = default;

le::GpuTexture* Artwork::Texture(const std::string& relative) {
    const auto path = Utf8(root_ / relative);
    auto it = textures_.find(path);
    if (it == textures_.end()) {
        int width = 0, height = 0;
        std::vector<le::uint8> pixels;
        if (!le::AssetManager::DecodeTextureCpu(path, width, height, pixels))
            throw std::runtime_error("CC0 이미지 에셋을 읽지 못했습니다: " + relative);
        auto texture = le::CreateRef<le::GpuTexture>();
        if (!texture->Initialize(&device_, static_cast<le::uint32>(width),
                                 static_cast<le::uint32>(height), pixels.data(),
                                 le::TextureFilter::Nearest))
            throw std::runtime_error("CC0 텍스처를 만들지 못했습니다: " + relative);
        it = textures_.emplace(path, std::move(texture)).first;
    }
    return it->second.get();
}

void Artwork::Sprite(const std::string& relative, le::Vec2 position, le::Vec2 size, le::Vec4 tint,
                     float angle) {
    Queue(Texture(relative), position, size, tint, angle);
}

void Artwork::Panel(le::Vec2 position, le::Vec2 size) {
    const float edge = std::min(.14f, std::min(size.x, size.y) * .25f);
    const std::array<float, 4> x{position.x - size.x * .5f, position.x - size.x * .5f + edge,
                                 position.x + size.x * .5f - edge, position.x + size.x * .5f};
    const std::array<float, 4> y{position.y + size.y * .5f, position.y + size.y * .5f - edge,
                                 position.y - size.y * .5f + edge, position.y - size.y * .5f};
    const std::array<float, 4> uv{0, .125f, .875f, 1};
    auto* texture = Texture("PixelAdventureUI/Tiles/Large tiles/Thin outline/tile_0000.png");
    for (size_t row = 0; row < 3; ++row)
        for (size_t col = 0; col < 3; ++col)
            Queue(texture, {(x[col] + x[col + 1]) * .5f, (y[row] + y[row + 1]) * .5f},
                  {x[col + 1] - x[col], y[row] - y[row + 1]}, {1, 1, 1, 1}, 0,
                  {uv[col], uv[row], uv[col + 1], uv[row + 1]});
}

Artwork::Glyph& Artwork::GetGlyph(std::uint32_t character) {
    auto it = glyphs_.find(character);
    if (it != glyphs_.end())
        return it->second;
    stbtt_fontinfo info{};
    stbtt_InitFont(&info, fontBytes_.data(), 0);
    const float scale = stbtt_ScaleForPixelHeight(&info, 32);
    int codepoint = static_cast<int>(character);
    if (!stbtt_FindGlyphIndex(&info, codepoint))
        codepoint = stbtt_FindGlyphIndex(&info, 0xFFFD) ? 0xFFFD : '?';
    int width = 0, height = 0, x = 0, y = 0, advance = 0;
    const auto freeBitmap = [](unsigned char* bitmap) { stbtt_FreeBitmap(bitmap, nullptr); };
    std::unique_ptr<unsigned char, decltype(freeBitmap)> bitmap(
        stbtt_GetCodepointBitmap(&info, scale, scale, codepoint, &width, &height, &x, &y),
        freeBitmap);
    stbtt_GetCodepointHMetrics(&info, codepoint, &advance, nullptr);
    Glyph glyph;
    glyph.width = float(width);
    glyph.height = float(height);
    glyph.offsetX = float(x);
    glyph.offsetY = float(y);
    glyph.advance = float(advance) * scale;
    if (width > 0 && height > 0) {
        if (!bitmap)
            throw std::runtime_error("폰트 비트맵을 만들지 못했습니다.");
        std::vector<unsigned char> rgba(static_cast<size_t>(width * height * 4), 255);
        for (int i = 0; i < width * height; ++i)
            rgba[static_cast<size_t>(i) * 4 + 3] = bitmap.get()[i];
        glyph.texture = le::CreateRef<le::GpuTexture>();
        if (!glyph.texture->Initialize(&device_, static_cast<le::uint32>(width),
                                       static_cast<le::uint32>(height), rgba.data(),
                                       le::TextureFilter::Nearest)) {
            throw std::runtime_error("폰트 텍스처를 만들지 못했습니다.");
        }
    }
    return glyphs_.emplace(character, std::move(glyph)).first->second;
}

void Artwork::Text(std::string_view text, le::Vec2 position, float pixel, le::Vec4 color) {
    const float factor = pixel * 7 / 32;
    const float start = position.x;
    std::size_t offset = 0;
    while (offset < text.size()) {
        const auto c = NextCodepoint(text, offset);
        if (c == '\r')
            continue;
        if (c == '\n') {
            position.x = start;
            position.y -= lineHeight_ * factor;
            continue;
        }
        auto& glyph = GetGlyph(c);
        if (glyph.texture)
            Queue(glyph.texture.get(),
                  {position.x + (glyph.offsetX + glyph.width * .5f) * factor,
                   position.y - (fontAscent_ + glyph.offsetY + glyph.height * .5f) * factor},
                  {glyph.width * factor, glyph.height * factor}, color, 0);
        position.x += glyph.advance * factor;
    }
}
} // namespace LuxSamples

namespace LuxSamples {
void Artwork::Queue(le::GpuTexture* texture, le::Vec2 position, le::Vec2 size, le::Vec4 color,
                    float angle, le::Vec4 region) {
    std::array<le::RenderVertex, 4> corners;
    const std::array<le::Vec2, 4> points{{{-.5f, -.5f}, {.5f, -.5f}, {.5f, .5f}, {-.5f, .5f}}};
    const std::array<le::Vec2, 4> uv{
        {{region.x, region.w}, {region.z, region.w}, {region.z, region.y}, {region.x, region.y}}};
    const float c = std::cos(angle), s = std::sin(angle);
    for (size_t i = 0; i < 4; ++i) {
        const float x = points[i].x * size.x, y = points[i].y * size.y;
        corners[i] = {{position.x + x * c - y * s, position.y + x * s + y * c, 0}, color, uv[i]};
    }
    draws_.push_back(
        {texture, {corners[0], corners[1], corners[2], corners[0], corners[2], corners[3]}});
}

void Artwork::Quad(le::Vec2 position, le::Vec2 size, le::Vec4 color, float angle) {
    Queue(white_.get(), position, size, color, angle);
}

void Artwork::Flush(const le::Mat4& projection) {
    std::vector<le::RenderVertex> batch;
    le::GpuTexture* texture = nullptr;
    const auto flush = [&] {
        if (!batch.empty() && !device_.DrawTriangles(batch, projection, texture, false))
            throw std::runtime_error("샘플 화면을 그리지 못했습니다.");
        batch.clear();
    };
    for (const auto& draw : draws_) {
        if (texture != draw.texture) {
            flush();
            texture = draw.texture;
        }
        batch.insert(batch.end(), draw.vertices.begin(), draw.vertices.end());
    }
    flush();
    draws_.clear();
}
} // namespace LuxSamples
