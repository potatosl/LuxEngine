#include "SampleApplication.hpp"
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_video.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace LuxSamples {
namespace le = LuxEngine;
namespace {
Artwork* activeArtwork = nullptr;
}
Options ParseOptions(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view option(argv[index]);
        if (option == "--smoke-test")
            options.frames = 10;
        else if (option == "--help")
            options.help = true;
        else if ((option == "--frames" || option == "--capture") && index + 1 < argc) {
            const std::string_view value(argv[++index]);
            if (option == "--capture")
                options.capture = std::filesystem::path(
                    std::u8string(reinterpret_cast<const char8_t*>(value.data()), value.size()));
            else {
                const auto [end, error] =
                    std::from_chars(value.data(), value.data() + value.size(), options.frames);
                if (error != std::errc{} || end != value.data() + value.size() ||
                    options.frames == 0 || options.frames > 100000)
                    throw std::runtime_error("프레임 수는 1부터 100000까지 지정해 주세요.");
            }
        } else
            throw std::runtime_error("지원하지 않는 옵션입니다. --help로 사용법을 확인해 주세요.");
    }
    return options;
}
void Text(std::string_view text, le::Vec2 position, float pixel, le::Vec4 color) {
    if (activeArtwork)
        activeArtwork->Text(text, position, pixel, color);
}
void Sprite(const std::string& relative, le::Vec2 position, le::Vec2 size, le::Vec4 tint,
            float angle) {
    if (activeArtwork)
        activeArtwork->Sprite(relative, position, size, tint, angle);
}
void Quad(le::Vec2 position, le::Vec2 size, le::Vec4 tint, float angle) {
    if (activeArtwork)
        activeArtwork->Quad(position, size, tint, angle);
}
void Panel(le::Vec2 center, le::Vec2 size) {
    if (activeArtwork)
        activeArtwork->Panel(center, size);
}
void BindMovement() {
    for (auto [action, key] : {std::pair{"left", le::LuxKey::A},
                               {"left", le::LuxKey::Left},
                               {"right", le::LuxKey::D},
                               {"right", le::LuxKey::Right},
                               {"up", le::LuxKey::W},
                               {"up", le::LuxKey::Up},
                               {"down", le::LuxKey::S},
                               {"down", le::LuxKey::Down}})
        le::Input::BindAction(action, key);
    le::Input::BindAction("left", le::LuxGamepadButton::DpadLeft);
    le::Input::BindAction("right", le::LuxGamepadButton::DpadRight);
    le::Input::BindAction("up", le::LuxGamepadButton::DpadUp);
    le::Input::BindAction("down", le::LuxGamepadButton::DpadDown);
}
le::Vec2 Movement() {
    le::Vec2 move{float(le::Input::IsActionDown("right")) - float(le::Input::IsActionDown("left")),
                  float(le::Input::IsActionDown("up")) - float(le::Input::IsActionDown("down"))};
    const float length = std::hypot(move.x, move.y);
    return length > 1 ? move * (1 / length) : move;
}
SampleApplication::SampleApplication(std::string title, std::string instructions, Options options)
    : le::Application({title, 1280, 720}), title_(std::move(title)),
      instructions_(std::move(instructions)), options_(std::move(options)) {}
void SampleApplication::OnInit() {
    SDL_SetWindowMinimumSize(GetWindow().GetNativeHandle(), 640, 360);
    const char* base = SDL_GetBasePath();
    if (!base)
        throw std::runtime_error("샘플 리소스 경로를 찾지 못했습니다.");
    artwork_ = std::make_unique<Artwork>(
        GetGpuDevice(),
        std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(base))) / "assets");
    activeArtwork = artwork_.get();
    BindMovement();
    le::Input::BindAction("quit", le::LuxKey::Escape);
    le::Input::BindAction("pause", le::LuxKey::P);
    le::Input::BindAction("reset", le::LuxKey::R);
    Reset();
    le::Log::Info(title_ + " 샘플이 준비되었습니다. " + instructions_);
    if (!options_.capture.empty() && !GetGpuDevice().RequestFrameCapture())
        throw std::runtime_error("화면 캡처를 요청하지 못했습니다.");
}
void SampleApplication::OnUpdate(double dt) {
    if (le::Input::WasActionPressed("quit"))
        Close();
    if (le::Input::WasActionPressed("pause"))
        paused_ = !paused_;
    if (le::Input::WasActionPressed("reset")) {
        paused_ = false;
        Reset();
    }
    if (!paused_)
        Step(static_cast<float>(dt));
}
void SampleApplication::OnRender(float elapsed) {
    halfWidth_ =
        6.0f * float(GetWindow().GetWidth()) / float(std::max(1u, GetWindow().GetHeight()));
    camera_.SetProjection(-halfWidth_, halfWidth_, -6, 6);
    GetGpuDevice().Clear(.12f, .15f, .17f, 1);
    Draw(elapsed);
    Panel({0, 5.35f}, {halfWidth_ * 2, 1.3f});
    Text(title_, {-halfWidth_ + .4f, 5.75f}, .055f);
    Text(instructions_, {-halfWidth_ + .4f, 5.05f}, .04f);
    Panel({0, -5.6f}, {halfWidth_ * 2, .75f});
    Text("P PAUSE   R RESET   ESC EXIT", {-halfWidth_ + .4f, -5.5f}, .035f);
    if (paused_) {
        Panel({0, 0}, {5, 1.3f});
        Text("PAUSED", {-1.1f, .22f}, .065f);
    }
    artwork_->Flush(camera_.GetViewProjectionMatrix());
    ++rendered_;
}
int SampleApplication::Run() {
    for (;;) {
        const auto result = RunFrame();
        if (result == FrameResult::Error)
            return 1;
        if (result == FrameResult::Stopped)
            return options_.capture.empty() ? 0 : 1;
        if (result == FrameResult::Skipped) {
            SDL_Delay(1);
            continue;
        }
        if (rendered_ && !options_.capture.empty())
            Capture();
        if (options_.frames && rendered_ >= options_.frames)
            Close();
    }
}
void SampleApplication::Capture() {
    le::GpuFrameCapture image;
    if (!GetGpuDevice().ReadFrameCapture(image))
        throw std::runtime_error("화면 캡처를 읽지 못했습니다.");
    std::ofstream output(options_.capture, std::ios::binary | std::ios::trunc);
    output << "P6\n" << image.width << ' ' << image.height << "\n255\n";
    for (size_t index = 0; index < image.rgba.size(); index += 4)
        output.write(reinterpret_cast<const char*>(image.rgba.data() + index), 3);
    // Final buffered writes can fail at close even when every preceding write succeeded.
    output.close();
    if (!output)
        throw std::runtime_error("화면 캡처를 저장하지 못했습니다.");
    options_.capture.clear();
}
void SampleApplication::OnShutdown() {
    activeArtwork = nullptr;
    artwork_.reset();
}
} // namespace LuxSamples
