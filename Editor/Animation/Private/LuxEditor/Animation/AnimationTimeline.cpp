#include <LuxEditor/Animation/AnimationTimeline.hpp>
#include <LuxUI/Widgets.hpp>
#include <algorithm>
#include <cmath>

namespace LuxEditor {
namespace {
constexpr int HeaderWidth = 174;
constexpr int RulerHeight = 38;
constexpr int RowHeight = 32;
constexpr int TrackCount = 4;

LuxUI::PolygonF Diamond(const LuxUI::PointF& center, double size) {
    return {center + LuxUI::PointF(0, -size), center + LuxUI::PointF(size, 0),
            center + LuxUI::PointF(0, size), center + LuxUI::PointF(-size, 0)};
}
} // namespace

AnimationTimeline::AnimationTimeline(LuxUI::Widget* parent, AnimationTimelineActions actions)
    : LuxUI::Slider(LuxUI::Flags::Horizontal, parent), actions_(std::move(actions)) {
    setMinimumHeight(RulerHeight + RowHeight * TrackCount + 28);
    setSizePolicy(LuxUI::SizePolicy::Expanding, LuxUI::SizePolicy::Expanding);
    setFocusPolicy(LuxUI::Flags::StrongFocus);
    setMouseTracking(true);
    setToolTip(LUX_UI_TEXT("시간 눈금: 재생 헤드 이동 · 키 드래그: 시간 변경 · Delete: 키 삭제"));
}

void AnimationTimeline::SetZoom(double zoom) {
    zoom_ = std::clamp(zoom, 1.0, 16.0);
    update();
}

void AnimationTimeline::SetScroll(double fraction) {
    scroll_ = std::clamp(fraction, 0.0, 1.0);
    update();
}

void AnimationTimeline::SetCurveMode(bool enabled) {
    curveMode_ = enabled;
    update();
}

float AnimationTimeline::Duration() const {
    const auto entity = actions_.target();
    const auto* animation =
        entity ? entity.TryGetComponent<LuxEngine::AnimationComponent>() : nullptr;
    return animation ? std::max(.001f, animation->clip.duration) : 1;
}

float AnimationTimeline::TimeAt(double x) const {
    const double span = std::max(1, width() - HeaderWidth - 18);
    const double origin = scroll_ * (1.0 - 1.0 / zoom_);
    return static_cast<float>(std::clamp(origin + (x - HeaderWidth) / (span * zoom_), 0.0, 1.0) *
                              Duration());
}

double AnimationTimeline::PositionAt(float time) const {
    const double span = std::max(1, width() - HeaderWidth - 18);
    const double origin = scroll_ * (1.0 - 1.0 / zoom_);
    return HeaderWidth + (time / Duration() - origin) * span * zoom_;
}

int AnimationTimeline::KeyAt(const LuxUI::PointF& point) const {
    if (point.x() < HeaderWidth || point.y() < RulerHeight ||
        (!curveMode_ && point.y() > RulerHeight + (expanded_ ? TrackCount : 1) * RowHeight))
        return -1;
    const auto entity = actions_.target();
    const auto* animation =
        entity ? entity.TryGetComponent<LuxEngine::AnimationComponent>() : nullptr;
    if (!animation)
        return -1;
    for (size_t index = 0; index < animation->clip.keyframes.size(); ++index)
        if (std::abs(PositionAt(animation->clip.keyframes[index].time) - point.x()) <= 8)
            return static_cast<int>(index);
    return -1;
}

void AnimationTimeline::paintEvent(LuxUI::PaintEvent*) {
    LuxUI::Painter painter(this);
    painter.setRenderHint(LuxUI::Painter::Antialiasing);
    // Read the workspace palette rather than QSlider's style palette. Some Qt styles keep
    // a white Base role on custom-painted slider children despite the dark parent stylesheet.
    const auto theme = window()->palette();
    const bool dark = theme.color(LuxUI::Palette::Window).lightness() < 128;
    const auto background = theme.color(LuxUI::Palette::Window).darker(dark ? 125 : 100);
    const auto alternate = dark ? background.lighter(118) : background.darker(104);
    const auto text = theme.color(LuxUI::Palette::WindowText);
    const auto divider = dark ? background.lighter(160) : background.darker(120);
    const auto accent = theme.color(LuxUI::Palette::Highlight).lighter(dark ? 150 : 100);
    painter.fillRect(rect(), background);
    painter.fillRect(LuxUI::Rect(0, 0, width(), RulerHeight), alternate);
    painter.fillRect(LuxUI::Rect(0, RulerHeight, HeaderWidth, height()), alternate);
    painter.setPen(divider);
    painter.drawLine(HeaderWidth, 0, HeaderWidth, height());
    painter.setPen(text);
    painter.drawText(LuxUI::Rect(12, 0, HeaderWidth - 24, RulerHeight), LuxUI::Flags::AlignVCenter,
                     curveMode_ ? LUX_UI_TEXT("위치 곡선") : LUX_UI_TEXT("변환 트랙"));
    const LuxUI::StringList labels{
        expanded_ ? LUX_UI_TEXT("▾  Transform") : LUX_UI_TEXT("▸  Transform"),
        LUX_UI_TEXT("    위치"), LUX_UI_TEXT("    회전"), LUX_UI_TEXT("    크기")};
    for (int row = 0; !curveMode_ && row < (expanded_ ? TrackCount : 1); ++row) {
        const int y = RulerHeight + row * RowHeight;
        if (row % 2 == 1)
            painter.fillRect(LuxUI::Rect(HeaderWidth + 1, y, width() - HeaderWidth, RowHeight),
                             alternate);
        painter.setPen(text);
        painter.drawText(LuxUI::Rect(12, y, HeaderWidth - 24, RowHeight),
                         LuxUI::Flags::AlignVCenter, labels[row]);
        painter.setPen(divider);
        painter.drawLine(0, y + RowHeight, width(), y + RowHeight);
    }
    const int fps = std::max(1, actions_.framesPerSecond());
    const double visibleSeconds = Duration() / zoom_;
    const double targetStep = visibleSeconds * 70.0 / std::max(1, width() - HeaderWidth);
    const double frameStep = 1.0 / fps;
    double tickStep = frameStep;
    while (tickStep < targetStep)
        tickStep *= 2;
    const double first = std::ceil(TimeAt(HeaderWidth) / tickStep) * tickStep;
    const double last = TimeAt(width() - 18);
    painter.save();
    painter.setClipRect(LuxUI::Rect(HeaderWidth, 0, width() - HeaderWidth, height()));
    for (double time = first; time <= last + tickStep * .1; time += tickStep) {
        const auto x = PositionAt(static_cast<float>(time));
        painter.setPen(divider);
        painter.drawLine(LuxUI::PointF(x, RulerHeight - 6), LuxUI::PointF(x, height()));
        painter.setPen(text);
        painter.drawText(LuxUI::RectF(x + 5, 1, 66, RulerHeight - 7), LuxUI::Flags::AlignVCenter,
                         LUX_UI_TEXT("%1\n%2 s")
                             .arg(std::llround(time * fps))
                             .arg(time, 0, 'f', tickStep < .1 ? 2 : 1));
    }
    const auto entity = actions_.target();
    const auto* animation =
        entity ? entity.TryGetComponent<LuxEngine::AnimationComponent>() : nullptr;
    if (animation) {
        if (curveMode_ && !animation->clip.keyframes.empty()) {
            const LuxUI::Color colors[] = {LuxUI::Color(228, 103, 103), LuxUI::Color(108, 193, 127),
                                           LuxUI::Color(106, 156, 229)};
            float low = 0, high = 1;
            for (const auto& key : animation->clip.keyframes) {
                low = std::min({low, key.position.x, key.position.y, key.position.z});
                high = std::max({high, key.position.x, key.position.y, key.position.z});
            }
            for (int axis = 0; axis < 3; ++axis) {
                LuxUI::PainterPath curve;
                bool firstPoint = true;
                for (const auto& key : animation->clip.keyframes) {
                    const float values[] = {key.position.x, key.position.y, key.position.z};
                    const auto y = RulerHeight + 10 +
                                   (high - values[axis]) / (high - low) *
                                       std::max(1, height() - RulerHeight - 30);
                    const LuxUI::PointF point(PositionAt(key.time), y);
                    if (firstPoint)
                        curve.moveTo(point);
                    else
                        curve.lineTo(point);
                    firstPoint = false;
                }
                painter.setPen(LuxUI::Pen(colors[axis], 2));
                painter.drawPath(curve);
                for (size_t index = 0; index < animation->clip.keyframes.size(); ++index) {
                    const auto& key = animation->clip.keyframes[index];
                    const float values[] = {key.position.x, key.position.y, key.position.z};
                    const auto y = RulerHeight + 10 +
                                   (high - values[axis]) / (high - low) *
                                       std::max(1, height() - RulerHeight - 30);
                    painter.setBrush(static_cast<int>(index) == actions_.selectedKey()
                                         ? LuxUI::Color(255, 192, 84)
                                         : colors[axis]);
                    painter.drawPolygon(Diamond({PositionAt(key.time), y}, 4.5));
                }
            }
        }
        for (size_t index = 0; !curveMode_ && index < animation->clip.keyframes.size(); ++index) {
            const auto x = PositionAt(animation->clip.keyframes[index].time);
            const bool selected = static_cast<int>(index) == actions_.selectedKey();
            painter.setPen(
                LuxUI::Pen(selected ? accent.lighter(140) : LuxUI::Color(150, 111, 49), 1));
            painter.setBrush(selected ? accent : LuxUI::Color(192, 151, 79));
            for (int row = 0; row < (expanded_ ? TrackCount : 1); ++row)
                painter.drawPolygon(Diamond({x, RulerHeight + row * RowHeight + RowHeight / 2.0},
                                            selected ? 5.5 : 4.5));
        }
    }
    if (draggedKey_ >= 0) {
        painter.setPen(LuxUI::Pen(accent, 1, LuxUI::Flags::DashLine));
        const double x = PositionAt(draggedTime_);
        painter.drawLine(LuxUI::PointF(x, RulerHeight), LuxUI::PointF(x, height()));
    }
    const float current = animation ? animation->currentTime : 0;
    const double x = PositionAt(current);
    painter.setPen(LuxUI::Pen(accent, 1.5));
    painter.drawLine(LuxUI::PointF(x, 0), LuxUI::PointF(x, height()));
    painter.setBrush(accent);
    painter.drawPolygon(LuxUI::PolygonF{{x - 6, 0}, {x + 6, 0}, {x + 6, 16}, {x, 22}, {x - 6, 16}});
    painter.restore();
    if (curveMode_) {
        const LuxUI::Color colors[] = {LuxUI::Color(228, 103, 103), LuxUI::Color(108, 193, 127),
                                       LuxUI::Color(106, 156, 229)};
        const LuxUI::StringList channels{LUX_UI_TEXT("●  위치 X"), LUX_UI_TEXT("●  위치 Y"),
                                         LUX_UI_TEXT("●  위치 Z")};
        for (int axis = 0; axis < 3; ++axis) {
            painter.setPen(colors[axis]);
            painter.drawText(
                LuxUI::Rect(16, RulerHeight + axis * RowHeight, HeaderWidth - 24, RowHeight),
                LuxUI::Flags::AlignVCenter, channels[axis]);
        }
        painter.setPen(text);
        painter.drawText(LuxUI::Rect(16, RulerHeight + 3 * RowHeight, HeaderWidth - 24, RowHeight),
                         LuxUI::Flags::AlignVCenter, LUX_UI_TEXT("선형 보간"));
    }
    if (!animation) {
        painter.setPen(text);
        painter.drawText(
            LuxUI::Rect(HeaderWidth + 20, RulerHeight + 12, width() - HeaderWidth - 40, 100),
            LuxUI::Flags::AlignCenter | LuxUI::Flags::TextWordWrap,
            LUX_UI_TEXT("애니메이션 클립을 생성하면 키프레임을 편집할 수 있습니다."));
    }
}

void AnimationTimeline::Scrub(double x) {
    const int fps = std::max(1, actions_.framesPerSecond());
    const auto time = std::round(TimeAt(x) * fps) / fps;
    if (actions_.seekTime)
        actions_.seekTime(time);
    else
        setValue(static_cast<int>(std::lround(1000 * time / Duration())));
}

void AnimationTimeline::mousePressEvent(LuxUI::MouseEvent* event) {
    if (event->button() == LuxUI::Flags::LeftButton && !curveMode_ &&
        event->position().x() < HeaderWidth && event->position().y() >= RulerHeight &&
        event->position().y() < RulerHeight + RowHeight) {
        expanded_ = !expanded_;
        update();
        event->accept();
        return;
    }
    if (event->button() != LuxUI::Flags::LeftButton || event->position().x() < HeaderWidth) {
        event->ignore();
        return;
    }
    setFocus();
    draggedKey_ = KeyAt(event->position());
    if (draggedKey_ >= 0)
        actions_.selectKey(draggedKey_);
    setSliderDown(true);
    draggedTime_ = TimeAt(event->position().x());
    if (draggedKey_ >= 0) {
        const auto entity = actions_.target();
        const auto& keys = entity.GetComponent<LuxEngine::AnimationComponent>().clip.keyframes;
        draggedTime_ = keys[static_cast<size_t>(draggedKey_)].time;
    }
    Scrub(event->position().x());
    event->accept();
}

void AnimationTimeline::mouseMoveEvent(LuxUI::MouseEvent* event) {
    if (!isSliderDown())
        return;
    const int fps = std::max(1, actions_.framesPerSecond());
    draggedTime_ = static_cast<float>(std::round(TimeAt(event->position().x()) * fps) / fps);
    Scrub(event->position().x());
    update();
    event->accept();
}

void AnimationTimeline::mouseReleaseEvent(LuxUI::MouseEvent* event) {
    if (event->button() != LuxUI::Flags::LeftButton || !isSliderDown())
        return;
    if (draggedKey_ >= 0)
        actions_.moveKey(draggedKey_, draggedTime_);
    draggedKey_ = -1;
    setSliderDown(false);
    update();
    event->accept();
}

void AnimationTimeline::mouseDoubleClickEvent(LuxUI::MouseEvent* event) {
    if (event->button() != LuxUI::Flags::LeftButton || event->position().x() < HeaderWidth)
        return;
    setSliderDown(true);
    Scrub(event->position().x());
    setSliderDown(false);
    actions_.addKey();
}

void AnimationTimeline::keyPressEvent(LuxUI::KeyEvent* event) {
    if (event->key() == LuxUI::Flags::Key_Space && actions_.togglePlayback) {
        actions_.togglePlayback();
        event->accept();
    } else if ((event->key() == LuxUI::Flags::Key_Left ||
                event->key() == LuxUI::Flags::Key_Right) &&
               actions_.seekTime) {
        const auto entity = actions_.target();
        const auto* animation =
            entity ? entity.TryGetComponent<LuxEngine::AnimationComponent>() : nullptr;
        if (animation) {
            setSliderDown(true);
            actions_.seekTime(animation->currentTime +
                              (event->key() == LuxUI::Flags::Key_Left ? -1.0 : 1.0) /
                                  std::max(1, actions_.framesPerSecond()));
            setSliderDown(false);
        }
        event->accept();
    } else if (event->key() == LuxUI::Flags::Key_Delete ||
               event->key() == LuxUI::Flags::Key_Backspace) {
        actions_.removeKey();
        event->accept();
    } else {
        LuxUI::Slider::keyPressEvent(event);
    }
}

void AnimationTimeline::wheelEvent(LuxUI::WheelEvent* event) {
    if (event->modifiers() & LuxUI::Flags::ControlModifier)
        SetZoom(zoom_ * (event->angleDelta().y() > 0 ? 1.25 : .8));
    else
        SetScroll(scroll_ - event->angleDelta().y() / 1200.0);
    event->accept();
}

void AnimationTimeline::contextMenuEvent(LuxUI::ContextMenuEvent* event) {
    const int key = KeyAt(event->pos());
    if (key >= 0)
        actions_.selectKey(key);
    LuxUI::Menu menu(this);
    menu.addAction(LUX_UI_TEXT("현재 시간에 키 추가"), [this] { actions_.addKey(); });
    auto* remove = menu.addAction(LUX_UI_TEXT("선택 키 삭제"), [this] { actions_.removeKey(); });
    remove->setEnabled(actions_.selectedKey() >= 0);
    menu.exec(event->globalPos());
}

bool AnimationTimeline::event(LuxUI::Event* event) {
    if (event->type() == LuxUI::Event::ShortcutOverride) {
        const auto key = static_cast<LuxUI::KeyEvent*>(event)->key();
        if (key == LuxUI::Flags::Key_Delete || key == LuxUI::Flags::Key_Backspace ||
            key == LuxUI::Flags::Key_Space) {
            event->accept();
            return true;
        }
    }
    return LuxUI::Slider::event(event);
}

} // namespace LuxEditor
