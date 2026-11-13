#include <LuxEditor/Scene/EditorDocument.hpp>
#include <LuxEditor/Scene/SelectionTransform.hpp>
#include <LuxEditor/Viewport/EngineSceneRenderer.hpp>
#include <LuxEditor/Viewport/SceneViewport2D.hpp>
#include <LuxEditor/Viewport/ViewportCamera3D.hpp>
#include <LuxEngine/ECS/ECS.hpp>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
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

void SceneViewport2D::SetCancelEditCallback(std::function<void()> callback) {
    onCancelEdit_ = std::move(callback);
}

void SceneViewport2D::BeginEdit() {
    gestureTab_ = document_.GetActiveTabId();
    gestureLifetime_ = document_.GetScene().Lifetime();
    gestureSpace_ = static_cast<int>(document_.GetTransformSpace());
    gesturePivotMode_ = static_cast<int>(document_.GetSelectionPivotMode());
    dirtyBefore_ = document_.IsDirty();
    selectionBefore_ = document_.GetSelection();
    primaryBefore_ = document_.GetSelectedId(gestureTab_);
    fallbackBefore_.clear();
    if (!onCancelEdit_)
        document_.GetScene().SerializeToString(fallbackBefore_);
    onBeginEdit_();
}

bool SceneViewport2D::GestureSceneValid() const {
    if (gestureLifetime_.expired() || gestureTab_ != document_.GetActiveTabId())
        return false;
    const auto lifetime = document_.GetScene().Lifetime();
    return !gestureLifetime_.owner_before(lifetime) && !lifetime.owner_before(gestureLifetime_) &&
           selectionBefore_ == document_.GetSelection() &&
           primaryBefore_ == document_.GetSelectedId(gestureTab_) &&
           gestureSpace_ == static_cast<int>(document_.GetTransformSpace()) &&
           gesturePivotMode_ == static_cast<int>(document_.GetSelectionPivotMode());
}

void SceneViewport2D::CancelInteraction() {
    const bool editing = dragging_ || tilePainting_;
    const auto origin = gestureTab_;
    const auto* scene = document_.GetScene(origin);
    const auto lifetime = scene ? scene->Lifetime() : std::weak_ptr<int>{};
    const bool valid = scene && !gestureLifetime_.expired() &&
                       !gestureLifetime_.owner_before(lifetime) &&
                       !lifetime.owner_before(gestureLifetime_);
    panning_ = dragging_ = tilePainting_ = selecting_ = selectionBox_ = false;
    if (QWidget::mouseGrabber() == this)
        releaseMouse();
    dragId_ = 0;
    dragAxis_ = -1;
    gestureTab_ = 0;
    gestureLifetime_.reset();
    if (editing) {
        if (onCancelEdit_) {
            onCancelEdit_();
        } else if (valid && !fallbackBefore_.empty()) {
            if (document_.ReplaceSceneFromData(origin, fallbackBefore_, primaryBefore_)) {
                document_.SetSelection(origin, selectionBefore_, primaryBefore_);
                document_.SetDirty(origin, dirtyBefore_);
            }
        }
    }
    fallbackBefore_.clear();
    update();
}

void SceneViewport2D::focusOutEvent(QFocusEvent* event) {
    CancelInteraction();
    QWidget::focusOutEvent(event);
}

bool SceneViewport2D::event(QEvent* event) {
    if (event->type() == QEvent::ShortcutOverride &&
        static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape &&
        (dragging_ || tilePainting_ || selecting_)) {
        event->accept();
        return true;
    }
    return QWidget::event(event);
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

void SceneViewport2D::FrameBounds(bool selectedOnly) {
    QRectF bounds;
    bool found = false;
    for (auto entity : document_.GetScene().GetEntities()) {
        if (!entity.HasComponent<LuxEngine::TransformComponent>() ||
            (selectedOnly && !document_.IsSelected(entity.GetId())))
            continue;
        QRectF local(-.5, -.5, 1, 1);
        if (auto* map = entity.TryGetComponent<LuxEngine::TilemapComponent>())
            local = {0, 0, map->width * map->tileSize, map->height * map->tileSize};
        const auto rectangle = World(entity).mapRect(local);
        if (!std::isfinite(rectangle.x()) || !std::isfinite(rectangle.y()) ||
            !std::isfinite(rectangle.width()) || !std::isfinite(rectangle.height()))
            continue;
        bounds = found ? bounds.united(rectangle) : rectangle;
        found = true;
    }
    if (!found)
        return;
    pixelsPerUnit_ =
        std::clamp(std::min(std::max(1, width() - 100) / std::max(.1, bounds.width()),
                            std::max(1, height() - 100) / std::max(.1, bounds.height())),
                   .01, 2048.0);
    pan_ = {-bounds.center().x() * pixelsPerUnit_, bounds.center().y() * pixelsPerUnit_};
    update();
}

void SceneViewport2D::FocusSelection() {
    FrameBounds(true);
}

void SceneViewport2D::FrameAll() {
    FrameBounds(false);
}

void SceneViewport2D::SetGridVisible(bool visible) {
    gridVisible_ = visible;
    update();
}

void SceneViewport2D::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape && (dragging_ || tilePainting_ || selecting_)) {
        CancelInteraction();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_F && event->modifiers() == Qt::NoModifier) {
        FocusSelection();
        return;
    }
    if (event->key() == Qt::Key_Home) {
        FrameAll();
        return;
    }
    QWidget::keyPressEvent(event);
}

void SceneViewport2D::DrawGrid(QPainter& painter, int alpha) const {
    if (!gridVisible_)
        return;
    const double spacing = ViewportGridSpacing(1 / pixelsPerUnit_);
    const double pixels = spacing * pixelsPerUnit_;
    const double originX = width() * .5 + pan_.x();
    const double originY = height() * .5 + pan_.y();
    const auto line = [&](double index, bool vertical) {
        QColor color =
            std::abs(std::remainder(index, 5.0)) < .1 ? QColor(75, 77, 82) : QColor(47, 49, 53);
        if (std::abs(index) < .01)
            color = vertical ? QColor(75, 135, 87) : QColor(155, 75, 77);
        color.setAlpha(alpha);
        painter.setPen(QPen(color, std::abs(index) < .01 ? 1.5 : 1));
        if (vertical) {
            const double x = originX + index * pixels;
            painter.drawLine(QPointF(x, 0), QPointF(x, height()));
        } else {
            const double y = originY - index * pixels;
            painter.drawLine(QPointF(0, y), QPointF(width(), y));
        }
    };
    const double firstX = std::floor(-originX / pixels);
    const double firstY = std::floor((originY - height()) / pixels);
    for (int index = 0; index <= int(std::ceil(width() / pixels)) + 1; ++index)
        line(firstX + index, true);
    for (int index = 0; index <= int(std::ceil(height() / pixels)) + 1; ++index)
        line(firstY + index, false);
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
    painter.fillRect(rect(), QColor(29, 30, 33));
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
        if (document_.IsSelected(entity.GetId())) {
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
        const bool active = document_.IsSelected(entity.GetId());
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
    DrawGizmo(painter);
    if (selecting_ && selectionBox_) {
        painter.setPen(QPen(QColor(91, 161, 229), 1, Qt::DashLine));
        painter.setBrush(QColor(70, 135, 210, 38));
        painter.drawRect(QRectF(selectionStart_, selectionEnd_).normalized());
    }
}

int SceneViewport2D::HitGizmo(QPointF point) const {
    const auto selected = document_.GetSelected();
    if (tool_ == EditorTool::Select || !selected ||
        !selected.HasComponent<LuxEngine::TransformComponent>())
        return -1;
    const auto pivot = GetSelectionPivot(document_);
    const auto origin = WorldToScreen(pivot.x, pivot.y);
    const double radius = QLineF(origin, point).length();
    if (tool_ == EditorTool::Rotate)
        return std::abs(radius - 54) <= 8 ? 2 : -1;
    if (radius <= 9)
        return 2;
    const auto transform = document_.GetScene().GetWorldTransform(selected.GetId());
    const double angle =
        document_.GetTransformSpace() == TransformSpace::Local ? transform.rotation.z : 0;
    const std::array<QPointF, 2> directions{QPointF(std::cos(angle), -std::sin(angle)),
                                            QPointF(-std::sin(angle), -std::cos(angle))};
    for (int axis = 0; axis < 2; ++axis) {
        const auto offset = point - origin;
        const double along = QPointF::dotProduct(offset, directions[axis]);
        const double perpendicular =
            std::abs(offset.x() * directions[axis].y() - offset.y() * directions[axis].x());
        if (along >= 14 && along <= 86 && perpendicular <= 8)
            return axis;
    }
    return -1;
}

void SceneViewport2D::DrawGizmo(QPainter& painter) const {
    const auto selected = document_.GetSelected();
    if (tool_ == EditorTool::Select || tool_ == EditorTool::TileBrush ||
        tool_ == EditorTool::TileErase || !selected ||
        !selected.HasComponent<LuxEngine::TransformComponent>())
        return;
    const auto pivot = GetSelectionPivot(document_);
    const auto origin = WorldToScreen(pivot.x, pivot.y);
    painter.setBrush(Qt::NoBrush);
    if (tool_ == EditorTool::Rotate) {
        painter.setPen(QPen(QColor(98, 148, 215), 2));
        painter.drawEllipse(origin, 54, 54);
    } else {
        const auto transform = document_.GetScene().GetWorldTransform(selected.GetId());
        const double angle =
            document_.GetTransformSpace() == TransformSpace::Local ? transform.rotation.z : 0;
        const std::array<QPointF, 2> directions{QPointF(std::cos(angle), -std::sin(angle)),
                                                QPointF(-std::sin(angle), -std::cos(angle))};
        const std::array<QColor, 2> colors{QColor(220, 91, 100), QColor(100, 194, 124)};
        for (int axis = 0; axis < 2; ++axis) {
            const auto tip = origin + directions[axis] * 78;
            painter.setPen(QPen(colors[axis], 2));
            painter.setBrush(colors[axis]);
            painter.drawLine(origin, tip);
            if (tool_ == EditorTool::Scale)
                painter.drawRect(QRectF(tip - QPointF(4, 4), QSizeF(8, 8)));
            else {
                const QPointF side(-directions[axis].y(), directions[axis].x());
                painter.drawPolygon(QPolygonF{tip, tip - directions[axis] * 11 + side * 4,
                                              tip - directions[axis] * 11 - side * 4});
            }
            painter.drawText(tip + QPointF(7, -5),
                             axis == 0 ? QStringLiteral("X") : QStringLiteral("Y"));
        }
    }
    painter.setPen(QPen(QColor(225, 225, 225), 1));
    painter.setBrush(QColor(94, 102, 112));
    painter.drawRect(QRectF(origin - QPointF(4, 4), QSizeF(8, 8)));
}

void SceneViewport2D::mousePressEvent(QMouseEvent* event) {
    if (dragging_ || tilePainting_ || selecting_)
        CancelInteraction();
    lastMouse_ = event->position();
    if (event->button() == Qt::MiddleButton) {
        panning_ = true;
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;
    setFocus();
    if (tool_ == EditorTool::TileBrush || tool_ == EditorTool::TileErase) {
        const auto world = ScreenToWorld(event->position());
        auto map = document_.GetSelected();
        if (!map || !InsideMap(map, world)) {
            map = {};
            const auto entities = document_.GetScene().GetEntitiesIn2DLayerOrder();
            for (auto iterator = entities.rbegin(); iterator != entities.rend(); ++iterator)
                if (InsideMap(*iterator, world)) {
                    map = *iterator;
                    break;
                }
        }
        if (!map)
            return;
        if (document_.GetSelected() != map) {
            document_.SetSelection({map.GetId()}, map.GetId());
            onSelect_(map.GetId());
        }
        dragId_ = map.GetId();
        tilePainting_ = true;
        BeginEdit();
        PaintAt(event->position());
        return;
    }
    const QPointF world = ScreenToWorld(event->position());
    std::optional<LuxEngine::uint32> hit;
    dragAxis_ = event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)
                    ? -1
                    : HitGizmo(event->position());
    if (dragAxis_ >= 0 && document_.GetSelected())
        hit = document_.GetSelected().GetId();
    const bool gizmoHit = hit.has_value();
    const auto entities = document_.GetScene().GetEntitiesIn2DLayerOrder();
    // Reverse the actual painter passes: sprites are in front of the tilemap pass.
    for (const bool sprites : {true, false}) {
        if (gizmoHit)
            break;
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
    const auto modifiers = event->modifiers();
    if (hit && (modifiers & (Qt::ControlModifier | Qt::ShiftModifier))) {
        document_.ModifySelection(hit, modifiers & Qt::ControlModifier ? SelectionMode::Toggle
                                                                       : SelectionMode::Add);
        onSelect_(document_.GetSelectedId(document_.GetActiveTabId()));
        update();
        return;
    }
    if (!hit) {
        selecting_ = true;
        selectionBox_ = false;
        selectionStart_ = selectionEnd_ = event->position();
        selectionModifiers_ = modifiers;
        selectionBefore_ = document_.GetSelection();
        primaryBefore_ = document_.GetSelectedId(document_.GetActiveTabId());
        gestureTab_ = document_.GetActiveTabId();
        gestureLifetime_ = document_.GetScene().Lifetime();
        gestureSpace_ = static_cast<int>(document_.GetTransformSpace());
        gesturePivotMode_ = static_cast<int>(document_.GetSelectionPivotMode());
        return;
    }
    if (!document_.IsSelected(*hit))
        document_.SetSelection({*hit}, hit);
    else
        document_.SetSelection(document_.GetSelection(), hit);
    onSelect_(document_.GetSelectedId(document_.GetActiveTabId()));
    LuxEngine::Entity selected = document_.GetSelected();
    if (!selected || !selected.HasComponent<LuxEngine::TransformComponent>())
        return;
    LuxEngine::TransformComponent transform;
    try {
        transform = selected.GetScene()->GetWorldTransform(selected.GetId());
        const auto pivot = GetSelectionPivot(document_);
        dragPivot_ = {pivot.x, pivot.y};
    } catch (const std::logic_error&) {
        return;
    }
    dragStartWorld_ = world;
    dragInitial_ = {transform.position.x, transform.position.y, transform.position.z,
                    transform.rotation.x, transform.rotation.y, transform.rotation.z,
                    transform.scale.x,    transform.scale.y,    transform.scale.z};
    dragId_ = selected.GetId();
    dragging_ = true;
    BeginEdit();
}

void SceneViewport2D::FinishSelection() {
    if (!GestureSceneValid()) {
        CancelInteraction();
        return;
    }
    auto selection = selectionModifiers_ & (Qt::ControlModifier | Qt::ShiftModifier)
                         ? selectionBefore_
                         : std::vector<LuxEngine::uint32>{};
    auto primary = selectionModifiers_ & (Qt::ControlModifier | Qt::ShiftModifier)
                       ? primaryBefore_
                       : std::optional<LuxEngine::uint32>{};
    const QRectF rectangle = QRectF(selectionStart_, selectionEnd_).normalized();
    if (selectionBox_) {
        QPainterPath area;
        area.addRect(rectangle);
        for (auto entity : document_.GetScene().GetEntitiesIn2DLayerOrder()) {
            if (!entity.HasComponent<LuxEngine::TransformComponent>())
                continue;
            QRectF local(-.5, -.5, 1, 1);
            if (const auto* map = entity.TryGetComponent<LuxEngine::TilemapComponent>())
                local = {0, 0, map->width * map->tileSize, map->height * map->tileSize};
            else if (!entity.HasComponent<LuxEngine::SpriteRendererComponent>())
                continue;
            QPolygonF polygon;
            for (const auto& corner :
                 {local.topLeft(), local.topRight(), local.bottomRight(), local.bottomLeft()}) {
                const auto world = World(entity).map(corner);
                polygon << WorldToScreen(world.x(), world.y());
            }
            QPainterPath shape;
            shape.addPolygon(polygon);
            if (!area.intersects(shape) && !area.contains(shape))
                continue;
            const auto found = std::find(selection.begin(), selection.end(), entity.GetId());
            if ((selectionModifiers_ & Qt::ControlModifier) && found != selection.end())
                selection.erase(found);
            else if (found == selection.end()) {
                selection.push_back(entity.GetId());
                primary = entity.GetId();
            }
        }
    }
    selecting_ = selectionBox_ = false;
    gestureTab_ = 0;
    gestureLifetime_.reset();
    document_.SetSelection(std::move(selection), primary);
    onSelect_(document_.GetSelectedId(document_.GetActiveTabId()));
    update();
}

void SceneViewport2D::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton)
        panning_ = false;
    if (event->button() != Qt::LeftButton)
        return;
    if (selecting_) {
        selectionEnd_ = event->position();
        FinishSelection();
        return;
    }
    if (tilePainting_ || dragging_) {
        if (!GestureSceneValid()) {
            CancelInteraction();
            return;
        }
        tilePainting_ = dragging_ = false;
        dragAxis_ = -1;
        dragId_ = 0;
        gestureTab_ = 0;
        gestureLifetime_.reset();
        fallbackBefore_.clear();
        onEndEdit_();
    }
}

void SceneViewport2D::mouseMoveEvent(QMouseEvent* event) {
    if ((dragging_ || tilePainting_ || selecting_) && !(event->buttons() & Qt::LeftButton)) {
        CancelInteraction();
        return;
    }
    if (panning_ && !(event->buttons() & Qt::MiddleButton))
        panning_ = false;
    if ((dragging_ || tilePainting_ || selecting_) && !GestureSceneValid()) {
        CancelInteraction();
        return;
    }
    if (selecting_ && (event->buttons() & Qt::LeftButton)) {
        selectionEnd_ = event->position();
        selectionBox_ = selectionBox_ || QLineF(selectionStart_, selectionEnd_).length() >= 4;
        update();
    } else if (panning_ && (event->buttons() & Qt::MiddleButton)) {
        pan_ += event->position() - lastMouse_;
        update();
    } else if (tilePainting_ && (event->buttons() & Qt::LeftButton)) {
        PaintAt(event->position());
    } else if (dragging_ && (event->buttons() & Qt::LeftButton)) {
        const QPointF world = ScreenToWorld(event->position());
        LuxEngine::Entity entity = document_.GetSelected();
        if (!entity || entity.GetId() != dragId_ ||
            !entity.HasComponent<LuxEngine::TransformComponent>()) {
            CancelInteraction();
            return;
        }
        LuxEngine::TransformComponent value;
        try {
            value = entity.GetScene()->GetWorldTransform(entity.GetId());
        } catch (const std::logic_error&) {
            CancelInteraction();
            return;
        }
        // Each preview is an absolute target derived from the gesture origin. A shared pivot
        // can move the active entity during rotation/scale, so reading its preview position here
        // would accidentally feed that orbit back as an additional translation on the next event.
        value.position = {dragInitial_[0], dragInitial_[1], dragInitial_[2]};
        value.rotation = {dragInitial_[3], dragInitial_[4], dragInitial_[5]};
        value.scale = {dragInitial_[6], dragInitial_[7], dragInitial_[8]};
        if (tool_ == EditorTool::Move || tool_ == EditorTool::Select) {
            QPointF movement = world - dragStartWorld_;
            if (dragAxis_ == 0 || dragAxis_ == 1) {
                const double angle =
                    document_.GetTransformSpace() == TransformSpace::Local ? dragInitial_[5] : 0;
                const QPointF direction = dragAxis_ == 0
                                              ? QPointF(std::cos(angle), std::sin(angle))
                                              : QPointF(-std::sin(angle), std::cos(angle));
                movement = direction * QPointF::dotProduct(movement, direction);
            }
            double x = dragInitial_[0] + movement.x();
            double y = dragInitial_[1] + movement.y();
            if (snapToGrid_ && !(event->modifiers() & Qt::AltModifier)) {
                x = std::round(x);
                y = std::round(y);
            }
            value.position.x = static_cast<float>(x);
            value.position.y = static_cast<float>(y);
        } else if (tool_ == EditorTool::Rotate) {
            const double start = std::atan2(dragStartWorld_.y() - dragPivot_.y(),
                                            dragStartWorld_.x() - dragPivot_.x());
            const double current =
                std::atan2(world.y() - dragPivot_.y(), world.x() - dragPivot_.x());
            float angle = static_cast<float>(current - start);
            if (event->modifiers() & Qt::ControlModifier)
                angle = std::round(angle / LuxEngine::ToRadians(15)) * LuxEngine::ToRadians(15);
            value.rotation.z = dragInitial_[5] + angle;
        } else if (tool_ == EditorTool::Scale) {
            double factor;
            if (dragAxis_ == 0 || dragAxis_ == 1) {
                const double angle =
                    document_.GetTransformSpace() == TransformSpace::Local ? dragInitial_[5] : 0;
                const QPointF direction = dragAxis_ == 0
                                              ? QPointF(std::cos(angle), std::sin(angle))
                                              : QPointF(-std::sin(angle), std::cos(angle));
                factor = std::exp(QPointF::dotProduct(world - dragStartWorld_, direction) *
                                  pixelsPerUnit_ * .01);
            } else {
                const double initial = QLineF(dragStartWorld_, dragPivot_).length();
                const double current = QLineF(world, dragPivot_).length();
                factor = current / std::max(.01, initial);
            }
            const float scale = static_cast<float>(std::clamp(factor, .05, 100.0));
            value.scale.x = dragInitial_[6] * (dragAxis_ != 1 ? scale : 1);
            value.scale.y = dragInitial_[7] * (dragAxis_ != 0 ? scale : 1);
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
    const auto entity = document_.GetSelected();
    if (!entity || entity.GetId() != dragId_) {
        CancelInteraction();
        return;
    }
    QPointF cell;
    if (InsideMap(entity, world, &cell))
        onPaintTile_(entity.GetId(), static_cast<LuxEngine::uint32>(cell.x()),
                     static_cast<LuxEngine::uint32>(cell.y()),
                     tool_ == EditorTool::TileErase ? 0 : tileId_);
}

void SceneViewport2D::wheelEvent(QWheelEvent* event) {
    const QPointF world = ScreenToWorld(event->position());
    pixelsPerUnit_ =
        std::clamp(pixelsPerUnit_ * std::pow(1.2, event->angleDelta().y() / 120.0), .01, 2048.0);
    pan_ = {event->position().x() - width() * .5 - world.x() * pixelsPerUnit_,
            event->position().y() - height() * .5 + world.y() * pixelsPerUnit_};
    update();
}

void SceneViewport2D::RefreshScene() {
    update();
}

void SceneViewport2D::SetSnapToGrid(bool enabled) {
    snapToGrid_ = enabled;
    update();
}

void SceneViewport2D::SetTool(EditorTool tool) {
    if (tool_ != tool)
        CancelInteraction();
    tool_ = tool;
}

void SceneViewport2D::SetTileId(uint8_t tile) {
    tileId_ = tile;
}

void SceneViewport2D::SetDebugColliders(bool enabled) {
    debugColliders_ = enabled;
    update();
}
} // namespace LuxEditor
