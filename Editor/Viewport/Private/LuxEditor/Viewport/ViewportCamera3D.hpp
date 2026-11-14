#pragma once
#include <LuxEngine/Renderer/Renderer3D/Renderer3D.hpp>
#include <LuxUI/Widgets.hpp>
#include <optional>

namespace LuxEditor {
LuxEngine::PerspectiveCamera MakeViewportCamera3D(LuxUI::SizeF size, LuxEngine::Vec3 focus,
                                                  double yaw, double pitch, double distance);
std::optional<LuxUI::PointF> ProjectViewportPoint(const LuxEngine::PerspectiveCamera& camera,
                                                  LuxUI::SizeF size, LuxEngine::Vec3 point);

// Clip segments against the complete camera frustum before perspective division.
std::optional<LuxUI::LineF> ProjectViewportSegment(const LuxEngine::PerspectiveCamera& camera,
                                                   LuxUI::SizeF size, LuxEngine::Vec3 start,
                                                   LuxEngine::Vec3 end);
// A 1/2/5 grid interval that remains readable while zooming.
double ViewportGridSpacing(double worldUnitsPerPixel);

struct ViewportRay3D {
    LuxEngine::Vec3 origin;
    LuxEngine::Vec3 direction;
};

std::optional<ViewportRay3D> MakeViewportRay3D(const LuxEngine::PerspectiveCamera& camera,
                                               LuxUI::SizeF size, LuxUI::PointF point);
// The local unit cube may have a full affine world transform, including parent-induced shear.
// The result uses the original ray parameter, so normalized rays return world-space distance.
std::optional<double> IntersectViewportBox(const ViewportRay3D& ray, const LuxEngine::Mat4& model);
} // namespace LuxEditor
