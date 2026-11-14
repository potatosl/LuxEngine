#pragma once
#include <LuxEngine/Core/Core.hpp>
#include <LuxEngine/Core/Math.hpp>
#include <LuxEngine/Renderer/Renderer2D/Renderer2D.hpp>
#include <LuxEngine/Renderer/Renderer3D/Renderer3D.hpp>
#include <LuxUI/Widgets.hpp>
#include <functional>
#include <memory>

namespace LuxEngine {
class Scene;
class GpuDevice;
} // namespace LuxEngine

namespace LuxEditor {

struct GameViewCamera {
    bool perspective = false;
    LuxEngine::OrthographicCamera camera2D;
    LuxEngine::PerspectiveCamera camera3D;
};

// Shared by GPU and software Game Views. Scene View camera state never enters this path.
GameViewCamera ResolveGameViewCamera(LuxEngine::Scene& scene, int width, int height);

// Renders a Scene with the engine's The Forge Renderer2D and Renderer3D into images the Qt viewport
// can show. The engine needs an SDL window for its swapchain, so a hidden window is used and every
// frame is read back with the GpuDevice frame capture.
class EngineSceneRenderer final {
public:
    EngineSceneRenderer();
    ~EngineSceneRenderer();
    EngineSceneRenderer(const EngineSceneRenderer&) = delete;
    EngineSceneRenderer& operator=(const EngineSceneRenderer&) = delete;

    // Returns false and fills LastError when the GPU path is unavailable; callers then fall back
    // to software drawing. If a previously ready device becomes lost, EnsureReady attempts bounded
    // automatic recovery.
    bool EnsureReady();
    bool IsReady() const;
    void Shutdown();
    void Reset();
    // worldBounds: left, top (larger y), right, bottom in world units.
    bool Render(LuxEngine::Scene& scene, int pixelWidth, int pixelHeight,
                const LuxUI::RectF& worldBounds, LuxUI::Image& output);
    // 3D perspective rendering with the engine Renderer3D.
    bool Render3D(LuxEngine::Scene& scene, int pixelWidth, int pixelHeight,
                  const LuxEngine::Vec3& focus, double yaw, double pitch, double distance,
                  LuxUI::Image& output, LuxUI::Image* skyboxBackground = nullptr);
    bool RenderGame(LuxEngine::Scene& scene, int pixelWidth, int pixelHeight, LuxUI::Image& output);

    const LuxUI::String& LastError() const;

    LuxEngine::GpuDevice* GetGpuDevice() noexcept;
    const LuxEngine::GpuDevice* GetGpuDevice() const noexcept;
    bool GetMeshBounds(const std::string& path, LuxEngine::Vec3& minimum,
                       LuxEngine::Vec3& maximum) const;

    LuxEngine::uint32 GetRecoveryCount() const noexcept;
    double LastRenderMs() const;

    // Disabled for offscreen/minimal Qt platforms unless SetForceAllowed(true) or
    // LUX_EDITOR_FORCE_GPU_VIEWPORT is set, and disabled when LUX_EDITOR_SOFTWARE_VIEWPORT is set.
    static bool Allowed();
    static void SetForceAllowed(bool force);
    static void SetDefaultValidation(int validationMode);

    void SetPostSceneHookForTesting(std::function<void()> hook);

private:
    bool Render3DPass(LuxEngine::Scene& scene, int pixelWidth, int pixelHeight,
                      const LuxEngine::Vec3& focus, double yaw, double pitch, double distance,
                      LuxUI::Image& output, bool backgroundOnly, bool drawSkybox);
    bool InitializeState(LuxEngine::uint32 initialWidth = 640,
                         LuxEngine::uint32 initialHeight = 360);
    bool BeginViewportFrame(LuxEngine::uint32 width, LuxEngine::uint32 height);
    bool ReadbackToImage(LuxUI::Image& output);

    struct State;
    std::unique_ptr<State> state_;
    bool attempted_ = false;
    double lastRenderMs_ = 0;
    bool rendering_ = false;
    bool pendingShutdown_ = false;
    LuxEngine::uint32 recoveries_ = 0;
    LuxUI::String error_;
    std::function<void()> postSceneHook_;
};
} // namespace LuxEditor
