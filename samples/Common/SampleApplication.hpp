#pragma once
#include "Artwork.hpp"
#include <LuxEngine/LuxEngine.hpp>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace LuxSamples {
struct Options {
    unsigned frames = 0;
    bool help = false;
    std::filesystem::path capture;
};

Options ParseOptions(int argc, char** argv);
void Text(std::string_view text, LuxEngine::Vec2 position, float pixel = .035f,
          LuxEngine::Vec4 color = {.22f, .16f, .12f, 1});
void Sprite(const std::string& relative, LuxEngine::Vec2 position, LuxEngine::Vec2 size,
            LuxEngine::Vec4 tint = {1, 1, 1, 1}, float angle = 0);
void Quad(LuxEngine::Vec2 position, LuxEngine::Vec2 size, LuxEngine::Vec4 tint, float angle = 0);
void Panel(LuxEngine::Vec2 center, LuxEngine::Vec2 size);
void BindMovement();
LuxEngine::Vec2 Movement();

class SampleApplication : public LuxEngine::Application {
public:
    SampleApplication(std::string title, std::string instructions, Options options);

    ~SampleApplication() override;

    int Run();

    float HalfWidth() const;

protected:
    virtual void Reset() = 0;
    virtual void Step(float dt) = 0;
    virtual void Draw(float elapsed) = 0;
    void OnInit() final;
    void OnUpdate(double dt) final;
    void OnRender(float elapsed) final;
    void OnShutdown() final;

private:
    void Capture();
    std::unique_ptr<Artwork> artwork_;
    std::string title_, instructions_;
    Options options_;
    LuxEngine::OrthographicCamera camera_;
    float halfWidth_ = 10.6667f;
    unsigned rendered_ = 0;
    bool paused_ = false;
};

template <class App> int RunSample(int argc, char** argv);
} // namespace LuxSamples

#include <Common/SampleApplication.inl>
