#pragma once

#include <LuxEngine/Core/Core.hpp>
#include <LuxEngine/Core/Math.hpp>
#include <LuxEngine/Renderer/Renderer3D/Renderer3D.hpp>
#include <LuxUI/Widgets.hpp>
#include <array>
#include <optional>
#include <span>

namespace LuxEngine {
class Scene;
}

namespace LuxEditor {

struct CameraGizmoGeometry {
    LuxEngine::Vec3 position;
    LuxEngine::Vec3 forward;
    LuxEngine::Vec3 up;
    bool perspective = false;
    float nearClip = 0;
    float farClip = 0;
    std::array<LuxEngine::Vec3, 4> nearCorners;
    std::array<LuxEngine::Vec3, 4> farCorners;
};

// Camera scale does not change its lens. Perspective orientation matches the runtime's world
// forward/up basis; a 2D camera uses the runtime's XY position and Z rotation convention.
std::optional<CameraGizmoGeometry>
BuildCameraGizmoGeometry(LuxEngine::Scene& scene, LuxEngine::uint32 entity, double aspect);

void DrawCameraGizmos(LuxUI::Painter& painter, LuxEngine::Scene& scene,
                      const LuxEngine::PerspectiveCamera& view, LuxUI::SizeF viewport,
                      std::span<const LuxEngine::uint32> selected);

// Only the small camera body marker is pickable. Frustum lines never steal mesh selections.
std::optional<LuxEngine::uint32> PickCameraGizmo(LuxEngine::Scene& scene,
                                                 const LuxEngine::PerspectiveCamera& view,
                                                 LuxUI::SizeF viewport, LuxUI::PointF point);

} // namespace LuxEditor
