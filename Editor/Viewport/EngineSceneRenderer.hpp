#pragma once
#include "Engine/Core/Core.hpp"
#include <QImage>
#include <QRectF>
#include <QString>
#include <memory>

namespace LuxEngine {
class Scene;

// Renders a Scene with the engine's Vulkan Renderer2D into an image the Qt viewport can show.
// The engine needs an SDL window for its swapchain, so a hidden window is used and every
// frame is read back with the GpuDevice frame capture.
class EngineSceneRenderer final {
public:
    EngineSceneRenderer();
    ~EngineSceneRenderer();
    EngineSceneRenderer(const EngineSceneRenderer&) = delete;
    EngineSceneRenderer& operator=(const EngineSceneRenderer&) = delete;

    // Returns false and fills LastError when the GPU path is unavailable; callers then fall back
    // to software drawing. A failed initialization is not retried.
    bool EnsureReady();
    bool IsReady() const;
    // worldBounds: left, top (larger y), right, bottom in world units.
    bool Render(Scene& scene, int pixelWidth, int pixelHeight, const QRectF& worldBounds,
                QImage& output);
    const QString& LastError() const {
        return error_;
    }
    // Disabled for offscreen/minimal Qt platforms and when LUX_EDITOR_SOFTWARE_VIEWPORT is set.
    static bool Allowed();

private:
    struct State;
    std::unique_ptr<State> state_;
    bool attempted_ = false;
    QString error_;
};
} // namespace LuxEngine
