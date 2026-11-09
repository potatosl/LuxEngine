#include "SceneViewport3D.hpp"
#include "EngineSceneRenderer.hpp"
#include "Engine/ECS/ECS.hpp"
#include "Scene/EditorDocument.hpp"
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace LuxEngine {
SceneViewport3D::SceneViewport3D(QWidget* parent, EditorDocument& document,
                                 std::function<void(std::optional<uint32>)> onSelect,
                                 std::function<void(uint32, const TransformComponent&)> onTransform,
                                 std::function<void()> onBeginEdit, std::function<void()> onEndEdit,
                                 std::shared_ptr<EngineSceneRenderer> renderer)
    : QWidget(parent), document_(document), onSelect_(std::move(onSelect)),
      onTransform_(std::move(onTransform)), onBeginEdit_(std::move(onBeginEdit)),
      onEndEdit_(std::move(onEndEdit)), engineRenderer_(std::move(renderer)) {
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(300, 200);
}
void SceneViewport3D::FocusSelection() {
    Entity selected = document_.GetSelected();
    if (selected && selected.HasComponent<TransformComponent>()) {
        focus_ = selected.GetComponent<TransformComponent>().position;
        if (selected.HasComponent<TilemapComponent>()) {
            const auto& map = selected.GetComponent<TilemapComponent>();
            focus_.x += map.width * map.tileSize * .5f;
            focus_.y += map.height * map.tileSize * .5f;
        }
        update();
    }
}
std::optional<QPointF> SceneViewport3D::Project(const Vec3& point) const {
    const double x = point.x - focus_.x, y = point.y - focus_.y, z = point.z - focus_.z;
    const double cy = std::cos(yaw_), sy = std::sin(yaw_), cp = std::cos(pitch_),
                 sp = std::sin(pitch_);
    const double horizontal = x * cy - z * sy;
    const double vertical = x * sy * sp + y * cp + z * cy * sp;
    const double depth = distance_ + x * sy * cp - y * sp + z * cy * cp;
    if (depth < .2 || !std::isfinite(depth))
        return std::nullopt;
    const double focal = std::max(1, height()) * .95;
    const double sx = width() * .5 + horizontal * focal / depth;
    const double syy = height() * .5 - vertical * focal / depth;
    if (!std::isfinite(sx) || !std::isfinite(syy) || std::abs(sx) > 1e6 || std::abs(syy) > 1e6)
        return std::nullopt;
    return QPointF(sx, syy);
}
void SceneViewport3D::paintEvent(QPaintEvent*) {
    // Decouple GPU render/readback phase from QPainter lifecycle.
    // Vulkan offscreen rendering must complete before QPainter opens on this QWidget
    // to prevent re-entrant Qt event loop message dispatch from invalidating the raster paint engine.
    engineFrame_ = false;
    if (EngineSceneRenderer::Allowed()) {
        if (!engineRenderer_)
            engineRenderer_ = std::make_shared<EngineSceneRenderer>();
        const qreal ratio = devicePixelRatioF();
        const int pxW = static_cast<int>(std::lround(width() * ratio));
        const int pxH = static_cast<int>(std::lround(height() * ratio));
        if (pxW > 0 && pxH > 0) {
            if (engineRenderer_->Render3D(document_.GetScene(), pxW, pxH,
                                         focus_, yaw_, pitch_, distance_, cachedFrame_)) {
                engineFrame_ = true;
            }
        }
    }

    QPainter painter(this);
    if (!painter.isActive())
        return;

    painter.fillRect(rect(), QColor(27, 33, 42));
    painter.setRenderHint(QPainter::Antialiasing);

    if (engineFrame_ && !cachedFrame_.isNull()) {
        painter.drawImage(rect(), cachedFrame_);
    }
    const int gridAlpha = engineFrame_ ? 75 : 255;
    auto line = [gridAlpha](QColor color) {
        color.setAlpha(gridAlpha);
        return color;
    };
    for (int unit = -20; unit <= 20; ++unit) {
        painter.setPen(QPen(line(unit == 0       ? QColor(95, 108, 124)
                                : unit % 5 == 0 ? QColor(70, 81, 95)
                                                : QColor(56, 66, 78)),
                            unit == 0 ? 2 : 1));
        auto a = Project({static_cast<float>(unit), 0, -20}),
             b = Project({static_cast<float>(unit), 0, 20});
        if (a && b)
            painter.drawLine(*a, *b);
        a = Project({-20, 0, static_cast<float>(unit)});
        b = Project({20, 0, static_cast<float>(unit)});
        if (a && b)
            painter.drawLine(*a, *b);
    }
    const auto origin = Project({0, 0, 0});
    for (const auto& [tip, color] :
         std::array<std::pair<Vec3, QColor>, 3>{{{{2, 0, 0}, QColor(218, 97, 99)},
                                                 {{0, 2, 0}, QColor(105, 190, 132)},
                                                 {{0, 0, 2}, QColor(102, 151, 232)}}}) {
        auto end = Project(tip);
        if (origin && end) {
            painter.setPen(QPen(color, 3));
            painter.drawLine(*origin, *end);
        }
    }
    const Entity selected = document_.GetSelected();
    std::vector<QRectF> occupiedLabels;
    for (Entity entity : document_.GetScene().GetEntities()) {
        if (!entity.HasComponent<TransformComponent>())
            continue;
        const auto& t = entity.GetComponent<TransformComponent>();
        if (!std::isfinite(t.position.x) || !std::isfinite(t.position.y) ||
            !std::isfinite(t.position.z) || !std::isfinite(t.scale.x) ||
            !std::isfinite(t.scale.y) || !std::isfinite(t.scale.z))
            continue;
        const bool sprite = entity.HasComponent<SpriteRendererComponent>();
        const bool mesh = entity.HasComponent<MeshRendererComponent>();
        const bool active = selected && selected.GetId() == entity.GetId();
        QColor color = active   ? QColor(248, 197, 91)
                       : mesh   ? QColor(117, 183, 223)
                       : sprite ? QColor(130, 205, 158)
                                : QColor(176, 187, 201);
        if (engineFrame_ && !active)
            color.setAlpha(120);
        Vec3 boxCenter = t.position;
        Vec3 half{std::max(.05f, std::abs(t.scale.x) * .5f),
                  std::max(.05f, std::abs(t.scale.y) * .5f),
                  sprite ? .02f : std::max(.05f, std::abs(t.scale.z) * .5f)};
        if (entity.HasComponent<TilemapComponent>()) {
            const auto& map = entity.GetComponent<TilemapComponent>();
            boxCenter.x += map.width * map.tileSize * .5f;
            boxCenter.y += map.height * map.tileSize * .5f;
            half = {map.width * map.tileSize * .5f, map.height * map.tileSize * .5f, .02f};
        }
        std::array<std::optional<QPointF>, 8> corners;
        for (size_t i = 0; i < 8; ++i)
            corners[i] = Project({boxCenter.x + ((i & 1) ? half.x : -half.x),
                                  boxCenter.y + ((i & 2) ? half.y : -half.y),
                                  boxCenter.z + ((i & 4) ? half.z : -half.z)});
        painter.setPen(QPen(color, active ? 3 : 2));
        for (const auto [a, b] : std::array<std::pair<int, int>, 12>{{{0, 1},
                                                                      {0, 2},
                                                                      {1, 3},
                                                                      {2, 3},
                                                                      {4, 5},
                                                                      {4, 6},
                                                                      {5, 7},
                                                                      {6, 7},
                                                                      {0, 4},
                                                                      {1, 5},
                                                                      {2, 6},
                                                                      {3, 7}}})
            if (corners[a] && corners[b])
                painter.drawLine(*corners[a], *corners[b]);
        auto center = Project(boxCenter);
        if (center) {
            painter.setPen(color);
            const QString label = entity.HasComponent<TagComponent>()
                                      ? QString::fromUtf8(entity.GetComponent<TagComponent>().tag)
                                      : QStringLiteral("#%1").arg(entity.GetId());
            const double textHeight = painter.fontMetrics().height();
            QRectF labelRect(center->x() + 9, center->y() - 10 - textHeight,
                             painter.fontMetrics().horizontalAdvance(label), textHeight);
            for (int attempt = 0; attempt < 8; ++attempt) {
                if (std::none_of(
                        occupiedLabels.begin(), occupiedLabels.end(),
                        [&labelRect](const QRectF& other) { return labelRect.intersects(other); }))
                    break;
                labelRect.translate(0, -textHeight - 2);
            }
            occupiedLabels.push_back(labelRect);
            painter.drawText(labelRect.bottomLeft(), label);
        }
    }
    painter.setPen(QColor(227, 235, 243));
    const QString help =
        QStringLiteral("3D 배치 미리보기  |  우클릭 회전 · 휠 확대 · F 선택 포커스");
    painter.drawText(
        14, 22, painter.fontMetrics().elidedText(help, Qt::ElideRight, std::max(0, width() - 28)));
    painter.setPen(QColor(153, 169, 185));
    painter.drawText(
        14, 44,
        painter.fontMetrics().elidedText(QStringLiteral("현재는 엔티티 경계 프록시를 표시합니다."),
                                         Qt::ElideRight, std::max(0, width() - 28)));
}
void SceneViewport3D::mousePressEvent(QMouseEvent* event) {
    setFocus();
    lastMouse_ = event->position();
    if (event->button() == Qt::RightButton) {
        orbiting_ = true;
        return;
    }
    if (event->button() == Qt::MiddleButton) {
        panning_ = true;
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;
    std::optional<uint32> nearest;
    double best = 22 * 22;
    for (Entity entity : document_.GetScene().GetEntities()) {
        if (!entity.HasComponent<TransformComponent>())
            continue;
        Vec3 position = entity.GetComponent<TransformComponent>().position;
        if (entity.HasComponent<TilemapComponent>()) {
            const auto& map = entity.GetComponent<TilemapComponent>();
            position.x += map.width * map.tileSize * .5f;
            position.y += map.height * map.tileSize * .5f;
        }
        auto point = Project(position);
        if (!point)
            continue;
        const QPointF delta = *point - event->position();
        const double distance = delta.x() * delta.x() + delta.y() * delta.y();
        if (distance < best) {
            best = distance;
            nearest = entity.GetId();
        }
    }
    onSelect_(nearest);
    if (nearest && tool_ != EditorTool::Select) {
        Entity selected = document_.GetSelected();
        const auto& transform = selected.GetComponent<TransformComponent>();
        dragId_ = *nearest;
        dragStart_ = event->position();
        initialPosition_ = transform.position;
        initialRotation_ = transform.rotation;
        initialScale_ = transform.scale;
        dragging_ = true;
        onBeginEdit_();
    }
}
void SceneViewport3D::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::RightButton)
        orbiting_ = false;
    if (event->button() == Qt::MiddleButton)
        panning_ = false;
    if (event->button() == Qt::LeftButton && dragging_) {
        dragging_ = false;
        onEndEdit_();
    }
}
void SceneViewport3D::mouseMoveEvent(QMouseEvent* event) {
    const QPointF delta = event->position() - lastMouse_;
    if (orbiting_ && (event->buttons() & Qt::RightButton)) {
        yaw_ += delta.x() * .008;
        pitch_ = std::clamp(pitch_ + delta.y() * .008, -1.45, 1.45);
        update();
    } else if (panning_ && (event->buttons() & Qt::MiddleButton)) {
        const double scale = distance_ / std::max(1, height());
        focus_.x -= static_cast<float>(delta.x() * scale * 1.6);
        focus_.y += static_cast<float>(delta.y() * scale * 1.6);
        update();
    } else if (dragging_ && (event->buttons() & Qt::LeftButton)) {
        Entity entity = document_.GetSelected();
        if (!entity || entity.GetId() != dragId_ || !entity.HasComponent<TransformComponent>())
            return;
        TransformComponent value = entity.GetComponent<TransformComponent>();
        const QPointF drag = event->position() - dragStart_;
        if (tool_ == EditorTool::Move) {
            const double scale = distance_ / std::max(1, height());
            value.position.x = initialPosition_.x + static_cast<float>(drag.x() * scale * 1.6);
            value.position.z = initialPosition_.z + static_cast<float>(drag.y() * scale * 1.6);
            if (event->modifiers() & Qt::ShiftModifier) {
                value.position.z = initialPosition_.z;
                value.position.y = initialPosition_.y - static_cast<float>(drag.y() * scale * 1.6);
            }
        } else if (tool_ == EditorTool::Rotate) {
            value.rotation.y = initialRotation_.y + static_cast<float>(drag.x() * .01);
        } else if (tool_ == EditorTool::Scale) {
            const float factor =
                static_cast<float>(std::clamp(std::exp(drag.x() * .01), .05, 100.0));
            value.scale = {initialScale_.x * factor, initialScale_.y * factor,
                           initialScale_.z * factor};
        }
        if (std::isfinite(value.position.x) && std::isfinite(value.position.y) &&
            std::isfinite(value.position.z) && std::isfinite(value.rotation.y) &&
            std::isfinite(value.scale.x))
            onTransform_(dragId_, value);
    }
    lastMouse_ = event->position();
}
void SceneViewport3D::wheelEvent(QWheelEvent* event) {
    distance_ = std::clamp(distance_ * std::pow(.9, event->angleDelta().y() / 120.0), 1.0, 1000.0);
    update();
}
} // namespace LuxEngine
