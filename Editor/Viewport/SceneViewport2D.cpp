#include "SceneViewport2D.hpp"
#include "Engine/ECS/ECS.hpp"
#include "EngineSceneRenderer.hpp"
#include "Scene/EditorDocument.hpp"
#include <QFocusEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTransform>
#include <QWheelEvent>
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace LuxEditor {
namespace {
QTransform World(LuxEngine::Entity entity) {
    const auto matrix = entity.GetScene()->GetWorldMatrix(entity.GetId());
    const auto* m = matrix.elements;
    return QTransform(m[0], m[1], m[4], m[5], m[12], m[13]);
}
bool LocalPoint(LuxEngine::Entity entity, QPointF world, QPointF& local) {
    bool valid = false;
    const auto inverse = World(entity).inverted(&valid);
    if (!valid)
        return false;
    local = inverse.map(world);
    return std::isfinite(local.x()) && std::isfinite(local.y());
}
bool InsideMap(LuxEngine::Entity entity, QPointF world, QPointF* cell = nullptr) {
    const auto* map = entity.TryGetComponent<LuxEngine::TilemapComponent>();
    QPointF local;
    if (!map || !entity.HasComponent<LuxEngine::TransformComponent>() ||
        !std::isfinite(map->tileSize) || map->tileSize <= 0 || !LocalPoint(entity, world, local))
        return false;
    const QPointF candidate(std::floor(local.x() / map->tileSize),
                            std::floor(local.y() / map->tileSize));
    if (!(candidate.x() >= 0 && candidate.y() >= 0 && candidate.x() < map->width &&
          candidate.y() < map->height))
        return false;
    if (cell)
        *cell = candidate;
    return true;
}
QColor Exposed(QColor color, float exposure) {
    return QColor::fromRgbF(std::clamp(color.redF() * exposure, 0.0f, 1.0f),
                            std::clamp(color.greenF() * exposure, 0.0f, 1.0f),
                            std::clamp(color.blueF() * exposure, 0.0f, 1.0f), color.alphaF());
}
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
QString Name(LuxEngine::Entity entity) {
    return entity.HasComponent<LuxEngine::TagComponent>()
               ? QString::fromUtf8(entity.GetComponent<LuxEngine::TagComponent>().tag)
               : QStringLiteral("엔티티 #%1").arg(entity.GetId());
}
} // namespace
SceneViewport2D::SceneViewport2D(
    QWidget* parent, EditorDocument& document,
    std::function<void(std::optional<LuxEngine::uint32>)> onSelect,
    std::function<void(LuxEngine::uint32, const LuxEngine::TransformComponent&)> onTransform,
    std::function<void(LuxEngine::uint32, LuxEngine::uint32, LuxEngine::uint32, uint8_t)>
        onPaintTile,
    std::function<void()> onBeginEdit, std::function<void()> onEndEdit,
    std::shared_ptr<EngineSceneRenderer> renderer)
    : QWidget(parent), document_(document), onSelect_(std::move(onSelect)),
      onTransform_(std::move(onTransform)), onPaintTile_(std::move(onPaintTile)),
      onBeginEdit_(std::move(onBeginEdit)), onEndEdit_(std::move(onEndEdit)),
      engineRenderer_(std::move(renderer)) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(300, 200);
}
SceneViewport2D::~SceneViewport2D() = default;
void SceneViewport2D::CancelInteraction() {
    panning_ = dragging_ = tilePainting_ = false;
    dragId_ = 0;
}
void SceneViewport2D::focusOutEvent(QFocusEvent* event) {
    const bool editing = dragging_ || tilePainting_;
    CancelInteraction();
    if (editing)
        onEndEdit_();
    QWidget::focusOutEvent(event);
}
QPointF SceneViewport2D::WorldToScreen(double x, double y) const {
    return {width() * 0.5 + pan_.x() + x * pixelsPerUnit_,
            height() * 0.5 + pan_.y() - y * pixelsPerUnit_};
}
QPointF SceneViewport2D::ScreenToWorld(QPointF point) const {
    return {(point.x() - width() * 0.5 - pan_.x()) / pixelsPerUnit_,
            (height() * 0.5 + pan_.y() - point.y()) / pixelsPerUnit_};
}
void SceneViewport2D::GetView(double& centerX, double& centerY, double& halfHeight) const {
    const QPointF center = ScreenToWorld(QPointF(width() * 0.5, height() * 0.5));
    centerX = center.x();
    centerY = center.y();
    halfHeight = std::max(1.0, height() * 0.5) / pixelsPerUnit_;
}
void SceneViewport2D::FocusSelection() {
    LuxEngine::Entity selected = document_.GetSelected();
    if (selected && selected.HasComponent<LuxEngine::TransformComponent>()) {
        auto position = selected.GetScene()->GetWorldTransform(selected.GetId()).position;
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
    // Decouple GPU render/readback phase from QPainter lifecycle.
    // Vulkan offscreen rendering must complete before QPainter opens on this QWidget
    // to prevent re-entrant Qt event loop message dispatch from invalidating the raster paint
    // engine.
    engineFrame_ = false;
    if (EngineSceneRenderer::Allowed()) {
        if (!engineRenderer_)
            engineRenderer_ = std::make_shared<EngineSceneRenderer>();
        const qreal ratio = devicePixelRatioF();
        const int pxW = static_cast<int>(std::lround(width() * ratio));
        const int pxH = static_cast<int>(std::lround(height() * ratio));
        if (pxW > 0 && pxH > 0) {
            const QPointF topLeft = ScreenToWorld(QPointF(0, 0));
            const QPointF bottomRight = ScreenToWorld(QPointF(width(), height()));
            if (engineRenderer_->Render(document_.GetScene(), pxW, pxH,
                                        QRectF(topLeft, bottomRight), cachedFrame_)) {
                engineFrame_ = true;
            }
        }
    }

    QPainter painter(this);
    if (!painter.isActive())
        return;

    const auto& settings = document_.GetScene().GetSettings();
    painter.fillRect(rect(), QColor::fromRgbF(settings.clearColor.x, settings.clearColor.y,
                                              settings.clearColor.z, settings.clearColor.w));
    const auto polygon = [this](LuxEngine::Entity entity, QRectF bounds) {
        QPolygonF shape;
        const auto matrix = World(entity);
        for (auto point :
             {bounds.topLeft(), bounds.topRight(), bounds.bottomRight(), bounds.bottomLeft()}) {
            const auto world = matrix.map(point);
            shape << WorldToScreen(world.x(), world.y());
        }
        return shape;
    };
    painter.setRenderHint(QPainter::Antialiasing);

    DrawGrid(painter, 255);
    if (engineFrame_ && !cachedFrame_.isNull()) {
        painter.drawImage(rect(), cachedFrame_);
    }
    const auto entities = document_.GetScene().GetEntitiesIn2DLayerOrder();
    const LuxEngine::Entity selected = document_.GetSelected();
    for (LuxEngine::Entity entity : entities) {
        if (!entity.HasComponent<LuxEngine::TilemapComponent>() ||
            !entity.HasComponent<LuxEngine::TransformComponent>())
            continue;
        const auto& map = entity.GetComponent<LuxEngine::TilemapComponent>();
        if (!std::isfinite(map.tileSize) || map.tileSize <= 0)
            continue;
        for (LuxEngine::uint32 y = 0; y < map.height; ++y)
            for (LuxEngine::uint32 x = 0; x < map.width; ++x) {
                const auto tile = map.Get(x, y);
                if (!tile)
                    continue;
                const auto shape = polygon(
                    entity, {x * map.tileSize, y * map.tileSize, map.tileSize, map.tileSize});
                if (!shape.boundingRect().intersects(rect()))
                    continue;
                if (engineFrame_)
                    continue; // GPU geometry already owns tile occlusion.
                painter.setPen(QPen(QColor(20, 24, 26, 130), 1));
                painter.setBrush(engineFrame_
                                     ? QBrush(Qt::NoBrush)
                                     : QBrush(Exposed(TileColor(tile), settings.exposure)));
                painter.drawPolygon(shape);
            }
        if (selected && selected.GetId() == entity.GetId()) {
            painter.setPen(QPen(Selection, 2, Qt::DashLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawPolygon(
                polygon(entity, {0, 0, map.width * map.tileSize, map.height * map.tileSize}));
        }
    }
    for (LuxEngine::Entity entity : entities) {
        if (!entity.HasComponent<LuxEngine::TransformComponent>() ||
            !entity.HasComponent<LuxEngine::SpriteRendererComponent>())
            continue;
        const auto matrix = entity.GetScene()->GetWorldMatrix(entity.GetId());
        const auto color = entity.GetComponent<LuxEngine::SpriteRendererComponent>().color;
        if (std::any_of(std::begin(matrix.elements), std::end(matrix.elements),
                        [](float value) { return !std::isfinite(value); }))
            continue;
        const QPolygonF shape = polygon(entity, {-.5, -.5, 1, 1});
        const bool active = selected && selected.GetId() == entity.GetId();
        painter.setPen(QPen(active ? Selection : QColor(178, 189, 204), active ? 3 : 1));
        if (engineFrame_)
            painter.setBrush(Qt::NoBrush);
        else
            painter.setBrush(QColor::fromRgbF(std::clamp(color.x * settings.exposure, 0.0f, 1.0f),
                                              std::clamp(color.y * settings.exposure, 0.0f, 1.0f),
                                              std::clamp(color.z * settings.exposure, 0.0f, 1.0f),
                                              std::clamp(color.w, 0.0f, 1.0f)));
        painter.drawPolygon(shape);
        painter.setPen(active ? Selection : QColor(215, 222, 231));
        if (active)
            painter.drawText(shape.boundingRect().topLeft() - QPointF(0, 6), Name(entity));
    }
    if (debugColliders_) {
        for (LuxEngine::Entity entity : entities) {
            if (!entity.HasComponent<LuxEngine::TransformComponent>() ||
                !entity.HasComponent<LuxEngine::BoxCollider2DComponent>())
                continue;
            const auto& collider = entity.GetComponent<LuxEngine::BoxCollider2DComponent>();
            painter.setPen(QPen(collider.isTrigger ? QColor(255, 179, 92) : QColor(73, 220, 230), 2,
                                Qt::DashLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawPolygon(polygon(entity, {collider.offset.x - collider.size.x * .5,
                                                 collider.offset.y - collider.size.y * .5,
                                                 collider.size.x, collider.size.y}));
        }
    }
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
    std::optional<LuxEngine::uint32> hit;
    const auto entities = document_.GetScene().GetEntitiesIn2DLayerOrder();
    // Reverse the actual painter passes: sprites are in front of the tilemap pass.
    for (const bool sprites : {true, false}) {
        for (auto it = entities.rbegin(); it != entities.rend(); ++it) {
            LuxEngine::Entity entity = *it;
            if (!entity.HasComponent<LuxEngine::TransformComponent>())
                continue;
            if (!sprites && InsideMap(entity, world)) {
                hit = entity.GetId();
                break;
            }
            if (!sprites || !entity.HasComponent<LuxEngine::SpriteRendererComponent>())
                continue;
            QPointF local;
            if (LocalPoint(entity, world, local) && std::abs(local.x()) <= .5 &&
                std::abs(local.y()) <= .5) {
                hit = entity.GetId();
                break;
            }
        }
        if (hit)
            break;
    }
    const auto selectedGroup = document_.GetSelected();
    if (selectedGroup && selectedGroup.HasComponent<LuxEngine::HierarchyComponent>() &&
        selectedGroup.GetComponent<LuxEngine::HierarchyComponent>().group) {
        if (hit) {
            auto ancestor = *hit;
            while (ancestor && ancestor != selectedGroup.GetId())
                ancestor = document_.GetScene().GetParent(ancestor);
            if (ancestor == selectedGroup.GetId())
                hit = ancestor;
        } else {
            const auto origin = World(selectedGroup).map(QPointF());
            if (std::hypot(origin.x() - world.x(), origin.y() - world.y()) * pixelsPerUnit_ <= 12)
                hit = selectedGroup.GetId();
        }
    }
    onSelect_(hit);
    if (hit) {
        LuxEngine::Entity selected = document_.GetSelected();
        LuxEngine::TransformComponent transform;
        try {
            transform = selected.GetScene()->GetWorldTransform(selected.GetId());
        } catch (const std::logic_error&) {
            return;
        }
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
        LuxEngine::Entity entity = document_.GetSelected();
        if (!entity || entity.GetId() != dragId_ ||
            !entity.HasComponent<LuxEngine::TransformComponent>())
            return;
        LuxEngine::TransformComponent value;
        try {
            value = entity.GetScene()->GetWorldTransform(entity.GetId());
        } catch (const std::logic_error&) {
            CancelInteraction();
            onEndEdit_();
            return;
        }
        if (tool_ == EditorTool::Move || tool_ == EditorTool::Select) {
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
    const auto world = ScreenToWorld(point);
    auto entity = document_.GetSelected();
    QPointF cell;
    if (!entity || !InsideMap(entity, world, &cell)) {
        const auto entities = document_.GetScene().GetEntitiesIn2DLayerOrder();
        entity = {};
        for (auto it = entities.rbegin(); it != entities.rend(); ++it)
            if (InsideMap(*it, world, &cell)) {
                entity = *it;
                onSelect_(entity.GetId());
                break;
            }
    }
    if (entity)
        onPaintTile_(entity.GetId(), static_cast<LuxEngine::uint32>(cell.x()),
                     static_cast<LuxEngine::uint32>(cell.y()),
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
} // namespace LuxEditor
