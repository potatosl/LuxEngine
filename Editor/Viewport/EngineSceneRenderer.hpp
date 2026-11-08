#pragma once
#include "Engine/Core/Core.hpp"
#include "Engine/Core/Math.hpp"
#include <QImage>
#include <QRectF>
#include <QString>
#include <memory>

namespace LuxEngine {
class Scene;
class GpuDevice;

// Renders a Scene with the engine's Vulkan Renderer2D and Renderer3D into images the Qt viewport
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
    bool Render(Scene& scene, int pixelWidth, int pixelHeight, const QRectF& worldBounds,
                QImage& output);
    // 3D perspective rendering with native Vulkan Renderer3D.
    bool Render3D(Scene& scene, int pixelWidth, int pixelHeight, const Vec3& focus, double yaw,
                  double pitch, double distance, QImage& output);
    const QString& LastError() const {
        return error_;
    }
    GpuDevice* GetGpuDevice() noexcept;
    const GpuDevice* GetGpuDevice() const noexcept;
    uint32 GetRecoveryCount() const noexcept {
        return recoveries_;
    }
    // Disabled for offscreen/minimal Qt platforms unless SetForceAllowed(true) or
    // LUX_EDITOR_FORCE_GPU_VIEWPORT is set, and disabled when LUX_EDITOR_SOFTWARE_VIEWPORT is set.
    static bool Allowed();
    static void SetForceAllowed(bool force);

private:
    bool InitializeState();
    bool BeginViewportFrame(uint32 width, uint32 height);
    bool ReadbackToImage(QImage& output);

    struct State;
    std::unique_ptr<State> state_;
    bool attempted_ = false;
    bool rendering_ = false;
    bool pendingShutdown_ = false;
    uint32 recoveries_ = 0;
    QString error_;
};
} // namespace LuxEngine
