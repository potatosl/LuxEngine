#pragma once

#include <LuxEditor/Modeling/MeshModeling.hpp>
#include <LuxEditor/Viewport/ViewportCamera3D.hpp>
#include <LuxUI/Widgets.hpp>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace LuxEditor {

enum class ModelingEditMode { Object, Vertex, Face };

struct ModelingSelection {
    std::vector<LuxEngine::uint32> vertices;
    std::vector<LuxEngine::uint32> faces;
};

std::optional<LuxEngine::uint32> PickModelingVertex(const ModelingMesh& mesh,
                                                    const LuxEngine::Mat4& world,
                                                    const LuxEngine::PerspectiveCamera& camera,
                                                    LuxUI::SizeF viewport, LuxUI::PointF point,
                                                    double radius = 9);

std::optional<LuxEngine::uint32>
PickModelingFace(const ModelingMesh& mesh, const LuxEngine::Mat4& world, const ViewportRay3D& ray);

std::vector<LuxEngine::uint32> ModelingFaceVertices(const ModelingMesh& mesh,
                                                    std::span<const LuxEngine::uint32> faces);

LuxEngine::Vec3 ModelingPivot(const ModelingMesh& mesh, const ModelingSelection& selection,
                              const LuxEngine::Mat4& world);

// Converts the complete world-space gesture to object-local space, including parent shear and
// nonuniform scale. Each preview starts from the captured mesh, never from a previous preview.
bool PreviewModelingTransform(const ModelingMesh& original, ModelingMesh& preview,
                              const ModelingSelection& selection, const LuxEngine::Mat4& world,
                              const LuxEngine::Mat4& worldDelta, std::string& error);

void DrawModelingOverlay(LuxUI::Painter& painter, const ModelingMesh& mesh,
                         const ModelingSelection& selection, const LuxEngine::Mat4& world,
                         const LuxEngine::PerspectiveCamera& camera, LuxUI::SizeF viewport,
                         ModelingEditMode mode);

} // namespace LuxEditor
