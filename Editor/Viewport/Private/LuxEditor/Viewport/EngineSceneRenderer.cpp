#include <LuxEditor/Viewport/EngineSceneRenderer.hpp>
#include <LuxEditor/Viewport/ViewportCamera3D.hpp>
#include <LuxEngine/ECS/ECS.hpp>
#include <LuxEngine/Platform/Window.hpp>
#include <LuxEngine/Renderer/RHI/GpuDevice.hpp>
#include <LuxEngine/Renderer/Renderer2D/Renderer2D.hpp>
#include <LuxEngine/Renderer/Renderer3D/Renderer3D.hpp>
#include <LuxEngine/Runtime/ScenePlayer.hpp>
#include <LuxEngine/Scene/Entity.hpp>
#include <LuxEngine/Scene/Scene.hpp>
#include <LuxUI/Widgets.hpp>
#include <SDL3/SDL.h>
#include <chrono>
#if defined(__linux__)
#include <SDL3/SDL_vulkan.h>
#endif
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <exception>
#include <mutex>

namespace LuxEditor {
namespace {
constexpr LuxEngine::uint32 kMaxDeviceRecoveries = 3;
constexpr int kMaxSwapchainAcquireRetries = 3;
std::atomic<bool> s_ForceAllowed{false};
std::atomic<int> s_DefaultValidation{static_cast<int>(LuxEngine::GpuValidation::Auto)};

// Keep SDL video alive across viewport rebuilds. On Linux, also retain the Vulkan loader
// used by The Forge. The D3D12 and Metal paths do not require a Vulkan installation.
struct ProcessViewportSubsystemRetention {
    std::mutex mutex;
    bool sdlVideoRetained = false;
#if defined(__linux__)
    bool vulkanLoaderRetained = false;
#endif

    void RetainLoaderAndVideo() {
        std::lock_guard<std::mutex> lock(mutex);
        if (!sdlVideoRetained && SDL_InitSubSystem(SDL_INIT_VIDEO))
            sdlVideoRetained = true;
#if defined(__linux__)
        if (sdlVideoRetained && !vulkanLoaderRetained && SDL_Vulkan_LoadLibrary(nullptr))
            vulkanLoaderRetained = true;
#endif
    }

    ~ProcessViewportSubsystemRetention() {
#if defined(__linux__)
        if (vulkanLoaderRetained)
            SDL_Vulkan_UnloadLibrary();
#endif
        if (sdlVideoRetained)
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
};

ProcessViewportSubsystemRetention& GetProcessRetention() {
    static ProcessViewportSubsystemRetention retention;
    return retention;
}

// Selectively drains window-specific events for the viewport SDL window without purging global
// events or affecting other SDL windows / consumers (e.g. keyboard, mouse, or runtime player
// windows). If pump is false, only filters already-queued events without pumping Win32 OS messages,
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

struct EngineSceneRenderer::MeshPreview {
    LuxEngine::uint32 entity = 0;
    std::vector<LuxEngine::Vertex3D> vertices;
    std::vector<LuxEngine::uint32> indices;
    LuxEngine::uint64 revision = 1;
    LuxEngine::uint64 topology = 1;
};

struct EngineSceneRenderer::State {
    bool sdlVideo = false;
    LuxEngine::Window window;
    LuxEngine::GpuDevice gpu;
    LuxEngine::Renderer2D::Context context2D;
    LuxEngine::Renderer3D::Context context3D;
    LuxEngine::uint32 width = 0;
    LuxEngine::uint32 height = 0;
    LuxEngine::GpuFrameCapture capture;
    LuxEngine::Mesh3D previewMesh;
    LuxEngine::uint64 previewRevision = 0;
    LuxEngine::uint64 previewTopology = 0;

    ~State() {
        previewMesh.Release();
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

bool EngineSceneRenderer::SetMeshPreview(LuxEngine::uint32 entity,
                                         std::span<const LuxEngine::Vertex3D> vertices,
                                         std::span<const LuxEngine::uint32> indices) {
    if (entity == 0 || vertices.empty() || vertices.size() > 1'000'000 || indices.empty() ||
        indices.size() > 3'000'000 || indices.size() % 3 != 0)
        return false;
    for (const auto index : indices)
        if (index >= vertices.size())
            return false;
    for (const auto& v : vertices)
        for (const auto value : {v.position.x, v.position.y, v.position.z, v.normal.x, v.normal.y,
                                 v.normal.z, v.texCoords.x, v.texCoords.y})
            if (!std::isfinite(value))
                return false;
    auto preview = std::make_unique<MeshPreview>();
    preview->entity = entity;
    preview->vertices.assign(vertices.begin(), vertices.end());
    preview->indices.assign(indices.begin(), indices.end());
    preview->revision = ++previewRevision_;
    preview->topology = preview->revision;
    if (meshPreview_) {
        if (meshPreview_->vertices.size() == vertices.size() &&
            meshPreview_->indices == preview->indices)
            preview->topology = meshPreview_->topology;
    }
    meshPreview_ = std::move(preview);
    return true;
}

void EngineSceneRenderer::ClearMeshPreview() {
    meshPreview_.reset();
    if (state_ && !rendering_) {
        state_->previewMesh.Release();
        state_->previewRevision = 0;
        state_->previewTopology = 0;
    }
}

GameViewCamera ResolveGameViewCamera(LuxEngine::Scene& scene, int width, int height) {
    GameViewCamera result;
    const float aspect = static_cast<float>(std::max(1, width)) / std::max(1, height);
    auto view = LuxEngine::FitSceneView(scene);
    float rotation = 0;
    auto cameras = scene.GetComponentEntityIds<LuxEngine::CameraComponent>();
    std::sort(cameras.begin(), cameras.end());
    bool foundCamera = false;
    for (const auto id : cameras) {
        const auto& camera = scene.GetComponent<LuxEngine::CameraComponent>(id);
        if (!camera.isPrimary || !scene.HasComponent<LuxEngine::TransformComponent>(id))
            continue;
        if (camera.isPerspective) {
            if (!std::isfinite(camera.fov) || camera.fov <= 0 || camera.fov >= 180 ||
                !std::isfinite(camera.nearClip) || !std::isfinite(camera.farClip) ||
                camera.nearClip <= 0 || camera.farClip <= camera.nearClip)
                continue;
            result.perspective = true;
        } else {
            if (!std::isfinite(camera.orthoSize) || camera.orthoSize <= 0)
                continue;
            const auto matrix = scene.GetWorldMatrix(id);
            const auto* m = matrix.elements;
            float angle = 0;
            try {
                angle = scene.GetWorldTransform(id).rotation.z;
            } catch (const std::logic_error&) {
                // A zero scale does not invalidate a camera's position or its authored 2D
                // orientation. Avoid decomposing the singular world matrix in this case.
                for (auto node = id; node; node = scene.GetParent(node))
                    if (const auto* transform =
                            scene.TryGetComponent<LuxEngine::TransformComponent>(node))
                        angle += transform->rotation.z;
            }
            if (!std::isfinite(m[12]) || !std::isfinite(m[13]) || !std::isfinite(angle))
                continue;
            view = {m[12], m[13], camera.orthoSize};
            rotation = angle;
        }
        foundCamera = true;
        break;
    }
    if (!foundCamera)
        result.perspective =
            !scene.GetComponentEntityIds<LuxEngine::MeshRendererComponent>().empty();
    const float halfWidth = view.halfHeight * aspect;
    result.camera2D.SetProjection(-halfWidth, halfWidth, -view.halfHeight, view.halfHeight);
    result.camera2D.SetPosition({view.centerX, view.centerY, 0});
    result.camera2D.SetRotation(rotation);
    const auto primary = LuxEngine::ResolveScenePlayerCamera(scene, view);
    result.camera3D.SetPerspective(LuxEngine::ToRadians(primary.fovDegrees), aspect,
                                   primary.nearClip, primary.farClip);
    result.camera3D.SetPosition(primary.position);
    result.camera3D.SetTarget(primary.target);
    result.camera3D.SetUp(primary.up);
    return result;
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
    const LuxUI::String platform = LuxUI::GuiApplication::platformName();
    return platform != LUX_UI_TEXT("offscreen") && platform != LUX_UI_TEXT("minimal");
}

bool EngineSceneRenderer::IsReady() const {
    return state_ && state_->gpu.IsReady();
}

bool EngineSceneRenderer::GetMeshBounds(const std::string& path, LuxEngine::Vec3& minimum,
                                        LuxEngine::Vec3& maximum) const {
    return state_ && state_->context3D.GetMeshBounds(path, minimum, maximum);
}

LuxEngine::GpuDevice* EngineSceneRenderer::GetGpuDevice() noexcept {
    return state_ ? &state_->gpu : nullptr;
}

const LuxEngine::GpuDevice* EngineSceneRenderer::GetGpuDevice() const noexcept {
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

bool EngineSceneRenderer::InitializeState(LuxEngine::uint32 initialWidth,
                                          LuxEngine::uint32 initialHeight) {
    GetProcessRetention().RetainLoaderAndVideo();
    auto state = std::make_unique<State>();
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        error_ =
            LUX_UI_TEXT("SDL 비디오 초기화 실패: %1").arg(LuxUI::String::fromUtf8(SDL_GetError()));
        return false;
    }
    state->sdlVideo = true;
    LuxEngine::WindowProps props;
    props.title = "LuxEditor viewport";
    props.width = initialWidth > 0 ? initialWidth : 640;
    props.height = initialHeight > 0 ? initialHeight : 360;
    props.resizable = true;
    props.vulkan = true;
    props.hidden = true;
    props.vsync = false;
    if (!state->window.Initialize(props)) {
        error_ = LUX_UI_TEXT("엔진 창을 만들지 못했습니다: %1")
                     .arg(LuxUI::String::fromUtf8(SDL_GetError()));
        return false;
    }
    DrainWindowEvents(state->window.GetNativeHandle(), false);
    LuxEngine::GpuDeviceConfig config;
    config.vsync = false;
    config.framesInFlight = 1;
    config.validation = static_cast<LuxEngine::GpuValidation>(s_DefaultValidation.load());
    if (!state->gpu.Initialize(state->window, config)) {
        error_ = LUX_UI_TEXT("GPU 장치 초기화 실패: %1")
                     .arg(LuxUI::String::fromStdString(state->gpu.GetLastError()));
        return false;
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
        error_ = LUX_UI_TEXT("GPU 뷰포트가 꺼져 있습니다.");
        return false;
    }
    // If a previously initialized device entered DeviceLost or Error state, attempt bounded
    // recovery.
    if (state_ && !state_->gpu.IsReady()) {
        if (recoveries_ >= kMaxDeviceRecoveries) {
            error_ = LUX_UI_TEXT("GPU 장치 복구 한도를 초과했습니다: %1")
                         .arg(LuxUI::String::fromStdString(state_->gpu.GetLastError()));
            return false;
        }
        const LuxEngine::uint32 prevW = state_->width > 0 ? state_->width : 640;
        const LuxEngine::uint32 prevH = state_->height > 0 ? state_->height : 360;
        ++recoveries_;
        state_.reset();
        return InitializeState(prevW, prevH);
    }
    if (attempted_)
        return false;
    attempted_ = true;
    return InitializeState(640, 360);
}

bool EngineSceneRenderer::BeginViewportFrame(LuxEngine::uint32 width, LuxEngine::uint32 height) {
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
                error_ = LUX_UI_TEXT("엔진 창 크기를 바꾸지 못했습니다.");
                return false;
            }
            DrainWindowEvents(s.window.GetNativeHandle(), false);
            s.gpu.NotifyResize();
            s.width = width;
            s.height = height;
        }
        if (!s.gpu.RequestFrameCapture()) {
            error_ = LuxUI::String::fromStdString(s.gpu.GetLastError());
            return false;
        }
        LuxEngine::GpuDevice::FrameStart start = LuxEngine::GpuDevice::FrameStart::Skipped;
        for (int retry = 0; retry < kMaxSwapchainAcquireRetries; ++retry) {
            start = s.gpu.BeginFrame();
            if (start == LuxEngine::GpuDevice::FrameStart::Ready ||
                start == LuxEngine::GpuDevice::FrameStart::Error || !s.gpu.IsReady())
                break;
        }
        if (start == LuxEngine::GpuDevice::FrameStart::Ready)
            return true;
        error_ = LUX_UI_TEXT("엔진 프레임을 시작하지 못했습니다: %1")
                     .arg(LuxUI::String::fromStdString(s.gpu.GetLastError()));
        if (s.gpu.IsReady())
            return false;
        // Device became unusable (e.g. DeviceLost); loop once more to trigger EnsureReady()
        // recovery.
    }
    return false;
}

bool EngineSceneRenderer::ReadbackToImage(LuxUI::Image& output) {
    if (!state_)
        return false;
    auto& s = *state_;
    if (!s.gpu.ReadFrameCapture(s.capture) || s.capture.width == 0 || s.capture.height == 0 ||
        s.capture.rgba.size() != static_cast<size_t>(s.capture.width) * s.capture.height * 4) {
        error_ = LUX_UI_TEXT("엔진 프레임을 읽지 못했습니다.");
        return false;
    }
    const auto captureWidth = static_cast<int>(s.capture.width);
    const auto captureHeight = static_cast<int>(s.capture.height);
    if (output.size() != LuxUI::Size(captureWidth, captureHeight) ||
        output.format() != LuxUI::Image::Format_RGBA8888) {
        output = LuxUI::Image(captureWidth, captureHeight, LuxUI::Image::Format_RGBA8888);
    }
    if (output.isNull()) {
        error_ = LUX_UI_TEXT("뷰포트 이미지 버퍼를 할당하지 못했습니다.");
        return false;
    }
    const auto rowBytes = static_cast<size_t>(s.capture.width) * 4;
    if (static_cast<size_t>(output.bytesPerLine()) == rowBytes) {
        std::memcpy(output.bits(), s.capture.rgba.data(), s.capture.rgba.size());
    } else {
        for (LuxEngine::uint32 y = 0; y < s.capture.height; ++y)
            std::memcpy(output.scanLine(static_cast<int>(y)), s.capture.rgba.data() + y * rowBytes,
                        rowBytes);
    }
    return true;
}

bool EngineSceneRenderer::Render(LuxEngine::Scene& scene, int pixelWidth, int pixelHeight,
                                 const LuxUI::RectF& worldBounds, LuxUI::Image& output) {
    if (rendering_ || pixelWidth <= 0 || pixelHeight <= 0 || pixelWidth > 16384 ||
        pixelHeight > 16384)
        return false;

    struct RenderScope {
        EngineSceneRenderer& self;

        ~RenderScope() {
            self.rendering_ = false;
            if (!self.meshPreview_)
                self.ClearMeshPreview();
            if (self.pendingShutdown_)
                self.Shutdown();
        }
    } renderScope{*this};

    rendering_ = true;

    const auto width = static_cast<LuxEngine::uint32>(pixelWidth);
    const auto height = static_cast<LuxEngine::uint32>(pixelHeight);
    if (!BeginViewportFrame(width, height))
        return false;

    auto& s = *state_;
    // The widget draws the scene background and grid before compositing object pixels.
    s.gpu.Clear(0, 0, 0, 0);
    const LuxEngine::OrthographicCamera camera(
        static_cast<float>(worldBounds.left()), static_cast<float>(worldBounds.right()),
        static_cast<float>(worldBounds.bottom()), static_cast<float>(worldBounds.top()));
    s.context2D.Init(&s.gpu);
    LuxEngine::Renderer2D::ScopedContext scope2D(s.context2D);
    try {
        LuxEngine::Renderer2D::BeginScene(camera);
        scene.OnRender();
        if (postSceneHook_)
            postSceneHook_();
        LuxEngine::Renderer2D::EndScene();
    } catch (const std::exception& exception) {
        s.context2D.AbortScene();
        s.gpu.EndFrame();
        error_ = LUX_UI_TEXT("엔진 렌더링 오류: %1").arg(LuxUI::String::fromUtf8(exception.what()));
        return false;
    } catch (...) {
        s.context2D.AbortScene();
        s.gpu.EndFrame();
        error_ = LUX_UI_TEXT("엔진 렌더링 중 알 수 없는 예외가 발생했습니다.");
        return false;
    }
    if (!s.gpu.EndFrame()) {
        error_ = LuxUI::String::fromStdString(s.gpu.GetLastError());
        return false;
    }
    output.reinterpretAsFormat(LuxUI::Image::Format_RGBA8888);
    if (!ReadbackToImage(output))
        return false;
    // 2D alpha blending onto transparent black produces premultiplied color.
    output.reinterpretAsFormat(LuxUI::Image::Format_RGBA8888_Premultiplied);
    return true;
}

bool EngineSceneRenderer::Render3D(LuxEngine::Scene& scene, int pixelWidth, int pixelHeight,
                                   const LuxEngine::Vec3& focus, double yaw, double pitch,
                                   double distance, LuxUI::Image& output,
                                   LuxUI::Image* skyboxBackground,
                                   const LuxEngine::PerspectiveCamera* cameraOverride) {
    struct Measurement {
        double& destination;
        std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

        ~Measurement() {
            destination =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                    .count();
        }
    } measurement{lastRenderMs_};

    if (skyboxBackground) {
        *skyboxBackground = {};
        if (scene.GetSettings().render3D.mode == LuxEngine::Render3DMode::Pbr &&
            scene.GetSettings().render3D.showSkybox)
            if (!Render3DPass(scene, pixelWidth, pixelHeight, focus, yaw, pitch, distance,
                              *skyboxBackground, true, true, cameraOverride))
                return false;
    }
    return Render3DPass(scene, pixelWidth, pixelHeight, focus, yaw, pitch, distance, output, false,
                        skyboxBackground == nullptr, cameraOverride);
}

bool EngineSceneRenderer::RenderGame(LuxEngine::Scene& scene, int pixelWidth, int pixelHeight,
                                     LuxUI::Image& output) {
    if (rendering_ || pixelWidth <= 0 || pixelHeight <= 0 || pixelWidth > 16384 ||
        pixelHeight > 16384)
        return false;

    struct RenderScope {
        EngineSceneRenderer& self;
        std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

        ~RenderScope() {
            self.lastRenderMs_ =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                    .count();
            self.rendering_ = false;
            if (!self.meshPreview_)
                self.ClearMeshPreview();
            if (self.pendingShutdown_)
                self.Shutdown();
        }
    } scope{*this};

    rendering_ = true;
    if (!BeginViewportFrame(static_cast<LuxEngine::uint32>(pixelWidth),
                            static_cast<LuxEngine::uint32>(pixelHeight)))
        return false;
    auto& state = *state_;
    const auto clear = scene.GetSettings().clearColor;
    state.gpu.Clear(clear.x, clear.y, clear.z, clear.w);
    const auto camera = ResolveGameViewCamera(scene, pixelWidth, pixelHeight);
    state.context3D.Init(&state.gpu);
    state.context2D.Init(&state.gpu);
    LuxEngine::Renderer3D::ScopedContext scope3D(state.context3D);
    LuxEngine::Renderer2D::ScopedContext scope2D(state.context2D);
    try {
        if (camera.perspective) {
            auto settings = scene.GetSettings().render3D;
            if (!settings.environmentPath.empty())
                settings.environmentPath = scene.ResolveAssetPath(settings.environmentPath);
            if (settings.mode == LuxEngine::Render3DMode::Pbr) {
                LuxEngine::Renderer3D::BeginScene(camera.camera3D, settings,
                                                  scene.GetSettings().exposure);
            } else {
                LuxEngine::DirectionalLight sun;
                sun.direction = LuxEngine::Vec3{.5f, -1, .5f}.Normalized();
                LuxEngine::Renderer3D::BeginScene(camera.camera3D, sun);
            }
            scene.OnRender3D();
            LuxEngine::Renderer3D::EndScene();
        }
        LuxEngine::Renderer2D::BeginScene(camera.camera2D);
        scene.OnRender();
        LuxEngine::Renderer2D::EndScene();
        if (postSceneHook_)
            postSceneHook_();
    } catch (const std::exception& exception) {
        state.context3D.AbortScene();
        state.context2D.AbortScene();
        state.gpu.EndFrame();
        error_ =
            LUX_UI_TEXT("게임 뷰 렌더링 오류: %1").arg(LuxUI::String::fromUtf8(exception.what()));
        return false;
    } catch (...) {
        state.context3D.AbortScene();
        state.context2D.AbortScene();
        state.gpu.EndFrame();
        error_ = LUX_UI_TEXT("게임 뷰 렌더링 중 알 수 없는 예외가 발생했습니다.");
        return false;
    }
    if (!state.gpu.EndFrame()) {
        error_ = LuxUI::String::fromStdString(state.gpu.GetLastError());
        return false;
    }
    if (!ReadbackToImage(output))
        return false;
    output.reinterpretAsFormat(LuxUI::Image::Format_RGBA8888_Premultiplied);
    error_.clear();
    return true;
}

bool EngineSceneRenderer::Render3DPass(LuxEngine::Scene& scene, int pixelWidth, int pixelHeight,
                                       const LuxEngine::Vec3& focus, double yaw, double pitch,
                                       double distance, LuxUI::Image& output, bool backgroundOnly,
                                       bool drawSkybox,
                                       const LuxEngine::PerspectiveCamera* cameraOverride) {
    if (rendering_ || pixelWidth <= 0 || pixelHeight <= 0 || pixelWidth > 16384 ||
        pixelHeight > 16384)
        return false;

    struct RenderScope {
        EngineSceneRenderer& self;

        ~RenderScope() {
            self.rendering_ = false;
            if (!self.meshPreview_)
                self.ClearMeshPreview();
            if (self.pendingShutdown_)
                self.Shutdown();
        }
    } renderScope{*this};

    rendering_ = true;

    const auto width = static_cast<LuxEngine::uint32>(pixelWidth);
    const auto height = static_cast<LuxEngine::uint32>(pixelHeight);
    if (!BeginViewportFrame(width, height))
        return false;

    auto& s = *state_;
    // The widget draws the scene background and grid before compositing object pixels.
    s.gpu.Clear(0, 0, 0, 0);

    const auto camera = cameraOverride ? *cameraOverride
                                       : MakeViewportCamera3D(LuxUI::SizeF(width, height), focus,
                                                              yaw, pitch, distance);

    LuxEngine::DirectionalLight sun;
    sun.direction = LuxEngine::Vec3(0.5f, -1.0f, 0.5f).Normalized();
    sun.color = {1.0f, 0.98f, 0.95f};
    sun.intensity = 1.0f;

    s.context3D.Init(&s.gpu);
    LuxEngine::Renderer3D::ScopedContext scope3D(s.context3D);
    try {
        auto renderSettings = scene.GetSettings().render3D;
        renderSettings.showSkybox = renderSettings.showSkybox && drawSkybox;
        if (backgroundOnly)
            renderSettings.shadows = false;
        if (!renderSettings.environmentPath.empty())
            renderSettings.environmentPath = scene.ResolveAssetPath(renderSettings.environmentPath);
        if (scene.GetSettings().render3D.mode == LuxEngine::Render3DMode::Pbr)
            LuxEngine::Renderer3D::BeginScene(camera, renderSettings, scene.GetSettings().exposure);
        else
            LuxEngine::Renderer3D::BeginScene(camera, sun);
        for (LuxEngine::Entity entity : scene.GetEntities()) {
            if (backgroundOnly)
                break;
            if (!entity.HasComponent<LuxEngine::TransformComponent>())
                continue;
            const auto world = scene.GetWorldMatrix(entity.GetId());
            const auto expose = [&](LuxEngine::Vec4 color) {
                if (scene.GetSettings().render3D.mode == LuxEngine::Render3DMode::Pbr)
                    return color;
                color.x *= scene.GetSettings().exposure;
                color.y *= scene.GetSettings().exposure;
                color.z *= scene.GetSettings().exposure;
                return color;
            };
            if (const auto* mesh = entity.TryGetComponent<LuxEngine::MeshRendererComponent>()) {
                auto material = mesh->material;
                if (scene.GetSettings().render3D.mode == LuxEngine::Render3DMode::Legacy)
                    material.albedoColor = expose(material.albedoColor);
                for (auto* resource : {&material.baseColorTexture, &material.normalTexture,
                                       &material.metallicTexture, &material.roughnessTexture,
                                       &material.occlusionTexture})
                    if (!resource->empty())
                        *resource = scene.ResolveAssetPath(*resource);
                if (meshPreview_ && meshPreview_->entity == entity.GetId()) {
                    if (s.previewRevision != meshPreview_->revision || !s.previewMesh.IsValid()) {
                        const bool updated = s.previewMesh.IsValid() &&
                                             s.previewTopology == meshPreview_->topology &&
                                             s.previewMesh.UpdateVertices(meshPreview_->vertices);
                        if (!updated && !s.previewMesh.Upload(&s.gpu, meshPreview_->vertices,
                                                              meshPreview_->indices))
                            throw std::runtime_error(
                                "모델링 미리보기 메시를 업로드하지 못했습니다.");
                        s.previewRevision = meshPreview_->revision;
                        s.previewTopology = meshPreview_->topology;
                    }
                    LuxEngine::Renderer3D::DrawMesh(s.previewMesh, world, material);
                } else if (mesh->meshPath.empty())
                    LuxEngine::Renderer3D::DrawMesh(world, material);
                else {
                    const auto* animation =
                        entity.TryGetComponent<LuxEngine::ModelAnimationComponent>();
                    LuxEngine::Renderer3D::DrawMeshAsset(
                        scene.ResolveAssetPath(mesh->meshPath), world, material, mesh->modelSubmesh,
                        animation ? animation->clip : -1, animation ? animation->time : 0.0,
                        entity.GetId());
                }
            } else if (const auto* sprite =
                           entity.TryGetComponent<LuxEngine::SpriteRendererComponent>()) {
                LuxEngine::Material3D material;
                material.albedoColor = expose(sprite->color);
                LuxEngine::Renderer3D::DrawMesh(world * LuxEngine::Mat4::Scale({1, 1, .02f}),
                                                material);
            } else if (const auto* map = entity.TryGetComponent<LuxEngine::TilemapComponent>()) {
                const std::array<LuxEngine::Vec4, 6> colors{{{},
                                                             {.36f, .65f, .27f, 1},
                                                             {.43f, .31f, .21f, 1},
                                                             {.48f, .51f, .54f, 1},
                                                             {.22f, .46f, .74f, .82f},
                                                             {.76f, .66f, .42f, 1}}};
                for (LuxEngine::uint32 y = 0; y < map->height; ++y)
                    for (LuxEngine::uint32 x = 0; x < map->width; ++x) {
                        const auto tile = map->Get(x, y);
                        if (tile == 0 || tile >= colors.size())
                            continue;
                        LuxEngine::Material3D material;
                        material.albedoColor = expose(colors[tile]);
                        const auto model =
                            world *
                            LuxEngine::Mat4::Translate(
                                {(x + .5f) * map->tileSize, (y + .5f) * map->tileSize, 0}) *
                            LuxEngine::Mat4::Scale({map->tileSize, map->tileSize, .02f});
                        LuxEngine::Renderer3D::DrawMesh(model, material);
                    }
            }
        }
        LuxEngine::Renderer3D::EndScene();
        error_ = LuxUI::String::fromUtf8(s.context3D.GetLastAssetError());
    } catch (const std::exception& exception) {
        s.context3D.AbortScene();
        s.gpu.EndFrame();
        error_ =
            LUX_UI_TEXT("엔진 3D 렌더링 오류: %1").arg(LuxUI::String::fromUtf8(exception.what()));
        return false;
    } catch (...) {
        s.context3D.AbortScene();
        s.gpu.EndFrame();
        error_ = LUX_UI_TEXT("엔진 3D 렌더링 중 알 수 없는 예외가 발생했습니다.");
        return false;
    }
    if (!s.gpu.EndFrame()) {
        error_ = LuxUI::String::fromStdString(s.gpu.GetLastError());
        return false;
    }
    if (!ReadbackToImage(output))
        return false;
    output.reinterpretAsFormat(LuxUI::Image::Format_RGBA8888_Premultiplied);
    output = output.convertToFormat(LuxUI::Image::Format_RGBA8888);
    if (output.isNull()) {
        error_ = LUX_UI_TEXT("3D 뷰포트의 알파 이미지를 변환하지 못했습니다.");
        return false;
    }
    return true;
}

const LuxUI::String& EngineSceneRenderer::LastError() const {
    return error_;
}

LuxEngine::uint32 EngineSceneRenderer::GetRecoveryCount() const noexcept {
    return recoveries_;
}

void EngineSceneRenderer::SetPostSceneHookForTesting(std::function<void()> hook) {
    postSceneHook_ = std::move(hook);
}
} // namespace LuxEditor

namespace LuxEditor {
double EngineSceneRenderer::LastRenderMs() const {
    return lastRenderMs_;
}
} // namespace LuxEditor
