#include "Engine/LuxEngine.hpp"
#include "Runtime/Runtime.hpp"
#include <SDL3/SDL_main.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace le = LuxEngine;

namespace {
struct ForestStyle {
    float width = 12.8f;
    float height = 7.2f;
    float ground = -1.8f;
    float tile = 0.32f;
    float treeSpacing = 1.65f;
    le::Vec4 dirt{0.39f, 0.27f, 0.17f, 1.0f};
    le::Vec4 stone{0.30f, 0.34f, 0.32f, 1.0f};
    le::Vec4 grass{0.31f, 0.65f, 0.30f, 1.0f};
    le::Vec4 leaf{0.16f, 0.48f, 0.28f, 1.0f};
    le::Vec4 bark{0.40f, 0.27f, 0.18f, 1.0f};
    le::Vec4 flower{1.0f, 0.73f, 0.54f, 1.0f};
};
constexpr ForestStyle style{};

le::Vec4 Mix(const le::Vec4& a, const le::Vec4& b, float amount) {
    return {a.x + (b.x - a.x) * amount, a.y + (b.y - a.y) * amount, a.z + (b.z - a.z) * amount,
            a.w + (b.w - a.w) * amount};
}

class PixelCanvas {
public:
    PixelCanvas(int width, int height)
        : width_(width), height_(height), pixels_(width * height * 4) {}

    void Set(int x, int y, const le::Vec4& color) {
        if (x < 0 || y < 0 || x >= width_ || y >= height_)
            return;
        const size_t index = static_cast<size_t>((y * width_ + x) * 4);
        pixels_[index] = Channel(color.x);
        pixels_[index + 1] = Channel(color.y);
        pixels_[index + 2] = Channel(color.z);
        pixels_[index + 3] = 255;
    }

    void Rect(int x, int y, int width, int height, const le::Vec4& color) {
        for (int row = y; row < y + height; ++row)
            for (int column = x; column < x + width; ++column)
                Set(column, row, color);
    }

    void Disc(int centerX, int centerY, int radius, const le::Vec4& color) {
        for (int y = -radius; y <= radius; ++y)
            for (int x = -radius; x <= radius; ++x)
                if (x * x + y * y <= radius * radius)
                    Set(centerX + x, centerY + y, color);
    }

    const std::vector<le::uint8>& Pixels() const {
        return pixels_;
    }

private:
    static le::uint8 Channel(float value) {
        return static_cast<le::uint8>(std::clamp(value, 0.0f, 1.0f) * 255.0f);
    }
    int width_;
    int height_;
    std::vector<le::uint8> pixels_;
};

std::vector<le::uint8> MakeLandscape(int width, int height) {
    PixelCanvas image(width, height);
    const le::Vec4 top{0.32f, 0.63f, 0.86f, 1.0f};
    const le::Vec4 horizon{0.86f, 0.91f, 0.76f, 1.0f};
    for (int y = 0; y < height; ++y) {
        const float t = static_cast<float>(y) / height;
        image.Rect(0, y, width, 1, Mix(top, horizon, t));
    }

    image.Disc(width * 3 / 4, height / 4, height / 12, {1.0f, 0.94f, 0.63f, 1.0f});
    le::Random random(0xC0FFEEu);
    for (int cloud = 0; cloud < 7; ++cloud) {
        const int x = static_cast<int>(random.Range(0.0f, static_cast<float>(width)));
        const int y = static_cast<int>(random.Range(6.0f, height * 0.36f));
        const int size = static_cast<int>(random.Range(10.0f, 23.0f));
        const le::Vec4 shade{0.94f, 0.97f, 0.91f, 1.0f};
        image.Rect(x, y, size * 2, size / 2, shade);
        image.Rect(x + size / 3, y - size / 3, size, size / 2, shade);
        image.Rect(x + size, y + size / 3, size, size / 4, {0.75f, 0.87f, 0.83f, 1.0f});
    }

    for (int layer = 0; layer < 3; ++layer) {
        const float base = height * (0.53f + layer * 0.11f);
        const float amplitude = height * (0.12f - layer * 0.02f);
        const le::Vec4 color = layer == 0   ? le::Vec4(0.48f, 0.68f, 0.64f, 1.0f)
                               : layer == 1 ? le::Vec4(0.32f, 0.59f, 0.54f, 1.0f)
                                            : le::Vec4(0.22f, 0.48f, 0.40f, 1.0f);
        for (int x = 0; x < width; ++x) {
            const float wave = std::sin(x * (0.020f + layer * 0.013f) + layer) +
                               0.42f * std::sin(x * 0.067f + layer * 2.0f);
            const int ridge = static_cast<int>(base - amplitude * wave);
            image.Rect(x, ridge, 1, height - ridge, color);
        }
    }
    return image.Pixels();
}

std::vector<le::uint8> MakeGlow(int side) {
    std::vector<le::uint8> pixels(static_cast<size_t>(side * side * 4));
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x) {
            const float dx = (x + 0.5f) * 2.0f / side - 1.0f;
            const float dy = (y + 0.5f) * 2.0f / side - 1.0f;
            const float alpha = std::exp(-(dx * dx + dy * dy) * 7.0f);
            const size_t index = static_cast<size_t>((y * side + x) * 4);
            pixels[index] = pixels[index + 1] = pixels[index + 2] = 255;
            pixels[index + 3] = static_cast<le::uint8>(alpha * 255.0f);
        }
    return pixels;
}

struct Tree {
    float x;
    float height;
    le::uint32 seed;
};

struct Firefly {
    le::Vec2 origin;
    float phase;
    float speed;
};
} // namespace

class SandboxApp final : public le::Application {
public:
    explicit SandboxApp(bool smokeTest)
        : le::Application({.name = "LuxEngine | 숲의 낮", .width = 1280, .height = 720}),
          smokeTest_(smokeTest),
          camera_(-style.width / 2, style.width / 2, -style.height / 2, style.height / 2) {}

protected:
    void OnInit() override {
        le::Runtime::PrintEngineInfo();
        le::Renderer2D::Init(&GetGpuDevice());
        le::PhysicsEngine::Init();
        if (!le::AudioEngine::Init())
            throw std::runtime_error("Audio initialization failed");

        constexpr int landscapeWidth = 320;
        constexpr int landscapeHeight = 180;
        constexpr int glowSide = 64;
        const auto landscape = MakeLandscape(landscapeWidth, landscapeHeight);
        const auto glow = MakeGlow(glowSide);
        if (!landscape_.Initialize(&GetGpuDevice(), landscapeWidth, landscapeHeight,
                                   landscape.data()) ||
            !glow_.Initialize(&GetGpuDevice(), glowSide, glowSide, glow.data()))
            throw std::runtime_error("Forest textures could not be initialized");

        floor_ = scene_.CreateEntity("ForestFloor");
        auto& floorTransform = floor_.GetComponent<le::TransformComponent>();
        floorTransform.position = {0.0f, (style.ground - style.height / 2) / 2, 0.0f};
        floorTransform.scale = {style.width, style.ground + style.height / 2, 1.0f};
        floor_.AddComponent<le::BoxCollider2DComponent>();

        crate_ = scene_.CreateEntity("WoodenCrate");
        auto& crateTransform = crate_.GetComponent<le::TransformComponent>();
        crateTransform.position = {-style.width / 4, style.ground + 2.6f, 0.0f};
        crateTransform.scale = {style.tile * 2.4f, style.tile * 2.4f, 1.0f};
        crate_.AddComponent<le::Rigidbody2DComponent>();
        crate_.AddComponent<le::BoxCollider2DComponent>();

        le::Random random(0xF0E57u);
        const int treeCount = static_cast<int>(style.width / style.treeSpacing);
        for (int index = 0; index < treeCount; ++index) {
            const float x = -style.width / 2 + (index + 0.5f) * style.width / treeCount +
                            random.Range(-style.tile, style.tile);
            trees_.push_back({x, random.Range(1.25f, 2.2f), random.Next()});
        }
        for (Firefly& fly : fireflies_) {
            fly.origin = {random.Range(-style.width / 2, style.width / 2),
                          random.Range(style.ground + style.tile, style.ground + 2.8f)};
            fly.phase = random.Range(0.0f, 6.28318f);
            fly.speed = random.Range(0.5f, 1.2f);
        }
        le::Log::Info("Sandbox 숲 장면과 2D 물리 시스템이 초기화되었습니다.");
    }

    void OnUpdate(double dt) override {
        time_ += static_cast<float>(dt);
        jumpTimer_ += static_cast<float>(dt);
        if (auto* body = crate_.TryGetComponent<le::Rigidbody2DComponent>();
            body && body->isGrounded && jumpTimer_ >= 2.4f) {
            body->velocity.y = 6.0f;
            jumpTimer_ = 0.0f;
        }
        scene_.OnUpdate(dt);
        if (smokeTest_ && ++frames_ >= 10)
            Close();
    }

    void OnRender(float elapsedSeconds) override {
        le::Renderer2D::BeginScene(camera_);
        le::Renderer2D::DrawSprite({0.0f, 0.0f}, {style.width, style.height}, &landscape_);
        DrawForest(elapsedSeconds);
        DrawGround();
        DrawCrate();
        DrawFireflies(elapsedSeconds);
        le::Renderer2D::EndScene();
    }

    void OnShutdown() override {
        glow_.Release();
        landscape_.Release();
        le::AudioEngine::Shutdown();
        le::PhysicsEngine::Shutdown();
        le::Renderer2D::Shutdown();
        le::Log::Info("Sandbox 응용 프로그램이 종료되었습니다.");
    }

private:
    void DrawTree(const Tree& tree) {
        le::Random random(tree.seed);
        const float trunkWidth = style.tile * 0.55f;
        const float canopyY = style.ground + tree.height * 0.78f;
        le::Renderer2D::DrawQuad({tree.x, style.ground + tree.height / 2},
                                 {trunkWidth, tree.height}, style.bark);
        le::Renderer2D::DrawQuad({tree.x + trunkWidth / 4, style.ground + tree.height / 2},
                                 {trunkWidth / 5, tree.height},
                                 Mix(style.bark, style.flower, 0.18f));

        const float leafTile = style.tile * 0.63f;
        for (int row = -3; row <= 3; ++row) {
            const int halfWidth = 4 - std::abs(row);
            for (int column = -halfWidth; column <= halfWidth; ++column) {
                if (random.Unit() < 0.14f)
                    continue;
                const float tone = random.Range(-0.16f, 0.16f);
                const le::Vec4 color =
                    Mix(style.leaf, tone > 0 ? style.grass : style.stone, std::abs(tone));
                le::Renderer2D::DrawQuad({tree.x + column * leafTile, canopyY + row * leafTile},
                                         {leafTile * 1.04f, leafTile * 1.04f}, color);
            }
        }
    }

    void DrawForest(float time) {
        for (const Tree& tree : trees_)
            DrawTree(tree);
        const int count = static_cast<int>(style.width / style.tile);
        le::Random random(0xB10F0u);
        for (int index = 0; index < count; ++index) {
            const float x = -style.width / 2 + (index + 0.5f) * style.tile;
            const float height = random.Range(style.tile * 0.3f, style.tile);
            le::Renderer2D::DrawQuad({x, style.ground + height / 2}, {style.tile * 0.12f, height},
                                     Mix(style.grass, style.leaf, random.Unit() * 0.5f));
            if (index % 6 == 0) {
                for (int leaf = -1; leaf <= 1; ++leaf)
                    le::Renderer2D::DrawQuad(
                        {x + leaf * style.tile * 0.45f,
                         style.ground + style.tile * (leaf == 0 ? 0.55f : 0.35f)},
                        {style.tile * 0.8f, style.tile * 0.72f},
                        Mix(style.leaf, style.grass, random.Unit() * 0.4f));
            }
            if (random.Unit() < 0.24f) {
                const float sway = std::sin(time * 1.5f + index) * style.tile * 0.08f;
                le::Renderer2D::DrawQuad(
                    {x + sway, style.ground + height}, {style.tile * 0.25f, style.tile * 0.25f},
                    index % 3 == 0 ? style.flower : le::Vec4(0.91f, 0.82f, 0.94f, 1.0f));
            }
        }
    }

    void DrawGround() {
        const int columns = static_cast<int>(style.width / style.tile) + 1;
        const int rows = static_cast<int>((style.ground + style.height / 2) / style.tile) + 1;
        le::Random random(0xD147u);
        for (int row = 0; row < rows; ++row) {
            for (int column = 0; column < columns; ++column) {
                const float x = -style.width / 2 + (column + 0.5f) * style.tile;
                const float y = style.ground - (row + 0.5f) * style.tile;
                const le::Vec4 base = row == 0 ? style.grass
                                      : random.Unit() < 0.12f
                                          ? style.stone
                                          : Mix(style.dirt, style.bark, random.Unit() * 0.32f);
                le::Renderer2D::DrawQuad({x, y}, {style.tile * 0.97f, style.tile * 0.97f}, base);
                if (row > 0 && random.Unit() < 0.55f) {
                    const float offset = random.Range(-style.tile * 0.25f, style.tile * 0.25f);
                    le::Renderer2D::DrawQuad({x + offset, y + style.tile * 0.12f},
                                             {style.tile * 0.18f, style.tile * 0.10f},
                                             Mix(base, style.stone, 0.24f + random.Unit() * 0.3f));
                }
                if (row == 0) {
                    le::Renderer2D::DrawQuad({x, style.ground - style.tile * 0.10f},
                                             {style.tile * 0.97f, style.tile * 0.22f},
                                             Mix(style.grass, le::Vec4(0.67f, 0.86f, 0.38f, 1.0f),
                                                 random.Unit() * 0.45f));
                }
            }
        }
    }

    void DrawCrate() {
        const auto* transform = crate_.TryGetComponent<le::TransformComponent>();
        if (!transform)
            return;
        const le::Vec2 center{transform->position.x, transform->position.y};
        const le::Vec2 size{transform->scale.x, transform->scale.y};
        const float angle = transform->rotation.z;
        le::Renderer2D::DrawRotatedQuad(center, size, angle, style.bark);
        le::Renderer2D::DrawRotatedQuad(center, size * 0.83f, angle,
                                        Mix(style.bark, style.flower, 0.24f));
        le::Renderer2D::DrawRotatedQuad(center, {size.x * 0.12f, size.y * 0.82f}, angle + 0.785398f,
                                        style.bark);
        le::Renderer2D::DrawRotatedQuad(center, {size.x * 0.12f, size.y * 0.82f}, angle - 0.785398f,
                                        style.bark);
        le::Renderer2D::DrawRotatedQuad(center, size * 0.14f, angle, {0.86f, 0.72f, 0.43f, 1.0f});
    }

    void DrawFireflies(float time) {
        for (const Firefly& fly : fireflies_) {
            const le::Vec2 position{
                fly.origin.x + std::sin(time * fly.speed + fly.phase) * style.tile,
                fly.origin.y + std::cos(time * fly.speed * 0.7f + fly.phase) * style.tile};
            const float alpha = 0.28f + 0.25f * std::sin(time * 2.0f + fly.phase);
            le::Renderer2D::DrawSprite(position, {style.tile * 1.8f, style.tile * 1.8f}, &glow_,
                                       {1.0f, 0.92f, 0.45f, alpha});
            le::Renderer2D::DrawQuad(position, {style.tile * 0.08f, style.tile * 0.08f},
                                     {1.0f, 0.97f, 0.67f, 1.0f});
        }
    }

    bool smokeTest_ = false;
    int frames_ = 0;
    float time_ = 0.0f;
    float jumpTimer_ = 0.0f;
    le::Scene scene_;
    le::Entity floor_;
    le::Entity crate_;
    le::OrthographicCamera camera_;
    le::GpuTexture landscape_;
    le::GpuTexture glow_;
    std::vector<Tree> trees_;
    std::array<Firefly, 32> fireflies_{};
};

int main(int argc, char* argv[]) {
    SandboxApp app(argc > 1 && std::string_view(argv[1]) == "--smoke-test");
    if (!app.Initialize())
        return 1;
    const int result = app.Run();
    app.Shutdown();
    return result;
}
