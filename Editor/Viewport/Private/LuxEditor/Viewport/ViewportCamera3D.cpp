#include <LuxEditor/Viewport/ViewportCamera3D.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <limits>
#include <numbers>

namespace LuxEditor {
LuxEngine::PerspectiveCamera MakeViewportCamera3D(QSizeF size, LuxEngine::Vec3 focus, double yaw,
                                                  double pitch, double distance) {
    yaw = std::isfinite(yaw) ? yaw : 0;
    pitch = std::isfinite(pitch) ? pitch : 0;
    yaw = std::remainder(yaw, 2 * std::numbers::pi);
    pitch = std::clamp(pitch, -std::numbers::pi / 2, std::numbers::pi / 2);
    distance = std::clamp(std::isfinite(distance) ? distance : 5, .1, 100000.0);
    if (!std::isfinite(focus.x) || !std::isfinite(focus.y) || !std::isfinite(focus.z))
        focus = {};
    const LuxEngine::Vec3 eye{
        static_cast<float>(focus.x + distance * std::sin(yaw) * std::cos(pitch)),
        static_cast<float>(focus.y - distance * std::sin(pitch)),
        static_cast<float>(focus.z + distance * std::cos(yaw) * std::cos(pitch))};
    const double aspect = size.height() > 0 ? size.width() / size.height() : 1;
    LuxEngine::PerspectiveCamera camera(
        LuxEngine::ToRadians(60),
        static_cast<float>(std::isfinite(aspect) && aspect > .001 ? aspect : 1),
        static_cast<float>(std::clamp(distance * .001, .001, 1000.0)),
        static_cast<float>(std::max(1000.0, distance * 100.0)));
    camera.SetPosition(eye);
    camera.SetTarget(focus);
    camera.SetUp(std::abs(std::cos(pitch)) < .0001 ? LuxEngine::Vec3{0, 0, -1}
                                                   : LuxEngine::Vec3{0, 1, 0});
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

std::optional<QLineF> ProjectViewportSegment(const LuxEngine::PerspectiveCamera& camera,
                                             QSizeF size, LuxEngine::Vec3 start,
                                             LuxEngine::Vec3 end) {
    if (!(size.width() > 0 && size.height() > 0) || !std::isfinite(size.width()) ||
        !std::isfinite(size.height()))
        return std::nullopt;
    const auto& m = camera.GetViewProjectionMatrix().elements;
    const auto clip = [&m](LuxEngine::Vec3 point) {
        return std::array<double, 4>{
            double(m[0]) * point.x + double(m[4]) * point.y + double(m[8]) * point.z + m[12],
            double(m[1]) * point.x + double(m[5]) * point.y + double(m[9]) * point.z + m[13],
            double(m[2]) * point.x + double(m[6]) * point.y + double(m[10]) * point.z + m[14],
            double(m[3]) * point.x + double(m[7]) * point.y + double(m[11]) * point.z + m[15]};
    };
    const auto a = clip(start), b = clip(end);
    if (std::any_of(a.begin(), a.end(), [](double value) { return !std::isfinite(value); }) ||
        std::any_of(b.begin(), b.end(), [](double value) { return !std::isfinite(value); }))
        return std::nullopt;
    const std::array<double, 6> from{a[3] + a[0], a[3] - a[0], a[3] + a[1],
                                     a[3] - a[1], a[2],        a[3] - a[2]};
    const std::array<double, 6> to{b[3] + b[0], b[3] - b[0], b[3] + b[1],
                                   b[3] - b[1], b[2],        b[3] - b[2]};
    double low = 0, high = 1;
    for (size_t plane = 0; plane < from.size(); ++plane) {
        if (from[plane] < 0 && to[plane] < 0)
            return std::nullopt;
        if ((from[plane] < 0) != (to[plane] < 0)) {
            const double crossing = from[plane] / (from[plane] - to[plane]);
            if (from[plane] < 0)
                low = std::max(low, crossing);
            else
                high = std::min(high, crossing);
        }
    }
    if (low > high)
        return std::nullopt;
    const auto project = [&](double fraction) -> std::optional<QPointF> {
        const double w = a[3] + (b[3] - a[3]) * fraction;
        if (w <= 0)
            return std::nullopt;
        return QPointF((1 + (a[0] + (b[0] - a[0]) * fraction) / w) * size.width() * .5,
                       (1 - (a[1] + (b[1] - a[1]) * fraction) / w) * size.height() * .5);
    };
    const auto first = project(low), last = project(high);
    return first && last ? std::optional<QLineF>(QLineF(*first, *last)) : std::nullopt;
}

double ViewportGridSpacing(double worldUnitsPerPixel) {
    if (!std::isfinite(worldUnitsPerPixel) || worldUnitsPerPixel <= 0)
        return 1;
    const double target = std::clamp(worldUnitsPerPixel * 45, .00001, 10000000.0);
    const double magnitude = std::pow(10.0, std::floor(std::log10(target)));
    const double normalized = target / magnitude;
    return magnitude * (normalized <= 1 ? 1 : normalized <= 2 ? 2 : normalized <= 5 ? 5 : 10);
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
    if (std::any_of(std::begin(m), std::end(m),
                    [](float value) { return !std::isfinite(value); }) ||
        std::abs(m[3]) > 1e-6f || std::abs(m[7]) > 1e-6f || std::abs(m[11]) > 1e-6f ||
        std::abs(m[15] - 1) > 1e-6f)
        return std::nullopt;
    const double a = m[0], b = m[4], c = m[8], d = m[1], e = m[5], f = m[9], g = m[2], h = m[6],
                 i = m[10];
    const double determinant = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    const double volume = std::hypot(a, d, g) * std::hypot(b, e, h) * std::hypot(c, f, i);
    if (!std::isfinite(determinant) || volume == 0 || std::abs(determinant) <= volume * 1e-12)
        return std::nullopt;
    const double inverse[3][3] = {{(e * i - f * h) / determinant, (c * h - b * i) / determinant,
                                   (b * f - c * e) / determinant},
                                  {(f * g - d * i) / determinant, (a * i - c * g) / determinant,
                                   (c * d - a * f) / determinant},
                                  {(d * h - e * g) / determinant, (b * g - a * h) / determinant,
                                   (a * e - b * d) / determinant}};
    const double relative[3] = {static_cast<double>(ray.origin.x) - m[12],
                                static_cast<double>(ray.origin.y) - m[13],
                                static_cast<double>(ray.origin.z) - m[14]};
    double entry = 0, exit = std::numeric_limits<double>::infinity();
    for (int axis = 0; axis < 3; ++axis) {
        // Transform both parts of the ray by the inverse linear matrix. Projecting onto each
        // basis independently only works for orthogonal columns and misses sheared children.
        const auto& row = inverse[axis];
        const double origin = row[0] * relative[0] + row[1] * relative[1] + row[2] * relative[2];
        const double direction =
            row[0] * ray.direction.x + row[1] * ray.direction.y + row[2] * ray.direction.z;
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
    return std::isfinite(exit) ? std::optional<double>(entry > 0 ? entry : exit) : std::nullopt;
}
} // namespace LuxEditor
