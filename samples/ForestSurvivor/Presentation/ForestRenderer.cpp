#include "Presentation/ForestRenderer.hpp"
#include "Engine/Assets/AssetManager.hpp"
#include "Engine/Renderer/Renderer2D/Renderer2D.hpp"
#include "Game/SurvivalGame.hpp"
#include "Presentation/KoreanUi.hpp"
#include "Presentation/TextLayout.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>
#include <vector>

namespace ForestGame {
namespace {
namespace le = LuxEngine;
using Color = le::Vec4;
struct Region {
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
    int width = 0, height = 0;
};
struct Rect {
    float x = 0, y = 0, width = 0, height = 0;
    bool Contains(float px, float py) const {
        return px >= x && py >= y && px < x + width && py < y + height;
    }
};
class SpriteBatch {
public:
    void Begin(le::GpuDevice& device, const le::Mat4& matrix, bool yDown) {
        Flush();
        device_ = &device;
        matrix_ = matrix;
        yDown_ = yDown;
    }
    void Sprite(le::GpuTexture* texture, Region uv, le::Vec2 center, le::Vec2 size,
                Color tint = {1, 1, 1, 1}, float rotation = 0) {
        if (texture != texture_ || vertices_.size() >= 6000) {
            Flush();
            texture_ = texture;
        }
        const float c = std::cos(rotation), s = std::sin(rotation);
        const std::array<le::Vec2, 4> corners{{{-.5f, -.5f}, {.5f, -.5f}, {.5f, .5f}, {-.5f, .5f}}};
        const float lower = yDown_ ? uv.v0 : uv.v1;
        const float upper = yDown_ ? uv.v1 : uv.v0;
        const std::array<le::Vec2, 4> texcoords{
            {{uv.u0, lower}, {uv.u1, lower}, {uv.u1, upper}, {uv.u0, upper}}};
        for (int i : {0, 1, 2, 2, 3, 0}) {
            const float x = corners[i].x * size.x, y = corners[i].y * size.y;
            vertices_.push_back(
                {{center.x + x * c - y * s, center.y + x * s + y * c, 0}, tint, texcoords[i]});
        }
    }
    void Solid(Rect rect, Color color) {
        Sprite(nullptr, {}, {rect.x + rect.width * .5f, rect.y + rect.height * .5f},
               {rect.width, rect.height}, color);
    }
    void Flush() {
        if (!vertices_.empty() && device_) {
            if (!device_->DrawTriangles(vertices_, matrix_, texture_, false))
                throw std::runtime_error("샘플의 스프라이트를 렌더링할 수 없습니다.");
        }
        vertices_.clear();
        texture_ = nullptr;
    }

private:
    le::GpuDevice* device_ = nullptr;
    le::GpuTexture* texture_ = nullptr;
    le::Mat4 matrix_;
    bool yDown_ = false;
    std::vector<le::RenderVertex> vertices_;
};
std::string Utf8(const std::filesystem::path& path) {
    const auto bytes = path.u8string();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
struct Sheet {
    le::GpuTexture texture;
    void Load(le::GpuDevice& device, const std::filesystem::path& path) {
        int width = 0, height = 0;
        std::vector<le::uint8> pixels;
        if (!le::AssetManager::DecodeTextureCpu(Utf8(path), width, height, pixels))
            throw std::runtime_error("CC0 이미지 에셋을 읽을 수 없습니다: " + Utf8(path));
        if (!texture.Initialize(&device, static_cast<le::uint32>(width),
                                static_cast<le::uint32>(height), pixels.data(),
                                le::TextureFilter::Nearest))
            throw std::runtime_error("CC0 이미지 GPU 리소스를 만들 수 없습니다: " + Utf8(path));
    }
    Region Tile(int index, int columns, int pixels, int wide = 1, int high = 1) const {
        if (index < 0 || columns <= 0 || pixels <= 0 || wide <= 0 || high <= 0)
            throw std::out_of_range("CC0 타일 선택 값이 올바르지 않습니다.");
        const float w = static_cast<float>(texture.GetWidth()),
                    h = static_cast<float>(texture.GetHeight());
        const int x = (index % columns) * pixels, y = (index / columns) * pixels;
        if (x + pixels * wide > w || y + pixels * high > h)
            throw std::out_of_range("CC0 타일 선택 범위가 원본 이미지 크기를 넘었습니다.");
        return {(x + .5f) / w,
                (y + .5f) / h,
                (x + pixels * wide - .5f) / w,
                (y + pixels * high - .5f) / h,
                pixels * wide,
                pixels * high};
    }
};
class Font {
public:
    void Load(le::GpuDevice& device, const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), {});
        if (!file || bytes.empty() || bytes.size() > 16 * 1024 * 1024)
            throw std::runtime_error("공개 도메인 폰트 에셋을 읽을 수 없습니다.");
        stbtt_fontinfo info{};
        const int offset = stbtt_GetFontOffsetForIndex(bytes.data(), 0);
        if (offset < 0 || !stbtt_InitFont(&info, bytes.data(), offset))
            throw std::runtime_error("폰트 형식이 올바르지 않습니다.");
        std::vector<int> codepoints;
        for (int codepoint = 32; codepoint <= 126; ++codepoint)
            codepoints.push_back(codepoint);
        const auto addText = [&](std::string_view text) {
            size_t position = 0;
            while (position < text.size()) {
                const auto codepoint = LuxSamples::NextCodepoint(text, position);
                if (codepoint == 0xFFFD)
                    throw std::runtime_error("게임 UI에 올바르지 않은 UTF-8 문자열이 있습니다.");
                if (codepoint >= 32)
                    codepoints.push_back(static_cast<int>(codepoint));
            }
        };
        addText("가힣"); // Verify both ends of the precomposed Hangul syllable block.
        for (const auto label : UiText::All)
            addText(label);
        for (const auto& upgrade : SurvivalGame::UpgradeCatalog()) {
            addText(upgrade.title);
            addText(upgrade.description);
        }
        std::sort(codepoints.begin(), codepoints.end());
        codepoints.erase(std::unique(codepoints.begin(), codepoints.end()), codepoints.end());
        for (const int codepoint : codepoints)
            if (stbtt_FindGlyphIndex(&info, codepoint) == 0)
                throw std::runtime_error("한국어 UI에 필요한 글리프가 폰트에 없습니다: " +
                                         std::to_string(codepoint));
        int ascent, descent, gap;
        stbtt_GetFontVMetrics(&info, &ascent, &descent, &gap);
        ascent_ = static_cast<float>(ascent) * stbtt_ScaleForPixelHeight(&info, BaseSize);
        std::vector<unsigned char> alpha(AtlasSize * AtlasSize);
        std::vector<stbtt_packedchar> packed(codepoints.size());
        stbtt_pack_context context{};
        if (!stbtt_PackBegin(&context, alpha.data(), AtlasSize, AtlasSize, 0, 2, nullptr))
            throw std::runtime_error("한국어 폰트 아틀라스를 만들 수 없습니다.");
        stbtt_pack_range range{};
        range.font_size = BaseSize;
        range.array_of_unicode_codepoints = codepoints.data();
        range.num_chars = static_cast<int>(codepoints.size());
        range.chardata_for_range = packed.data();
        const bool complete = stbtt_PackFontRanges(&context, bytes.data(), 0, &range, 1) != 0;
        stbtt_PackEnd(&context);
        if (!complete)
            throw std::runtime_error("한국어 폰트 글리프를 아틀라스에 배치할 수 없습니다.");
        for (size_t index = 0; index < codepoints.size(); ++index)
            glyphs_.emplace(static_cast<std::uint32_t>(codepoints[index]), packed[index]);
        std::vector<le::uint8> rgba(alpha.size() * 4, 255);
        for (size_t index = 0; index < alpha.size(); ++index)
            rgba[index * 4 + 3] = alpha[index];
        if (!texture_.Initialize(&device, AtlasSize, AtlasSize, rgba.data(),
                                 le::TextureFilter::Nearest))
            throw std::runtime_error("한국어 폰트 GPU 리소스를 만들 수 없습니다.");
    }
    float Width(std::string_view text, float height) const {
        float width = 0;
        size_t position = 0;
        while (position < text.size())
            width += Glyph(LuxSamples::NextCodepoint(text, position)).xadvance;
        return width * height / BaseSize;
    }
    void Draw(SpriteBatch& batch, std::string_view text, float x, float y, float height,
              Color color) {
        const float scale = height / BaseSize;
        const float baseline = y + ascent_ * scale;
        size_t position = 0;
        while (position < text.size()) {
            const auto& glyph = Glyph(LuxSamples::NextCodepoint(text, position));
            const float w = static_cast<float>(glyph.x1 - glyph.x0),
                        h = static_cast<float>(glyph.y1 - glyph.y0);
            Region uv{glyph.x0 / static_cast<float>(AtlasSize),
                      glyph.y0 / static_cast<float>(AtlasSize),
                      glyph.x1 / static_cast<float>(AtlasSize),
                      glyph.y1 / static_cast<float>(AtlasSize),
                      static_cast<int>(w),
                      static_cast<int>(h)};
            if (w > 0 && h > 0)
                batch.Sprite(
                    &texture_, uv,
                    {x + (glyph.xoff + w * .5f) * scale, baseline + (glyph.yoff + h * .5f) * scale},
                    {w * scale, h * scale}, color);
            x += glyph.xadvance * scale;
        }
    }

private:
    const stbtt_packedchar& Glyph(std::uint32_t codepoint) const {
        if (codepoint == 0xFFFD)
            codepoint = '?';
        const auto found = glyphs_.find(codepoint);
        if (found == glyphs_.end())
            throw std::runtime_error("게임 UI 글리프 캐시에 없는 문자입니다: " +
                                     std::to_string(codepoint));
        return found->second;
    }
    static constexpr float BaseSize = 32;
    static constexpr int AtlasSize = 1024;
    le::GpuTexture texture_;
    // All actual UI codepoints are packed at load time, so const measurements never mutate the
    // atlas.
    std::map<std::uint32_t, stbtt_packedchar> glyphs_;
    float ascent_ = 0;
};
constexpr Color White{1, 1, 1, 1};
constexpr Color Ink{.16f, .20f, .24f, 1};
constexpr Color Muted{.35f, .40f, .42f, 1};
constexpr Color Light{.99f, .96f, .87f, 1};
constexpr Color Accent{.21f, .49f, .58f, 1};
constexpr float Pi = 3.14159265359f;
std::uint32_t Hash(int x, int y) {
    std::uint32_t value = static_cast<std::uint32_t>(x) * 374761393u +
                          static_cast<std::uint32_t>(y) * 668265263u + 67891;
    value = (value ^ (value >> 13)) * 1274126177u;
    return value ^ (value >> 16);
}
std::string Clock(float value) {
    const int seconds = std::max(0, static_cast<int>(value));
    std::ostringstream out;
    out << std::setfill('0') << std::setw(2) << seconds / 60 << ':' << std::setw(2) << seconds % 60;
    return out.str();
}
} // namespace

struct ForestRenderer::State {
    le::GpuDevice* device = nullptr;
    SpriteBatch batch;
    Sheet town, dungeon, ui;
    Font font;
    std::vector<std::pair<Rect, UiAction>> buttons;
    float uiWidth = 1280;
    float pointerX = -1, pointerY = -1;
    bool pressed = false;
    bool keyboardChoice = false;
    int selectedChoice = 0;
    Phase phase = Phase::Title;
    bool textLayoutFits = true;

    void Sprite(Sheet& sheet, int index, int columns, int size, le::Vec2 center, le::Vec2 scale,
                Color tint = White, float angle = 0) {
        batch.Sprite(&sheet.texture, sheet.Tile(index, columns, size), center, scale, tint, angle);
    }
    void Town(int index, le::Vec2 center, float size = 1, Color tint = White) {
        Sprite(town, index, 12, 16, center, {size, size}, tint);
    }
    void Dungeon(int index, le::Vec2 center, float size = 1, Color tint = White, float angle = 0) {
        Sprite(dungeon, index, 12, 16, center, {size, size}, tint, angle);
    }
    void Text(std::string_view text, float x, float y, float size = 22, Color color = Ink) {
        textLayoutFits = textLayoutFits && x >= 0 && y >= 0 &&
                         x + font.Width(text, size) <= uiWidth + .5f && y + size <= 720.5f;
        font.Draw(batch, text, x, y, size, color);
    }
    void Center(std::string_view text, float x, float y, float size = 22, Color color = Ink) {
        Text(text, x - font.Width(text, size) * .5f, y, size, color);
    }
    void Wrap(std::string_view text, float x, float y, float width, float size = 22, int lines = 4,
              Color color = Muted) {
        const auto wrapped = WrapText(
            text, width, lines, [&](std::string_view line) { return font.Width(line, size); });
        textLayoutFits = textLayoutFits && wrapped.complete;
        for (size_t line = 0; line < wrapped.lines.size(); ++line)
            Text(wrapped.lines[line], x, y + static_cast<float>(line) * size * 1.25f, size, color);
    }
    void NineSlice(int tile, Rect rect, Color tint = White) {
        const auto source = ui.Tile(tile, 13, 32);
        const float border = std::min({12.0f, rect.width * .25f, rect.height * .25f});
        const std::array<float, 4> x{rect.x, rect.x + border, rect.x + rect.width - border,
                                     rect.x + rect.width};
        const std::array<float, 4> y{rect.y, rect.y + border, rect.y + rect.height - border,
                                     rect.y + rect.height};
        const float du = (source.u1 - source.u0) * .25f, dv = (source.v1 - source.v0) * .25f;
        const std::array<float, 4> u{source.u0, source.u0 + du, source.u1 - du, source.u1};
        const std::array<float, 4> v{source.v0, source.v0 + dv, source.v1 - dv, source.v1};
        for (int yy = 0; yy < 3; ++yy)
            for (int xx = 0; xx < 3; ++xx) {
                Region crop{u[xx], v[yy], u[xx + 1], v[yy + 1], 0, 0};
                batch.Sprite(&ui.texture, crop,
                             {(x[xx] + x[xx + 1]) * .5f, (y[yy] + y[yy + 1]) * .5f},
                             {x[xx + 1] - x[xx], y[yy + 1] - y[yy]}, tint);
            }
    }
    bool Hover(Rect rect) const {
        return rect.Contains(pointerX * uiWidth, pointerY * 720);
    }
    void Button(Rect rect, std::string_view label, UiAction action, bool primary = false) {
        const bool hover = Hover(rect);
        const float offset = hover && pressed ? 3 : 0;
        const Color tint = hover ? Color{1.10f, 1.10f, 1.07f, 1} : White;
        NineSlice(primary ? 0 : 15, {rect.x, rect.y + offset, rect.width, rect.height}, tint);
        const float labelSize = std::min(30.0f, (rect.width - 24) / font.Width(label, 1));
        textLayoutFits = textLayoutFits && labelSize >= 18;
        Center(label, rect.x + rect.width * .5f, rect.y + offset + (rect.height - labelSize) * .5f,
               labelSize, Ink);
        buttons.emplace_back(rect, action);
    }
    Rect ChoiceCard(int index) const {
        const float gap = 22, width = std::min(300.0f, (uiWidth - 80 - 2 * gap) / 3);
        return {(uiWidth - (3 * width + 2 * gap)) * .5f + index * (width + gap), 220, width, 286};
    }
    void DrawWorld(const SurvivalGame& game, float time, float aspect) {
        const auto& player = game.GetPlayer();
        const le::Vec2 camera = player.position;
        const float halfWidth = 9 * aspect;
        le::OrthographicCamera view(-halfWidth, halfWidth, -9, 9);
        view.SetPosition({camera.x, camera.y, 0});
        batch.Begin(*device, view.GetViewProjectionMatrix(), false);
        batch.Solid({camera.x - halfWidth, camera.y - 9, halfWidth * 2, 18}, {.26f, .43f, .26f, 1});
        const int minX = std::max(-30, static_cast<int>(std::floor(camera.x - halfWidth - 1)));
        const int maxX = std::min(30, static_cast<int>(std::ceil(camera.x + halfWidth + 1)));
        const int minY = std::max(-30, static_cast<int>(std::floor(camera.y - 10)));
        const int maxY = std::min(30, static_cast<int>(std::ceil(camera.y + 10)));
        for (int y = minY; y <= maxY; ++y)
            for (int x = minX; x <= maxX; ++x) {
                const auto hash = Hash(x, y);
                int tile = hash % 19 == 0 ? 2 : hash % 5 == 0 ? 1 : 0;
                // Actual TinyTown grass/path tiles form a broad crossroad and a small spawn plaza.
                const bool plaza = std::abs(x) <= 4 && std::abs(y) <= 3;
                if (plaza) {
                    tile = y == 3    ? (x == -4  ? 12
                                        : x == 4 ? 14
                                                 : 13)
                           : y == -3 ? (x == -4  ? 36
                                        : x == 4 ? 38
                                                 : 37)
                           : x == -4 ? 24
                           : x == 4  ? 26
                                     : 25;
                } else if (std::abs(x) <= 1 || (std::abs(y) <= 1 && std::abs(x) < 25))
                    tile = 25;
                Town(tile, {static_cast<float>(x), static_cast<float>(y)});
            }
        // The visible fence is placed beyond the game's actual arena boundary.
        for (int x = minX; x <= maxX; ++x) {
            if (maxY >= 29)
                Town(45, {static_cast<float>(x), 30.5f});
            if (minY <= -29)
                Town(45, {static_cast<float>(x), -30.5f});
        }
        for (int y = minY; y <= maxY; ++y) {
            if (maxX >= 29)
                Town(47, {30.5f, static_cast<float>(y)});
            if (minX <= -29)
                Town(47, {-30.5f, static_cast<float>(y)});
        }
        const auto visible = [&](le::Vec2 p, float margin = 2.0f) {
            return std::abs(p.x - camera.x) < halfWidth + margin &&
                   std::abs(p.y - camera.y) < 9 + margin;
        };
        Town(43, {0, 0}, 1.6f);
        Town(83, {-3.4f, 1.8f}, 1.1f);
        Town(131, {3.2f, 2}, 1.1f);
        for (const auto& gem : game.GetGems())
            if (visible(gem.position))
                Town(93, gem.position + le::Vec2{0, std::sin(time * 3 + gem.position.x) * .035f},
                     .34f);
        // All character art is from the verified Tiny Dungeon sheet. Terrain uses Tiny Town.
        struct Actor {
            le::Vec2 position;
            int kind;
            size_t index;
            float scale;
        };
        std::vector<Actor> actors;
        for (int y = -7; y <= 7; ++y)
            for (int x = -7; x <= 7; ++x) {
                if (x * x + y * y < 3 || std::abs(x) < 1 || std::abs(y) < 1 || Hash(x, y) % 3 != 0)
                    continue;
                const le::Vec2 p{x * 4.0f + static_cast<float>(Hash(x, y) & 15) / 15 - .5f,
                                 y * 4.0f + static_cast<float>((Hash(x, y) >> 4) & 15) / 15 - .5f};
                if (visible(p, 4))
                    actors.push_back({p, 0, Hash(x, y) % 4, 1.0f});
            }
        for (size_t i = 0; i < game.GetEnemies().size(); ++i)
            if (visible(game.GetEnemies()[i].position, 3))
                actors.push_back({game.GetEnemies()[i].position, 1, i, 1});
        actors.push_back({player.position, 2, 0, 1});
        std::stable_sort(actors.begin(), actors.end(), [](const Actor& a, const Actor& b) {
            return a.position.y > b.position.y;
        });
        for (const auto& actor : actors) {
            if (actor.kind == 0) {
                const bool covered = std::abs(player.position.x - actor.position.x) < 1.4f &&
                                     player.position.y > actor.position.y &&
                                     player.position.y < actor.position.y + 2.5f;
                const bool tall = actor.index < 2;
                const int tile = std::array<int, 4>{4, 3, 28, 27}[actor.index];
                // The tall source trees span two tile rows; preserve the whole sprite and trunk.
                const le::Vec2 size = tall ? le::Vec2{1.55f, 3.1f} : le::Vec2{1.25f, 1.25f};
                batch.Sprite(&town.texture, town.Tile(tile, 12, 16, 1, tall ? 2 : 1),
                             actor.position + le::Vec2{0, size.y * .45f}, size,
                             {1, 1, 1, covered ? .45f : 1});
            } else if (actor.kind == 1) {
                const auto& enemy = game.GetEnemies()[actor.index];
                const float size = enemy.boss ? 3.2f : enemy.radius * 3.0f;
                const int tile =
                    enemy.boss
                        ? 110
                        : std::array<int, 3>{
                              120, 121, 122}[static_cast<size_t>(std::clamp(enemy.type, 0, 2))];
                Dungeon(tile,
                        enemy.position + le::Vec2{0, .08f + std::sin(time * 8 + enemy.id) * .025f},
                        size, enemy.flash > 0 ? Color{1.6f, 1.6f, 1.6f, 1} : White);
                if (!enemy.boss && enemy.health < enemy.maxHealth) {
                    batch.Solid({enemy.position.x - size * .4f, enemy.position.y + size * .6f,
                                 size * .8f, .07f},
                                {.20f, .18f, .19f, .9f});
                    batch.Solid(
                        {enemy.position.x - size * .4f, enemy.position.y + size * .6f,
                         size * .8f * std::clamp(enemy.health / enemy.maxHealth, 0.0f, 1.0f), .07f},
                        {.88f, .30f, .25f, 1});
                }
            } else {
                const float alpha =
                    player.invulnerable > 0 && static_cast<int>(time * 15) % 2 == 0 ? .45f : 1;
                Dungeon(112, player.position + le::Vec2{0, .13f}, 1.05f, {1, 1, 1, alpha});
                Town(118, player.position + le::Vec2{.35f, .1f}, .54f, {1, 1, 1, alpha});
            }
        }
        for (const auto& projectile : game.GetProjectiles())
            if (visible(projectile.position)) {
                const float a = std::atan2(projectile.direction.y, projectile.direction.x);
                if (projectile.hostile)
                    Dungeon(129, projectile.position, .44f, White, a - Pi * .5f);
                else
                    Sprite(town, 119, 12, 16, projectile.position, {.65f, .65f}, White,
                           a - Pi * .25f);
            }
        for (int i = 0; i < game.OrbitCount(); ++i) {
            const float a = game.OrbitAngle() + i * 2 * Pi / static_cast<float>(game.OrbitCount());
            Dungeon(104, player.position + le::Vec2{std::cos(a), std::sin(a)} * game.OrbitRadius(),
                    .74f, White, a);
        }
        for (const auto& effect : game.GetEffects())
            if (visible(effect.position, effect.radius)) {
                const float progress = std::clamp(effect.age / effect.duration, 0.0f, 1.0f);
                if (effect.type == 0)
                    Dungeon(60, effect.position, .38f + progress * .5f, {1, 1, 1, 1 - progress});
                else {
                    const int segments = effect.type == 1 ? 12 : 8;
                    for (int i = 0; i < segments; ++i) {
                        const float a = i * 2 * Pi / segments;
                        const auto p = effect.position + le::Vec2{std::cos(a), std::sin(a)} *
                                                             effect.radius * (.2f + progress * .8f);
                        Town(effect.type == 4 ? 29 : 2, p, .22f, {1, 1, 1, 1 - progress});
                    }
                }
            }
        batch.Flush();
    }
    void Meter(Rect rect, float ratio, Color color) {
        NineSlice(16, rect);
        batch.Solid({rect.x + 5, rect.y + 5, (rect.width - 10) * std::clamp(ratio, 0.0f, 1.0f),
                     rect.height - 10},
                    color);
    }
    void DrawTitle(const SurvivalGame&, float time) {
        const float panelWidth = std::min(440.0f, uiWidth * .46f);
        NineSlice(18, {44, 42, panelWidth, 634});
        Text(UiText::Original, 76, 78, 16, Muted);
        Text(UiText::Brand, 76, 122, 52, Ink);
        Wrap(UiText::Subtitle, 78, 184, panelWidth - 64, 24, 1, Accent);
        Sprite(town, 4, 12, 16, {91, 251}, {36, 36});
        Text(UiText::Genre, 120, 238, 21, Ink);
        Wrap(UiText::Story, 78, 286, panelWidth - 66, 23, 3, Muted);
        Button({78, 397, panelWidth - 68, 62}, UiText::Play, UiAction::Start, true);
        Button({78, 475, panelWidth - 68, 54}, UiText::Quit, UiAction::Quit);
        Text(UiText::ClickPlay, 80, 550, 17, Accent);
        Text(UiText::Move, 80, 588, 18, Muted);
        Text(UiText::Controls, 80, 616, 16, Muted);
        const float right = std::max(panelWidth + 100, (uiWidth + panelWidth) * .5f);
        NineSlice(15, {right - 213, 80, 426, 100});
        Center(UiText::Adventure, right, 98, 29, Ink);
        Center(UiText::Tagline, right, 141, 18, Muted);
        Dungeon(112, {right, 361 + std::sin(time * 2) * 3}, 170);
        Town(118, {right + 73, 377}, 88);
        Town(93, {right - 108, 447}, 42);
        Dungeon(120, {right + 132, 497}, 70);
        Dungeon(121, {right - 125, 325}, 56);
        NineSlice(15, {right - 173, 556, 346, 76});
        Center(UiText::Features, right, 577, 20, Ink);
        Center(UiText::Art, right, 646, 16, Ink);
        Center(UiText::Typeface, right, 671, 16, Ink);
    }
    void DrawHud(const SurvivalGame& game) {
        const auto& player = game.GetPlayer();
        NineSlice(15, {uiWidth * .5f - 117, 16, 234, 76});
        Center(Clock(game.Elapsed()), uiWidth * .5f, 27, 38, Ink);
        Center(std::string(UiText::Survive) + Clock(game.Duration()), uiWidth * .5f, 69, 13, Muted);
        NineSlice(18, {20, 18, 208, 57});
        Text(std::string(UiText::Level) + std::to_string(player.level), 40, 29, 26, Ink);
        NineSlice(15, {20, 83, 208, 45});
        Text(std::string(UiText::Slain) + std::to_string(game.Kills()), 40, 95, 20, Ink);
        Button({uiWidth - 138, 20, 118, 49}, UiText::Pause, UiAction::Pause);
        NineSlice(18, {20, 596, 300, 104});
        Dungeon(112, {56, 631}, 52);
        Text(UiText::Keeper, 89, 614, 22, Ink);
        Text(std::to_string(static_cast<int>(std::ceil(std::max(0.0f, player.health)))) + " / " +
                 std::to_string(static_cast<int>(player.maxHealth)),
             89, 641, 20, Muted);
        Meter({36, 675, 268, 12}, player.health / std::max(1.0f, player.maxHealth),
              {.83f, .26f, .24f, 1});
        const float middleWidth = std::max(220.0f, uiWidth - 660);
        NineSlice(15, {336, 646, middleWidth, 54});
        Text(std::string(UiText::Essence) + std::to_string(player.xp) + " / " +
                 std::to_string(player.xpNeeded),
             352, 655, 16, Ink);
        Meter({351, 680, middleWidth - 30, 10},
              static_cast<float>(player.xp) / std::max(1, player.xpNeeded), {.23f, .58f, .72f, 1});
        NineSlice(18, {uiWidth - 308, 630, 288, 70});
        Dungeon(104, {uiWidth - 270, 663}, 40);
        const bool ready = player.dashCooldown <= 0;
        Text(ready ? UiText::DashReady : UiText::DashCharging, uiWidth - 239, 643, 18, Ink);
        Meter({uiWidth - 240, 677, 200, 10}, 1 - player.dashCooldown / SurvivalGame::DashRecharge,
              {.85f, .62f, .27f, 1});
        for (const auto& enemy : game.GetEnemies())
            if (enemy.boss) {
                NineSlice(18, {uiWidth * .5f - 230, 105, 460, 58});
                Center(UiText::Boss, uiWidth * .5f, 111, 20, Ink);
                Meter({uiWidth * .5f - 210, 143, 420, 9},
                      enemy.health / std::max(1.0f, enemy.maxHealth), {.73f, .24f, .24f, 1});
                break;
            }
        if (game.Elapsed() < 7) {
            NineSlice(15, {uiWidth * .5f - 215, 526, 430, 43});
            Center(UiText::AutoAttack, uiWidth * .5f, 538, 18, Ink);
        }
    }
    void DrawChoices(const SurvivalGame& game) {
        Center(UiText::Gift, uiWidth * .5f, 130, 44, Light);
        Center(UiText::Choose, uiWidth * .5f, 181, 22, Light);
        const std::array<int, 9> icons{104, 119, 129, 116, 93, 105, 118, 2, 114};
        for (int i = 0; i < 3; ++i) {
            const Rect card = ChoiceCard(i);
            const bool focused = keyboardChoice && selectedChoice == i;
            NineSlice(Hover(card) || focused ? 18 : 15, card,
                      Hover(card) && pressed ? Color{.9f, .9f, .9f, 1} : White);
            const auto& choice = game.GetChoices()[i];
            const int id = std::clamp(static_cast<int>(choice.id), 0, 8);
            if (id == 1 || id == 4 || id == 6)
                Town(icons[static_cast<size_t>(id)], {card.x + card.width * .5f, card.y + 57}, 52);
            else
                Dungeon(icons[static_cast<size_t>(id)], {card.x + card.width * .5f, card.y + 57},
                        52);
            Wrap(choice.title, card.x + 22, card.y + 99, card.width - 44, 27, 2, Ink);
            Wrap(choice.description, card.x + 22, card.y + 166, card.width - 44, 21, 3, Muted);
            Text(focused ? UiText::Enter : UiText::Click, card.x + 22, card.y + 252, 17, Accent);
            buttons.emplace_back(card,
                                 static_cast<UiAction>(static_cast<int>(UiAction::Choice0) + i));
        }
        Center(UiText::Wait, uiWidth * .5f, 550, 18, Light);
    }
    void DrawOverlay(const SurvivalGame& game) {
        const auto phase = game.GetPhase();
        batch.Solid({0, 0, uiWidth, 720}, {.08f, .12f, .17f, .65f});
        if (phase == Phase::Upgrade) {
            DrawChoices(game);
            return;
        }
        const bool paused = phase == Phase::Paused, won = phase == Phase::Victory;
        const float width = std::min(640.0f, uiWidth - 80), x = (uiWidth - width) * .5f;
        NineSlice(18, {x, 113, width, 490});
        Center(paused ? UiText::Paused
               : won  ? UiText::Victory
                      : UiText::Defeat,
               uiWidth * .5f, 155, 40, Ink);
        Center(paused ? UiText::PausedDetail
               : won  ? UiText::VictoryDetail
                      : UiText::DefeatDetail,
               uiWidth * .5f, 212, 20, Muted);
        Center(Clock(game.Elapsed()) + "  /  " + std::string(UiText::Slain) +
                   std::to_string(game.Kills()) + "  /  " + std::string(UiText::Level) +
                   std::to_string(game.GetPlayer().level),
               uiWidth * .5f, 268, 24, Ink);
        const float buttonX = x + 90, buttonWidth = width - 180;
        if (paused) {
            Button({buttonX, 326, buttonWidth, 60}, UiText::Resume, UiAction::Resume, true);
            Button({buttonX, 403, buttonWidth, 55}, UiText::Restart, UiAction::Restart);
            Button({buttonX, 474, buttonWidth, 55}, UiText::Quit, UiAction::Quit);
        } else {
            Button({buttonX, 343, buttonWidth, 66}, UiText::Restart, UiAction::Restart, true);
            Button({buttonX, 433, buttonWidth, 58}, UiText::Quit, UiAction::Quit);
        }
    }
};
ForestRenderer::ForestRenderer() : state_(std::make_unique<State>()) {}
ForestRenderer::~ForestRenderer() = default;
void ForestRenderer::Initialize(le::GpuDevice& device, const std::filesystem::path& root) {
    auto next = std::make_unique<State>();
    next->device = &device;
    next->town.Load(device, root / "TinyTown/Tilemap/tilemap_packed.png");
    next->dungeon.Load(device, root / "TinyDungeon/Tilemap/tilemap_packed.png");
    next->ui.Load(device,
                  root / "PixelAdventureUI/Tilesheets/Large tiles/Thin outline/tilemap_packed.png");
    next->font.Load(device, root / "DungGeunMo/DungGeunMo.ttf");
    constexpr float minimumCardTextWidth = (936.0f - 80 - 44) / 3 - 44;
    for (const auto& upgrade : SurvivalGame::UpgradeCatalog()) {
        const auto fits = [&](std::string_view text, float size, int lines) {
            return WrapText(text, minimumCardTextWidth, lines,
                            [&](std::string_view line) { return next->font.Width(line, size); })
                .complete;
        };
        if (!fits(upgrade.title, 27, 2) || !fits(upgrade.description, 21, 3))
            throw std::runtime_error("한국어 강화 설명이 최소 창 크기의 카드 영역을 넘었습니다.");
    }
    state_ = std::move(next);
}
bool ForestRenderer::TextLayoutFits() const {
    return state_->textLayoutFits;
}
void ForestRenderer::Shutdown() {
    state_ = std::make_unique<State>();
}
void ForestRenderer::Render(const SurvivalGame& game, float time, float aspect) {
    if (!state_->device)
        return;
    if (!std::isfinite(aspect) || aspect <= 0)
        aspect = 16.0f / 9;
    aspect = std::clamp(aspect, 1.3f, 3.5f);
    auto& s = *state_;
    s.uiWidth = 720 * aspect;
    s.phase = game.GetPhase();
    s.buttons.clear();
    s.textLayoutFits = true;
    s.DrawWorld(game, time, aspect);
    const le::OrthographicCamera ui(0, s.uiWidth, 720, 0);
    s.batch.Begin(*s.device, ui.GetViewProjectionMatrix(), true);
    if (s.phase == Phase::Title)
        s.DrawTitle(game, time);
    else {
        s.DrawHud(game);
        if (s.phase != Phase::Playing) {
            s.buttons.clear();
            s.DrawOverlay(game);
        }
    }
    s.batch.Flush();
}
UiAction ForestRenderer::HitAction(float x, float y) const {
    if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0 || x > 1 || y > 1)
        return UiAction::None;
    for (auto it = state_->buttons.rbegin(); it != state_->buttons.rend(); ++it)
        if (it->first.Contains(x * state_->uiWidth, y * 720))
            return it->second;
    return UiAction::None;
}
int ForestRenderer::HitChoice(float x, float y) const {
    const auto action = HitAction(x, y);
    return action >= UiAction::Choice0 && action <= UiAction::Choice2
               ? static_cast<int>(action) - static_cast<int>(UiAction::Choice0)
               : -1;
}
void ForestRenderer::SetPointer(float x, float y, bool down) {
    if (std::abs(x - state_->pointerX) > .001f || std::abs(y - state_->pointerY) > .001f)
        state_->keyboardChoice = false;
    state_->pointerX = x;
    state_->pointerY = y;
    state_->pressed = down;
}
void ForestRenderer::SetSelectedChoice(int index) {
    index = std::clamp(index, 0, 2);
    if (index != state_->selectedChoice)
        state_->keyboardChoice = true;
    state_->selectedChoice = index;
}
} // namespace ForestGame
