#include <LuxEditor/Viewport/CameraGizmos.hpp>
#include <LuxEditor/Viewport/ViewportCamera3D.hpp>
#include <LuxEngine/Scene/Entity.hpp>
#include <LuxEngine/Scene/Scene.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace LuxEditor {
namespace {
using namespace LuxEngine;

Vec3 Unit(Vec3 value, Vec3 fallback) {
    const double length = std::hypot(double(value.x), double(value.y), double(value.z));
    return std::isfinite(length) && length > 1e-30
               ? Vec3{float(value.x / length), float(value.y / length), float(value.z / length)}
               : fallback;
}

Vec3 Up(Vec3 forward, Vec3 authored) {
    auto right = forward.Cross(Unit(authored, {0, 1, 0}));
    if (right.Dot(right) < 1e-10f)
        right = forward.Cross(std::abs(forward.y) < .99f ? Vec3{0, 1, 0} : Vec3{0, 0, 1});
    return Unit(Unit(right, {1, 0, 0}).Cross(forward), {0, 1, 0});
}

std::optional<Vec3> Corner(Vec3 position, Vec3 forward, Vec3 right, Vec3 up, double depth, double x,
                           double y) {
    std::array<double, 3> result;
    result[0] =
        double(position.x) + double(forward.x) * depth + double(right.x) * x + double(up.x) * y;
    result[1] =
        double(position.y) + double(forward.y) * depth + double(right.y) * x + double(up.y) * y;
    result[2] =
        double(position.z) + double(forward.z) * depth + double(right.z) * x + double(up.z) * y;
    for (const auto value : result)
        if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
            return std::nullopt;
    return Vec3{float(result[0]), float(result[1]), float(result[2])};
}

bool ValidViewport(LuxUI::SizeF size) {
    return std::isfinite(size.width()) && std::isfinite(size.height()) && size.width() > 0 &&
           size.height() > 0;
}

std::optional<LuxUI::PointF> Marker(const CameraGizmoGeometry& geometry,
                                    const PerspectiveCamera& view, LuxUI::SizeF viewport) {
    const auto position = ProjectViewportPoint(view, viewport, geometry.position);
    return position && LuxUI::RectF(0, 0, viewport.width(), viewport.height()).contains(*position)
               ? position
               : std::nullopt;
}

void Line(LuxUI::Painter& painter, const PerspectiveCamera& view, LuxUI::SizeF viewport, Vec3 first,
          Vec3 last) {
    // The shared helper clips homogeneous X/Y and Vulkan-style depth (0 <= Z <= W)
    // before perspective division, including segments crossing the view's near plane.
    if (const auto line = ProjectViewportSegment(view, viewport, first, last))
        painter.drawLine(*line);
}

Vec3 Center(const std::array<Vec3, 4>& corners) {
    // Scale each term before summing to avoid overflowing valid large clipping ranges.
    return corners[0] * .25f + corners[1] * .25f + corners[2] * .25f + corners[3] * .25f;
}
} // namespace

std::optional<CameraGizmoGeometry> BuildCameraGizmoGeometry(Scene& scene, uint32 entity,
                                                            double aspect) {
    if (!scene.IsAlive(entity) || !scene.HasComponent<TransformComponent>(entity) ||
        !std::isfinite(aspect) || aspect <= 0 || aspect > std::numeric_limits<float>::max())
        return std::nullopt;
    const auto* camera = scene.TryGetComponent<CameraComponent>(entity);
    if (!camera)
        return std::nullopt;
    try {
        const auto world = scene.GetWorldMatrix(entity);
        if (std::any_of(std::begin(world.elements), std::end(world.elements),
                        [](float value) { return !std::isfinite(value); }))
            return std::nullopt;
        CameraGizmoGeometry geometry;
        geometry.position = {world.elements[12], world.elements[13], world.elements[14]};
        geometry.perspective = camera->isPerspective;
        if (camera->isPerspective) {
            if (!std::isfinite(camera->fov) || camera->fov <= 0 || camera->fov >= 180 ||
                !std::isfinite(camera->nearClip) || !std::isfinite(camera->farClip) ||
                camera->nearClip <= 0 || camera->farClip <= camera->nearClip)
                return std::nullopt;
            // Match runtime projection validation, including ranges whose float coefficients
            // cannot represent distinct near/far planes despite individually finite values.
            (void)Mat4::Perspective(ToRadians(camera->fov), float(aspect), camera->nearClip,
                                    camera->farClip);
            geometry.forward =
                Unit({-world.elements[8], -world.elements[9], -world.elements[10]}, {0, 0, -1});
            geometry.up =
                Up(geometry.forward, {world.elements[4], world.elements[5], world.elements[6]});
            geometry.nearClip = camera->nearClip;
            geometry.farClip = camera->farClip;
        } else {
            if (!std::isfinite(camera->orthoSize) || camera->orthoSize <= 0)
                return std::nullopt;
            float angle = 0;
            try {
                angle = scene.GetWorldTransform(entity).rotation.z;
            } catch (const std::logic_error&) {
                for (auto parent = entity; parent; parent = scene.GetParent(parent))
                    if (const auto* transform = scene.TryGetComponent<TransformComponent>(parent))
                        angle += transform->rotation.z;
            }
            if (!std::isfinite(angle))
                return std::nullopt;
            geometry.forward = {0, 0, -1};
            geometry.up = {-std::sin(angle), std::cos(angle), 0};
        }
        const auto right = Unit(geometry.forward.Cross(geometry.up), {1, 0, 0});
        const double tangent = camera->isPerspective ? std::tan(double(ToRadians(camera->fov)) * .5)
                                                     : double(camera->orthoSize);
        for (size_t index = 0; index < 4; ++index) {
            const double x = index == 0 || index == 3 ? -1 : 1;
            const double y = index < 2 ? -1 : 1;
            const auto plane = [&](double depth) {
                const double halfHeight = camera->isPerspective ? tangent * depth : tangent;
                return Corner(geometry.position, geometry.forward, right, geometry.up, depth,
                              x * halfHeight * aspect, y * halfHeight);
            };
            const auto nearPoint = plane(geometry.nearClip), farPoint = plane(geometry.farClip);
            if (!nearPoint || !farPoint)
                return std::nullopt;
            geometry.nearCorners[index] = *nearPoint;
            geometry.farCorners[index] = *farPoint;
        }
        return geometry;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

void DrawCameraGizmos(LuxUI::Painter& painter, Scene& scene, const PerspectiveCamera& view,
                      LuxUI::SizeF viewport, std::span<const uint32> selected) {
    if (!ValidViewport(viewport))
        return;
    painter.save();
    painter.setClipRect(LuxUI::RectF(0, 0, viewport.width(), viewport.height()));
    painter.setRenderHint(LuxUI::Painter::Antialiasing);
    auto ids = scene.GetComponentEntityIds<CameraComponent>();
    std::sort(ids.begin(), ids.end());
    for (const auto id : ids) {
        const auto geometry =
            BuildCameraGizmoGeometry(scene, id, viewport.width() / viewport.height());
        if (!geometry)
            continue;
        const bool active = std::find(selected.begin(), selected.end(), id) != selected.end();
        const LuxUI::Color color =
            active ? LuxUI::Color(255, 198, 86) : LuxUI::Color(130, 184, 221);
        painter.setBrush(LuxUI::Flags::NoBrush);
        if (active) {
            for (size_t corner = 0; corner < 4; ++corner) {
                const auto next = (corner + 1) % 4;
                painter.setPen(LuxUI::Pen(LuxUI::Color(117, 203, 240), 1.5));
                Line(painter, view, viewport, geometry->nearCorners[corner],
                     geometry->nearCorners[next]);
                if (geometry->perspective) {
                    painter.setPen(LuxUI::Pen(color, 1.5));
                    Line(painter, view, viewport, geometry->farCorners[corner],
                         geometry->farCorners[next]);
                    Line(painter, view, viewport, geometry->nearCorners[corner],
                         geometry->farCorners[corner]);
                    painter.setPen(
                        LuxUI::Pen(LuxUI::Color(255, 198, 86, 120), 1, LuxUI::Flags::DashLine));
                    Line(painter, view, viewport, geometry->position,
                         geometry->nearCorners[corner]);
                }
            }
            if (geometry->perspective) {
                painter.setPen(color);
                for (const auto& [center, label] : std::array<std::pair<Vec3, LuxUI::String>, 2>{
                         {{Center(geometry->nearCorners),
                           LUX_UI_TEXT("N %1").arg(geometry->nearClip, 0, 'g', 4)},
                          {Center(geometry->farCorners),
                           LUX_UI_TEXT("F %1").arg(geometry->farClip, 0, 'g', 4)}}})
                    if (const auto point = ProjectViewportPoint(view, viewport, center);
                        point &&
                        LuxUI::RectF(0, 0, viewport.width(), viewport.height()).contains(*point))
                        painter.drawText(*point + LuxUI::PointF(6, -6), label);
            }
        }
        const auto body = Marker(*geometry, view, viewport);
        if (!body)
            continue;
        // Fixed logical pixels keep distant cameras selectable without huge world-space boxes.
        painter.setPen(LuxUI::Pen(color, active ? 2 : 1.5));
        painter.setBrush(LuxUI::Color(35, 44, 54, 235));
        painter.drawRoundedRect(LuxUI::RectF(*body - LuxUI::PointF(9, 6), LuxUI::SizeF(14, 12)), 2,
                                2);
        painter.drawPolygon(
            LuxUI::PolygonF{*body + LuxUI::PointF(5, -3), *body + LuxUI::PointF(11, -7),
                            *body + LuxUI::PointF(11, 7), *body + LuxUI::PointF(5, 3)});
        painter.setBrush(LuxUI::Flags::NoBrush);
        const auto direction = geometry->perspective ? geometry->forward : geometry->up;
        const auto forward = Unit(view.GetTarget() - view.GetPosition(), {0, 0, -1});
        const double depth = (geometry->position - view.GetPosition()).Dot(forward);
        const double projection = std::abs(double(view.GetProjectionMatrix().elements[5]));
        const float length = float(std::clamp(
            depth * 48 / std::max(.000001, projection) / viewport.height(), .01, 1000000.0));
        const auto tip = geometry->position + direction * length;
        const auto side = Unit(direction.Cross(view.GetPosition() - geometry->position), {1, 0, 0});
        Line(painter, view, viewport, geometry->position, tip);
        Line(painter, view, viewport, tip,
             tip - direction * (length * .25f) + side * (length * .15f));
        Line(painter, view, viewport, tip,
             tip - direction * (length * .25f) - side * (length * .15f));
    }
    painter.restore();
}

std::optional<uint32> PickCameraGizmo(Scene& scene, const PerspectiveCamera& view,
                                      LuxUI::SizeF viewport, LuxUI::PointF point) {
    if (!ValidViewport(viewport) || !std::isfinite(point.x()) || !std::isfinite(point.y()))
        return std::nullopt;
    std::optional<uint32> hit;
    double nearest = std::numeric_limits<double>::infinity();
    auto ids = scene.GetComponentEntityIds<CameraComponent>();
    std::sort(ids.begin(), ids.end());
    const auto forward = Unit(view.GetTarget() - view.GetPosition(), {0, 0, -1});
    for (const auto id : ids) {
        const auto geometry =
            BuildCameraGizmoGeometry(scene, id, viewport.width() / viewport.height());
        if (!geometry)
            continue;
        const auto body = Marker(*geometry, view, viewport);
        if (!body ||
            !LuxUI::RectF(*body - LuxUI::PointF(10, 8), LuxUI::SizeF(22, 16)).contains(point))
            continue;
        const double depth = (geometry->position - view.GetPosition()).Dot(forward);
        if (depth < nearest) {
            nearest = depth;
            hit = id;
        }
    }
    return hit;
}
} // namespace LuxEditor
