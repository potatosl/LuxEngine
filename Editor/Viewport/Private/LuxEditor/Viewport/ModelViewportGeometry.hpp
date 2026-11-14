#pragma once

#include <LuxEditor/Viewport/ViewportCamera3D.hpp>
#include <LuxEngine/Scene/Entity.hpp>
#include <optional>

namespace LuxEditor {
// Structured FBX parts stay node-local, including their currently deformed skin/morph pose.
bool ModelViewportBounds(LuxEngine::Entity entity, LuxEngine::Vec3& minimum,
                         LuxEngine::Vec3& maximum);
std::optional<double> IntersectModelViewport(const ViewportRay3D& ray, LuxEngine::Entity entity);
} // namespace LuxEditor
