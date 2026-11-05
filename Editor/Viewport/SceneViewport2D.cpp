#include "SceneViewport2D.hpp"
#include "EngineSceneRenderer.hpp"
#include "Engine/ECS/ECS.hpp"
#include "Scene/EditorDocument.hpp"
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace LuxEngine {
namespace {
constexpr QColor Background(30, 34, 42);
constexpr QColor Selection(255, 205, 92);
QColor TileColor(uint8_t tile) {
    switch (tile) {
    case 1:
        return QColor(92, 166, 69);
    case 2:
        return QColor(110, 79, 54);
    case 3:
        return QColor(122, 130, 138);
    case 4:
        return QColor(56, 117, 189, 210);
    case 5:
        return QColor(194, 168, 107);
    default:
        return Qt::transparent;
    }
}
QString Name(Entity entity) {
    return entity.HasComponent<TagComponent>()
               ? QString::fromUtf8(entity.GetComponent<TagComponent>().tag)
               : QStringLiteral("엔티티 #%1").arg(entity.GetId());
}
} // namespace
SceneViewport2D::SceneViewport2D(QWidget* parent, EditorDocument& document,
                                 std::function<void(std::optional<uint32>)> onSelect,
                                 std::function<void(uint32, const TransformComponent&)> onTransform,
                                 std::function<void(uint32, uint32, uint32, uint8_t)> onPaintTile,
                                 std::function<void()> onBeginEdit, std::function<void()> onEndEdit)
    : QWidget(parent), document_(document), onSelect_(std::move(onSelect)),
      onTransform_(std::move(onTransform)), onPaintTile_(std::move(onPaintTile)),
      onBeginEdit_(std::move(onBeginEdit)), onEndEdit_(std::move(onEndEdit)) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(300, 200);
}
SceneViewport2D::~SceneViewport2D() = default;
QPointF SceneViewport2D::WorldToScreen(double x, double y) const {
    return {width() * 0.5 + pan_.x() + x * pixelsPerUnit_,
            height() * 0.5 + pan_.y() - y * pixelsPerUnit_};
}
QPointF SceneViewport2D::ScreenToWorld(QPointF point) const {
    return {(point.x() - width() * 0.5 - pan_.x()) / pixelsPerUnit_,
            (height() * 0.5 + pan_.y() - point.y()) / pixelsPerUnit_};
}
void SceneViewport2D::FocusSelection() {
    Entity selected = document_.GetSelected();
    if (selected && selected.HasComponent<TransformComponent>()) {
        auto position = selected.GetComponent<TransformComponent>().position;
        pan_ = {-position.x * pixelsPerUnit_, position.y * pixelsPerUnit_};
        update();
    }
}
void SceneViewport2D::DrawGrid(QPainter& painter, int alpha) const {
    auto line = [alpha](QColor color) {
        color.setAlpha(alpha);
        return color;
    };
    const double originX = width() * 0.5 + pan_.x();
    const double originY = height() * 0.5 + pan_.y();
    const int minX = std::max(-10000, static_cast<int>(std::floor(-originX / pixelsPerUnit_)));
    const int maxX =
        std::min(10000, static_cast<int>(std::ceil((width() - originX) / pixelsPerUnit_)));
    const int minY =
        std::max(-10000, static_cast<int>(std::floor((originY - height()) / pixelsPerUnit_)));
    const int maxY = std::min(10000, static_cast<int>(std::ceil(originY / pixelsPerUnit_)));
    for (int unit = minX; unit <= maxX; ++unit) {
        painter.setPen(QPen(line(unit == 0       ? QColor(170, 84, 91)
                                 : unit % 5 == 0 ? QColor(67, 74, 86)
                                                 : QColor(48, 54, 64)),
                            unit == 0 ? 2 : 1));
        const double x = originX + unit * pixelsPerUnit_;
        painter.drawLine(QPointF(x, 0), QPointF(x, height()));
    }
    for (int unit = minY; unit <= maxY; ++unit) {
        painter.setPen(QPen(line(unit == 0       ? QColor(89, 157, 113)
                                 : unit % 5 == 0 ? QColor(67, 74, 86)
                                                 : QColor(48, 54, 64)),
                            unit == 0 ? 2 : 1));
        const double y = originY - unit * pixelsPerUnit_;
        painter.drawLine(QPointF(0, y), QPointF(width(), y));
    }
}
void SceneViewport2D::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), Background);
    painter.setRenderHint(QPainter::Antialiasing);
    engineFrame_ = false;
    if (EngineSceneRenderer::Allowed()) {
        if (!engineRenderer_)
            engineRenderer_ = std::make_unique<EngineSceneRenderer>();
        const qreal ratio = devicePixelRatioF();
        const QPointF topLeft = ScreenToWorld(QPointF(0, 0));
        const QPointF bottomRight = ScreenToWorld(QPointF(width(), height()));
        QImage frame;
        if (engineRenderer_->Render(document_.GetScene(), static_cast<int>(std::lround(width() * ratio)),
                                    static_cast<int>(std::lround(height() * ratio)),
                                    QRectF(topLeft, bottomRight), frame)) {
            painter.drawImage(rect(), frame);
            engineFrame_ = true;
        }
    }
    DrawGrid(painter, engineFrame_ ? 70 : 255);
    auto entities = document_.GetScene().GetEntities();
    std::stable_sort(entities.begin(), entities.end(), [](Entity a, Entity b) {
        const float az = a.HasComponent<TransformComponent>()
                             ? a.GetComponent<TransformComponent>().position.z
                             : 0.0f;
        const float bz = b.HasComponent<TransformComponent>()
                             ? b.GetComponent<TransformComponent>().position.z
                             : 0.0f;
        return az < bz;
    });
    const Entity selected = document_.GetSelected();
    for (Entity entity : entities) {
        if (!entity.HasComponent<TilemapComponent>() || !entity.HasComponent<TransformComponent>())
            continue;
        const auto& map = entity.GetComponent<TilemapComponent>();
        const auto& position = entity.GetComponent<TransformComponent>().position;
        if (!std::isfinite(map.tileSize) || map.tileSize <= 0)
            continue;
        const double size = map.tileSize * pixelsPerUnit_;
        for (uint32 y = 0; y < map.height; ++y)
            for (uint32 x = 0; x < map.width; ++x) {
                const uint8_t tile = map.Get(x, y);
                if (tile == 0)
                    continue;
                const QPointF top = WorldToScreen(position.x + x * map.tileSize,
                                                  position.y + (y + 1) * map.tileSize);
                if (top.x() + size < 0 || top.x() > width() || top.y() + size < 0 ||
                    top.y() > height())
                    continue;
                painter.setPen(QPen(QColor(20, 24, 26, engineFrame_ ? 70 : 130), 1));
                painter.setBrush(engineFrame_ ? QBrush(Qt::NoBrush) : QBrush(TileColor(tile)));
                painter.drawRect(QRectF(top, QSizeF(size, size)));
            }
        if (selected && selected.GetId() == entity.GetId()) {
            const QPointF top = WorldToScreen(position.x, position.y + map.height * map.tileSize);
            painter.setPen(QPen(Selection, 2, Qt::DashLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(QRectF(top, QSizeF(map.width * size, map.height * size)));
        }
    }
    for (Entity entity : entities) {
        if (!entity.HasComponent<TransformComponent>() ||
            !entity.HasComponent<SpriteRendererComponent>())
            continue;
        const auto transform = entity.GetComponent<TransformComponent>();
        const auto color = entity.GetComponent<SpriteRendererComponent>().color;
        if (!std::isfinite(transform.position.x) || !std::isfinite(transform.position.y) ||
            !std::isfinite(transform.scale.x) || !std::isfinite(transform.scale.y) ||
            !std::isfinite(transform.rotation.z))
            continue;
        QPolygonF polygon;
        const double cosine = std::cos(transform.rotation.z), sine = std::sin(transform.rotation.z);
        for (const QPointF local :
             {QPointF(-.5, -.5), QPointF(.5, -.5), QPointF(.5, .5), QPointF(-.5, .5)}) {
            const double x = local.x() * transform.scale.x, y = local.y() * transform.scale.y;
            polygon << WorldToScreen(transform.position.x + x * cosine - y * sine,
                                     transform.position.y + x * sine + y * cosine);
        }
        const bool active = selected && selected.GetId() == entity.GetId();
        painter.setPen(QPen(active ? Selection : QColor(178, 189, 204), active ? 3 : 1));
        if (engineFrame_)
            painter.setBrush(Qt::NoBrush);
        else
            painter.setBrush(QColor::fromRgbF(
                std::clamp(color.x, 0.0f, 1.0f), std::clamp(color.y, 0.0f, 1.0f),
                std::clamp(color.z, 0.0f, 1.0f), std::clamp(color.w, 0.0f, 1.0f)));
        painter.drawPolygon(polygon);
        painter.setPen(active ? Selection : QColor(215, 222, 231));
        painter.drawText(polygon.boundingRect().topLeft() - QPointF(0, 6), Name(entity));
    }
    if (debugColliders_) {
        for (Entity entity : entities) {
            if (!entity.HasComponent<TransformComponent>() ||
                !entity.HasComponent<BoxCollider2DComponent>())
                continue;
            const auto& transform = entity.GetComponent<TransformComponent>();
            const auto& collider = entity.GetComponent<BoxCollider2DComponent>();
            const double offsetX = collider.offset.x * transform.scale.x;
            const double offsetY = collider.offset.y * transform.scale.y;
            const double cosine = std::cos(transform.rotation.z);
            const double sine = std::sin(transform.rotation.z);
            const QPointF center =
                WorldToScreen(transform.position.x + offsetX * cosine - offsetY * sine,
                              transform.position.y + offsetX * sine + offsetY * cosine);
            const QSizeF size(std::abs(collider.size.x * transform.scale.x * pixelsPerUnit_),
                              std::abs(collider.size.y * transform.scale.y * pixelsPerUnit_));
            painter.save();
            painter.translate(center);
            painter.rotate(-transform.rotation.z * 180.0 / std::numbers::pi);
            painter.setPen(QPen(collider.isTrigger ? QColor(255, 179, 92) : QColor(73, 220, 230), 2,
                                Qt::DashLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(
                QRectF(-size.width() * .5, -size.height() * .5, size.width(), size.height()));
            painter.restore();
        }
    }
    painter.setPen(QColor(219, 226, 235));
    painter.drawText(12, 22,
                     QStringLiteral("2D 장면  |  %1 px/단위  |  %2")
                         .arg(pixelsPerUnit_, 0, 'f', 0)
                         .arg(engineFrame_ ? QStringLiteral("엔진 Vulkan")
                                           : QStringLiteral("소프트웨어")));
    painter.setPen(QColor(166, 178, 194));
    const QString help = snapToGrid_ ? QStringLiteral("휠 확대 · 가운데 드래그 이동 · 스냅 켜짐")
                                     : QStringLiteral("휠 확대 · 가운데 드래그 이동 · 스냅 꺼짐");
    painter.drawText(
        12, height() - 14,
        painter.fontMetrics().elidedText(help, Qt::ElideRight, std::max(0, width() - 24)));
}
void SceneViewport2D::mousePressEvent(QMouseEvent* event) {
    lastMouse_ = event->position();
    if (event->button() == Qt::MiddleButton) {
        panning_ = true;
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;
    setFocus();
    if (tool_ == EditorTool::TileBrush || tool_ == EditorTool::TileErase) {
        tilePainting_ = true;
        onBeginEdit_();
        PaintAt(event->position());
        return;
    }
    const QPointF world = ScreenToWorld(event->position());
    std::optional<uint32> hit;
    auto entities = document_.GetScene().GetEntities();
    for (auto it = entities.rbegin(); it != entities.rend(); ++it) {
        Entity entity = *it;
        if (!entity.HasComponent<TransformComponent>())
            continue;
        const auto& t = entity.GetComponent<TransformComponent>();
        if (entity.HasComponent<TilemapComponent>()) {
            const auto& map = entity.GetComponent<TilemapComponent>();
            if (world.x() >= t.position.x && world.y() >= t.position.y &&
                world.x() < t.position.x + map.width * map.tileSize &&
                world.y() < t.position.y + map.height * map.tileSize) {
                hit = entity.GetId();
                break;
            }
        }
        if (!entity.HasComponent<SpriteRendererComponent>())
            continue;
        const double dx = world.x() - t.position.x, dy = world.y() - t.position.y;
        const double c = std::cos(t.rotation.z), s = std::sin(t.rotation.z);
        if (std::abs(dx * c + dy * s) <= std::abs(t.scale.x) * .5 &&
            std::abs(-dx * s + dy * c) <= std::abs(t.scale.y) * .5) {
            hit = entity.GetId();
            break;
        }
    }
    onSelect_(hit);
    if (hit && tool_ != EditorTool::Select) {
        Entity selected = document_.GetSelected();
        const auto& transform = selected.GetComponent<TransformComponent>();
        dragStartWorld_ = world;
        dragInitial_ = {transform.position.x, transform.position.y, transform.position.z,
                        transform.rotation.x, transform.rotation.y, transform.rotation.z,
                        transform.scale.x,    transform.scale.y,    transform.scale.z};
        dragId_ = *hit;
        dragging_ = true;
        onBeginEdit_();
    }
}
void SceneViewport2D::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton)
        panning_ = false;
    if (event->button() == Qt::LeftButton && tilePainting_) {
        tilePainting_ = false;
        onEndEdit_();
    }
    if (event->button() == Qt::LeftButton && dragging_) {
        dragging_ = false;
        onEndEdit_();
    }
}
void SceneViewport2D::mouseMoveEvent(QMouseEvent* event) {
    if (panning_ && (event->buttons() & Qt::MiddleButton)) {
        pan_ += event->position() - lastMouse_;
        update();
    } else if (tilePainting_ && (event->buttons() & Qt::LeftButton)) {
        PaintAt(event->position());
    } else if (dragging_ && (event->buttons() & Qt::LeftButton)) {
        const QPointF world = ScreenToWorld(event->position());
        Entity entity = document_.GetSelected();
        if (!entity || entity.GetId() != dragId_ || !entity.HasComponent<TransformComponent>())
            return;
        TransformComponent value = entity.GetComponent<TransformComponent>();
        if (tool_ == EditorTool::Move) {
            double x = dragInitial_[0] + world.x() - dragStartWorld_.x();
            double y = dragInitial_[1] + world.y() - dragStartWorld_.y();
            if (snapToGrid_) {
                x = std::round(x);
                y = std::round(y);
            }
            value.position.x = static_cast<float>(x);
            value.position.y = static_cast<float>(y);
        } else if (tool_ == EditorTool::Rotate) {
            const QPointF center(dragInitial_[0], dragInitial_[1]);
            const double start =
                std::atan2(dragStartWorld_.y() - center.y(), dragStartWorld_.x() - center.x());
            const double current = std::atan2(world.y() - center.y(), world.x() - center.x());
            value.rotation.z = dragInitial_[5] + static_cast<float>(current - start);
        } else if (tool_ == EditorTool::Scale) {
            const QPointF center(dragInitial_[0], dragInitial_[1]);
            const double initial =
                std::hypot(dragStartWorld_.x() - center.x(), dragStartWorld_.y() - center.y());
            const double current = std::hypot(world.x() - center.x(), world.y() - center.y());
            const float factor =
                static_cast<float>(std::clamp(current / std::max(.01, initial), .05, 100.0));
            value.scale.x = dragInitial_[6] * factor;
            value.scale.y = dragInitial_[7] * factor;
        }
        if (std::isfinite(value.position.x) && std::isfinite(value.position.y) &&
            std::isfinite(value.rotation.z) && std::isfinite(value.scale.x) &&
            std::isfinite(value.scale.y))
            onTransform_(dragId_, value);
    }
    lastMouse_ = event->position();
}
void SceneViewport2D::PaintAt(QPointF point) {
    const QPointF world = ScreenToWorld(point);
    Entity mapEntity = document_.GetSelected();
    if (!mapEntity || !mapEntity.HasComponent<TilemapComponent>()) {
        for (Entity candidate : document_.GetScene().GetEntities()) {
            if (!candidate.HasComponent<TilemapComponent>())
                continue;
            const auto& map = candidate.GetComponent<TilemapComponent>();
            const auto& position = candidate.GetComponent<TransformComponent>().position;
            if (world.x() >= position.x && world.y() >= position.y &&
                world.x() < position.x + map.width * map.tileSize &&
                world.y() < position.y + map.height * map.tileSize) {
                mapEntity = candidate;
                onSelect_(candidate.GetId());
                break;
            }
        }
    }
    if (!mapEntity || !mapEntity.HasComponent<TilemapComponent>())
        return;
    const auto& map = mapEntity.GetComponent<TilemapComponent>();
    const auto& position = mapEntity.GetComponent<TransformComponent>().position;
    const int x = static_cast<int>(std::floor((world.x() - position.x) / map.tileSize));
    const int y = static_cast<int>(std::floor((world.y() - position.y) / map.tileSize));
    if (x >= 0 && y >= 0 && x < static_cast<int>(map.width) && y < static_cast<int>(map.height))
        onPaintTile_(mapEntity.GetId(), static_cast<uint32>(x), static_cast<uint32>(y),
                     tool_ == EditorTool::TileErase ? 0 : tileId_);
}
void SceneViewport2D::wheelEvent(QWheelEvent* event) {
    const QPointF world = ScreenToWorld(event->position());
    pixelsPerUnit_ =
        std::clamp(pixelsPerUnit_ * std::pow(1.2, event->angleDelta().y() / 120.0), 16.0, 320.0);
    pan_ = {event->position().x() - width() * .5 - world.x() * pixelsPerUnit_,
            event->position().y() - height() * .5 + world.y() * pixelsPerUnit_};
    update();
}
} // namespace LuxEngine
