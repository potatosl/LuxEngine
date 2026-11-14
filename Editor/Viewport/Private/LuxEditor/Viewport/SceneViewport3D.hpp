#pragma once
#include <LuxEditor/Viewport/EditorTool.hpp>
#include <LuxEditor/Viewport/ModelingViewport.hpp>
#include <LuxEngine/Core/Math.hpp>
#include <LuxUI/PointerCapture.hpp>
#include <LuxUI/Widgets.hpp>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <vector>

namespace LuxEngine {
struct TransformComponent;
class PerspectiveCamera;
} // namespace LuxEngine

namespace LuxEditor {
class EditorDocument;
class EngineSceneRenderer;

class SceneViewport3D final : public LuxUI::Widget {
public:
    SceneViewport3D(
        LuxUI::Widget* parent, EditorDocument& document,
        std::function<void(std::optional<LuxEngine::uint32>)> onSelect,
        std::function<void(LuxEngine::uint32, const LuxEngine::TransformComponent&)> onTransform,
        std::function<void()> onBeginEdit, std::function<void()> onEndEdit,
        std::shared_ptr<EngineSceneRenderer> renderer = nullptr);

    enum class ViewPreset { Perspective, Front, Right, Top };

    void RefreshScene();
    void SetViewPreset(ViewPreset preset);
    void FrameAll();
    void SetGridVisible(bool visible);

    void FocusSelection();
    LuxEngine::PerspectiveCamera GetViewCamera() const;
    bool SetViewCamera(const LuxEngine::PerspectiveCamera& camera, float fovDegrees, float nearClip,
                       float farClip);
    void GetViewProjection(float& fovDegrees, float& nearClip, float& farClip) const;
    bool SetMoveSpeed(double unitsPerSecond);
    double GetMoveSpeed() const;
    double GetEffectiveMoveSpeed() const;
    void SetMoveSpeedChangedCallback(std::function<void(double)> callback);
    void SetNavigationMessageCallback(std::function<void(const LuxUI::String&)> callback);
    std::shared_ptr<EngineSceneRenderer> GetRenderer() const;
    void CancelInteraction();
    void SetCancelEditCallback(std::function<void()> callback);

    void SetTool(EditorTool tool);

    void SetModelingMode(ModelingEditMode mode);
    ModelingEditMode GetModelingMode() const;
    void SetModelingMesh(LuxEngine::uint32 entity, const ModelingMesh& mesh);
    void ClearModelingMesh();
    const ModelingMesh* GetModelingMesh() const;
    LuxEngine::uint32 GetModelingEntity() const;
    void SetModelingSelection(const ModelingSelection& selection);
    const ModelingSelection& GetModelingSelection() const;
    void SetModelingCommitCallback(
        std::function<void(const ModelingMesh&, const ModelingSelection&)> callback);
    void SetModelingSelectionCallback(std::function<void(const ModelingSelection&)> callback);

private:
    bool ModelingActive() const;
    void SelectModeling(LuxUI::PointF point, LuxUI::Flags::KeyboardModifiers modifiers);
    void PublishModelingPreview();
    bool event(LuxUI::Event*) override;
    void paintEvent(LuxUI::PaintEvent*) override;
    void keyPressEvent(LuxUI::KeyEvent*) override;
    void keyReleaseEvent(LuxUI::KeyEvent*) override;
    void AdvanceFlyCamera();
    void RotateView(LuxUI::PointF delta);
    void ReportPointerLimitation();
    void UseEditorProjection();
    int HitGizmo(LuxUI::PointF point) const;
    void DrawGizmo(LuxUI::Painter& painter) const;
    void BeginDrag(LuxEngine::uint32 id, LuxUI::PointF point);
    void FrameBounds(bool selectedOnly);
    void NotifySelection();
    void SelectRectangle(LuxUI::RectF rectangle, LuxUI::Flags::KeyboardModifiers modifiers);
    LuxEngine::Mat4 GizmoBasis() const;
    LuxEngine::Vec3 GizmoPosition() const;
    void mousePressEvent(LuxUI::MouseEvent*) override;
    void mouseReleaseEvent(LuxUI::MouseEvent*) override;
    void mouseMoveEvent(LuxUI::MouseEvent*) override;
    void wheelEvent(LuxUI::WheelEvent*) override;
    void focusOutEvent(LuxUI::FocusEvent*) override;
    EditorDocument& document_;
    std::function<void(std::optional<LuxEngine::uint32>)> onSelect_;
    std::function<void(LuxEngine::uint32, const LuxEngine::TransformComponent&)> onTransform_;
    std::function<void()> onBeginEdit_;
    std::function<void()> onEndEdit_;
    std::function<void()> onCancelEdit_;
    LuxEngine::Vec3 focus_{0, 0, 0};
    double yaw_ = .65, pitch_ = -.48, distance_ = 22;
    double roll_ = 0;
    float viewFovDegrees_ = 60;
    float viewNearClip_ = 0;
    float viewFarClip_ = 0;
    double moveSpeed_ = 0;
    std::function<void(double)> onMoveSpeedChanged_;
    std::function<void(const LuxUI::String&)> onNavigationMessage_;
    LuxUI::PointerCapture pointerCapture_;
    bool pointerLimitationShown_ = false;
    LuxUI::PointF lastMouse_{};
    LuxUI::PointF dragStart_{};
    LuxEngine::Vec3 initialPosition_{};
    LuxEngine::Vec3 initialPivot_{};
    LuxEngine::Mat4 initialBasis_{};
    LuxEngine::Vec3 initialRotation_{};
    LuxEngine::Vec3 initialScale_{1, 1, 1};
    LuxEngine::uint32 dragId_ = 0;
    EditorTool tool_ = EditorTool::Select;
    ViewPreset preset_ = ViewPreset::Perspective;
    int axis_ = -1;
    bool axisGesture_ = false;
    bool gridVisible_ = true;
    LuxUI::Timer cameraTimer_;
    std::chrono::steady_clock::time_point cameraTick_;
    std::set<int> flyKeys_;
    bool orbiting_ = false, panning_ = false, dragging_ = false;
    bool orbitDrag_ = false;
    bool flyFast_ = false;
    LuxUI::Flags::MouseButton orbitButton_ = LuxUI::Flags::RightButton;
    bool marquee_ = false;
    LuxUI::PointF marqueeStart_{};
    LuxUI::PointF marqueeEnd_{};
    LuxUI::Flags::KeyboardModifiers marqueeModifiers_ = LuxUI::Flags::NoModifier;
    std::vector<LuxEngine::uint32> dragSelection_;
    std::weak_ptr<int> dragLifetime_;
    std::uint64_t dragTab_ = 0;
    int dragSpace_ = 0;
    int dragPivotMode_ = 0;
    std::shared_ptr<EngineSceneRenderer> engineRenderer_;
    bool engineFrame_ = false;
    LuxUI::Image cachedFrame_;
    LuxUI::Image cachedBackground_;
    ModelingEditMode modelingMode_ = ModelingEditMode::Object;
    LuxEngine::uint32 modelingEntity_ = 0;
    std::uint64_t modelingTab_ = 0;
    std::weak_ptr<int> modelingLifetime_;
    ModelingMesh modelingMesh_;
    ModelingMesh modelingInitialMesh_;
    ModelingSelection modelingSelection_;
    LuxEngine::Mat4 modelingWorld_;
    bool modelingDrag_ = false;
    bool modelingChanged_ = false;
    std::function<void(const ModelingMesh&, const ModelingSelection&)> onModelingCommit_;
    std::function<void(const ModelingSelection&)> onModelingSelection_;
};
} // namespace LuxEditor
