#include <LuxEditor/Scene/EditorDocument.hpp>
#include <LuxEditor/Scene/SelectionTransform.hpp>
#include <LuxEditor/Viewport/EngineSceneRenderer.hpp>
#include <LuxEditor/Viewport/SceneViewport3D.hpp>
#include <LuxEditor/Viewport/ViewportCamera3D.hpp>
#include <LuxEngine/Assets/AssetManager.hpp>
#include <LuxEngine/ECS/ECS.hpp>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <limits>
#include <numbers>
#include <vector>

namespace LuxEditor {
namespace {
bool SameLifetime(const std::weak_ptr<int>& first, const std::weak_ptr<int>& second) {
    return !first.owner_before(second) && !second.owner_before(first);
}

LuxEngine::Mat4 BoundsModel(LuxEngine::Entity entity,
                            const EngineSceneRenderer* renderer = nullptr) {
    const auto world = entity.GetScene()->GetWorldMatrix(entity.GetId());
    if (entity.HasComponent<LuxEngine::MeshRendererComponent>()) {
        const auto& mesh = entity.GetComponent<LuxEngine::MeshRendererComponent>();
        LuxEngine::Vec3 minimum, maximum;
        const auto path = entity.GetScene()->ResolveAssetPath(mesh.meshPath);
        if (!mesh.meshPath.empty() &&
            ((renderer && renderer->GetMeshBounds(path, minimum, maximum)) ||
             LuxEngine::AssetManager::GetMeshBounds(path, minimum, maximum))) {
            auto size = maximum - minimum;
            size.x = std::max(size.x, .001f);
            size.y = std::max(size.y, .001f);
            size.z = std::max(size.z, .001f);
            return world * LuxEngine::Mat4::Translate((minimum + maximum) * .5f) *
                   LuxEngine::Mat4::Scale(size);
        }
        return world;
    }
    if (entity.HasComponent<LuxEngine::SpriteRendererComponent>())
        return world * LuxEngine::Mat4::Scale({1, 1, .02f});
    if (entity.HasComponent<LuxEngine::TilemapComponent>()) {
        const auto& map = entity.GetComponent<LuxEngine::TilemapComponent>();
        const float width = map.width * map.tileSize, height = map.height * map.tileSize;
        return world * LuxEngine::Mat4::Translate({width * .5f, height * .5f, 0}) *
               LuxEngine::Mat4::Scale({width, height, .04f});
    }
    return world;
}

QPolygonF Outline(const std::vector<QPointF>& points) {
    auto sorted = points;
    std::sort(sorted.begin(), sorted.end(),
              [](QPointF a, QPointF b) { return a.x() == b.x() ? a.y() < b.y() : a.x() < b.x(); });
    if (sorted.size() < 3)
        return QPolygonF();
    const auto cross = [](QPointF a, QPointF b, QPointF c) {
        return (b.x() - a.x()) * (c.y() - a.y()) - (b.y() - a.y()) * (c.x() - a.x());
    };
    QPolygonF hull;
    for (const auto p : sorted) {
        while (hull.size() > 1 && cross(hull[hull.size() - 2], hull.last(), p) <= 0)
            hull.removeLast();
        hull << p;
    }
    const auto lower = hull.size();
    for (auto it = sorted.rbegin() + 1; it != sorted.rend(); ++it) {
        while (hull.size() > lower && cross(hull[hull.size() - 2], hull.last(), *it) <= 0)
            hull.removeLast();
        hull << *it;
    }
    hull.removeLast();
    return hull;
}

LuxEngine::Vec3 TransformCorner(const LuxEngine::Mat4& model, LuxEngine::Vec3 point) {
    const auto& m = model.elements;
    return {m[0] * point.x + m[4] * point.y + m[8] * point.z + m[12],
            m[1] * point.x + m[5] * point.y + m[9] * point.z + m[13],
            m[2] * point.x + m[6] * point.y + m[10] * point.z + m[14]};
}
} // namespace

SceneViewport3D::SceneViewport3D(
    QWidget* parent, EditorDocument& document,
    std::function<void(std::optional<LuxEngine::uint32>)> onSelect,
    std::function<void(LuxEngine::uint32, const LuxEngine::TransformComponent&)> onTransform,
    std::function<void()> onBeginEdit, std::function<void()> onEndEdit,
    std::shared_ptr<EngineSceneRenderer> renderer)
    : QWidget(parent), document_(document), onSelect_(std::move(onSelect)),
      onTransform_(std::move(onTransform)), onBeginEdit_(std::move(onBeginEdit)),
      onEndEdit_(std::move(onEndEdit)), engineRenderer_(std::move(renderer)) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(300, 200);
}

void SceneViewport3D::SetCancelEditCallback(std::function<void()> callback) {
    onCancelEdit_ = std::move(callback);
}

void SceneViewport3D::CancelInteraction() {
    const bool editing = dragging_;
    orbiting_ = panning_ = dragging_ = marquee_ = false;
    const auto id = dragId_;
    dragId_ = 0;
    axis_ = -1;
    axisGesture_ = false;
    if (mouseGrabber() == this)
        releaseMouse();
    if (editing) {
        if (onCancelEdit_) {
            onCancelEdit_();
        } else if (auto* scene = document_.GetScene(dragTab_);
                   scene && !dragLifetime_.expired() &&
                   SameLifetime(dragLifetime_, scene->Lifetime()) && scene->IsAlive(id)) {
            LuxEngine::TransformComponent original;
            original.position = initialPosition_;
            original.rotation = initialRotation_;
            original.scale = initialScale_;
            scene->SetWorldTransform(id, original);
        }
    }
    dragSelection_.clear();
    dragLifetime_.reset();
    update();
}

void SceneViewport3D::focusOutEvent(QFocusEvent* event) {
    CancelInteraction();
    QWidget::focusOutEvent(event);
}

void SceneViewport3D::FrameBounds(bool selectedOnly) {
    LuxEngine::Vec3 minimum{INFINITY, INFINITY, INFINITY};
    LuxEngine::Vec3 maximum{-INFINITY, -INFINITY, -INFINITY};
    for (auto entity : document_.GetScene().GetEntities()) {
        if (!entity.HasComponent<LuxEngine::TransformComponent>() ||
            (selectedOnly && !document_.IsSelected(entity.GetId())))
            continue;
        const auto model = BoundsModel(entity, engineRenderer_.get());
        for (int corner = 0; corner < 8; ++corner) {
            const auto point = TransformCorner(
                model, {corner & 1 ? .5f : -.5f, corner & 2 ? .5f : -.5f, corner & 4 ? .5f : -.5f});
            if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))
                continue;
            minimum = {std::min(minimum.x, point.x), std::min(minimum.y, point.y),
                       std::min(minimum.z, point.z)};
            maximum = {std::max(maximum.x, point.x), std::max(maximum.y, point.y),
                       std::max(maximum.z, point.z)};
        }
    }
    if (!std::isfinite(minimum.x))
        return;
    focus_ = (minimum + maximum) * .5f;
    const auto extent = maximum - minimum;
    const double radius = std::hypot(extent.x, extent.y, extent.z) * .5;
    const double aspect = double(std::max(1, width())) / std::max(1, height());
    const double halfFov = std::atan(std::tan(std::numbers::pi / 6) * std::min(1.0, aspect));
    distance_ = std::clamp(radius / std::sin(halfFov) * 1.2, 1.0, 100000.0);
    update();
}

void SceneViewport3D::FocusSelection() {
    FrameBounds(true);
}

void SceneViewport3D::FrameAll() {
    FrameBounds(false);
}

void SceneViewport3D::SetViewPreset(ViewPreset preset) {
    CancelInteraction();
    preset_ = preset;
    switch (preset) {
    case ViewPreset::Front:
        yaw_ = 0;
        pitch_ = 0;
        break;
    case ViewPreset::Right:
        yaw_ = std::numbers::pi / 2;
        pitch_ = 0;
        break;
    case ViewPreset::Top:
        yaw_ = 0;
        pitch_ = -std::numbers::pi / 2;
        break;
    case ViewPreset::Perspective:
        yaw_ = .65;
        pitch_ = -.48;
        break;
    }
    update();
}

void SceneViewport3D::SetSnapToGrid(bool enabled) {
    snapToGrid_ = enabled;
    update();
}

void SceneViewport3D::SetGridVisible(bool visible) {
    gridVisible_ = visible;
    update();
}

void SceneViewport3D::paintEvent(QPaintEvent*) {
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
            if (engineRenderer_->Render3D(document_.GetScene(), pxW, pxH, focus_, yaw_, pitch_,
                                          distance_, cachedFrame_, &cachedBackground_)) {
                engineFrame_ = true;
            }
        }
    }

    QPainter painter(this);
    if (!painter.isActive())
        return;

    painter.fillRect(rect(), QColor(29, 30, 33));
    if (engineFrame_ && !cachedBackground_.isNull())
        painter.drawImage(rect(), cachedBackground_);
    painter.setRenderHint(QPainter::Antialiasing);

    const auto camera = MakeViewportCamera3D(size(), focus_, yaw_, pitch_, distance_);
    const auto project = [&](LuxEngine::Vec3 point) {
        return ProjectViewportPoint(camera, size(), point);
    };
    const int gridAlpha = 255;
    auto line = [gridAlpha](QColor color) {
        color.setAlpha(gridAlpha);
        return color;
    };
    painter.save();
    if (!engineFrame_) {
        QPainterPath visibleGrid;
        visibleGrid.addRect(rect());
        for (auto entity : document_.GetScene().GetEntities()) {
            if (!entity.HasComponent<LuxEngine::TransformComponent>() ||
                !(entity.HasComponent<LuxEngine::MeshRendererComponent>() ||
                  entity.HasComponent<LuxEngine::SpriteRendererComponent>() ||
                  entity.HasComponent<LuxEngine::TilemapComponent>()))
                continue;
            std::vector<QPointF> points;
            const auto model = BoundsModel(entity, engineFrame_ ? engineRenderer_.get() : nullptr);
            for (int corner = 0; corner < 8; ++corner) {
                auto point = project(
                    TransformCorner(model, {corner & 1 ? .5f : -.5f, corner & 2 ? .5f : -.5f,
                                            corner & 4 ? .5f : -.5f}));
                if (point)
                    points.push_back(*point);
            }
            QPainterPath object;
            object.addPolygon(Outline(points));
            visibleGrid = visibleGrid.subtracted(object);
        }
        painter.setClipPath(visibleGrid);
    }
    if (gridVisible_) {
        const double spacing = ViewportGridSpacing(distance_ * 1.1547 / std::max(1, height()));
        const bool front = preset_ == ViewPreset::Front;
        const bool side = preset_ == ViewPreset::Right;
        const double firstCenter = std::round((side ? focus_.z : focus_.x) / spacing) * spacing;
        const double secondCenter =
            std::round((front || side ? focus_.y : focus_.z) / spacing) * spacing;
        const double extent = spacing * 48;
        const auto point = [&](double first, double second) {
            if (front)
                return LuxEngine::Vec3{float(first), float(second), focus_.z};
            if (side)
                return LuxEngine::Vec3{focus_.x, float(second), float(first)};
            return LuxEngine::Vec3{float(first), 0, float(second)};
        };
        const auto gridLine = [&](double coordinate, bool first) {
            const auto segment =
                ProjectViewportSegment(camera, size(),
                                       first ? point(coordinate, secondCenter - extent)
                                             : point(firstCenter - extent, coordinate),
                                       first ? point(coordinate, secondCenter + extent)
                                             : point(firstCenter + extent, coordinate));
            if (!segment)
                return;
            const double index = std::round(coordinate / spacing);
            QColor color =
                std::abs(std::remainder(index, 5.0)) < .1 ? QColor(75, 77, 82) : QColor(47, 49, 53);
            if (std::abs(coordinate) < spacing * .01)
                color = first ? (front || side ? QColor(78, 135, 92) : QColor(75, 105, 155))
                              : (side ? QColor(75, 105, 155) : QColor(155, 75, 77));
            painter.setPen(QPen(line(color), std::abs(coordinate) < spacing * .01 ? 1.5 : 1));
            painter.drawLine(*segment);
        };
        for (int unit = -48; unit <= 48; ++unit) {
            gridLine(firstCenter + unit * spacing, true);
            gridLine(secondCenter + unit * spacing, false);
        }
    }
    painter.restore();
    if (engineFrame_ && !cachedFrame_.isNull())
        painter.drawImage(rect(), cachedFrame_);
    if (engineFrame_ && engineRenderer_ && !engineRenderer_->LastError().isEmpty()) {
        const auto message = painter.fontMetrics().elidedText(
            engineRenderer_->LastError(), Qt::ElideMiddle, std::max(1, width() - 32));
        painter.fillRect(QRect(8, 8, std::max(1, width() - 16), 30), QColor(80, 24, 28, 225));
        painter.setPen(QColor(255, 210, 210));
        painter.drawText(QRect(16, 8, std::max(1, width() - 32), 30), Qt::AlignVCenter, message);
    }
    const auto origin = project({0, 0, 0});
    for (const auto& [tip, color] :
         std::array<std::pair<LuxEngine::Vec3, QColor>, 3>{{{{2, 0, 0}, QColor(218, 97, 99)},
                                                            {{0, 2, 0}, QColor(105, 190, 132)},
                                                            {{0, 0, 2}, QColor(102, 151, 232)}}}) {
        auto end = project(tip);
        if (origin && end) {
            painter.setPen(QPen(color, 3));
            painter.drawLine(*origin, *end);
        }
    }
    std::vector<QRectF> occupiedLabels;
    for (LuxEngine::Entity entity : document_.GetScene().GetEntities()) {
        if (!entity.HasComponent<LuxEngine::TransformComponent>())
            continue;
        const auto matrix = entity.GetScene()->GetWorldMatrix(entity.GetId());
        if (std::any_of(std::begin(matrix.elements), std::end(matrix.elements),
                        [](float value) { return !std::isfinite(value); }))
            continue;
        const bool sprite = entity.HasComponent<LuxEngine::SpriteRendererComponent>();
        const bool mesh = entity.HasComponent<LuxEngine::MeshRendererComponent>();
        const bool active = document_.IsSelected(entity.GetId());
        if (engineFrame_ && !active)
            continue;
        QColor color = active   ? QColor(248, 197, 91)
                       : mesh   ? QColor(117, 183, 223)
                       : sprite ? QColor(130, 205, 158)
                                : QColor(176, 187, 201);
        if (engineFrame_ && !active)
            color.setAlpha(120);
        const LuxEngine::Mat4 model =
            BoundsModel(entity, engineFrame_ ? engineRenderer_.get() : nullptr);
        const LuxEngine::Vec3 boxCenter = TransformCorner(model, {});
        std::array<std::optional<QPointF>, 8> corners;
        for (size_t i = 0; i < 8; ++i)
            corners[i] = project(TransformCorner(
                model, {(i & 1) ? .5f : -.5f, (i & 2) ? .5f : -.5f, (i & 4) ? .5f : -.5f}));
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
        auto center = project(boxCenter);
        if (center) {
            painter.setPen(color);
            const QString label =
                entity.HasComponent<LuxEngine::TagComponent>()
                    ? QString::fromUtf8(entity.GetComponent<LuxEngine::TagComponent>().tag)
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
    DrawGizmo(painter);
    if (marquee_) {
        painter.setPen(QPen(QColor(91, 163, 230), 1, Qt::DashLine));
        painter.setBrush(QColor(70, 140, 215, 35));
        painter.drawRect(QRectF(marqueeStart_, marqueeEnd_).normalized());
    }
}

namespace {
constexpr std::array<LuxEngine::Vec3, 3> GizmoAxes{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};

LuxEngine::Vec3 GizmoDirection(const LuxEngine::Mat4& world, int axis) {
    const auto* column = &world.elements[axis * 4];
    const auto direction = LuxEngine::Vec3{column[0], column[1], column[2]}.Normalized();
    return direction.Dot(direction) > .5f ? direction : GizmoAxes[axis];
}

QColor GizmoColor(int axis) {
    return axis == 0   ? QColor(222, 92, 89)
           : axis == 1 ? QColor(101, 193, 121)
                       : QColor(96, 152, 232);
}

double SegmentDistance(QPointF point, QLineF line) {
    const QPointF direction = line.p2() - line.p1();
    const double lengthSquared = QPointF::dotProduct(direction, direction);
    const double t =
        lengthSquared > .001
            ? std::clamp(QPointF::dotProduct(point - line.p1(), direction) / lengthSquared, 0.0,
                         1.0)
            : 0;
    const auto nearest = line.p1() + direction * t;
    return std::hypot(point.x() - nearest.x(), point.y() - nearest.y());
}

double GizmoLength(const LuxEngine::PerspectiveCamera& camera, LuxEngine::Vec3 position,
                   int height) {
    const auto forward = (camera.GetTarget() - camera.GetPosition()).Normalized();
    const double depth = (position - camera.GetPosition()).Dot(forward);
    return std::max(.001, depth * 1.1547 * 74 / std::max(1, height));
}

LuxEngine::Vec3 RingPoint(int axis, double angle, float radius) {
    const float a = float(std::cos(angle)) * radius;
    const float b = float(std::sin(angle)) * radius;
    return axis == 0   ? LuxEngine::Vec3{0, a, b}
           : axis == 1 ? LuxEngine::Vec3{a, 0, b}
                       : LuxEngine::Vec3{a, b, 0};
}
} // namespace

int SceneViewport3D::HitGizmo(QPointF point) const {
    auto selected = document_.GetSelected();
    if (!selected || !selected.HasComponent<LuxEngine::TransformComponent>())
        return -1;
    const auto position = GizmoPosition();
    const auto basis = GizmoBasis();
    const auto camera = MakeViewportCamera3D(size(), focus_, yaw_, pitch_, distance_);
    const float length = float(GizmoLength(camera, position, height()));
    if (const auto center = ProjectViewportPoint(camera, size(), position);
        center && std::hypot(point.x() - center->x(), point.y() - center->y()) < 12)
        return -1;
    int axis = -1;
    double best = 9;
    for (int candidate = 0; candidate < 3; ++candidate) {
        const int segments = tool_ == EditorTool::Rotate ? 72 : 1;
        for (int index = 0; index < segments; ++index) {
            const auto a =
                tool_ == EditorTool::Rotate
                    ? position +
                          TransformCorner(
                              basis,
                              RingPoint(candidate, index * 2 * std::numbers::pi / segments, length))
                    : position + GizmoDirection(basis, candidate) * (length * .15f);
            const auto b =
                tool_ == EditorTool::Rotate
                    ? position + TransformCorner(
                                     basis, RingPoint(candidate,
                                                      (index + 1) * 2 * std::numbers::pi / segments,
                                                      length))
                    : position + GizmoDirection(basis, candidate) * length;
            const auto projected = ProjectViewportSegment(camera, size(), a, b);
            if (projected && projected->length() > .1) {
                const double distance = SegmentDistance(point, *projected);
                if (distance < best) {
                    best = distance;
                    axis = candidate;
                }
            }
        }
    }
    return axis;
}

void SceneViewport3D::DrawGizmo(QPainter& painter) const {
    auto selected = document_.GetSelected();
    if (!selected || !selected.HasComponent<LuxEngine::TransformComponent>())
        return;
    const auto position = GizmoPosition();
    const auto basis = GizmoBasis();
    const auto camera = MakeViewportCamera3D(size(), focus_, yaw_, pitch_, distance_);
    const float length = float(GizmoLength(camera, position, height()));
    painter.save();
    painter.setBrush(Qt::NoBrush);
    for (int axis = 0; axis < 3; ++axis) {
        const QColor color = axis == axis_ ? QColor(255, 204, 91) : GizmoColor(axis);
        painter.setPen(QPen(color, axis == axis_ ? 3 : 2));
        if (tool_ == EditorTool::Rotate) {
            for (int index = 0; index < 72; ++index) {
                const auto segment = ProjectViewportSegment(
                    camera, size(),
                    position +
                        TransformCorner(basis,
                                        RingPoint(axis, index * 2 * std::numbers::pi / 72, length)),
                    position + TransformCorner(
                                   basis, RingPoint(axis, (index + 1) * 2 * std::numbers::pi / 72,
                                                    length)));
                if (segment)
                    painter.drawLine(*segment);
            }
        } else {
            const auto end = position + GizmoDirection(basis, axis) * length;
            const auto segment = ProjectViewportSegment(camera, size(), position, end);
            if (!segment || segment->length() < 6)
                continue;
            painter.drawLine(*segment);
            const QPointF tip = segment->p2();
            painter.setBrush(color);
            if (tool_ == EditorTool::Scale) {
                painter.drawRect(QRectF(tip - QPointF(4, 4), QSizeF(8, 8)));
            } else {
                const double angle = std::atan2(segment->dy(), segment->dx());
                const QPointF back(std::cos(angle) * 10, std::sin(angle) * 10);
                const QPointF side(-std::sin(angle) * 4, std::cos(angle) * 4);
                painter.drawPolygon(QPolygonF{tip, tip - back + side, tip - back - side});
            }
            painter.drawText(tip + QPointF(7, -5), QString(QChar('X' + axis)));
            painter.setBrush(Qt::NoBrush);
        }
    }
    if (auto center = ProjectViewportPoint(camera, size(), position)) {
        painter.setPen(QPen(QColor(235, 235, 235), 1));
        painter.setBrush(QColor(70, 72, 77));
        painter.drawRect(QRectF(*center - QPointF(4, 4), QSizeF(8, 8)));
    }
    painter.restore();
}

LuxEngine::Mat4 SceneViewport3D::GizmoBasis() const {
    if (dragging_)
        return initialBasis_;
    if (document_.GetTransformSpace() == TransformSpace::World)
        return LuxEngine::Mat4::Identity();
    const auto transform = GetSelectionTransformPivot(document_);
    return LuxEngine::Mat4::RotateZ(transform.rotation.z) *
           LuxEngine::Mat4::RotateY(transform.rotation.y) *
           LuxEngine::Mat4::RotateX(transform.rotation.x);
}

LuxEngine::Vec3 SceneViewport3D::GizmoPosition() const {
    return GetSelectionPivot(document_);
}

void SceneViewport3D::BeginDrag(LuxEngine::uint32 id, QPointF point) {
    auto selected = document_.GetSelected();
    if (!selected || selected.GetId() != id ||
        !selected.HasComponent<LuxEngine::TransformComponent>())
        return;
    LuxEngine::TransformComponent transform;
    try {
        transform = selected.GetScene()->GetWorldTransform(id);
    } catch (const std::exception&) {
        return;
    }
    initialPivot_ = GetSelectionPivot(document_);
    initialBasis_ = GizmoBasis();
    dragSelection_ = document_.GetSelection();
    dragTab_ = document_.GetActiveTabId();
    dragSpace_ = static_cast<int>(document_.GetTransformSpace());
    dragPivotMode_ = static_cast<int>(document_.GetSelectionPivotMode());
    dragLifetime_ = document_.GetScene().Lifetime();
    dragId_ = id;
    dragStart_ = point;
    initialPosition_ = transform.position;
    initialRotation_ = transform.rotation;
    initialScale_ = transform.scale;
    dragging_ = true;
    onBeginEdit_();
    if (isVisible())
        grabMouse();
}

bool SceneViewport3D::event(QEvent* event) {
    if (event->type() == QEvent::ShortcutOverride) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (key->modifiers() == Qt::NoModifier &&
            (key->key() == Qt::Key_X || key->key() == Qt::Key_Y || key->key() == Qt::Key_Z ||
             key->key() == Qt::Key_Escape)) {
            event->accept();
            return true;
        }
    }
    if ((event->type() == QEvent::Hide || event->type() == QEvent::WindowDeactivate ||
         event->type() == QEvent::UngrabMouse) &&
        (dragging_ || marquee_))
        CancelInteraction();
    return QWidget::event(event);
}

void SceneViewport3D::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        CancelInteraction();
        event->accept();
        return;
    }
    if (!(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
        switch (event->key()) {
        case Qt::Key_F:
            FocusSelection();
            return;
        case Qt::Key_Home:
            FrameAll();
            return;
        case Qt::Key_1:
            SetViewPreset(ViewPreset::Front);
            return;
        case Qt::Key_3:
            SetViewPreset(ViewPreset::Right);
            return;
        case Qt::Key_7:
            SetViewPreset(ViewPreset::Top);
            return;
        case Qt::Key_5:
            SetViewPreset(ViewPreset::Perspective);
            return;
        case Qt::Key_X:
            axis_ = axis_ == 0 ? -1 : 0;
            update();
            return;
        case Qt::Key_Y:
            axis_ = axis_ == 1 ? -1 : 1;
            update();
            return;
        case Qt::Key_Z:
            axis_ = axis_ == 2 ? -1 : 2;
            update();
            return;
        default:
            break;
        }
    }
    QWidget::keyPressEvent(event);
}

void SceneViewport3D::NotifySelection() {
    const auto tab = document_.GetActiveTabId();
    const auto lifetime = document_.GetScene().Lifetime();
    const auto selection = document_.GetSelection();
    const auto primary = document_.GetSelectedId(tab);
    onSelect_(primary);
    // Keep callbacks written for the former single-selection API compatible.
    if (document_.GetActiveTabId() == tab && !lifetime.expired() &&
        SameLifetime(lifetime, document_.GetScene().Lifetime()))
        document_.SetSelection(selection, primary);
    update();
}

void SceneViewport3D::SelectRectangle(QRectF rectangle, Qt::KeyboardModifiers modifiers) {
    const auto camera = MakeViewportCamera3D(size(), focus_, yaw_, pitch_, distance_);
    std::vector<LuxEngine::uint32> hits;
    rectangle = rectangle.normalized().intersected(QRectF(rect()));
    for (auto entity : document_.GetScene().GetEntities()) {
        if (!entity.HasComponent<LuxEngine::TransformComponent>())
            continue;
        const auto model = BoundsModel(entity, engineFrame_ ? engineRenderer_.get() : nullptr);
        std::vector<QPointF> points;
        for (int corner = 0; corner < 8; ++corner) {
            const LuxEngine::Vec3 first{corner & 1 ? .5f : -.5f, corner & 2 ? .5f : -.5f,
                                        corner & 4 ? .5f : -.5f};
            for (int axis = 0; axis < 3; ++axis) {
                const int other = corner ^ (1 << axis);
                if (other < corner)
                    continue;
                const LuxEngine::Vec3 second{other & 1 ? .5f : -.5f, other & 2 ? .5f : -.5f,
                                             other & 4 ? .5f : -.5f};
                if (auto edge =
                        ProjectViewportSegment(camera, size(), TransformCorner(model, first),
                                               TransformCorner(model, second))) {
                    points.push_back(edge->p1());
                    points.push_back(edge->p2());
                }
            }
        }
        if (points.empty())
            continue;
        const auto hull = Outline(points);
        if (hull.isEmpty())
            continue;
        QPainterPath shape, area;
        shape.addPolygon(hull);
        area.addRect(rectangle);
        if (area.intersects(shape) || area.contains(hull.first()))
            hits.push_back(entity.GetId());
    }
    auto selection = modifiers & (Qt::ControlModifier | Qt::ShiftModifier)
                         ? document_.GetSelection()
                         : std::vector<LuxEngine::uint32>{};
    for (const auto id : hits) {
        const auto found = std::find(selection.begin(), selection.end(), id);
        if ((modifiers & Qt::ControlModifier) && found != selection.end())
            selection.erase(found);
        else if (found == selection.end())
            selection.push_back(id);
    }
    document_.SetSelection(selection);
    NotifySelection();
}

void SceneViewport3D::mousePressEvent(QMouseEvent* event) {
    setFocus();
    lastMouse_ = event->position();
    if (event->button() == Qt::RightButton || event->button() == Qt::MiddleButton) {
        if (dragging_ || marquee_)
            CancelInteraction();
        orbiting_ = event->button() == Qt::RightButton;
        panning_ = event->button() == Qt::MiddleButton;
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;
    if (dragging_ || marquee_)
        CancelInteraction();
    if (auto selected = document_.GetSelected();
        selected && selected.HasComponent<LuxEngine::TransformComponent>() &&
        !(event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier))) {
        const auto camera = MakeViewportCamera3D(size(), focus_, yaw_, pitch_, distance_);
        const auto center = ProjectViewportPoint(camera, size(), GizmoPosition());
        if (document_.GetSelection().size() > 1 && center &&
            !(event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)) &&
            QLineF(*center, event->position()).length() < 12) {
            BeginDrag(selected.GetId(), event->position());
            return;
        }
        const int handle = HitGizmo(event->position());
        if (handle >= 0) {
            axis_ = handle;
            axisGesture_ = true;
            BeginDrag(selected.GetId(), event->position());
            update();
            return;
        }
    }
    std::optional<LuxEngine::uint32> nearest;
    const auto camera = MakeViewportCamera3D(size(), focus_, yaw_, pitch_, distance_);
    const auto ray = MakeViewportRay3D(camera, size(), event->position());
    if (!ray)
        return;
    double best = std::numeric_limits<double>::infinity();
    for (LuxEngine::Entity entity : document_.GetScene().GetEntities()) {
        if (!entity.HasComponent<LuxEngine::TransformComponent>())
            continue;
        const auto distance = IntersectViewportBox(
            *ray, BoundsModel(entity, engineFrame_ ? engineRenderer_.get() : nullptr));
        if (!distance || *distance >= best)
            continue;
        const auto surface = ray->origin + ray->direction * static_cast<float>(*distance);
        if (!ProjectViewportPoint(camera, size(), surface))
            continue;
        best = *distance;
        nearest = entity.GetId();
    }
    if (!nearest) {
        dragTab_ = document_.GetActiveTabId();
        dragLifetime_ = document_.GetScene().Lifetime();
        marquee_ = true;
        marqueeStart_ = marqueeEnd_ = event->position();
        marqueeModifiers_ = event->modifiers();
        if (isVisible())
            grabMouse();
        update();
        return;
    }
    if (event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)) {
        document_.ModifySelection(nearest, event->modifiers() & Qt::ControlModifier
                                               ? SelectionMode::Toggle
                                               : SelectionMode::Add);
        NotifySelection();
        return;
    }
    if (document_.IsSelected(*nearest))
        document_.SetSelection(document_.GetSelection(), nearest);
    else
        document_.ModifySelection(nearest, SelectionMode::Replace);
    NotifySelection();
    if (document_.IsSelected(*nearest))
        BeginDrag(*nearest, event->position());
}

void SceneViewport3D::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::RightButton)
        orbiting_ = false;
    if (event->button() == Qt::MiddleButton)
        panning_ = false;
    if (event->button() != Qt::LeftButton)
        return;
    if (marquee_) {
        if (dragLifetime_.expired() || document_.GetActiveTabId() != dragTab_ ||
            !SameLifetime(dragLifetime_, document_.GetScene().Lifetime())) {
            CancelInteraction();
            return;
        }
        marquee_ = false;
        marqueeEnd_ = event->position();
        if (mouseGrabber() == this)
            releaseMouse();
        if (QLineF(marqueeStart_, marqueeEnd_).length() >= 4)
            SelectRectangle(QRectF(marqueeStart_, marqueeEnd_), marqueeModifiers_);
        else if (!(marqueeModifiers_ & (Qt::ControlModifier | Qt::ShiftModifier))) {
            document_.SetSelection({});
            NotifySelection();
        }
        update();
    }
    if (dragging_) {
        if (dragLifetime_.expired() || document_.GetActiveTabId() != dragTab_ ||
            !SameLifetime(dragLifetime_, document_.GetScene().Lifetime()) ||
            document_.GetSelection() != dragSelection_) {
            CancelInteraction();
            return;
        }
        dragging_ = false;
        dragId_ = 0;
        if (mouseGrabber() == this)
            releaseMouse();
        onEndEdit_();
        if (axisGesture_)
            axis_ = -1;
        axisGesture_ = false;
        dragSelection_.clear();
        dragLifetime_.reset();
        update();
    }
}

void SceneViewport3D::mouseMoveEvent(QMouseEvent* event) {
    const QPointF delta = event->position() - lastMouse_;
    if (orbiting_ && !(event->buttons() & Qt::RightButton))
        orbiting_ = false;
    if (panning_ && !(event->buttons() & Qt::MiddleButton))
        panning_ = false;
    if ((dragging_ || marquee_) && !(event->buttons() & Qt::LeftButton)) {
        CancelInteraction();
        return;
    }
    if (marquee_) {
        if (dragLifetime_.expired() || document_.GetActiveTabId() != dragTab_ ||
            !SameLifetime(dragLifetime_, document_.GetScene().Lifetime())) {
            CancelInteraction();
            return;
        }
        marqueeEnd_ = event->position();
        update();
        return;
    }
    if (orbiting_ && (event->buttons() & Qt::RightButton)) {
        yaw_ += delta.x() * .008;
        pitch_ = std::clamp(pitch_ + delta.y() * .008, -1.55, 1.55);
        preset_ = ViewPreset::Perspective;
        update();
    } else if (panning_ && (event->buttons() & Qt::MiddleButton)) {
        const auto camera = MakeViewportCamera3D(size(), focus_, yaw_, pitch_, distance_);
        const auto& view = camera.GetViewMatrix().elements;
        const double scale = distance_ * 1.1547 / std::max(1, height());
        const LuxEngine::Vec3 right{view[0], view[4], view[8]};
        const LuxEngine::Vec3 up{view[1], view[5], view[9]};
        focus_ += right * float(-delta.x() * scale) + up * float(delta.y() * scale);
        update();
    } else if (dragging_ && (event->buttons() & Qt::LeftButton)) {
        LuxEngine::Entity entity = document_.GetSelected();
        if (!entity || entity.GetId() != dragId_ ||
            !entity.HasComponent<LuxEngine::TransformComponent>() || dragLifetime_.expired() ||
            document_.GetActiveTabId() != dragTab_ ||
            !SameLifetime(dragLifetime_, document_.GetScene().Lifetime()) ||
            document_.GetSelection() != dragSelection_ ||
            static_cast<int>(document_.GetTransformSpace()) != dragSpace_ ||
            static_cast<int>(document_.GetSelectionPivotMode()) != dragPivotMode_) {
            CancelInteraction();
            return;
        }
        LuxEngine::TransformComponent value;
        try {
            (void)entity.GetScene()->GetWorldTransform(entity.GetId());
            value.position = initialPosition_;
            value.rotation = initialRotation_;
            value.scale = initialScale_;
        } catch (const std::exception&) {
            CancelInteraction();
            return;
        }
        const QPointF drag = event->position() - dragStart_;
        if (tool_ == EditorTool::Move || tool_ == EditorTool::Select) {
            const auto camera = MakeViewportCamera3D(size(), focus_, yaw_, pitch_, distance_);
            const auto startRay = MakeViewportRay3D(camera, size(), dragStart_);
            const auto currentRay = MakeViewportRay3D(camera, size(), event->position());
            if (!startRay || !currentRay)
                return;
            LuxEngine::Vec3 movement;
            if (axis_ >= 0) {
                const auto axis = GizmoDirection(initialBasis_, axis_);
                const auto parameter = [&](const ViewportRay3D& ray) -> std::optional<double> {
                    const double parallel = axis.Dot(ray.direction);
                    const double denominator = 1 - parallel * parallel;
                    if (denominator < .0001)
                        return std::nullopt;
                    const auto relative = ray.origin - initialPivot_;
                    return (axis.Dot(relative) - parallel * ray.direction.Dot(relative)) /
                           denominator;
                };
                const auto start = parameter(*startRay), current = parameter(*currentRay);
                if (!start || !current)
                    return;
                double amount = *current - *start;
                if (snapToGrid_ && !(event->modifiers() & Qt::AltModifier))
                    amount = std::round(amount);
                movement = axis * float(amount);
            } else {
                auto normal = (camera.GetPosition() - focus_).Normalized();
                if (tool_ == EditorTool::Select) {
                    normal = event->modifiers() & Qt::ShiftModifier ? LuxEngine::Vec3{0, 0, 1}
                                                                    : LuxEngine::Vec3{0, 1, 0};
                    if (std::abs(currentRay->direction.Dot(normal)) < .01f ||
                        std::abs(startRay->direction.Dot(normal)) < .01f)
                        normal = (camera.GetPosition() - focus_).Normalized();
                }
                const auto intersect = [&](const ViewportRay3D& ray) {
                    return ray.origin + ray.direction * ((initialPivot_ - ray.origin).Dot(normal) /
                                                         ray.direction.Dot(normal));
                };
                if (std::abs(startRay->direction.Dot(normal)) < .0001f ||
                    std::abs(currentRay->direction.Dot(normal)) < .0001f)
                    return;
                movement = intersect(*currentRay) - intersect(*startRay);
                if (snapToGrid_ && !(event->modifiers() & Qt::AltModifier))
                    movement = {float(std::round(movement.x)), float(std::round(movement.y)),
                                float(std::round(movement.z))};
            }
            value.position = initialPosition_ + movement;
        } else if (tool_ == EditorTool::Rotate) {
            const int axis = axis_ >= 0 ? axis_ : 1;
            float angle = static_cast<float>((drag.x() - drag.y()) * .01);
            const auto camera = MakeViewportCamera3D(size(), focus_, yaw_, pitch_, distance_);
            const auto from = MakeViewportRay3D(camera, size(), dragStart_);
            const auto to = MakeViewportRay3D(camera, size(), event->position());
            const auto normal = GizmoDirection(initialBasis_, axis);
            if (from && to && std::abs(from->direction.Dot(normal)) > .001f &&
                std::abs(to->direction.Dot(normal)) > .001f &&
                std::abs((initialPivot_ - from->origin).Dot(normal)) > .001f) {
                const auto direction = [&](const ViewportRay3D& ray) {
                    return (ray.origin +
                            ray.direction * ((initialPivot_ - ray.origin).Dot(normal) /
                                             ray.direction.Dot(normal)) -
                            initialPivot_)
                        .Normalized();
                };
                const auto start = direction(*from), current = direction(*to);
                angle = std::atan2(normal.Dot(start.Cross(current)), start.Dot(current));
            }
            if (event->modifiers() & Qt::ControlModifier)
                angle = std::round(angle / LuxEngine::ToRadians(15)) * LuxEngine::ToRadians(15);
            const auto localRotation = axis == 0   ? LuxEngine::Mat4::RotateX(angle)
                                       : axis == 1 ? LuxEngine::Mat4::RotateY(angle)
                                                   : LuxEngine::Mat4::RotateZ(angle);
            const auto originalRotation = LuxEngine::Mat4::RotateZ(initialRotation_.z) *
                                          LuxEngine::Mat4::RotateY(initialRotation_.y) *
                                          LuxEngine::Mat4::RotateX(initialRotation_.x);
            const auto rotation = document_.GetTransformSpace() == TransformSpace::Local
                                      ? originalRotation * localRotation
                                      : localRotation * originalRotation;
            const auto* m = rotation.elements;
            value.rotation.y = std::asin(std::clamp(-m[2], -1.0f, 1.0f));
            if (std::abs(std::cos(value.rotation.y)) > .00001f) {
                value.rotation.x = std::atan2(m[6], m[10]);
                value.rotation.z = std::atan2(m[1], m[0]);
            } else {
                value.rotation.x = 0;
                value.rotation.z = std::atan2(-m[4], m[5]);
            }
        } else if (tool_ == EditorTool::Scale) {
            double pixels = drag.x() - drag.y();
            if (axis_ >= 0) {
                const auto camera = MakeViewportCamera3D(size(), focus_, yaw_, pitch_, distance_);
                const auto direction = GizmoDirection(initialBasis_, axis_);
                const auto projected = ProjectViewportSegment(
                    camera, size(), initialPivot_,
                    initialPivot_ +
                        direction * float(GizmoLength(camera, initialPosition_, height())));
                if (!projected || projected->length() < 1)
                    return;
                pixels = QPointF::dotProduct(drag, projected->p2() - projected->p1()) /
                         projected->length();
            }
            const float factor = static_cast<float>(std::clamp(std::exp(pixels * .01), .05, 100.0));
            value.scale = {initialScale_.x * (axis_ < 0 || axis_ == 0 ? factor : 1),
                           initialScale_.y * (axis_ < 0 || axis_ == 1 ? factor : 1),
                           initialScale_.z * (axis_ < 0 || axis_ == 2 ? factor : 1)};
        }
        if (std::isfinite(value.position.x) && std::isfinite(value.position.y) &&
            std::isfinite(value.position.z) && std::isfinite(value.rotation.x) &&
            std::isfinite(value.rotation.y) && std::isfinite(value.rotation.z) &&
            std::isfinite(value.scale.x) && std::isfinite(value.scale.y) &&
            std::isfinite(value.scale.z))
            onTransform_(dragId_, value);
    }
    lastMouse_ = event->position();
}

void SceneViewport3D::wheelEvent(QWheelEvent* event) {
    if (dragging_ || marquee_)
        CancelInteraction();
    distance_ = std::clamp(distance_ * std::pow(.9, event->angleDelta().y() / 120.0), .1, 100000.0);
    update();
}

void SceneViewport3D::RefreshScene() {
    update();
}

void SceneViewport3D::SetTool(EditorTool tool) {
    if (tool_ != tool)
        CancelInteraction();
    tool_ = tool;
}
} // namespace LuxEditor
