#include <LuxEditor/Scene/EditorDocument.hpp>
#include <LuxEditor/Viewport/EngineSceneRenderer.hpp>
#include <LuxEditor/Viewport/SceneViewport3D.hpp>
#include <LuxEditor/Viewport/ViewportCamera3D.hpp>
#include <LuxEngine/Assets/AssetManager.hpp>
#include <LuxEngine/ECS/ECS.hpp>
#include <QFocusEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <limits>
#include <vector>

namespace LuxEditor {
namespace {
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
    setMinimumSize(300, 200);
}

void SceneViewport3D::CancelInteraction() {
    orbiting_ = panning_ = dragging_ = false;
    dragId_ = 0;
}

void SceneViewport3D::focusOutEvent(QFocusEvent* event) {
    const bool editing = dragging_;
    CancelInteraction();
    if (editing)
        onEndEdit_();
    QWidget::focusOutEvent(event);
}

void SceneViewport3D::FocusSelection() {
    LuxEngine::Entity selected = document_.GetSelected();
    if (selected && selected.HasComponent<LuxEngine::TransformComponent>()) {
        focus_ = TransformCorner(
            BoundsModel(selected, engineFrame_ ? engineRenderer_.get() : nullptr), {});
        update();
    }
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

    const auto& color = document_.GetScene().GetSettings().clearColor;
    painter.fillRect(rect(), QColor::fromRgbF(color.x, color.y, color.z, color.w));
    if (engineFrame_ && !cachedBackground_.isNull())
        painter.drawImage(rect(), cachedBackground_);
    painter.setRenderHint(QPainter::Antialiasing);

    const auto camera = MakeViewportCamera3D(size(), focus_, yaw_, pitch_, distance_);
    const auto project = [&](LuxEngine::Vec3 point) {
        return ProjectViewportPoint(camera, size(), point);
    };
    const int gridAlpha = engineFrame_ ? 75 : 255;
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
    for (int unit = -20; unit <= 20; ++unit) {
        painter.setPen(QPen(line(unit == 0       ? QColor(95, 108, 124)
                                 : unit % 5 == 0 ? QColor(70, 81, 95)
                                                 : QColor(56, 66, 78)),
                            unit == 0 ? 2 : 1));
        auto a = project({static_cast<float>(unit), 0, -20}),
             b = project({static_cast<float>(unit), 0, 20});
        if (a && b)
            painter.drawLine(*a, *b);
        a = project({-20, 0, static_cast<float>(unit)});
        b = project({20, 0, static_cast<float>(unit)});
        if (a && b)
            painter.drawLine(*a, *b);
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
    const LuxEngine::Entity selected = document_.GetSelected();
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
        const bool active = selected && selected.GetId() == entity.GetId();
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
        const LuxEngine::Vec3 surface =
            ray->origin + ray->direction * static_cast<float>(*distance);
        if (!ProjectViewportPoint(camera, size(), surface))
            continue;
        best = *distance;
        nearest = entity.GetId();
    }
    onSelect_(nearest);
    if (nearest) {
        LuxEngine::Entity selected = document_.GetSelected();
        if (!selected || selected.GetId() != *nearest)
            return;
        LuxEngine::TransformComponent transform;
        try {
            transform = selected.GetScene()->GetWorldTransform(selected.GetId());
        } catch (const std::exception&) {
            CancelInteraction();
            return;
        }
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
        LuxEngine::Entity entity = document_.GetSelected();
        if (!entity || entity.GetId() != dragId_ ||
            !entity.HasComponent<LuxEngine::TransformComponent>()) {
            CancelInteraction();
            onEndEdit_();
            return;
        }
        LuxEngine::TransformComponent value;
        try {
            value = entity.GetScene()->GetWorldTransform(entity.GetId());
        } catch (const std::exception&) {
            CancelInteraction();
            onEndEdit_();
            return;
        }
        const QPointF drag = event->position() - dragStart_;
        if (tool_ == EditorTool::Move || tool_ == EditorTool::Select) {
            const auto camera = MakeViewportCamera3D(size(), focus_, yaw_, pitch_, distance_);
            const auto startRay = MakeViewportRay3D(camera, size(), dragStart_);
            const auto currentRay = MakeViewportRay3D(camera, size(), event->position());
            if (!startRay || !currentRay)
                return;
            auto normal = event->modifiers() & Qt::ShiftModifier ? LuxEngine::Vec3{0, 0, 1}
                                                                 : LuxEngine::Vec3{0, 1, 0};
            if (std::abs(currentRay->direction.Dot(normal)) < .01f)
                normal = (camera.GetPosition() - focus_).Normalized();
            const auto intersect = [&](const ViewportRay3D& ray) {
                const auto denominator = ray.direction.Dot(normal);
                return ray.origin +
                       ray.direction * ((initialPosition_ - ray.origin).Dot(normal) / denominator);
            };
            if (std::abs(startRay->direction.Dot(normal)) < .0001f)
                return;
            value.position = initialPosition_ + intersect(*currentRay) - intersect(*startRay);
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
} // namespace LuxEditor
