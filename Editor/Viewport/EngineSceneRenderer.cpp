#include "ViewportCamera3D.hpp"
#include "EngineSceneRenderer.hpp"
#include "Engine/ECS/ECS.hpp"
#include "Engine/Platform/Window.hpp"
#include "Engine/Renderer/RHI/GpuDevice.hpp"
#include "Engine/Renderer/Renderer2D/Renderer2D.hpp"
#include "Engine/Renderer/Renderer3D/Renderer3D.hpp"
#include "Engine/Scene/Entity.hpp"
#include "Engine/Scene/Scene.hpp"
#include <QGuiApplication>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <exception>
#include <mutex>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace LuxEngine {
namespace {
constexpr uint32 kMaxDeviceRecoveries = 3;
constexpr int kMaxSwapchainAcquireRetries = 3;
std::atomic<bool> s_ForceAllowed{false};
std::atomic<int> s_DefaultValidation{static_cast<int>(GpuValidation::Auto)};

// Keeps SDL video subsystem, vulkan-1.dll loader, and (when validation is active) the Khronos
// validation layer module resident across repeated viewport teardown/re-initialization cycles so
// that vkDestroyInstance / SDL_DestroyWindow do not repeatedly unload and reload the Vulkan loader
// and validation layer DLLs into new VirtualAlloc arenas.
struct ProcessVulkanSubsystemRetention {
    std::mutex mutex;
    bool sdlVideoRetained = false;
    bool vulkanLoaderRetained = false;
#ifdef _WIN32
    HMODULE validationLayerModule = nullptr;
#endif

    void RetainLoaderAndVideo() {
        std::lock_guard<std::mutex> lock(mutex);
        if (!sdlVideoRetained) {
            if (SDL_InitSubSystem(SDL_INIT_VIDEO))
                sdlVideoRetained = true;
        }
        if (sdlVideoRetained && !vulkanLoaderRetained) {
            if (SDL_Vulkan_LoadLibrary(nullptr))
                vulkanLoaderRetained = true;
        }
    }

    void RetainValidationLayerIfLoaded() {
#ifdef _WIN32
        std::lock_guard<std::mutex> lock(mutex);
        if (!validationLayerModule) {
            GetModuleHandleExW(0, L"VkLayer_khronos_validation.dll", &validationLayerModule);
        }
#endif
    }

    ~ProcessVulkanSubsystemRetention() {
#ifdef _WIN32
        if (validationLayerModule) {
            FreeLibrary(validationLayerModule);
            validationLayerModule = nullptr;
        }
#endif
        if (vulkanLoaderRetained) {
            SDL_Vulkan_UnloadLibrary();
            vulkanLoaderRetained = false;
        }
        if (sdlVideoRetained) {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            sdlVideoRetained = false;
        }
    }
};

ProcessVulkanSubsystemRetention& GetProcessRetention() {
    static ProcessVulkanSubsystemRetention s_Retention;
    return s_Retention;
}

// Selectively drains window-specific events for the viewport SDL window without purging global events
// or affecting other SDL windows / consumers (e.g. keyboard, mouse, or runtime player windows).
// If pump is false, only filters already-queued events without pumping Win32 OS messages,
// preventing re-entrant message dispatch during active Qt paint events.
void DrainWindowEvents(SDL_Window* window, bool pump = false) {
    if (!window)
        return;
    const SDL_WindowID targetId = SDL_GetWindowID(window);
    if (targetId == 0)
        return;
    if (pump)
        SDL_PumpEvents();
    SDL_FilterEvents(
        [](void* userdata, SDL_Event* event) -> bool {
            const SDL_WindowID id = *static_cast<SDL_WindowID*>(userdata);
            if (event->type >= SDL_EVENT_WINDOW_FIRST && event->type <= SDL_EVENT_WINDOW_LAST) {
                if (event->window.windowID == id)
                    return false; // Drop only events targeted to this specific viewport window
            }
            return true; // Keep all other events intact for other windows and subsystems
        },
        const_cast<SDL_WindowID*>(&targetId));
}
} // namespace

struct EngineSceneRenderer::State {
    bool sdlVideo = false;
    Window window;
    GpuDevice gpu;
    Renderer2D::Context context2D;
    Renderer3D::Context context3D;
    uint32 width = 0;
    uint32 height = 0;
    GpuFrameCapture capture;

    ~State() {
        context2D.Shutdown();
        context3D.Shutdown();
        gpu.Shutdown();
        DrainWindowEvents(window.GetNativeHandle(), true);
        window.Shutdown();
        if (sdlVideo)
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
};

EngineSceneRenderer::EngineSceneRenderer() = default;
EngineSceneRenderer::~EngineSceneRenderer() {
    Shutdown();
}

void EngineSceneRenderer::SetForceAllowed(bool force) {
    s_ForceAllowed.store(force);
}

void EngineSceneRenderer::SetDefaultValidation(int validationMode) {
    s_DefaultValidation.store(validationMode);
}

bool EngineSceneRenderer::Allowed() {
    if (qEnvironmentVariableIsSet("LUX_EDITOR_SOFTWARE_VIEWPORT"))
        return false;
    if (s_ForceAllowed.load() || qEnvironmentVariableIsSet("LUX_EDITOR_FORCE_GPU_VIEWPORT"))
        return true;
    const QString platform = QGuiApplication::platformName();
    return platform != QStringLiteral("offscreen") && platform != QStringLiteral("minimal");
}

bool EngineSceneRenderer::IsReady() const {
    return state_ && state_->gpu.IsReady();
}

GpuDevice* EngineSceneRenderer::GetGpuDevice() noexcept {
    return state_ ? &state_->gpu : nullptr;
}

const GpuDevice* EngineSceneRenderer::GetGpuDevice() const noexcept {
    return state_ ? &state_->gpu : nullptr;
}

void EngineSceneRenderer::Shutdown() {
    if (rendering_) {
        pendingShutdown_ = true;
        return;
    }
    pendingShutdown_ = false;
    state_.reset();
}

void EngineSceneRenderer::Reset() {
    if (rendering_) {
        pendingShutdown_ = true;
        return;
    }
    pendingShutdown_ = false;
    state_.reset();
    attempted_ = false;
    recoveries_ = 0;
    error_.clear();
}

bool EngineSceneRenderer::InitializeState(uint32 initialWidth, uint32 initialHeight) {
    GetProcessRetention().RetainLoaderAndVideo();
    auto state = std::make_unique<State>();
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        error_ = QStringLiteral("SDL 비디오 초기화 실패: %1").arg(QString::fromUtf8(SDL_GetError()));
        return false;
    }
    state->sdlVideo = true;
    WindowProps props;
    props.title = "LuxEditor viewport";
    props.width = initialWidth > 0 ? initialWidth : 640;
    props.height = initialHeight > 0 ? initialHeight : 360;
    props.resizable = true;
    props.vulkan = true;
    props.hidden = true;
    props.vsync = false;
    if (!state->window.Initialize(props)) {
        error_ =
            QStringLiteral("엔진 창을 만들지 못했습니다: %1").arg(QString::fromUtf8(SDL_GetError()));
        return false;
    }
    DrainWindowEvents(state->window.GetNativeHandle(), false);
    GpuDeviceConfig config;
    config.vsync = false;
    config.framesInFlight = 1;
    config.validation = static_cast<GpuValidation>(s_DefaultValidation.load());
    if (!state->gpu.Initialize(state->window, config)) {
        error_ = QStringLiteral("Vulkan 장치 초기화 실패: %1")
                     .arg(QString::fromStdString(state->gpu.GetLastError()));
        return false;
    }
    if (state->gpu.GetInfo().validation) {
        GetProcessRetention().RetainValidationLayerIfLoaded();
    }
    state->context2D.Init(&state->gpu);
    state->context3D.Init(&state->gpu);
    state->width = props.width;
    state->height = props.height;
    state_ = std::move(state);
    error_.clear();
    return true;
}

bool EngineSceneRenderer::EnsureReady() {
    if (IsReady())
        return true;
    if (!Allowed()) {
        error_ = QStringLiteral("GPU 뷰포트가 꺼져 있습니다.");
        return false;
    }
    // If a previously initialized device entered DeviceLost or Error state, attempt bounded recovery.
    if (state_ && !state_->gpu.IsReady()) {
        if (recoveries_ >= kMaxDeviceRecoveries) {
            error_ = QStringLiteral("GPU 장치 복구 한도를 초과했습니다: %1")
                         .arg(QString::fromStdString(state_->gpu.GetLastError()));
            return false;
        }
        const uint32 prevW = state_->width > 0 ? state_->width : 640;
        const uint32 prevH = state_->height > 0 ? state_->height : 360;
        ++recoveries_;
        state_.reset();
        return InitializeState(prevW, prevH);
    }
    if (attempted_)
        return false;
    attempted_ = true;
    return InitializeState(640, 360);
}

bool EngineSceneRenderer::BeginViewportFrame(uint32 width, uint32 height) {
    for (int recoveryAttempt = 0; recoveryAttempt < 2; ++recoveryAttempt) {
        if (!IsReady() && Allowed() && !attempted_ && !state_) {
            attempted_ = true;
            if (!InitializeState(width, height))
                return false;
        }
        if (!EnsureReady() || !state_)
            return false;
        auto& s = *state_;
        if (width != s.width || height != s.height) {
            if (!s.window.SetSize(width, height)) {
                error_ = QStringLiteral("엔진 창 크기를 바꾸지 못했습니다.");
                return false;
            }
            DrainWindowEvents(s.window.GetNativeHandle(), false);
            s.gpu.NotifyResize();
            s.width = width;
            s.height = height;
        }
        if (!s.gpu.RequestFrameCapture()) {
            error_ = QString::fromStdString(s.gpu.GetLastError());
            return false;
        }
        GpuDevice::FrameStart start = GpuDevice::FrameStart::Skipped;
        for (int retry = 0; retry < kMaxSwapchainAcquireRetries; ++retry) {
            start = s.gpu.BeginFrame();
            if (start == GpuDevice::FrameStart::Ready || start == GpuDevice::FrameStart::Error ||
                !s.gpu.IsReady())
                break;
        }
        if (start == GpuDevice::FrameStart::Ready)
            return true;
        error_ = QStringLiteral("엔진 프레임을 시작하지 못했습니다: %1")
                     .arg(QString::fromStdString(s.gpu.GetLastError()));
        if (s.gpu.IsReady())
            return false;
        // Device became unusable (e.g. DeviceLost); loop once more to trigger EnsureReady() recovery.
    }
    return false;
}

bool EngineSceneRenderer::ReadbackToImage(QImage& output) {
    if (!state_)
        return false;
    auto& s = *state_;
    if (!s.gpu.ReadFrameCapture(s.capture) || s.capture.width == 0 || s.capture.height == 0 ||
        s.capture.rgba.size() != static_cast<size_t>(s.capture.width) * s.capture.height * 4) {
        error_ = QStringLiteral("엔진 프레임을 읽지 못했습니다.");
        return false;
    }
    const auto captureWidth = static_cast<int>(s.capture.width);
    const auto captureHeight = static_cast<int>(s.capture.height);
    if (output.size() != QSize(captureWidth, captureHeight) ||
        output.format() != QImage::Format_RGBA8888) {
        output = QImage(captureWidth, captureHeight, QImage::Format_RGBA8888);
    }
    if (output.isNull()) {
        error_ = QStringLiteral("뷰포트 이미지 버퍼를 할당하지 못했습니다.");
        return false;
    }
    const auto rowBytes = static_cast<size_t>(s.capture.width) * 4;
    if (static_cast<size_t>(output.bytesPerLine()) == rowBytes) {
        std::memcpy(output.bits(), s.capture.rgba.data(), s.capture.rgba.size());
    } else {
        for (uint32 y = 0; y < s.capture.height; ++y)
            std::memcpy(output.scanLine(static_cast<int>(y)), s.capture.rgba.data() + y * rowBytes,
                        rowBytes);
    }
    return true;
}

bool EngineSceneRenderer::Render(Scene& scene, int pixelWidth, int pixelHeight,
                                 const QRectF& worldBounds, QImage& output) {
    if (rendering_ || pixelWidth <= 0 || pixelHeight <= 0 || pixelWidth > 16384 ||
        pixelHeight > 16384)
        return false;
    struct RenderScope {
        EngineSceneRenderer& self;
        ~RenderScope() {
            self.rendering_ = false;
            if (self.pendingShutdown_)
                self.Shutdown();
        }
    } renderScope{*this};
    rendering_ = true;

    const auto width = static_cast<uint32>(pixelWidth);
    const auto height = static_cast<uint32>(pixelHeight);
    if (!BeginViewportFrame(width, height))
        return false;

    auto& s = *state_;
    s.gpu.Clear(30.0f / 255.0f, 34.0f / 255.0f, 42.0f / 255.0f, 1.0f);
    const OrthographicCamera camera(static_cast<float>(worldBounds.left()),
                                    static_cast<float>(worldBounds.right()),
                                    static_cast<float>(worldBounds.bottom()),
                                    static_cast<float>(worldBounds.top()));
    s.context2D.Init(&s.gpu);
    Renderer2D::ScopedContext scope2D(s.context2D);
    try {
        Renderer2D::BeginScene(camera);
        scene.OnRender();
        if (postSceneHook_)
            postSceneHook_();
        Renderer2D::EndScene();
    } catch (const std::exception& exception) {
        s.context2D.AbortScene();
        s.gpu.EndFrame();
        error_ = QStringLiteral("엔진 렌더링 오류: %1").arg(QString::fromUtf8(exception.what()));
        return false;
    } catch (...) {
        s.context2D.AbortScene();
        s.gpu.EndFrame();
        error_ = QStringLiteral("엔진 렌더링 중 알 수 없는 예외가 발생했습니다.");
        return false;
    }
    if (!s.gpu.EndFrame()) {
        error_ = QString::fromStdString(s.gpu.GetLastError());
        return false;
    }
    return ReadbackToImage(output);
}

bool EngineSceneRenderer::Render3D(Scene& scene, int pixelWidth, int pixelHeight,
                                   const Vec3& focus, double yaw, double pitch, double distance,
                                   QImage& output) {
    if (rendering_ || pixelWidth <= 0 || pixelHeight <= 0 || pixelWidth > 16384 ||
        pixelHeight > 16384)
        return false;
    struct RenderScope {
        EngineSceneRenderer& self;
        ~RenderScope() {
            self.rendering_ = false;
            if (self.pendingShutdown_)
                self.Shutdown();
        }
    } renderScope{*this};
    rendering_ = true;

    const auto width = static_cast<uint32>(pixelWidth);
    const auto height = static_cast<uint32>(pixelHeight);
    if (!BeginViewportFrame(width, height))
        return false;

    auto& s = *state_;
    s.gpu.Clear(27.0f / 255.0f, 33.0f / 255.0f, 42.0f / 255.0f, 1.0f);

    const auto camera = MakeViewportCamera3D(QSizeF(width, height), focus, yaw, pitch, distance);

    DirectionalLight sun;
    sun.direction = Vec3(0.5f, -1.0f, 0.5f).Normalized();
    sun.color = {1.0f, 0.98f, 0.95f};
    sun.intensity = 1.0f;

    s.context3D.Init(&s.gpu);
    Renderer3D::ScopedContext scope3D(s.context3D);
    try {
        Renderer3D::BeginScene(camera, sun);
        for (Entity entity : scene.GetEntities()) {
            if (!entity.HasComponent<TransformComponent>())
                continue;
            const auto& transform = entity.GetComponent<TransformComponent>();
            if (!std::isfinite(transform.position.x) || !std::isfinite(transform.position.y) ||
                !std::isfinite(transform.position.z) || !std::isfinite(transform.rotation.x) ||
                !std::isfinite(transform.rotation.y) || !std::isfinite(transform.rotation.z) ||
                !std::isfinite(transform.scale.x) || !std::isfinite(transform.scale.y) ||
                !std::isfinite(transform.scale.z))
                continue;

            if (entity.HasComponent<MeshRendererComponent>()) {
                const auto& meshComp = entity.GetComponent<MeshRendererComponent>();
                Renderer3D::DrawMesh(transform.GetTransform(), meshComp.material);
            } else if (entity.HasComponent<SpriteRendererComponent>()) {
                const auto& sprite = entity.GetComponent<SpriteRendererComponent>();
                Material3D mat;
                mat.albedoColor = sprite.color;
                Renderer3D::DrawMesh(
                    Mat4::Translate(transform.position) * Mat4::RotateZ(transform.rotation.z) *
                        Mat4::Scale(Vec3{transform.scale.x, transform.scale.y, 0.02f}),
                    mat);
            }
        }
        Renderer3D::EndScene();
    } catch (const std::exception& exception) {
        s.context3D.AbortScene();
        s.gpu.EndFrame();
        error_ =
            QStringLiteral("엔진 3D 렌더링 오류: %1").arg(QString::fromUtf8(exception.what()));
        return false;
    } catch (...) {
        s.context3D.AbortScene();
        s.gpu.EndFrame();
        error_ = QStringLiteral("엔진 3D 렌더링 중 알 수 없는 예외가 발생했습니다.");
        return false;
    }
    if (!s.gpu.EndFrame()) {
        error_ = QString::fromStdString(s.gpu.GetLastError());
        return false;
    }
    return ReadbackToImage(output);
}
} // namespace LuxEngine
