#include <LuxEngine/EntryPoint.hpp>
#include <LuxEngine/LuxEngine.hpp>
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <random>
#include <stdexcept>
#include <vector>

namespace le = LuxEngine;

class PandaGarden final : public le::Application {
    struct Sprite {
        le::Ref<le::GpuTexture> texture;
        le::Vec4 uv;
        float aspect = 1;
    };

    struct Particle {
        le::Vec2 p, v;
        float life;
    };

    Sprite panda_, bamboo_, obstacle_, shrub_;
    std::array<Sprite, 4> terrain_;
    le::OrthographicCamera camera_{0, 1280, 0, 800};
    std::vector<le::RenderVertex> shapes_;
    std::vector<Particle> particles_;
    std::array<le::Vec2, 6> shoots_;
    std::array<le::Vec2, 3> hazards_;
    std::mt19937 random_{std::random_device{}()};
    std::filesystem::path root_;
    le::Vec2 player_{640, 380};
    float clock_ = 0, motion_ = 0, stamina_ = 1, immune_ = 0, comboTime_ = 0;
    int combo_ = 0;
    bool facingLeft_ = false, paused_ = false;

    float Random(float a, float b) {
        return std::uniform_real_distribution<float>(a, b)(random_);
    }

    static float Distance(le::Vec2 a, le::Vec2 b) {
        return std::hypot(a.x - b.x, a.y - b.y);
    }

    void Circle(le::Vec2 p, le::Vec2 radius, le::Vec4 color) {
        constexpr int count = 28;
        for (int i = 0; i < count; ++i) {
            const float a = float(i) * 6.2831853f / count, b = float(i + 1) * 6.2831853f / count;
            shapes_.push_back({{p.x, p.y, 0}, color, {}});
            shapes_.push_back(
                {{p.x + std::cos(a) * radius.x, p.y + std::sin(a) * radius.y, 0}, color, {}});
            shapes_.push_back(
                {{p.x + std::cos(b) * radius.x, p.y + std::sin(b) * radius.y, 0}, color, {}});
        }
    }

    void FlushShapes() {
        if (!shapes_.empty() &&
            !GetGpuDevice().DrawTriangles(shapes_, camera_.GetViewProjectionMatrix()))
            throw std::runtime_error("정원 도형을 그리지 못했습니다.");
        shapes_.clear();
    }

    Sprite Load(const std::filesystem::path& file, bool blackKey,
                le::TextureFilter filter = le::TextureFilter::Linear, bool trim = true) {
        const auto utf8 = file.u8string();
        std::string path(reinterpret_cast<const char*>(utf8.data()), utf8.size());
        int w = 0, h = 0;
        std::vector<le::uint8> pixels;
        if (!le::AssetManager::DecodeTextureCpu(path, w, h, pixels))
            throw std::runtime_error("이미지를 읽지 못했습니다: " + path);
        if (blackKey) {
            std::vector<size_t> queue;
            auto push = [&](int x, int y) {
                size_t i = size_t(y) * w + x, k = i * 4;
                if (pixels[k + 3] && pixels[k] <= 8 && pixels[k + 1] <= 8 && pixels[k + 2] <= 8) {
                    pixels[k + 3] = 0;
                    queue.push_back(i);
                }
            };
            for (int x = 0; x < w; ++x) {
                push(x, 0);
                push(x, h - 1);
            }
            for (int y = 0; y < h; ++y) {
                push(0, y);
                push(w - 1, y);
            }
            for (size_t q = 0; q < queue.size(); ++q) {
                int x = int(queue[q] % size_t(w)), y = int(queue[q] / size_t(w));
                if (x > 0)
                    push(x - 1, y);
                if (x + 1 < w)
                    push(x + 1, y);
                if (y > 0)
                    push(x, y - 1);
                if (y + 1 < h)
                    push(x, y + 1);
            }
        }
        int x0 = 0, y0 = 0, x1 = w, y1 = h;
        if (trim) {
            x0 = w;
            y0 = h;
            x1 = y1 = 0;
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                    if (pixels[(size_t(y) * w + x) * 4 + 3] > 16) {
                        x0 = std::min(x0, x);
                        y0 = std::min(y0, y);
                        x1 = std::max(x1, x + 1);
                        y1 = std::max(y1, y + 1);
                    }
        }
        if (x1 <= x0 || y1 <= y0)
            throw std::runtime_error("비어 있는 이미지입니다.");
        Sprite sprite;
        sprite.texture = le::CreateRef<le::GpuTexture>();
        if (!sprite.texture->Initialize(&GetGpuDevice(), le::uint32(w), le::uint32(h),
                                        pixels.data(), filter))
            throw std::runtime_error("게임 텍스처를 만들지 못했습니다.");
        sprite.uv = {float(x0) / w, float(y0) / h, float(x1) / w, float(y1) / h};
        sprite.aspect = float(x1 - x0) / float(y1 - y0);
        return sprite;
    }

    void Image(const Sprite& s, le::Vec2 p, float height, float angle = 0, float alpha = 1,
               bool flip = false, le::Vec4 tint = {1, 1, 1, 1}) {
        float width = height * s.aspect, c = std::cos(angle), n = std::sin(angle);
        std::array<le::RenderVertex, 4> corners;
        std::array<le::Vec2, 4> positions{{{-.5f, -.5f}, {.5f, -.5f}, {.5f, .5f}, {-.5f, .5f}}};
        float u0 = flip ? s.uv.z : s.uv.x, u1 = flip ? s.uv.x : s.uv.z;
        std::array<le::Vec2, 4> uv{{{u0, s.uv.w}, {u1, s.uv.w}, {u1, s.uv.y}, {u0, s.uv.y}}};
        for (size_t i = 0; i < 4; ++i) {
            float x = positions[i].x * width, y = positions[i].y * height;
            corners[i] = {{p.x + x * c - y * n, p.y + x * n + y * c, 0},
                          {tint.x, tint.y, tint.z, tint.w * alpha},
                          uv[i]};
        }
        const std::array<le::RenderVertex, 6> v{corners[0], corners[1], corners[2],
                                                corners[0], corners[2], corners[3]};
        if (!GetGpuDevice().DrawTriangles(v, camera_.GetViewProjectionMatrix(), s.texture.get()))
            throw std::runtime_error("게임 이미지를 그리지 못했습니다.");
    }

    void Spawn(le::Vec2& p) {
        for (int i = 0; i < 50; ++i) {
            p = {Random(75, 1205), Random(80, 720)};
            if (Distance(p, player_) > 140)
                break;
        }
    }

    void Start() {
        player_ = {640, 400};
        combo_ = 0;
        comboTime_ = 0;
        immune_ = 0;
        stamina_ = 1;
        clock_ = 0;
        particles_.clear();
        for (auto& p : shoots_)
            Spawn(p);
        paused_ = false;
    }

    void OnInit() override {
        auto* window = GetWindow().GetNativeHandle();
        if (!SDL_SetWindowResizable(window, false) ||
            !SDL_SetWindowMinimumSize(window, 1280, 800) ||
            !SDL_SetWindowMaximumSize(window, 1280, 800))
            throw std::runtime_error(SDL_GetError());
        const auto* base = SDL_GetBasePath();
        if (!base)
            throw std::runtime_error(SDL_GetError());
        root_ = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(base))) /
                "panda-assets";
        panda_ = Load(root_ / "panda.png", true);
        bamboo_ = Load(root_ / "bamboo.png", false);
        constexpr std::array<const char*, 4> groundFiles{"tile_0000.png", "tile_0001.png",
                                                         "tile_0002.png", "tile_0043.png"};
        for (size_t i = 0; i < terrain_.size(); ++i)
            terrain_[i] = Load(root_ / "BackgroundTinyTown" / groundFiles[i], false,
                               le::TextureFilter::Nearest, false);
        obstacle_ =
            Load(root_ / "BackgroundTinyTown/tile_0029.png", false, le::TextureFilter::Nearest);
        shrub_ =
            Load(root_ / "BackgroundTinyTown/tile_0005.png", false, le::TextureFilter::Nearest);
        for (auto [name, key] : {std::pair{"left", le::LuxKey::A},
                                 {"left", le::LuxKey::Left},
                                 {"right", le::LuxKey::D},
                                 {"right", le::LuxKey::Right},
                                 {"up", le::LuxKey::W},
                                 {"up", le::LuxKey::Up},
                                 {"down", le::LuxKey::S},
                                 {"down", le::LuxKey::Down}})
            le::Input::BindAction(name, key);
        le::Input::BindAction("reset", le::LuxKey::R);
        le::Input::BindAction("pause", le::LuxKey::P);
        le::Input::BindAction("pause", le::LuxKey::Escape);
        Start();
    }

    void OnUpdate(double delta) override {
        const float dt = std::min(float(delta), .05f);
        motion_ += dt;
        if (le::Input::WasActionPressed("reset"))
            Start();
        if (le::Input::WasActionPressed("pause"))
            paused_ = !paused_;
        if (paused_)
            return;
        clock_ += dt;
        immune_ = std::max(0.f, immune_ - dt);
        comboTime_ = std::max(0.f, comboTime_ - dt);
        if (comboTime_ == 0)
            combo_ = 0;
        le::Vec2 direction{
            float(le::Input::IsActionDown("right")) - float(le::Input::IsActionDown("left")),
            float(le::Input::IsActionDown("up")) - float(le::Input::IsActionDown("down"))};
        float length = std::hypot(direction.x, direction.y);
        if (length > 1)
            direction = direction * (1 / length);
        const bool* keys = SDL_GetKeyboardState(nullptr);
        bool dash = keys[SDL_SCANCODE_LSHIFT] && stamina_ > .06f && length > 0;
        stamina_ = std::clamp(stamina_ + (dash ? -.42f : .24f) * dt, 0.f, 1.f);
        player_ = player_ + direction * (dt * (dash ? 450.f : 270.f));
        player_.x = std::clamp(player_.x, 64.f, 1216.f);
        player_.y = std::clamp(player_.y, 56.f, 744.f);
        if (direction.x != 0)
            facingLeft_ = direction.x < 0;
        for (auto& p : shoots_)
            if (Distance(player_, p) < 55) {
                combo_ = std::min(5, combo_ + 1);
                comboTime_ = 3.5f;
                for (int i = 0; i < 8 + combo_ * 2; ++i)
                    particles_.push_back(
                        {p, {Random(-90, 90), Random(20, 130)}, Random(.35f, .8f)});
                Spawn(p);
            }
        for (size_t i = 0; i < hazards_.size(); ++i) {
            hazards_[i] = {640 +
                               std::sin(clock_ * (.55f + float(i) * .12f) + float(i) * 2.1f) * 550,
                           140 + float(i) * 260};
            if (immune_ == 0 && Distance(player_, hazards_[i]) < 55) {
                immune_ = 1.5f;
                combo_ = 0;
                stamina_ = std::max(0.f, stamina_ - .25f);
                const auto away = player_ - hazards_[i];
                const float separation = std::hypot(away.x, away.y);
                if (separation > .01f)
                    player_ = player_ + away * (45 / separation);
                player_.x = std::clamp(player_.x, 64.f, 1216.f);
                player_.y = std::clamp(player_.y, 56.f, 744.f);
            }
        }
        for (auto& p : particles_) {
            p.life -= dt;
            p.p = p.p + p.v * dt;
            p.v.y -= 120 * dt;
        }
        std::erase_if(particles_, [](const Particle& p) { return p.life <= 0; });
    }

    void OnRender(float) override {
        GetGpuDevice().Clear(.27f, .62f, .17f, 1);
        for (int y = 0; y < 800; y += 64) {
            for (int x = 0; x < 1280; x += 64) {
                const int tileX = x / 64, tileY = y / 64;
                const int variety = (tileX * 17 + tileY * 13 + tileX * tileY * 7) % 53;
                size_t tile = variety < 3 ? 2 : variety < 18 ? 1 : 0;
                if (tileY > 1 && tileY < 11 && tileX == 4 + tileY)
                    tile = 3;
                Image(terrain_[tile], {float(x + 32), float(y + 32)}, 64, 0, 1, false,
                      {.52f, .82f, .42f, 1});
            }
        }
        constexpr std::array<le::Vec2, 7> shrubs{
            {{165, 645}, {360, 150}, {1110, 660}, {980, 190}, {250, 310}, {1085, 410}, {700, 690}}};
        for (auto p : shrubs)
            Image(shrub_, p, 64);
        for (auto p : shoots_)
            Circle({p.x, p.y - 31}, {26, 8}, {.24f, .40f, .18f, .18f});
        Circle({player_.x, player_.y - 37}, {44, 12}, {.20f, .34f, .17f, .22f});
        for (auto p : hazards_)
            Circle({p.x, p.y - 23}, {33, 10}, {.20f, .34f, .17f, .20f});
        for (const auto& p : particles_)
            Circle(p.p, {3, 5}, {.95f, .94f, .66f, std::min(1.f, p.life * 2)});
        FlushShapes();
        for (auto p : hazards_)
            Image(obstacle_, p, 54);
        for (size_t i = 0; i < shoots_.size(); ++i) {
            auto p = shoots_[i];
            p.y += std::sin(motion_ * 2 + float(i)) * 4;
            Image(bamboo_, p, 72, .08f * std::sin(motion_ + float(i)));
        }
        Image(panda_, {player_.x, player_.y + std::sin(motion_ * 4) * 2}, 83, 0,
              immune_ > 0 ? .68f : 1.f, facingLeft_);
    }

    void OnShutdown() override {
        le::Input::ClearBindings();
        panda_.texture.reset();
        bamboo_.texture.reset();
        obstacle_.texture.reset();
        shrub_.texture.reset();
        for (auto& tile : terrain_)
            tile.texture.reset();
    }

public:
    PandaGarden() : Application({"판다의 대나무 정원", 1280, 800}) {}

    ~PandaGarden() override {
        Shutdown();
    }
};

int PlayGarden(int, char**) {
    PandaGarden game;
    if (!game.Initialize())
        return 1;
    return game.Run();
}

LUX_ENGINE_MAIN(PlayGarden)
