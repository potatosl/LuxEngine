#include <RainyCreek/Capture.hpp>
#include <RainyCreek/Catalog.hpp>
#include <RainyCreek/CreekApplication.hpp>
#include <SDL3/SDL_timer.h>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace RainyCreek {
namespace le = LuxEngine;

CreekApplication::CreekApplication(Options options, Catalog catalog)
    : Application({"Rainy Creek | LuxEngine", options.width, options.height}),
      options_(std::move(options)), catalog_(std::move(catalog)) {}

CreekApplication::~CreekApplication() {
    Shutdown();
}

void CreekApplication::OnInit() {
    renderer_.Initialize(GetGpuDevice(), std::move(catalog_));
    for (unsigned i = 0; i < static_cast<unsigned>(options_.lighting); ++i)
        state_.Apply(Command::Lighting);
    if (options_.fixedTime)
        state_.SetTime(*options_.fixedTime);
    environmentWait_ = std::chrono::steady_clock::now();
    GetWindow().SetVSync(!options_.smoke);
}

void CreekApplication::Execute(Command command) {
    state_.Apply(command);
    if (command == Command::Reset)
        orbit_.Reset();
}

void CreekApplication::OnEvent(le::Event& event) {
    // 닫기/크기 변경은 기본 Application 처리도 반드시 거칩니다.
    Application::OnEvent(event);
    le::EventDispatcher dispatcher(event);
    dispatcher.Dispatch<le::MouseMovedEvent>([&](le::MouseMovedEvent& moved) {
        // 현재 Window API는 focus-lost 콜백을 발행하지 않으므로 실제 상태로 드래그를 해제합니다.
        if (!GetWindow().HasFocus() || !le::Input::IsMouseButtonPressed(le::LuxMouseButton::Left))
            orbiting_ = false;
        if (!GetWindow().HasFocus() || !le::Input::IsMouseButtonPressed(le::LuxMouseButton::Middle))
            panning_ = false;
        const float dx = moved.GetX() - mouseX_;
        const float dy = moved.GetY() - mouseY_;
        mouseX_ = moved.GetX();
        mouseY_ = moved.GetY();
        if (orbiting_)
            orbit_.Drag(dx, dy);
        if (panning_)
            orbit_.Pan(dx, dy);
        return orbiting_ || panning_;
    });
    dispatcher.Dispatch<le::MouseButtonPressedEvent>([&](le::MouseButtonPressedEvent& pressed) {
        if (!GetWindow().HasFocus())
            return false;
        const auto [x, y] = le::Input::GetMousePosition();
        mouseX_ = x;
        mouseY_ = y;
        if (pressed.GetMouseButton() == static_cast<int>(le::LuxMouseButton::Left)) {
            orbiting_ = true;
        } else if (pressed.GetMouseButton() == static_cast<int>(le::LuxMouseButton::Middle))
            panning_ = true;
        return orbiting_ || panning_;
    });
    dispatcher.Dispatch<le::MouseButtonReleasedEvent>([&](le::MouseButtonReleasedEvent& released) {
        if (released.GetMouseButton() == static_cast<int>(le::LuxMouseButton::Left))
            orbiting_ = false;
        if (released.GetMouseButton() == static_cast<int>(le::LuxMouseButton::Middle))
            panning_ = false;
        return false;
    });
    dispatcher.Dispatch<le::MouseScrolledEvent>([&](le::MouseScrolledEvent& scrolled) {
        orbit_.Zoom(scrolled.GetYOffset());
        return true;
    });
    dispatcher.Dispatch<le::KeyPressedEvent>([&](le::KeyPressedEvent& pressed) {
        if (pressed.IsRepeat())
            return false;
        switch (static_cast<le::LuxKey>(pressed.GetKeyCode())) {
        case le::LuxKey::Space:
            Execute(Command::Pause);
            break;
        case le::LuxKey::Escape:
        case le::LuxKey::R:
            Execute(Command::Reset);
            break;
        case le::LuxKey::L:
            Execute(Command::Lighting);
            break;
        case le::LuxKey::P:
            Execute(Command::Lamps);
            break;
        case le::LuxKey::S:
            Execute(Command::Shadows);
            break;
        case le::LuxKey::W:
            Execute(Command::Rain);
            break;
        case le::LuxKey::H:
            Execute(Command::Water);
            break;
        case le::LuxKey::G:
            Execute(Command::Wind);
            break;
        case le::LuxKey::J:
            Execute(Command::Lod);
            break;
        case le::LuxKey::Equals:
            state_.AdjustExposure(.1f);
            break;
        case le::LuxKey::Minus:
            state_.AdjustExposure(-.1f);
            break;
        default:
            return false;
        }
        return true;
    });
    if (event.GetEventType() == le::EventType::WindowLostFocus)
        orbiting_ = panning_ = false;
}

void CreekApplication::OnUpdate(double dt) {
    if (!options_.fixedTime && !options_.smoke)
        state_.Update(dt);
}

void CreekApplication::SmokeStep() {
    const auto press = [&](le::LuxKey key) {
        le::KeyPressedEvent event(static_cast<int>(key));
        OnEvent(event);
    };
    switch (rendered_) {
    case 5:
        press(le::LuxKey::Space);
        orbit_.Drag(55, 20);
        break;
    case 9:
        press(le::LuxKey::Space);
        orbit_.Zoom(2);
        break;
    case 13:
        press(le::LuxKey::P);
        press(le::LuxKey::W);
        break;
    case 18:
        press(le::LuxKey::H);
        press(le::LuxKey::G);
        break;
    case 23:
        press(le::LuxKey::S);
        press(le::LuxKey::L);
        break;
    case 28:
        press(le::LuxKey::J);
        GetWindow().SetSize(960, 640);
        break;
    case 34:
        orbit_.Pan(25, -18);
        break;
    case 40:
        GetWindow().SetSize(options_.width, options_.height);
        break;
    case 48:
        press(le::LuxKey::Escape);
        break;
    default:
        break;
    }
    if (!options_.fixedTime && !state_.GetSettings().paused)
        state_.Update(1.0 / 60);
}

void CreekApplication::OnRender(float) {
    const bool environmentReady = renderer_.EnvironmentReady();
    if (options_.smoke && environmentReady)
        SmokeStep();
    const bool capture =
        environmentReady && !options_.capture.empty() && rendered_ + 1 >= options_.frames;
    if (capture && !GetGpuDevice().RequestFrameCapture())
        throw std::runtime_error("GPU 프레임 캡처를 요청하지 못했습니다.");
    GetGpuDevice().Clear(.015f, .025f, .045f, 1);
    const auto width = GetWindow().GetWidth();
    const auto height = GetWindow().GetHeight();
    renderer_.Draw(state_, orbit_.Camera(static_cast<float>(width) / std::max(1u, height)));
    if (renderer_.Statistics().meshCount < 160)
        throw std::runtime_error("Rainy Creek의 일부 장면 메시가 누락되었습니다.");
    captureReady_ = capture && renderer_.EnvironmentReady();
    if (environmentReady)
        ++rendered_;
    if (renderer_.EnvironmentReady()) {
        environmentWait_ = std::chrono::steady_clock::now();
    } else if (std::chrono::steady_clock::now() - environmentWait_ > std::chrono::seconds(45))
        throw std::runtime_error("Rainy Creek HDR 환경맵 준비 시간을 초과했습니다.");
}

int CreekApplication::RunSample() {
    auto lastReady = std::chrono::steady_clock::now();
    for (;;) {
        const auto result = RunFrame();
        if (result == FrameResult::Error)
            return 1;
        if (result == FrameResult::Stopped)
            return options_.capture.empty() ? 0 : 1;
        if (result == FrameResult::Skipped) {
            if (options_.frames &&
                std::chrono::steady_clock::now() - lastReady > std::chrono::seconds(30))
                throw std::runtime_error("GPU 프레임 준비 시간을 초과했습니다.");
            SDL_Delay(1);
            continue;
        }
        lastReady = std::chrono::steady_clock::now();
        if (options_.frames && rendered_ >= options_.frames && renderer_.EnvironmentReady() &&
            (options_.capture.empty() || captureReady_)) {
            if (!options_.capture.empty())
                WriteCapture(GetGpuDevice(), options_.capture);
            const auto counters = GetGpuDevice().GetCounters();
            if (options_.smoke || !options_.capture.empty())
                std::cout << "Rainy Creek: " << counters.framesSubmitted
                          << " 프레임, GPU 검증 오류 " << counters.validationErrors << "개\n";
            return counters.validationErrors == 0 ? 0 : 1;
        }
    }
}

void CreekApplication::OnShutdown() {
    renderer_.Shutdown();
    orbiting_ = panning_ = false;
}

int RunRainyCreek(int argc, char** argv) {
    auto options = ParseOptions(argc, argv);
    if (options.help) {
        PrintHelp();
        return 0;
    }
    auto catalog = LoadCatalog(options.assets);
    if (options.validate) {
        ValidateCatalog(catalog);
        std::cout << "Rainy Creek 모델·텍스처·환경맵 검사를 통과했습니다.\n";
        return 0;
    }
    CreekApplication app(std::move(options), std::move(catalog));
    return app.Initialize() ? app.RunSample() : 1;
}

} // namespace RainyCreek
