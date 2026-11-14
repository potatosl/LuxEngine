#pragma once

#include <LuxEngine/Renderer/Renderer3D/Renderer3D.hpp>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace LuxEngine {
class Scene;
class Entity;
} // namespace LuxEngine

namespace LuxEditor {

struct ModelingMesh {
    std::vector<LuxEngine::Vertex3D> vertices;
    std::vector<LuxEngine::uint32> indices;
    bool hasTexCoords = false;
};

struct ModelingMeshSource {
    const ModelingMesh* mesh = nullptr;
    LuxEngine::Mat4 transform;
};

// Returns an independent editable snapshot. Source assets, animation and scene data are unchanged.
bool LoadModelingMesh(LuxEngine::Scene& scene, LuxEngine::Entity entity, ModelingMesh& result,
                      std::string& error);

std::vector<LuxEngine::uint32> CoincidentVertices(const ModelingMesh& mesh,
                                                  LuxEngine::uint32 vertex);

// Returns the connected, consistently oriented plane patch containing a triangle (triangle IDs).
std::vector<LuxEngine::uint32> CoplanarFaces(const ModelingMesh& mesh, LuxEngine::uint32 triangle);

// All mutations are transactional. Callers expand seam selections with CoincidentVertices.
bool TransformModelingVertices(ModelingMesh& mesh, std::span<const LuxEngine::uint32> vertexIds,
                               const LuxEngine::Mat4& localDelta, std::string& error);

bool ExtrudeModelingFaces(ModelingMesh& mesh, std::span<const LuxEngine::uint32> triangles,
                          float distance, std::string& error);

bool MergeModelingMeshes(std::span<const ModelingMeshSource> sources, ModelingMesh& result,
                         std::string& error);

bool RelativeModelingTransform(const LuxEngine::Mat4& targetWorld,
                               const LuxEngine::Mat4& sourceWorld, LuxEngine::Mat4& result,
                               std::string& error);

// Saves immutable, content-addressed OBJ under Assets/Models/Generated. Existing files are verified
// and reused, never overwritten. Returns the absolute UTF-8 project-owned asset path.
bool SaveModelingMesh(const ModelingMesh& mesh, const std::filesystem::path& projectRoot,
                      const std::string& label, std::string& path, std::string& error);

} // namespace LuxEditor
