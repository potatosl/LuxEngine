#include <LuxEditor/Modeling/MeshModeling.hpp>
#include <LuxEngine/Assets/AssetManager.hpp>
#include <LuxEngine/Assets/ModelAsset.hpp>
#include <LuxEngine/Scene/Entity.hpp>
#include <LuxEngine/Scene/Scene.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

namespace {
using namespace LuxEngine;
using namespace LuxEditor;
namespace fs = std::filesystem;

void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

bool Near(float left, float right, float tolerance = 1e-5f) {
    return std::abs(left - right) <= tolerance;
}

std::string Bytes(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), {});
}

std::vector<uint32> Fingerprint(const ModelingMesh& mesh) {
    auto words = mesh.indices;
    words.push_back(mesh.hasTexCoords ? 1 : 0);
    for (const auto& vertex : mesh.vertices)
        for (const auto value :
             {vertex.position.x, vertex.position.y, vertex.position.z, vertex.normal.x,
              vertex.normal.y, vertex.normal.z, vertex.texCoords.x, vertex.texCoords.y})
            words.push_back(std::bit_cast<uint32>(value));
    return words;
}

void CheckNormals(const ModelingMesh& mesh) {
    for (const auto& vertex : mesh.vertices)
        Check(Near(vertex.normal.Dot(vertex.normal), 1, 1e-4f),
              "Modeled vertex normal is not finite and normalized");
    for (size_t index = 0; index < mesh.indices.size(); index += 3) {
        const auto& a = mesh.vertices[mesh.indices[index]];
        const auto& b = mesh.vertices[mesh.indices[index + 1]];
        const auto& c = mesh.vertices[mesh.indices[index + 2]];
        const auto normal = (b.position - a.position).Cross(c.position - a.position);
        Check(normal.Dot(normal) > 1e-12f && normal.Dot(a.normal) > 0,
              "Modeled triangle is degenerate or its winding disagrees with its normal");
    }
}

void CheckClosed(const ModelingMesh& mesh) {
    using Position = std::array<int64, 3>;
    std::map<Position, uint32> positions;
    std::vector<uint32> welded;
    for (const auto& vertex : mesh.vertices) {
        const Position key{std::llround(vertex.position.x * 100000),
                           std::llround(vertex.position.y * 100000),
                           std::llround(vertex.position.z * 100000)};
        const auto [found, inserted] =
            positions.emplace(key, static_cast<uint32>(positions.size()));
        static_cast<void>(inserted);
        welded.push_back(found->second);
    }
    std::map<std::pair<uint32, uint32>, std::pair<uint32, int>> edges;
    for (size_t index = 0; index < mesh.indices.size(); index += 3)
        for (size_t corner = 0; corner < 3; ++corner) {
            const auto a = welded[mesh.indices[index + corner]];
            const auto b = welded[mesh.indices[index + (corner + 1) % 3]];
            auto& edge = edges[std::minmax(a, b)];
            ++edge.first;
            edge.second += a < b ? 1 : -1;
        }
    for (const auto& [key, count] : edges) {
        static_cast<void>(key);
        Check(count.first == 2 && count.second == 0,
              "Extruded closed cube has an open, duplicated or inconsistently oriented edge");
    }
}

ModelingMesh BuiltinCube() {
    Scene scene;
    auto entity = scene.CreateEntity("Cube");
    entity.AddComponent<MeshRendererComponent>();
    std::string before, after, error;
    Check(scene.SerializeToString(before), "Could not snapshot source scene");
    ModelingMesh mesh;
    Check(LoadModelingMesh(scene, entity, mesh, error), error.c_str());
    Check(mesh.vertices.size() == 36 && mesh.indices.size() == 36 && mesh.hasTexCoords,
          "Empty asset path did not load the editable hard-edged unit cube");
    Check(scene.SerializeToString(after) && before == after,
          "Loading an editable mesh changed its source scene");
    CheckNormals(mesh);
    CheckClosed(mesh);
    return mesh;
}

void SelectionAndTransform() {
    auto mesh = BuiltinCube();
    const auto seams = CoincidentVertices(mesh, 0);
    Check(seams.size() == 6, "Cube corner seams were not selected together");
    const auto plane = CoplanarFaces(mesh, 0);
    Check(plane == std::vector<uint32>({0, 1}), "Cube quad face did not select its two triangles");
    const auto old = mesh;
    std::string error;
    Check(TransformModelingVertices(mesh, seams, Mat4::Translate({.125f, .25f, -.125f}), error),
          error.c_str());
    for (const auto index : seams) {
        const auto& position = mesh.vertices[index].position;
        Check(Near(position.x, old.vertices[index].position.x + .125f) &&
                  Near(position.y, old.vertices[index].position.y + .25f) &&
                  Near(position.z, old.vertices[index].position.z - .125f) &&
                  mesh.vertices[index].texCoords.x == old.vertices[index].texCoords.x &&
                  mesh.vertices[index].texCoords.y == old.vertices[index].texCoords.y,
              "Moving a cube corner tore a UV seam or changed UV coordinates");
    }
    for (const auto& vertex : mesh.vertices)
        Check(Near(vertex.normal.Dot(vertex.normal), 1, 1e-4f), "Edited normal is not normalized");
    const auto snapshot = Fingerprint(mesh);
    const std::array<uint32, 2> invalid{0, 1000000};
    Check(!TransformModelingVertices(mesh, invalid, Mat4::Translate({1, 0, 0}), error) &&
              Fingerprint(mesh) == snapshot && !error.empty(),
          "Invalid vertex selection partially mutated the modeling mesh");
    std::vector<uint32> all;
    for (uint32 index = 0; index < mesh.vertices.size(); ++index)
        all.push_back(index);
    Check(!TransformModelingVertices(mesh, all, Mat4::Scale({0, 0, 0}), error) &&
              Fingerprint(mesh) == snapshot,
          "Degenerate vertex scaling was committed");
    Check(CoincidentVertices(mesh, 1000000).empty() && CoplanarFaces(mesh, 1000000).empty(),
          "Invalid selection indices were accepted");
}

void Extrusion() {
    auto mesh = BuiltinCube();
    const auto face = CoplanarFaces(mesh, 0);
    std::string error;
    Check(ExtrudeModelingFaces(mesh, face, .75f, error), error.c_str());
    Check(mesh.indices.size() == 60, "Quad extrusion did not add exactly four side quads");
    for (const auto triangle : face)
        for (uint32 corner = 0; corner < 3; ++corner)
            Check(Near(mesh.vertices[mesh.indices[triangle * 3 + corner]].position.z, -1.25f),
                  "Extruded cap did not retain the selected triangle IDs");
    Check(CoplanarFaces(mesh, face[0]) == face,
          "Extruded cap cannot be selected for another extrusion");
    CheckNormals(mesh);
    CheckClosed(mesh);
    Check(ExtrudeModelingFaces(mesh, face, .25f, error), error.c_str());
    CheckClosed(mesh);
    const auto snapshot = Fingerprint(mesh);
    const std::array<uint32, 2> disconnected{0, 2};
    Check(!ExtrudeModelingFaces(mesh, disconnected, 1, error) && Fingerprint(mesh) == snapshot,
          "Non-planar/disconnected extrusion partially mutated the mesh");
    Check(!ExtrudeModelingFaces(mesh, face, std::numeric_limits<float>::quiet_NaN(), error) &&
              Fingerprint(mesh) == snapshot,
          "Non-finite extrusion distance was accepted");
    ModelingMesh quad;
    quad.hasTexCoords = true;
    quad.vertices = {{{0, 0, 0}, {0, 0, 1}, {0, 1}},
                     {{1, 0, 0}, {0, 0, 1}, {1, 1}},
                     {{1, 1, 0}, {0, 0, 1}, {1, 0}},
                     {{0, 1, 0}, {0, 0, 1}, {0, 0}}};
    quad.indices = {0, 1, 2, 0, 2, 3};
    for (auto& vertex : quad.vertices)
        vertex.texCoords = {.125f, .875f};
    Check(ExtrudeModelingFaces(quad, std::array<uint32, 2>{0, 1}, 1, error), error.c_str());
    Check(quad.indices.size() == 30, "Open quad extrusion lost cap or boundary sides");
    CheckNormals(quad);
    for (const auto& vertex : quad.vertices)
        Check(vertex.texCoords.x == .125f && vertex.texCoords.y == .875f,
              "Extruding a palette-mapped face changed its cap or side atlas color");
    std::set<uint32> used(quad.indices.begin(), quad.indices.end());
    Check(used.size() == quad.vertices.size(),
          "Extrusion left ghost vertices that are not used by any face");
}

void MergeAndRelativeTransform() {
    const auto cube = BuiltinCube();
    const auto a = Mat4::Translate({2, 0, 0}) * Mat4::RotateY(.7f) * Mat4::Scale({2, 1, .5f});
    auto b = Mat4::Translate({-3, 2, 0}) * Mat4::RotateZ(-.4f) * Mat4::Scale({-1, 2, .75f});
    b.elements[4] += .15f;
    const std::array<ModelingMeshSource, 2> sources{{{&cube, a}, {&cube, b}}};
    ModelingMesh merged;
    std::string error;
    Check(MergeModelingMeshes(sources, merged, error), error.c_str());
    Check(merged.vertices.size() == 72 && merged.indices.size() == 72 && merged.hasTexCoords,
          "Merging independent cubes lost vertices, faces or UVs");
    CheckNormals(merged);
    CheckClosed(merged);
    Mat4 relative;
    Check(RelativeModelingTransform(b, a, relative, error), error.c_str());
    const auto reconstructed = b * relative;
    for (size_t index = 0; index < 16; ++index)
        Check(Near(reconstructed.elements[index], a.elements[index], 1e-4f),
              "Relative modeling transform failed for mirrored non-uniform scale/shear");
    const auto before = Fingerprint(merged);
    const std::array<ModelingMeshSource, 1> invalid{{{&cube, Mat4::Scale({0, 1, 1})}}};
    Check(!MergeModelingMeshes(invalid, merged, error) && Fingerprint(merged) == before,
          "Singular source transform partially changed merged output");
    Check(!RelativeModelingTransform(Mat4::Scale({0, 1, 1}), a, relative, error),
          "Singular target transform was accepted");
}

void SaveAndRoundTrip(const fs::path& project) {
    const auto cube = BuiltinCube();
    std::string path = "unchanged", error;
    Check(SaveModelingMesh(cube, project, "Edited Cube", path, error), error.c_str());
    Check(fs::is_regular_file(fs::path(path)) &&
              fs::path(path).parent_path() ==
                  fs::canonical(project) / "Assets" / "Models" / "Generated",
          "Generated modeling asset was not saved inside the project");
    const auto bytes = Bytes(path);
    const auto modified = fs::last_write_time(path);
    std::string same;
    Check(SaveModelingMesh(cube, project, "Edited Cube", same, error) && same == path &&
              Bytes(path) == bytes && fs::last_write_time(path) == modified,
          "Content-addressed model was not reused without rewriting");
    RawMeshData raw;
    Check(AssetManager::LoadMesh(path, raw, true) && raw.hasTexCoords &&
              raw.indices.size() == cube.indices.size(),
          "Generated OBJ could not round-trip through the engine asset loader");
    for (size_t index = 0; index < cube.indices.size(); ++index) {
        const auto& source = cube.vertices[cube.indices[index]];
        const auto target = raw.indices[index];
        Check(Near(source.position.x, raw.vertices[target * 3]) &&
                  Near(source.position.y, raw.vertices[target * 3 + 1]) &&
                  Near(source.position.z, raw.vertices[target * 3 + 2]) &&
                  Near(source.normal.x, raw.normals[target * 3]) &&
                  Near(source.normal.y, raw.normals[target * 3 + 1]) &&
                  Near(source.normal.z, raw.normals[target * 3 + 2]) &&
                  source.texCoords.x == raw.texCoords[target * 2] &&
                  source.texCoords.y == raw.texCoords[target * 2 + 1],
              "OBJ round-trip changed position, normal or the normalized FBX UV origin");
    }
    std::ofstream tamper(path, std::ios::binary | std::ios::trunc);
    tamper << "preserve collaborator data";
    tamper.close();
    same = "preserved output";
    Check(!SaveModelingMesh(cube, project, "Edited Cube", same, error) &&
              same == "preserved output" && Bytes(path) == "preserve collaborator data",
          "Saving a generated model overwrote a conflicting existing file");
    auto invalid = cube;
    invalid.indices[0] = 1000000;
    Check(!SaveModelingMesh(invalid, project, "Invalid", same, error) && same == "preserved output",
          "Invalid model data was published or changed the caller's asset path");
}

void AssetSnapshots(const fs::path& project) {
    const auto repository = fs::path(__FILE__).parent_path().parent_path().parent_path();
    const auto file = repository / "Engine" / "Resources" / "Models" / "Primitives" /
                      "KenneyPrototype" / "shape-cube.fbx";
    const auto source = Bytes(file);
    Check(!source.empty(), "Bundled cube FBX could not be read");
    Scene scene;
    auto entity = scene.CreateEntity("Bundled cube");
    auto& component = entity.AddComponent<MeshRendererComponent>();
    component.meshPath = file.string();
    ModelingMesh mesh;
    std::string error, saved;
    Check(LoadModelingMesh(scene, entity, mesh, error) &&
              SaveModelingMesh(mesh, project, "FBX Cube", saved, error),
          error.c_str());
    Check(Bytes(file) == source && component.meshPath == file.string(),
          "Modeling modified the bundled source FBX or authored source entity");
    RawMeshData roundTrip;
    Check(AssetManager::LoadMesh(saved, roundTrip, true) && roundTrip.hasTexCoords,
          "Bundled FBX modeling snapshot lost texture coordinates");
    for (size_t index = 0; index < mesh.indices.size(); ++index) {
        const auto& uv = mesh.vertices[mesh.indices[index]].texCoords;
        const auto target = roundTrip.indices[index];
        Check(uv.x == roundTrip.texCoords[target * 2] &&
                  uv.y == roundTrip.texCoords[target * 2 + 1],
              "FBX-to-OBJ modeling snapshot flipped or rounded its normalized UVs");
    }
    const auto rig = repository / "Engine" / "Tests" / "Fixtures" / "AnimatedTriangle.fbx";
    const auto rigBytes = Bytes(rig);
    component.meshPath = rig.string();
    component.modelSubmesh = 0;
    auto& animation = entity.AddComponent<ModelAnimationComponent>();
    animation.modelPath = component.meshPath;
    animation.clip = 0;
    animation.time = .5;
    Check(LoadModelingMesh(scene, entity, mesh, error), error.c_str());
    const auto model = ModelAsset::LoadFbx(component.meshPath, error);
    ModelPose pose;
    Check(model && model->Evaluate(.5, 0, pose, error) &&
              mesh.vertices.size() == pose.submeshes[0].size(),
          error.c_str());
    for (size_t index = 0; index < mesh.vertices.size(); ++index)
        Check(Near(mesh.vertices[index].position.x, pose.submeshes[0][index].position.x),
              "Structured FBX modeling snapshot did not bake the current CPU-skinned pose");
    Check(Bytes(rig) == rigBytes && Near(static_cast<float>(animation.time), .5f),
          "Baking a modeling snapshot modified the source rig or playback state");
}

} // namespace

int main() {
    const auto project =
        fs::temp_directory_path() /
        ("lux-modeling-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        fs::create_directories(project);
        SelectionAndTransform();
        Extrusion();
        MergeAndRelativeTransform();
        SaveAndRoundTrip(project);
        AssetSnapshots(project);
        fs::remove_all(project);
        AssetManager::Clear();
        std::cout << "Mesh modeling selection/extrusion/merge/immutable OBJ checks passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::error_code ignored;
        fs::remove_all(project, ignored);
        AssetManager::Clear();
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
