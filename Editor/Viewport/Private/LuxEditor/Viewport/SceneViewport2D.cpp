#include <LuxEditor/Scene/EditorDocument.hpp>
#include <LuxEditor/Scene/SelectionTransform.hpp>
#include <LuxEditor/Viewport/EngineSceneRenderer.hpp>
#include <LuxEditor/Viewport/SceneViewport2D.hpp>
#include <LuxEditor/Viewport/ViewportCamera3D.hpp>
#include <LuxEngine/ECS/ECS.hpp>
#include <LuxUI/Widgets.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace LuxEditor {
namespace {
LuxUI::Transform World(LuxEngine::Entity entity) {
    const auto matrix = entity.GetScene()->GetWorldMatrix(entity.GetId());
    const auto* m = matrix.elements;
    return LuxUI::Transform(m[0], m[1], m[4], m[5], m[12], m[13]);
}

bool LocalPoint(LuxEngine::Entity entity, LuxUI::PointF world, LuxUI::PointF& local) {
    bool valid = false;
    const auto inverse = World(entity).inverted(&valid);
    if (!valid)
        return false;
    local = inverse.map(world);
    return std::isfinite(local.x()) && std::isfinite(local.y());
}

bool InsideMap(LuxEngine::Entity entity, LuxUI::PointF world, LuxUI::PointF* cell = nullptr) {
    const auto* map = entity.TryGetComponent<LuxEngine::TilemapComponent>();
    LuxUI::PointF local;
    if (!map || !entity.HasComponent<LuxEngine::TransformComponent>() ||
        !std::isfinite(map->tileSize) || map->tileSize <= 0 || !LocalPoint(entity, world, local))
        return false;
    const LuxUI::PointF candidate(std::floor(local.x() / map->tileSize),
                                  std::floor(local.y() / map->tileSize));
    if (!(candidate.x() >= 0 && candidate.y() >= 0 && candidate.x() < map->width &&
          candidate.y() < map->height))
        return false;
    if (cell)
        *cell = candidate;
    return true;
}

LuxUI::Color Exposed(LuxUI::Color color, float exposure) {
    return LuxUI::Color::fromRgbF(std::clamp(color.redF() * exposure, 0.0f, 1.0f),
                                  std::clamp(color.greenF() * exposure, 0.0f, 1.0f),
                                  std::clamp(color.blueF() * exposure, 0.0f, 1.0f), color.alphaF());
}

constexpr LuxUI::Color Selection(255, 205, 92);

LuxUI::Color TileColor(uint8_t tile) {
    switch (tile) {
    case 1:
        return LuxUI::Color(92, 166, 69);
    case 2:
        return LuxUI::Color(110, 79, 54);
    case 3:
        return LuxUI::Color(122, 130, 138);
    case 4:
        return LuxUI::Color(56, 117, 189, 210);
    case 5:
        return LuxUI::Color(194, 168, 107);
    default:
        return LuxUI::Flags::transparent;
    }
}

LuxUI::String Name(LuxEngine::Entity entity) {
    return entity.HasComponent<LuxEngine::TagComponent>()
               ? LuxUI::String::fromUtf8(entity.GetComponent<LuxEngine::TagComponent>().tag)
               : LUX_UI_TEXT("엔티티 #%1").arg(entity.GetId());
}
} // namespace

SceneViewport2D::SceneViewport2D(
    LuxUI::Widget* parent, EditorDocument& document,
    std::function<void(std::optional<LuxEngine::uint32>)> onSelect,
    std::function<void(LuxEngine::uint32, const LuxEngine::TransformComponent&)> onTransform,
    std::function<void(LuxEngine::uint32, LuxEngine::uint32, LuxEngine::uint32, uint8_t)>
        onPaintTile,
    std::function<void()> onBeginEdit, std::function<void()> onEndEdit,
    std::shared_ptr<EngineSceneRenderer> renderer)
    : LuxUI::Widget(parent), document_(document), onSelect_(std::move(onSelect)),
      onTransform_(std::move(onTransform)), onPaintTile_(std::move(onPaintTile)),
      onBeginEdit_(std::move(onBeginEdit)), onEndEdit_(std::move(onEndEdit)),
      engineRenderer_(std::move(renderer)) {
    setMouseTracking(true);
    setFocusPolicy(LuxUI::Flags::StrongFocus);
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
    if (LuxUI::Widget::mouseGrabber() == this)
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

void SceneViewport2D::focusOutEvent(LuxUI::FocusEvent* event) {
    CancelInteraction();
    LuxUI::Widget::focusOutEvent(event);
}

bool SceneViewport2D::event(LuxUI::Event* event) {
    if (event->type() == LuxUI::Event::ShortcutOverride &&
        static_cast<LuxUI::KeyEvent*>(event)->key() == LuxUI::Flags::Key_Escape &&
        (dragging_ || tilePainting_ || selecting_)) {
        event->accept();
        return true;
    }
    return LuxUI::Widget::event(event);
}

LuxUI::PointF SceneViewport2D::WorldToScreen(double x, double y) const {
    return {width() * 0.5 + pan_.x() + x * pixelsPerUnit_,
            height() * 0.5 + pan_.y() - y * pixelsPerUnit_};
}

LuxUI::PointF SceneViewport2D::ScreenToWorld(LuxUI::PointF point) const {
    return {(point.x() - width() * 0.5 - pan_.x()) / pixelsPerUnit_,
            (height() * 0.5 + pan_.y() - point.y()) / pixelsPerUnit_};
}

void SceneViewport2D::GetView(double& centerX, double& centerY, double& halfHeight) const {
    const LuxUI::PointF center = ScreenToWorld(LuxUI::PointF(width() * 0.5, height() * 0.5));
    centerX = center.x();
    centerY = center.y();
    halfHeight = std::max(1.0, height() * 0.5) / pixelsPerUnit_;
}

bool SceneViewport2D::SetView(double centerX, double centerY, double halfHeight) {
    if (!std::isfinite(centerX) || !std::isfinite(centerY) || !std::isfinite(halfHeight) ||
        halfHeight <= 0)
        return false;
    const double scale = std::max(1.0, height() * .5) / halfHeight;
    const LuxUI::PointF pan(-centerX * scale, centerY * scale);
    if (!std::isfinite(scale) || scale <= 0 || !std::isfinite(pan.x()) || !std::isfinite(pan.y()))
        return false;
    CancelInteraction();
    pixelsPerUnit_ = scale;
    pan_ = pan;
    update();
    return true;
}

void SceneViewport2D::FrameBounds(bool selectedOnly) {
    LuxUI::RectF bounds;
    bool found = false;
    for (auto entity : document_.GetScene().GetEntities()) {
        if (!entity.HasComponent<LuxEngine::TransformComponent>() ||
            (selectedOnly && !document_.IsSelected(entity.GetId())))
            continue;
        LuxUI::RectF local(-.5, -.5, 1, 1);
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

void SceneViewport2D::keyPressEvent(LuxUI::KeyEvent* event) {
    if (event->key() == LuxUI::Flags::Key_Escape && (dragging_ || tilePainting_ || selecting_)) {
        CancelInteraction();
        event->accept();
        return;
    }
    if (event->key() == LuxUI::Flags::Key_F && event->modifiers() == LuxUI::Flags::NoModifier) {
        FocusSelection();
        return;
    }
    if (event->key() == LuxUI::Flags::Key_Home) {
        FrameAll();
        return;
    }
    LuxUI::Widget::keyPressEvent(event);
}

void SceneViewport2D::DrawGrid(LuxUI::Painter& painter, int alpha) const {
    if (!gridVisible_)
        return;
    const double spacing = ViewportGridSpacing(1 / pixelsPerUnit_);
    const double pixels = spacing * pixelsPerUnit_;
    const double originX = width() * .5 + pan_.x();
    const double originY = height() * .5 + pan_.y();
    const auto line = [&](double index, bool vertical) {
        LuxUI::Color color = std::abs(std::remainder(index, 5.0)) < .1 ? LuxUI::Color(75, 77, 82)
                                                                       : LuxUI::Color(47, 49, 53);
        if (std::abs(index) < .01)
            color = vertical ? LuxUI::Color(75, 135, 87) : LuxUI::Color(155, 75, 77);
        color.setAlpha(alpha);
        painter.setPen(LuxUI::Pen(color, std::abs(index) < .01 ? 1.5 : 1));
        if (vertical) {
            const double x = originX + index * pixels;
            painter.drawLine(LuxUI::PointF(x, 0), LuxUI::PointF(x, height()));
        } else {
            const double y = originY - index * pixels;
            painter.drawLine(LuxUI::PointF(0, y), LuxUI::PointF(width(), y));
        }
    };
    const double firstX = std::floor(-originX / pixels);
    const double firstY = std::floor((originY - height()) / pixels);
    for (int index = 0; index <= int(std::ceil(width() / pixels)) + 1; ++index)
        line(firstX + index, true);
    for (int index = 0; index <= int(std::ceil(height() / pixels)) + 1; ++index)
        line(firstY + index, false);
}

void SceneViewport2D::paintEvent(LuxUI::PaintEvent*) {
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
            const LuxUI::PointF topLeft = ScreenToWorld(LuxUI::PointF(0, 0));
            const LuxUI::PointF bottomRight = ScreenToWorld(LuxUI::PointF(width(), height()));
            if (engineRenderer_->Render(document_.GetScene(), pxW, pxH,
                                        LuxUI::RectF(topLeft, bottomRight), cachedFrame_)) {
                engineFrame_ = true;
            }
        }
    }

    LuxUI::Painter painter(this);
    if (!painter.isActive())
        return;

    const auto& settings = document_.GetScene().GetSettings();
    painter.fillRect(rect(), LuxUI::Color(29, 30, 33));
    const auto polygon = [this](LuxEngine::Entity entity, LuxUI::RectF bounds) {
        LuxUI::PolygonF shape;
        const auto matrix = World(entity);
        for (auto point :
             {bounds.topLeft(), bounds.topRight(), bounds.bottomRight(), bounds.bottomLeft()}) {
            const auto world = matrix.map(point);
            shape << WorldToScreen(world.x(), world.y());
        }
        return shape;
    };
    painter.setRenderHint(LuxUI::Painter::Antialiasing);

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
                painter.setPen(LuxUI::Pen(LuxUI::Color(20, 24, 26, 130), 1));
                painter.setBrush(engineFrame_
                                     ? LuxUI::Brush(LuxUI::Flags::NoBrush)
                                     : LuxUI::Brush(Exposed(TileColor(tile), settings.exposure)));
                painter.drawPolygon(shape);
            }
        if (document_.IsSelected(entity.GetId())) {
            painter.setPen(LuxUI::Pen(Selection, 2, LuxUI::Flags::DashLine));
            painter.setBrush(LuxUI::Flags::NoBrush);
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
        const LuxUI::PolygonF shape = polygon(entity, {-.5, -.5, 1, 1});
        const bool active = document_.IsSelected(entity.GetId());
        painter.setPen(
            LuxUI::Pen(active ? Selection : LuxUI::Color(178, 189, 204), active ? 3 : 1));
        if (engineFrame_)
            painter.setBrush(LuxUI::Flags::NoBrush);
        else
            painter.setBrush(
                LuxUI::Color::fromRgbF(std::clamp(color.x * settings.exposure, 0.0f, 1.0f),
                                       std::clamp(color.y * settings.exposure, 0.0f, 1.0f),
                                       std::clamp(color.z * settings.exposure, 0.0f, 1.0f),
                                       std::clamp(color.w, 0.0f, 1.0f)));
        painter.drawPolygon(shape);
        painter.setPen(active ? Selection : LuxUI::Color(215, 222, 231));
        if (active)
            painter.drawText(shape.boundingRect().topLeft() - LuxUI::PointF(0, 6), Name(entity));
    }
    if (debugColliders_) {
        for (LuxEngine::Entity entity : entities) {
            if (!entity.HasComponent<LuxEngine::TransformComponent>() ||
                !entity.HasComponent<LuxEngine::BoxCollider2DComponent>())
                continue;
            const auto& collider = entity.GetComponent<LuxEngine::BoxCollider2DComponent>();
            painter.setPen(LuxUI::Pen(collider.isTrigger ? LuxUI::Color(255, 179, 92)
                                                         : LuxUI::Color(73, 220, 230),
                                      2, LuxUI::Flags::DashLine));
            painter.setBrush(LuxUI::Flags::NoBrush);
            painter.drawPolygon(polygon(entity, {collider.offset.x - collider.size.x * .5,
                                                 collider.offset.y - collider.size.y * .5,
                                                 collider.size.x, collider.size.y}));
        }
    }
    DrawGizmo(painter);
    if (selecting_ && selectionBox_) {
        painter.setPen(LuxUI::Pen(LuxUI::Color(91, 161, 229), 1, LuxUI::Flags::DashLine));
        painter.setBrush(LuxUI::Color(70, 135, 210, 38));
        painter.drawRect(LuxUI::RectF(selectionStart_, selectionEnd_).normalized());
    }
}

int SceneViewport2D::HitGizmo(LuxUI::PointF point) const {
    const auto selected = document_.GetSelected();
    if (tool_ == EditorTool::Select || !selected ||
        !selected.HasComponent<LuxEngine::TransformComponent>())
        return -1;
    const auto pivot = GetSelectionPivot(document_);
    const auto origin = WorldToScreen(pivot.x, pivot.y);
    const double radius = LuxUI::LineF(origin, point).length();
    if (tool_ == EditorTool::Rotate)
        return std::abs(radius - 54) <= 8 ? 2 : -1;
    if (radius <= 9)
        return 2;
    const auto transform = document_.GetScene().GetWorldTransform(selected.GetId());
    const double angle =
        document_.GetTransformSpace() == TransformSpace::Local ? transform.rotation.z : 0;
    const std::array<LuxUI::PointF, 2> directions{
        LuxUI::PointF(std::cos(angle), -std::sin(angle)),
        LuxUI::PointF(-std::sin(angle), -std::cos(angle))};
    for (int axis = 0; axis < 2; ++axis) {
        const auto offset = point - origin;
        const double along = LuxUI::PointF::dotProduct(offset, directions[axis]);
        const double perpendicular =
            std::abs(offset.x() * directions[axis].y() - offset.y() * directions[axis].x());
        if (along >= 14 && along <= 86 && perpendicular <= 8)
            return axis;
    }
    return -1;
}

void SceneViewport2D::DrawGizmo(LuxUI::Painter& painter) const {
    const auto selected = document_.GetSelected();
    if (tool_ == EditorTool::Select || tool_ == EditorTool::TileBrush ||
        tool_ == EditorTool::TileErase || !selected ||
        !selected.HasComponent<LuxEngine::TransformComponent>())
        return;
    const auto pivot = GetSelectionPivot(document_);
    const auto origin = WorldToScreen(pivot.x, pivot.y);
    painter.setBrush(LuxUI::Flags::NoBrush);
    if (tool_ == EditorTool::Rotate) {
        painter.setPen(LuxUI::Pen(LuxUI::Color(98, 148, 215), 2));
        painter.drawEllipse(origin, 54, 54);
    } else {
        const auto transform = document_.GetScene().GetWorldTransform(selected.GetId());
        const double angle =
            document_.GetTransformSpace() == TransformSpace::Local ? transform.rotation.z : 0;
        const std::array<LuxUI::PointF, 2> directions{
            LuxUI::PointF(std::cos(angle), -std::sin(angle)),
            LuxUI::PointF(-std::sin(angle), -std::cos(angle))};
        const std::array<LuxUI::Color, 2> colors{LuxUI::Color(220, 91, 100),
                                                 LuxUI::Color(100, 194, 124)};
        for (int axis = 0; axis < 2; ++axis) {
            const auto tip = origin + directions[axis] * 78;
            painter.setPen(LuxUI::Pen(colors[axis], 2));
            painter.setBrush(colors[axis]);
            painter.drawLine(origin, tip);
            if (tool_ == EditorTool::Scale)
                painter.drawRect(LuxUI::RectF(tip - LuxUI::PointF(4, 4), LuxUI::SizeF(8, 8)));
            else {
                const LuxUI::PointF side(-directions[axis].y(), directions[axis].x());
                painter.drawPolygon(LuxUI::PolygonF{tip, tip - directions[axis] * 11 + side * 4,
                                                    tip - directions[axis] * 11 - side * 4});
            }
            painter.drawText(tip + LuxUI::PointF(7, -5),
                             axis == 0 ? LUX_UI_TEXT("X") : LUX_UI_TEXT("Y"));
        }
    }
    painter.setPen(LuxUI::Pen(LuxUI::Color(225, 225, 225), 1));
    painter.setBrush(LuxUI::Color(94, 102, 112));
    painter.drawRect(LuxUI::RectF(origin - LuxUI::PointF(4, 4), LuxUI::SizeF(8, 8)));
}

void SceneViewport2D::mousePressEvent(LuxUI::MouseEvent* event) {
    if (dragging_ || tilePainting_ || selecting_)
        CancelInteraction();
    lastMouse_ = event->position();
    if (event->button() == LuxUI::Flags::MiddleButton) {
        panning_ = true;
        return;
    }
    if (event->button() != LuxUI::Flags::LeftButton)
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
    const LuxUI::PointF world = ScreenToWorld(event->position());
    std::optional<LuxEngine::uint32> hit;
    dragAxis_ = event->modifiers() & (LuxUI::Flags::ControlModifier | LuxUI::Flags::ShiftModifier)
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
            LuxUI::PointF local;
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
            const auto origin = World(selectedGroup).map(LuxUI::PointF());
            if (std::hypot(origin.x() - world.x(), origin.y() - world.y()) * pixelsPerUnit_ <= 12)
                hit = selectedGroup.GetId();
        }
    }
    const auto modifiers = event->modifiers();
    if (hit && (modifiers & (LuxUI::Flags::ControlModifier | LuxUI::Flags::ShiftModifier))) {
        document_.ModifySelection(hit, modifiers & LuxUI::Flags::ControlModifier
                                           ? SelectionMode::Toggle
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
    auto selection =
        selectionModifiers_ & (LuxUI::Flags::ControlModifier | LuxUI::Flags::ShiftModifier)
            ? selectionBefore_
            : std::vector<LuxEngine::uint32>{};
    auto primary =
        selectionModifiers_ & (LuxUI::Flags::ControlModifier | LuxUI::Flags::ShiftModifier)
            ? primaryBefore_
            : std::optional<LuxEngine::uint32>{};
    const LuxUI::RectF rectangle = LuxUI::RectF(selectionStart_, selectionEnd_).normalized();
    if (selectionBox_) {
        LuxUI::PainterPath area;
        area.addRect(rectangle);
        for (auto entity : document_.GetScene().GetEntitiesIn2DLayerOrder()) {
            if (!entity.HasComponent<LuxEngine::TransformComponent>())
                continue;
            LuxUI::RectF local(-.5, -.5, 1, 1);
            if (const auto* map = entity.TryGetComponent<LuxEngine::TilemapComponent>())
                local = {0, 0, map->width * map->tileSize, map->height * map->tileSize};
            else if (!entity.HasComponent<LuxEngine::SpriteRendererComponent>())
                continue;
            LuxUI::PolygonF polygon;
            for (const auto& corner :
                 {local.topLeft(), local.topRight(), local.bottomRight(), local.bottomLeft()}) {
                const auto world = World(entity).map(corner);
                polygon << WorldToScreen(world.x(), world.y());
            }
            LuxUI::PainterPath shape;
            shape.addPolygon(polygon);
            if (!area.intersects(shape) && !area.contains(shape))
                continue;
            const auto found = std::find(selection.begin(), selection.end(), entity.GetId());
            if ((selectionModifiers_ & LuxUI::Flags::ControlModifier) && found != selection.end())
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

void SceneViewport2D::mouseReleaseEvent(LuxUI::MouseEvent* event) {
    if (event->button() == LuxUI::Flags::MiddleButton)
        panning_ = false;
    if (event->button() != LuxUI::Flags::LeftButton)
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

void SceneViewport2D::mouseMoveEvent(LuxUI::MouseEvent* event) {
    if ((dragging_ || tilePainting_ || selecting_) &&
        !(event->buttons() & LuxUI::Flags::LeftButton)) {
        CancelInteraction();
        return;
    }
    if (panning_ && !(event->buttons() & LuxUI::Flags::MiddleButton))
        panning_ = false;
    if ((dragging_ || tilePainting_ || selecting_) && !GestureSceneValid()) {
        CancelInteraction();
        return;
    }
    if (selecting_ && (event->buttons() & LuxUI::Flags::LeftButton)) {
        selectionEnd_ = event->position();
        selectionBox_ = selectionBox_ || LuxUI::LineF(selectionStart_, selectionEnd_).length() >= 4;
        update();
    } else if (panning_ && (event->buttons() & LuxUI::Flags::MiddleButton)) {
        pan_ += event->position() - lastMouse_;
        update();
    } else if (tilePainting_ && (event->buttons() & LuxUI::Flags::LeftButton)) {
        PaintAt(event->position());
    } else if (dragging_ && (event->buttons() & LuxUI::Flags::LeftButton)) {
        const LuxUI::PointF world = ScreenToWorld(event->position());
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
            LuxUI::PointF movement = world - dragStartWorld_;
            if (dragAxis_ == 0 || dragAxis_ == 1) {
                const double angle =
                    document_.GetTransformSpace() == TransformSpace::Local ? dragInitial_[5] : 0;
                const LuxUI::PointF direction =
                    dragAxis_ == 0 ? LuxUI::PointF(std::cos(angle), std::sin(angle))
                                   : LuxUI::PointF(-std::sin(angle), std::cos(angle));
                movement = direction * LuxUI::PointF::dotProduct(movement, direction);
            }
            double x = dragInitial_[0] + movement.x();
            double y = dragInitial_[1] + movement.y();
            value.position.x = static_cast<float>(x);
            value.position.y = static_cast<float>(y);
        } else if (tool_ == EditorTool::Rotate) {
            const double start = std::atan2(dragStartWorld_.y() - dragPivot_.y(),
                                            dragStartWorld_.x() - dragPivot_.x());
            const double current =
                std::atan2(world.y() - dragPivot_.y(), world.x() - dragPivot_.x());
            float angle = static_cast<float>(current - start);
            if (event->modifiers() & LuxUI::Flags::ControlModifier)
                angle = std::round(angle / LuxEngine::ToRadians(15)) * LuxEngine::ToRadians(15);
            value.rotation.z = dragInitial_[5] + angle;
        } else if (tool_ == EditorTool::Scale) {
            double factor;
            if (dragAxis_ == 0 || dragAxis_ == 1) {
                const double angle =
                    document_.GetTransformSpace() == TransformSpace::Local ? dragInitial_[5] : 0;
                const LuxUI::PointF direction =
                    dragAxis_ == 0 ? LuxUI::PointF(std::cos(angle), std::sin(angle))
                                   : LuxUI::PointF(-std::sin(angle), std::cos(angle));
                factor = std::exp(LuxUI::PointF::dotProduct(world - dragStartWorld_, direction) *
                                  pixelsPerUnit_ * .01);
            } else {
                const double initial = LuxUI::LineF(dragStartWorld_, dragPivot_).length();
                const double current = LuxUI::LineF(world, dragPivot_).length();
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

void SceneViewport2D::PaintAt(LuxUI::PointF point) {
    const auto world = ScreenToWorld(point);
    const auto entity = document_.GetSelected();
    if (!entity || entity.GetId() != dragId_) {
        CancelInteraction();
        return;
    }
    LuxUI::PointF cell;
    if (InsideMap(entity, world, &cell))
        onPaintTile_(entity.GetId(), static_cast<LuxEngine::uint32>(cell.x()),
                     static_cast<LuxEngine::uint32>(cell.y()),
                     tool_ == EditorTool::TileErase ? 0 : tileId_);
}

void SceneViewport2D::wheelEvent(LuxUI::WheelEvent* event) {
    const LuxUI::PointF world = ScreenToWorld(event->position());
    pixelsPerUnit_ =
        std::clamp(pixelsPerUnit_ * std::pow(1.2, event->angleDelta().y() / 120.0), .01, 2048.0);
    pan_ = {event->position().x() - width() * .5 - world.x() * pixelsPerUnit_,
            event->position().y() - height() * .5 + world.y() * pixelsPerUnit_};
    update();
}

void SceneViewport2D::RefreshScene() {
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
