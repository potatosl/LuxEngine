#include "EngineSceneRenderer.hpp"
#include "Engine/Platform/Window.hpp"
#include "Engine/Renderer/RHI/GpuDevice.hpp"
#include "Engine/Renderer/Renderer2D/Renderer2D.hpp"
#include "Engine/Scene/Scene.hpp"
#include <QGuiApplication>
#include <SDL3/SDL.h>
#include <cstring>

namespace LuxEngine {
struct EngineSceneRenderer::State {
    bool sdlVideo = false;
    Window window;
    GpuDevice gpu;
    bool renderer = false;
    uint32 width = 0;
    uint32 height = 0;

    ~State() {
        if (renderer)
            Renderer2D::Shutdown();
        gpu.Shutdown();
        window.Shutdown();
        if (sdlVideo)
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
};

EngineSceneRenderer::EngineSceneRenderer() = default;
EngineSceneRenderer::~EngineSceneRenderer() = default;

bool EngineSceneRenderer::Allowed() {
    if (qEnvironmentVariableIsSet("LUX_EDITOR_SOFTWARE_VIEWPORT"))
        return false;
    const QString platform = QGuiApplication::platformName();
    return platform != QStringLiteral("offscreen") && platform != QStringLiteral("minimal");
}

bool EngineSceneRenderer::IsReady() const {
    return state_ && state_->gpu.IsReady();
}

bool EngineSceneRenderer::EnsureReady() {
    if (IsReady())
        return true;
    if (attempted_)
        return false;
    attempted_ = true;
    if (!Allowed()) {
        error_ = QStringLiteral("GPU 뷰포트가 꺼져 있습니다.");
        return false;
    }
    auto state = std::make_unique<State>();
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        error_ = QStringLiteral("SDL 비디오 초기화 실패: %1").arg(QString::fromUtf8(SDL_GetError()));
        return false;
    }
    state->sdlVideo = true;
    WindowProps props;
    props.title = "LuxEditor viewport";
    props.width = 640;
    props.height = 360;
    props.resizable = true;
    props.vulkan = true;
    props.hidden = true;
    props.vsync = false;
    if (!state->window.Initialize(props)) {
        error_ = QStringLiteral("엔진 창을 만들지 못했습니다: %1").arg(QString::fromUtf8(SDL_GetError()));
        return false;
    }
    GpuDeviceConfig config;
    config.vsync = false;
    config.framesInFlight = 1;
    if (!state->gpu.Initialize(state->window, config)) {
        error_ = QStringLiteral("Vulkan 장치 초기화 실패: %1")
                     .arg(QString::fromStdString(state->gpu.GetLastError()));
        return false;
    }
    Renderer2D::Init(&state->gpu);
    state->renderer = true;
    state->width = props.width;
    state->height = props.height;
    state_ = std::move(state);
    error_.clear();
    return true;
}

bool EngineSceneRenderer::Render(Scene& scene, int pixelWidth, int pixelHeight,
                                 const QRectF& worldBounds, QImage& output) {
    if (!EnsureReady() || pixelWidth <= 0 || pixelHeight <= 0)
        return false;
    auto& s = *state_;
    const auto width = static_cast<uint32>(pixelWidth);
    const auto height = static_cast<uint32>(pixelHeight);
    if (width != s.width || height != s.height) {
        if (!s.window.SetSize(width, height)) {
            error_ = QStringLiteral("엔진 창 크기를 바꾸지 못했습니다.");
            return false;
        }
        s.gpu.NotifyResize();
        s.width = width;
        s.height = height;
    }
    if (!s.gpu.RequestFrameCapture()) {
        error_ = QString::fromStdString(s.gpu.GetLastError());
        return false;
    }
    if (s.gpu.BeginFrame() != GpuDevice::FrameStart::Ready) {
        error_ = QStringLiteral("엔진 프레임을 시작하지 못했습니다: %1")
                     .arg(QString::fromStdString(s.gpu.GetLastError()));
        return false;
    }
    s.gpu.Clear(30.0f / 255.0f, 34.0f / 255.0f, 42.0f / 255.0f, 1.0f);
    const OrthographicCamera camera(static_cast<float>(worldBounds.left()),
                                    static_cast<float>(worldBounds.right()),
                                    static_cast<float>(worldBounds.bottom()),
                                    static_cast<float>(worldBounds.top()));
    Renderer2D::BeginScene(camera);
    scene.OnRender();
    Renderer2D::EndScene();
    if (!s.gpu.EndFrame()) {
        error_ = QString::fromStdString(s.gpu.GetLastError());
        return false;
    }
    GpuFrameCapture capture;
    if (!s.gpu.ReadFrameCapture(capture) || capture.width == 0 || capture.height == 0 ||
        capture.rgba.size() != static_cast<size_t>(capture.width) * capture.height * 4) {
        error_ = QStringLiteral("엔진 프레임을 읽지 못했습니다.");
        return false;
    }
    QImage image(static_cast<int>(capture.width), static_cast<int>(capture.height),
                 QImage::Format_RGBA8888);
    const auto rowBytes = static_cast<size_t>(capture.width) * 4;
    for (uint32 y = 0; y < capture.height; ++y)
        std::memcpy(image.scanLine(static_cast<int>(y)), capture.rgba.data() + y * rowBytes,
                    rowBytes);
    output = std::move(image);
    return true;
}
} // namespace LuxEngine
