#pragma once
#include <LuxEngine/Renderer/Renderer3D/Renderer3D.hpp>
#include <QPointF>
#include <QSizeF>
#include <optional>

namespace LuxEditor {
LuxEngine::PerspectiveCamera MakeViewportCamera3D(QSizeF size, LuxEngine::Vec3 focus, double yaw,
                                                  double pitch, double distance);
std::optional<QPointF> ProjectViewportPoint(const LuxEngine::PerspectiveCamera& camera, QSizeF size,
                                            LuxEngine::Vec3 point);
struct ViewportRay3D {
    LuxEngine::Vec3 origin;
    LuxEngine::Vec3 direction;
};
std::optional<ViewportRay3D> MakeViewportRay3D(const LuxEngine::PerspectiveCamera& camera,
                                               QSizeF size, QPointF point);
// The local unit cube may have a full affine world transform, including parent-induced shear.
// The result uses the original ray parameter, so normalized rays return world-space distance.
std::optional<double> IntersectViewportBox(const ViewportRay3D& ray, const LuxEngine::Mat4& model);
} // namespace LuxEditor
