#include "ForestSurvivorApp.hpp"
#include "Engine/Audio/AudioEngine.hpp"
#include "Engine/Platform/Input.hpp"
#include "Engine/Renderer/Renderer2D/Renderer2D.hpp"
#include "Runtime/Runtime.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <utility>

namespace ForestGame {
namespace le = LuxEngine;
namespace {
constexpr std::array<const char*, 13> Actions{
    "Forest.Left",       "Forest.Right",    "Forest.Up",      "Forest.Down",    "Forest.Confirm",
    "Forest.Dash",       "Forest.Pause",    "Forest.Choice1", "Forest.Choice2", "Forest.Choice3",
    "Forest.Fullscreen", "Forest.Previous", "Forest.Next"};
bool Pressed(const char* action) {
    return le::Input::WasActionPressed(action);
}
} // namespace

ForestSurvivorApp::ForestSurvivorApp(RunOptions options)
    : le::Application({.name = "MOSSBOUND | 숲의 수호자", .width = 1280, .height = 720}),
      options_(std::move(options)), game_(options_.seed) {}

void ForestSurvivorApp::OnInit() {
    le::Runtime::PrintEngineInfo();
    SDL_SetWindowMinimumSize(GetWindow().GetNativeHandle(), 960, 540);
    pointerX_ = le::Input::GetMouseX();
    pointerY_ = le::Input::GetMouseY();
    le::Renderer2D::Init(&GetGpuDevice());
    const char* basePath = SDL_GetBasePath();
    if (!basePath)
        throw std::runtime_error("샘플 실행 경로를 확인할 수 없습니다.");
    const auto fromUtf8 = [](const std::string& path) {
        return std::filesystem::path(
            std::u8string(reinterpret_cast<const char8_t*>(path.data()), path.size()));
    };
    const auto assets =
        options_.assets.empty() ? fromUtf8(basePath) / "assets" : fromUtf8(options_.assets);
    renderer_.Initialize(GetGpuDevice(), assets);
    const std::array<const char*, 4> soundFiles{"click_001.wav", "drop_001.wav",
                                                "confirmation_001.wav", "error_001.wav"};
    for (size_t i = 0; i < soundFiles.size(); ++i) {
        const auto bytes = (assets / "InterfaceSounds/Audio" / soundFiles[i]).u8string();
        sounds_[i] = std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }
    audioReady_ = le::AudioEngine::Init();
    if (!audioReady_)
        le::Log::Warn("오디오 장치를 열지 못했습니다. 소리 없이 게임을 계속 실행합니다.");
    const auto bindMove = [](const char* action, le::LuxKey first, le::LuxKey second,
                             le::LuxGamepadButton pad) {
        le::Input::BindAction(action, first);
        le::Input::BindAction(action, second);
        le::Input::BindAction(action, pad);
    };
    bindMove("Forest.Left", le::LuxKey::A, le::LuxKey::Left, le::LuxGamepadButton::DpadLeft);
    bindMove("Forest.Right", le::LuxKey::D, le::LuxKey::Right, le::LuxGamepadButton::DpadRight);
    bindMove("Forest.Up", le::LuxKey::W, le::LuxKey::Up, le::LuxGamepadButton::DpadUp);
    bindMove("Forest.Down", le::LuxKey::S, le::LuxKey::Down, le::LuxGamepadButton::DpadDown);
    le::Input::BindAction("Forest.Confirm", le::LuxKey::Enter);
    le::Input::BindAction("Forest.Confirm", le::LuxGamepadButton::South);
    le::Input::BindAction("Forest.Dash", le::LuxKey::Space);
    le::Input::BindAction("Forest.Dash", le::LuxKey::LeftShift);
    le::Input::BindAction("Forest.Dash", le::LuxGamepadButton::West);
    le::Input::BindAction("Forest.Pause", le::LuxKey::P);
    le::Input::BindAction("Forest.Pause", le::LuxKey::Escape);
    le::Input::BindAction("Forest.Pause", le::LuxGamepadButton::Start);
    le::Input::BindAction("Forest.Choice1", le::LuxKey::Num1);
    le::Input::BindAction("Forest.Choice2", le::LuxKey::Num2);
    le::Input::BindAction("Forest.Choice3", le::LuxKey::Num3);
    le::Input::BindAction("Forest.Fullscreen", le::LuxKey::F11);
    le::Input::BindAction("Forest.Previous", le::LuxGamepadButton::LeftShoulder);
    le::Input::BindAction("Forest.Next", le::LuxGamepadButton::RightShoulder);
    if (options_.smokeTest || options_.autoplay || options_.showcaseSeconds > 0)
        game_.Start();
    if (options_.showcaseSeconds > 0) {
        for (int step = 0; step < static_cast<int>(options_.showcaseSeconds * 60); ++step) {
            if (options_.stopAtUpgrade && game_.GetPhase() == Phase::Upgrade)
                break;
            AdvanceAutoplay(1.0 / 60.0);
        }
    }
    lastPhase_ = game_.GetPhase();
    lastHealth_ = game_.GetPlayer().health;
    lastXp_ = game_.GetPlayer().xp;
    le::Log::Info("숲의 수호자 샘플이 준비되었습니다. 화면의 시작 버튼을 눌러 주세요.");
}

le::Vec2 ForestSurvivorApp::Movement() const {
    le::Vec2 movement{(le::Input::IsActionDown("Forest.Right") ? 1.0f : 0.0f) -
                          (le::Input::IsActionDown("Forest.Left") ? 1.0f : 0.0f),
                      (le::Input::IsActionDown("Forest.Up") ? 1.0f : 0.0f) -
                          (le::Input::IsActionDown("Forest.Down") ? 1.0f : 0.0f)};
    int count = 0;
    SDL_JoystickID* pads = SDL_GetGamepads(&count);
    for (int index = 0; index < count; ++index) {
        if (SDL_Gamepad* pad = SDL_GetGamepadFromID(pads[index])) {
            const float x = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f;
            const float y = -SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f;
            const float length = std::hypot(x, y);
            if (length > .18f) {
                const float scale = (std::min(1.0f, length) - .18f) / (.82f * length);
                movement += le::Vec2{x, y} * scale;
                break;
            }
        }
    }
    SDL_free(pads);
    return movement;
}

le::Vec2 ForestSurvivorApp::AutomaticMovement() const {
    const auto position = game_.GetPlayer().position;
    const float time = game_.Elapsed();
    // A reproducible, ordinary player path for visual captures and unattended GPU smoke runs.
    const le::Vec2 target{std::cos(time * .21f) * 6.0f, std::sin(time * .21f) * 4.5f};
    return target - position;
}

void ForestSurvivorApp::AdvanceAutoplay(double dt) {
    if (game_.GetPhase() == Phase::Upgrade)
        game_.ChooseUpgrade((game_.GetPlayer().level - 2) % 3);
    game_.Tick(dt, AutomaticMovement());
}

void ForestSurvivorApp::OnEvent(le::Event& event) {
    le::Application::OnEvent(event);
    le::EventDispatcher dispatcher(event);
    dispatcher.Dispatch<le::MouseMovedEvent>([this](const le::MouseMovedEvent& moved) {
        pointerX_ = moved.GetX();
        pointerY_ = moved.GetY();
        return false;
    });
    dispatcher.Dispatch<le::MouseButtonPressedEvent>(
        [this](const le::MouseButtonPressedEvent& button) {
            if (button.GetMouseButton() == static_cast<int>(le::LuxMouseButton::Left)) {
                pointerDown_ = true;
                downAction_ = ActionAt(pointerX_ / std::max(1u, GetWindow().GetWidth()),
                                       pointerY_ / std::max(1u, GetWindow().GetHeight()));
            }
            return false;
        });
    dispatcher.Dispatch<le::MouseButtonReleasedEvent>(
        [this](const le::MouseButtonReleasedEvent& button) {
            if (button.GetMouseButton() == static_cast<int>(le::LuxMouseButton::Left)) {
                const auto released = ActionAt(pointerX_ / std::max(1u, GetWindow().GetWidth()),
                                               pointerY_ / std::max(1u, GetWindow().GetHeight()));
                if (pointerDown_ && downAction_ != UiAction::None && released == downAction_)
                    queuedAction_ = released;
                downAction_ = UiAction::None;
                pointerDown_ = false;
            }
            return false;
        });
}

void ForestSurvivorApp::ExecuteButton(UiAction action) {
    const auto phase = game_.GetPhase();
    bool activated = false;
    switch (action) {
    case UiAction::Start:
        if (phase == Phase::Title) {
            game_.Start();
            activated = true;
        }
        break;
    case UiAction::Pause:
        if (phase == Phase::Playing) {
            game_.TogglePause();
            activated = true;
        }
        break;
    case UiAction::Resume:
        if (phase == Phase::Paused) {
            game_.TogglePause();
            activated = true;
        }
        break;
    case UiAction::Restart:
        if (phase == Phase::Paused || phase == Phase::Defeat || phase == Phase::Victory) {
            game_.Restart();
            activated = true;
        }
        break;
    case UiAction::Quit:
        Close();
        activated = true;
        break;
    case UiAction::Choice0:
    case UiAction::Choice1:
    case UiAction::Choice2:
        if (game_.ChooseUpgrade(static_cast<int>(action) - static_cast<int>(UiAction::Choice0))) {
            selectedChoice_ = 0;
            activated = true;
        }
        break;
    case UiAction::None:
        break;
    }
    if (activated && audioReady_)
        le::AudioEngine::PlaySound(sounds_[0], .10f);
}

void ForestSurvivorApp::ProcessControls() {
    if (Pressed("Forest.Fullscreen"))
        GetWindow().SetFullscreen(!GetWindow().IsFullscreen());
    const auto action = std::exchange(queuedAction_, UiAction::None);
    ExecuteButton(action);
    const auto phase = game_.GetPhase();
    // Menus start/restart/quit through their visible buttons. Keyboard shortcuts are gameplay aids.
    if (phase == Phase::Upgrade) {
        if (Pressed("Forest.Previous") || Pressed("Forest.Left"))
            selectedChoice_ = (selectedChoice_ + 2) % 3;
        if (Pressed("Forest.Next") || Pressed("Forest.Right"))
            selectedChoice_ = (selectedChoice_ + 1) % 3;
        int selected = -1;
        if (Pressed("Forest.Choice1"))
            selected = 0;
        if (Pressed("Forest.Choice2"))
            selected = 1;
        if (Pressed("Forest.Choice3"))
            selected = 2;
        if (Pressed("Forest.Confirm"))
            selected = selectedChoice_;
        if (selected >= 0 && game_.ChooseUpgrade(selected))
            selectedChoice_ = 0;
    } else if ((phase == Phase::Playing || phase == Phase::Paused) && Pressed("Forest.Pause")) {
        game_.TogglePause();
    }
}

void ForestSurvivorApp::SaveCapture() {
    le::GpuFrameCapture image;
    if (!GetGpuDevice().ReadFrameCapture(image))
        return;
    SDL_Surface* surface = SDL_CreateSurfaceFrom(
        static_cast<int>(image.width), static_cast<int>(image.height), SDL_PIXELFORMAT_RGBA32,
        image.rgba.data(), static_cast<int>(image.width * 4));
    if (!surface)
        throw std::runtime_error("캡처 이미지를 만들 수 없습니다.");
    const bool saved = SDL_SaveBMP(surface, options_.capture.c_str());
    SDL_DestroySurface(surface);
    if (!saved)
        throw std::runtime_error("캡처 이미지를 저장할 수 없습니다.");
    le::Log::Info("게임 화면 캡처를 저장했습니다.");
    Close();
}

void ForestSurvivorApp::UpdateSound(double dt) {
    soundCooldown_ = std::max(0.0f, soundCooldown_ - static_cast<float>(dt));
    const auto phase = game_.GetPhase();
    const auto& player = game_.GetPlayer();
    if (audioReady_ && phase != lastPhase_ &&
        (phase == Phase::Upgrade || phase == Phase::Victory || phase == Phase::Defeat)) {
        le::AudioEngine::PlaySound(sounds_[phase == Phase::Defeat ? 3 : 2], .15f);
        soundCooldown_ = .15f;
    } else if (audioReady_ && soundCooldown_ <= 0) {
        if (player.health < lastHealth_) {
            le::AudioEngine::PlaySound(sounds_[3], .12f);
            soundCooldown_ = .15f;
        } else if (player.xp > lastXp_) {
            le::AudioEngine::PlaySound(sounds_[1], .07f);
            soundCooldown_ = .12f;
        }
    }
    lastPhase_ = phase;
    lastHealth_ = player.health;
    lastXp_ = player.xp;
}

void ForestSurvivorApp::OnUpdate(double dt) {
    ProcessControls();
    if (options_.pauseOnFocusLoss && !GetWindow().HasFocus()) {
        pointerDown_ = false;
        downAction_ = queuedAction_ = UiAction::None;
        if (!options_.autoplay && !options_.smokeTest && game_.GetPhase() == Phase::Playing)
            game_.TogglePause();
    }
    if (options_.autoplay)
        AdvanceAutoplay(dt);
    else
        game_.Tick(dt, Movement(), Pressed("Forest.Dash"));
    UpdateSound(dt);
    if (options_.maxFrames != 0 && ++frames_ >= options_.maxFrames) {
        if (options_.capture.empty())
            Close();
        else if (captureRequested_)
            SaveCapture();
        else if (GetGpuDevice().RequestFrameCapture())
            captureRequested_ = true;
        else
            throw std::runtime_error("게임 화면 캡처를 요청할 수 없습니다.");
    }
}

void ForestSurvivorApp::OnRender(float elapsedSeconds) {
    const auto width = GetWindow().GetWidth();
    const auto height = GetWindow().GetHeight();
    renderer_.SetSelectedChoice(selectedChoice_);
    const float x = pointerX_ / std::max(1u, width), y = pointerY_ / std::max(1u, height);
    renderer_.SetPointer(x, y, pointerDown_ && downAction_ == ActionAt(x, y));
    renderer_.Render(game_, elapsedSeconds, static_cast<float>(width) / std::max(1u, height));
}

void ForestSurvivorApp::OnShutdown() {
    for (const char* action : Actions)
        le::Input::ClearAction(action);
    renderer_.Shutdown();
    le::AudioEngine::Shutdown();
    audioReady_ = false;
    le::Renderer2D::Shutdown();
    le::Log::Info("숲의 수호자 샘플을 종료했습니다.");
}
} // namespace ForestGame
