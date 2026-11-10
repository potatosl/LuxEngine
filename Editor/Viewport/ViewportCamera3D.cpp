#include "ViewportCamera3D.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace LuxEditor {
LuxEngine::PerspectiveCamera MakeViewportCamera3D(QSizeF size, LuxEngine::Vec3 focus, double yaw,
                                                  double pitch, double distance) {
    yaw = std::isfinite(yaw) ? yaw : 0;
    pitch = std::isfinite(pitch) ? pitch : 0;
    distance = std::max(.1, std::isfinite(distance) ? distance : 5);
    if (!std::isfinite(focus.x) || !std::isfinite(focus.y) || !std::isfinite(focus.z))
        focus = {};
    const LuxEngine::Vec3 eye{
        static_cast<float>(focus.x + distance * std::sin(yaw) * std::cos(pitch)),
        static_cast<float>(focus.y - distance * std::sin(pitch)),
        static_cast<float>(focus.z + distance * std::cos(yaw) * std::cos(pitch))};
    const double aspect = size.height() > 0 ? size.width() / size.height() : 1;
    LuxEngine::PerspectiveCamera camera(
        LuxEngine::ToRadians(60),
        static_cast<float>(std::isfinite(aspect) && aspect > .001 ? aspect : 1), .1f, 1000);
    camera.SetPosition(eye);
    camera.SetTarget(focus);
    camera.SetUp({0, 1, 0});
    return camera;
}
std::optional<QPointF> ProjectViewportPoint(const LuxEngine::PerspectiveCamera& camera, QSizeF size,
                                            LuxEngine::Vec3 point) {
    if (size.width() <= 0 || size.height() <= 0)
        return std::nullopt;
    const auto& m = camera.GetViewProjectionMatrix().elements;
    const double x = m[0] * point.x + m[4] * point.y + m[8] * point.z + m[12];
    const double y = m[1] * point.x + m[5] * point.y + m[9] * point.z + m[13];
    const double z = m[2] * point.x + m[6] * point.y + m[10] * point.z + m[14];
    const double w = m[3] * point.x + m[7] * point.y + m[11] * point.z + m[15];
    if (!std::isfinite(w) || w <= 0 || z < 0 || z > w)
        return std::nullopt;
    const QPointF screen((x / w + 1) * size.width() * .5, (1 - y / w) * size.height() * .5);
    if (!std::isfinite(screen.x()) || !std::isfinite(screen.y()))
        return std::nullopt;
    return screen;
}
std::optional<ViewportRay3D> MakeViewportRay3D(const LuxEngine::PerspectiveCamera& camera,
                                               QSizeF size, QPointF point) {
    if (size.width() <= 0 || size.height() <= 0 || !std::isfinite(point.x()) ||
        !std::isfinite(point.y()))
        return std::nullopt;
    const auto& p = camera.GetProjectionMatrix().elements;
    const auto& v = camera.GetViewMatrix().elements;
    const double x = (2 * point.x() / size.width() - 1) / p[0];
    const double y = (1 - 2 * point.y() / size.height()) / p[5];
    const LuxEngine::Vec3 direction{static_cast<float>(v[0] * x + v[1] * y - v[2]),
                                    static_cast<float>(v[4] * x + v[5] * y - v[6]),
                                    static_cast<float>(v[8] * x + v[9] * y - v[10])};
    return ViewportRay3D{camera.GetPosition(), direction.Normalized()};
}
std::optional<double> IntersectViewportBox(const ViewportRay3D& ray, const LuxEngine::Mat4& model) {
    const auto& m = model.elements;
    const LuxEngine::Vec3 relative = ray.origin - LuxEngine::Vec3{m[12], m[13], m[14]};
    double entry = 0, exit = std::numeric_limits<double>::infinity();
    for (int axis = 0; axis < 3; ++axis) {
        const LuxEngine::Vec3 basis{m[axis * 4], m[axis * 4 + 1], m[axis * 4 + 2]};
        const double squaredLength = basis.Dot(basis);
        if (!std::isfinite(squaredLength) || squaredLength < 1e-12)
            return std::nullopt;
        const double origin = basis.Dot(relative) / squaredLength;
        const double direction = basis.Dot(ray.direction) / squaredLength;
        if (!std::isfinite(origin) || !std::isfinite(direction))
            return std::nullopt;
        if (std::abs(direction) < 1e-12) {
            if (origin < -.5 || origin > .5)
                return std::nullopt;
            continue;
        }
        double a = (-.5 - origin) / direction, b = (.5 - origin) / direction;
        if (a > b)
            std::swap(a, b);
        entry = std::max(entry, a);
        exit = std::min(exit, b);
        if (entry > exit)
            return std::nullopt;
    }
    return entry > 0 ? entry : exit;
}
} // namespace LuxEditor
