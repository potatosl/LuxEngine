#pragma once

#include <LuxEngine/Scene/Entity.hpp>
#include <QSlider>
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

class AnimationTimeline final : public QSlider {
public:
    AnimationTimeline(QWidget* parent, AnimationTimelineActions actions);
    void SetZoom(double zoom);
    void SetScroll(double fraction);
    void SetCurveMode(bool enabled);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    bool event(QEvent* event) override;

private:
    float Duration() const;
    float TimeAt(double x) const;
    double PositionAt(float time) const;
    int KeyAt(const QPointF& point) const;
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
