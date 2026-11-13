#pragma once
#include <LuxEditor/Viewport/EditorTool.hpp>
#include <LuxEngine/Core/Core.hpp>
#include <QImage>
#include <QPointF>
#include <QWidget>
#include <array>
#include <functional>
#include <memory>
#include <optional>

namespace LuxEngine {
struct TransformComponent;
}

namespace LuxEditor {
class EditorDocument;
class EngineSceneRenderer;

class SceneViewport2D final : public QWidget {
public:
    SceneViewport2D(
        QWidget* parent, EditorDocument& document,
        std::function<void(std::optional<LuxEngine::uint32>)> onSelect,
        std::function<void(LuxEngine::uint32, const LuxEngine::TransformComponent&)> onTransform,
        std::function<void(LuxEngine::uint32, LuxEngine::uint32, LuxEngine::uint32, uint8_t)>
            onPaintTile,
        std::function<void()> onBeginEdit, std::function<void()> onEndEdit,
        std::shared_ptr<EngineSceneRenderer> renderer = nullptr);
    ~SceneViewport2D() override;

    void RefreshScene();

    void FocusSelection();
    void FrameAll();
    void SetGridVisible(bool visible);
    void CancelInteraction();
    // Visible area in world units: centre and half height.
    void GetView(double& centerX, double& centerY, double& halfHeight) const;

    void SetSnapToGrid(bool enabled);

    void SetTool(EditorTool tool);

    void SetTileId(uint8_t tile);

    void SetDebugColliders(bool enabled);

private:
    void FrameBounds(bool selectedOnly);
    void keyPressEvent(QKeyEvent*) override;
    QPointF WorldToScreen(double x, double y) const;
    QPointF ScreenToWorld(QPointF point) const;
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void focusOutEvent(QFocusEvent*) override;
    EditorDocument& document_;
    std::function<void(std::optional<LuxEngine::uint32>)> onSelect_;
    std::function<void(LuxEngine::uint32, const LuxEngine::TransformComponent&)> onTransform_;
    std::function<void(LuxEngine::uint32, LuxEngine::uint32, LuxEngine::uint32, uint8_t)>
        onPaintTile_;
    std::function<void()> onBeginEdit_;
    std::function<void()> onEndEdit_;
    double pixelsPerUnit_ = 72.0;
    QPointF pan_{};
    QPointF lastMouse_{};
    QPointF dragOffset_{};
    QPointF dragStartWorld_{};
    std::array<float, 9> dragInitial_{};
    bool panning_ = false;
    bool dragging_ = false;
    bool tilePainting_ = false;
    bool snapToGrid_ = true;
    bool gridVisible_ = true;
    bool debugColliders_ = false;
    LuxEngine::uint32 dragId_ = 0;
    EditorTool tool_ = EditorTool::Select;
    uint8_t tileId_ = 1;
    void PaintAt(QPointF point);
    void DrawGrid(class QPainter& painter, int alpha) const;
    std::shared_ptr<EngineSceneRenderer> engineRenderer_;
    bool engineFrame_ = false;
    QImage cachedFrame_;
};
} // namespace LuxEditor
