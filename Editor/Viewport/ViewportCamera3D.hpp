#pragma once
#include "Engine/Renderer/Renderer3D/Renderer3D.hpp"
#include <QPointF>
#include <QSizeF>
#include <optional>

namespace LuxEngine {
PerspectiveCamera MakeViewportCamera3D(QSizeF size, Vec3 focus, double yaw, double pitch,
                                       double distance);
std::optional<QPointF> ProjectViewportPoint(const PerspectiveCamera& camera, QSizeF size,
                                            Vec3 point);
struct ViewportRay3D {
    Vec3 origin;
    Vec3 direction;
};
std::optional<ViewportRay3D> MakeViewportRay3D(const PerspectiveCamera& camera, QSizeF size,
                                               QPointF point);
// Models contain translation, rotation and scale; the mesh bounds are the local unit cube.
std::optional<double> IntersectViewportBox(const ViewportRay3D& ray, const Mat4& model);
} // namespace LuxEngine
