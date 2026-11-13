#pragma once
#include <LuxEditor/Viewport/EditorTool.hpp>
#include <LuxEngine/Core/Math.hpp>
#include <QImage>
#include <QPointF>
#include <QWidget>
#include <functional>
#include <memory>
#include <optional>

class QPainter;

namespace LuxEngine {
struct TransformComponent;
}

namespace LuxEditor {
class EditorDocument;
class EngineSceneRenderer;

class SceneViewport3D final : public QWidget {
public:
    SceneViewport3D(
        QWidget* parent, EditorDocument& document,
        std::function<void(std::optional<LuxEngine::uint32>)> onSelect,
        std::function<void(LuxEngine::uint32, const LuxEngine::TransformComponent&)> onTransform,
        std::function<void()> onBeginEdit, std::function<void()> onEndEdit,
        std::shared_ptr<EngineSceneRenderer> renderer = nullptr);

    enum class ViewPreset { Perspective, Front, Right, Top };

    void RefreshScene();
    void SetViewPreset(ViewPreset preset);
    void FrameAll();
    void SetSnapToGrid(bool enabled);
    void SetGridVisible(bool visible);

    void FocusSelection();
    void CancelInteraction();

    void SetTool(EditorTool tool);

private:
    bool event(QEvent*) override;
    void paintEvent(QPaintEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    int HitGizmo(QPointF point) const;
    void DrawGizmo(QPainter& painter) const;
    void BeginDrag(LuxEngine::uint32 id, QPointF point);
    void FrameBounds(bool selectedOnly);
    void mousePressEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void focusOutEvent(QFocusEvent*) override;
    EditorDocument& document_;
    std::function<void(std::optional<LuxEngine::uint32>)> onSelect_;
    std::function<void(LuxEngine::uint32, const LuxEngine::TransformComponent&)> onTransform_;
    std::function<void()> onBeginEdit_;
    std::function<void()> onEndEdit_;
    LuxEngine::Vec3 focus_{0, 0, 0};
    double yaw_ = .65, pitch_ = -.48, distance_ = 22;
    QPointF lastMouse_{};
    QPointF dragStart_{};
    LuxEngine::Vec3 initialPosition_{};
    LuxEngine::Vec3 initialRotation_{};
    LuxEngine::Vec3 initialScale_{1, 1, 1};
    LuxEngine::uint32 dragId_ = 0;
    EditorTool tool_ = EditorTool::Select;
    ViewPreset preset_ = ViewPreset::Perspective;
    int axis_ = -1;
    bool axisGesture_ = false;
    bool snapToGrid_ = false;
    bool gridVisible_ = true;
    bool orbiting_ = false, panning_ = false, dragging_ = false;
    std::shared_ptr<EngineSceneRenderer> engineRenderer_;
    bool engineFrame_ = false;
    QImage cachedFrame_;
    QImage cachedBackground_;
};
} // namespace LuxEditor
