#pragma once
#include <LuxUI/Widgets.hpp>

#include <LuxEngine/Scene/Entity.hpp>
#include <functional>

namespace LuxEditor {

struct AnimationTimelineActions {
    std::function<LuxEngine::Entity()> target;
    std::function<int()> selectedKey;
    std::function<int()> framesPerSecond;
    std::function<void(int)> selectKey;
    std::function<void(int, float)> moveKey;
    std::function<void()> removeKey;
    std::function<void()> addKey;
    std::function<void(double)> seekTime;
    std::function<void()> togglePlayback;
};

class AnimationTimeline final : public LuxUI::Slider {
public:
    AnimationTimeline(LuxUI::Widget* parent, AnimationTimelineActions actions);
    void SetZoom(double zoom);
    void SetScroll(double fraction);
    void SetCurveMode(bool enabled);

protected:
    void paintEvent(LuxUI::PaintEvent*) override;
    void mousePressEvent(LuxUI::MouseEvent* event) override;
    void mouseMoveEvent(LuxUI::MouseEvent* event) override;
    void mouseReleaseEvent(LuxUI::MouseEvent* event) override;
    void mouseDoubleClickEvent(LuxUI::MouseEvent* event) override;
    void keyPressEvent(LuxUI::KeyEvent* event) override;
    void wheelEvent(LuxUI::WheelEvent* event) override;
    void contextMenuEvent(LuxUI::ContextMenuEvent* event) override;
    bool event(LuxUI::Event* event) override;

private:
    float Duration() const;
    float TimeAt(double x) const;
    double PositionAt(float time) const;
    int KeyAt(const LuxUI::PointF& point) const;
    void Scrub(double x);

    AnimationTimelineActions actions_;
    double zoom_ = 1;
    double scroll_ = 0;
    int draggedKey_ = -1;
    float draggedTime_ = 0;
    bool curveMode_ = false;
    bool expanded_ = true;
};

} // namespace LuxEditor
