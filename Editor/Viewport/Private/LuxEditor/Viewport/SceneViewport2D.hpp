#pragma once
#include <LuxEditor/Viewport/EditorTool.hpp>
#include <LuxEngine/Core/Core.hpp>
#include <LuxUI/Widgets.hpp>
#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace LuxEngine {
struct TransformComponent;
}

namespace LuxEditor {
class EditorDocument;
class EngineSceneRenderer;

class SceneViewport2D final : public LuxUI::Widget {
public:
    SceneViewport2D(
        LuxUI::Widget* parent, EditorDocument& document,
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
    void SetCancelEditCallback(std::function<void()> callback);
    // Visible area in world units: centre and half height.
    void GetView(double& centerX, double& centerY, double& halfHeight) const;
    bool SetView(double centerX, double centerY, double halfHeight);

    void SetTool(EditorTool tool);

    void SetTileId(uint8_t tile);

    void SetDebugColliders(bool enabled);

private:
    bool event(LuxUI::Event*) override;
    void BeginEdit();
    bool GestureSceneValid() const;
    int HitGizmo(LuxUI::PointF point) const;
    void DrawGizmo(LuxUI::Painter& painter) const;
    void FinishSelection();
    void FrameBounds(bool selectedOnly);
    void keyPressEvent(LuxUI::KeyEvent*) override;
    LuxUI::PointF WorldToScreen(double x, double y) const;
    LuxUI::PointF ScreenToWorld(LuxUI::PointF point) const;
    void paintEvent(LuxUI::PaintEvent*) override;
    void mousePressEvent(LuxUI::MouseEvent*) override;
    void mouseReleaseEvent(LuxUI::MouseEvent*) override;
    void mouseMoveEvent(LuxUI::MouseEvent*) override;
    void wheelEvent(LuxUI::WheelEvent*) override;
    void focusOutEvent(LuxUI::FocusEvent*) override;
    EditorDocument& document_;
    std::function<void(std::optional<LuxEngine::uint32>)> onSelect_;
    std::function<void(LuxEngine::uint32, const LuxEngine::TransformComponent&)> onTransform_;
    std::function<void(LuxEngine::uint32, LuxEngine::uint32, LuxEngine::uint32, uint8_t)>
        onPaintTile_;
    std::function<void()> onBeginEdit_;
    std::function<void()> onEndEdit_;
    std::function<void()> onCancelEdit_;
    std::weak_ptr<int> gestureLifetime_;
    std::uint64_t gestureTab_ = 0;
    int gestureSpace_ = 0;
    int gesturePivotMode_ = 0;
    std::string fallbackBefore_;
    std::vector<LuxEngine::uint32> selectionBefore_;
    std::optional<LuxEngine::uint32> primaryBefore_;
    bool dirtyBefore_ = false;
    bool selecting_ = false;
    bool selectionBox_ = false;
    LuxUI::Flags::KeyboardModifiers selectionModifiers_ = LuxUI::Flags::NoModifier;
    LuxUI::PointF selectionStart_{};
    LuxUI::PointF selectionEnd_{};
    LuxUI::PointF dragPivot_{};
    int dragAxis_ = -1;
    double pixelsPerUnit_ = 72.0;
    LuxUI::PointF pan_{};
    LuxUI::PointF lastMouse_{};
    LuxUI::PointF dragOffset_{};
    LuxUI::PointF dragStartWorld_{};
    std::array<float, 9> dragInitial_{};
    bool panning_ = false;
    bool dragging_ = false;
    bool tilePainting_ = false;
    bool gridVisible_ = true;
    bool debugColliders_ = false;
    LuxEngine::uint32 dragId_ = 0;
    EditorTool tool_ = EditorTool::Select;
    uint8_t tileId_ = 1;
    void PaintAt(LuxUI::PointF point);
    void DrawGrid(LuxUI::Painter& painter, int alpha) const;
    std::shared_ptr<EngineSceneRenderer> engineRenderer_;
    bool engineFrame_ = false;
    LuxUI::Image cachedFrame_;
};
} // namespace LuxEditor
