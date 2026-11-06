#pragma once
#include "EditorTool.hpp"
#include "Engine/Core/Math.hpp"
#include <QPointF>
#include <QWidget>
#include <functional>
#include <memory>
#include <optional>

namespace LuxEngine {
class EditorDocument;
class EngineSceneRenderer;
struct TransformComponent;
class SceneViewport3D final : public QWidget {
public:
    SceneViewport3D(QWidget* parent, EditorDocument& document,
                    std::function<void(std::optional<uint32>)> onSelect,
                    std::function<void(uint32, const TransformComponent&)> onTransform,
                    std::function<void()> onBeginEdit, std::function<void()> onEndEdit,
                    std::shared_ptr<EngineSceneRenderer> renderer = nullptr);
    void RefreshScene() {
        update();
    }
    void FocusSelection();
    void SetTool(EditorTool tool) {
        tool_ = tool;
    }

private:
    std::optional<QPointF> Project(const Vec3& point) const;
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    EditorDocument& document_;
    std::function<void(std::optional<uint32>)> onSelect_;
    std::function<void(uint32, const TransformComponent&)> onTransform_;
    std::function<void()> onBeginEdit_;
    std::function<void()> onEndEdit_;
    Vec3 focus_{0, 0, 0};
    double yaw_ = .65, pitch_ = .48, distance_ = 22;
    QPointF lastMouse_{};
    QPointF dragStart_{};
    Vec3 initialPosition_{};
    Vec3 initialRotation_{};
    Vec3 initialScale_{1, 1, 1};
    uint32 dragId_ = 0;
    EditorTool tool_ = EditorTool::Select;
    bool orbiting_ = false, panning_ = false, dragging_ = false;
    std::shared_ptr<EngineSceneRenderer> engineRenderer_;
    bool engineFrame_ = false;
};
} // namespace LuxEngine
