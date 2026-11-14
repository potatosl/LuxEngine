#include <LuxEditor/Modeling/MeshModeling.hpp>
#include <LuxEngine/Assets/AssetManager.hpp>
#include <LuxEngine/Assets/ModelAsset.hpp>
#include <LuxEngine/Scene/Entity.hpp>
#include <LuxEngine/Scene/Scene.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace LuxEditor {
namespace {
using namespace LuxEngine;
namespace fs = std::filesystem;

constexpr size_t MaxVertices = 1'000'000;
constexpr size_t MaxIndices = 3'000'000;
constexpr size_t MaxFileBytes = 64 * 1024 * 1024;
constexpr double WeldTolerance = 1e-6;
constexpr double PlaneTolerance = 1e-5;

struct Vector {
    double x = 0, y = 0, z = 0;
};

Vector Point(Vec3 value) {
    return {value.x, value.y, value.z};
}

Vector Subtract(Vector left, Vector right) {
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

Vector Add(Vector left, Vector right) {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

Vector Scale(Vector value, double scale) {
    return {value.x * scale, value.y * scale, value.z * scale};
}

double Dot(Vector left, Vector right) {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

Vector Cross(Vector left, Vector right) {
    return {left.y * right.z - left.z * right.y, left.z * right.x - left.x * right.z,
            left.x * right.y - left.y * right.x};
}

void Require(bool value, const char* error) {
    if (!value)
        throw std::runtime_error(error);
}

Vector Normalize(Vector value) {
    const auto length = std::sqrt(Dot(value, value));
    Require(std::isfinite(length) && length > 1e-20,
            "면이 겹치거나 퇴화하여 법선을 계산할 수 없습니다.");
    return Scale(value, 1.0 / length);
}

Vec3 FloatVector(Vector value) {
    Require(std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) &&
                std::abs(value.x) <= 1e9 && std::abs(value.y) <= 1e9 && std::abs(value.z) <= 1e9,
            "모델 정점이 유효한 좌표 범위를 벗어났습니다.");
    return {static_cast<float>(value.x), static_cast<float>(value.y), static_cast<float>(value.z)};
}

Vector FaceNormal(const ModelingMesh& mesh, uint32 triangle) {
    const auto offset = static_cast<size_t>(triangle) * 3;
    const auto a = Point(mesh.vertices.at(mesh.indices.at(offset)).position);
    const auto b = Point(mesh.vertices.at(mesh.indices.at(offset + 1)).position);
    const auto c = Point(mesh.vertices.at(mesh.indices.at(offset + 2)).position);
    return Cross(Subtract(b, a), Subtract(c, a));
}

void Validate(const ModelingMesh& mesh) {
    Require(!mesh.vertices.empty() && mesh.vertices.size() <= MaxVertices &&
                !mesh.indices.empty() && mesh.indices.size() <= MaxIndices &&
                mesh.indices.size() % 3 == 0,
            "모델은 1,000,000개 이하의 정점과 3,000,000개 이하의 삼각형 인덱스가 필요합니다.");
    for (const auto& vertex : mesh.vertices) {
        FloatVector(Point(vertex.position));
        const auto normal = Point(vertex.normal);
        const auto length = std::sqrt(Dot(normal, normal));
        Require(std::isfinite(length) && std::abs(length - 1.0) <= 1e-3,
                "모델의 법선은 정규화된 벡터여야 합니다.");
        Require(std::isfinite(vertex.texCoords.x) && std::isfinite(vertex.texCoords.y),
                "모델의 UV 좌표가 올바르지 않습니다.");
    }
    for (const auto index : mesh.indices)
        Require(index < mesh.vertices.size(), "모델의 정점 인덱스가 올바르지 않습니다.");
    for (uint32 triangle = 0; triangle < mesh.indices.size() / 3; ++triangle)
        Normalize(FaceNormal(mesh, triangle));
}

double Determinant(const Mat4& matrix) {
    const auto* m = matrix.elements;
    return double(m[0]) * (double(m[5]) * m[10] - double(m[9]) * m[6]) -
           double(m[4]) * (double(m[1]) * m[10] - double(m[9]) * m[2]) +
           double(m[8]) * (double(m[1]) * m[6] - double(m[5]) * m[2]);
}

void Affine(const Mat4& matrix) {
    for (const auto value : matrix.elements)
        Require(std::isfinite(value), "모델 변환 행렬에 유효하지 않은 값이 있습니다.");
    Require(std::abs(matrix.elements[3]) < 1e-7 && std::abs(matrix.elements[7]) < 1e-7 &&
                std::abs(matrix.elements[11]) < 1e-7 && std::abs(matrix.elements[15] - 1.0f) < 1e-7,
            "모델 편집에는 아핀 변환 행렬이 필요합니다.");
}

Vec3 TransformPoint(const Mat4& matrix, Vec3 value) {
    const auto* m = matrix.elements;
    return FloatVector(
        {double(m[0]) * value.x + double(m[4]) * value.y + double(m[8]) * value.z + m[12],
         double(m[1]) * value.x + double(m[5]) * value.y + double(m[9]) * value.z + m[13],
         double(m[2]) * value.x + double(m[6]) * value.y + double(m[10]) * value.z + m[14]});
}

Vec3 TransformNormal(const Mat4& matrix, Vec3 value) {
    const auto* m = matrix.elements;
    const double determinant = Determinant(matrix);
    Require(std::isfinite(determinant) && std::abs(determinant) > 1e-20,
            "크기가 0인 모델은 합칠 수 없습니다.");
    const Vector x{m[0], m[1], m[2]}, y{m[4], m[5], m[6]}, z{m[8], m[9], m[10]};
    // Cofactor columns are the inverse transpose multiplied by the determinant.
    const auto normal = Add(
        Add(Scale(Cross(y, z), value.x / determinant), Scale(Cross(z, x), value.y / determinant)),
        Scale(Cross(x, y), value.z / determinant));
    return FloatVector(Normalize(normal));
}

using Cell = std::tuple<int64, int64, int64>;

Cell PositionCell(Vec3 value) {
    return {static_cast<int64>(std::floor(double(value.x) / WeldTolerance)),
            static_cast<int64>(std::floor(double(value.y) / WeldTolerance)),
            static_cast<int64>(std::floor(double(value.z) / WeldTolerance))};
}

std::vector<uint32> WeldedVertices(const ModelingMesh& mesh) {
    std::map<Cell, std::vector<uint32>> cells;
    std::vector<uint32> groups(mesh.vertices.size());
    for (uint32 index = 0; index < mesh.vertices.size(); ++index) {
        const auto position = mesh.vertices[index].position;
        const auto [x, y, z] = PositionCell(position);
        uint32 found = index;
        for (int dx = -1; dx <= 1 && found == index; ++dx)
            for (int dy = -1; dy <= 1 && found == index; ++dy)
                for (int dz = -1; dz <= 1 && found == index; ++dz) {
                    const auto cell = cells.find({x + dx, y + dy, z + dz});
                    if (cell == cells.end())
                        continue;
                    for (const auto candidate : cell->second) {
                        const auto difference =
                            Subtract(Point(position), Point(mesh.vertices[candidate].position));
                        if (Dot(difference, difference) <= WeldTolerance * WeldTolerance) {
                            found = candidate;
                            break;
                        }
                    }
                }
        groups[index] = found;
        if (found == index)
            cells[{x, y, z}].push_back(index);
    }
    return groups;
}

uint64 Edge(uint32 a, uint32 b) {
    return (uint64(std::min(a, b)) << 32) | std::max(a, b);
}

using EdgeFaces = std::unordered_map<uint64, std::vector<uint32>>;

EdgeFaces Adjacency(const ModelingMesh& mesh, const std::vector<uint32>& welded) {
    EdgeFaces edges;
    for (uint32 triangle = 0; triangle < mesh.indices.size() / 3; ++triangle)
        for (uint32 corner = 0; corner < 3; ++corner) {
            const auto a = welded[mesh.indices[triangle * 3 + corner]];
            const auto b = welded[mesh.indices[triangle * 3 + (corner + 1) % 3]];
            Require(a != b, "면의 모서리가 너무 짧아 편집할 수 없습니다.");
            auto& adjacent = edges[Edge(a, b)];
            Require(adjacent.size() < 64, "하나의 모서리에 너무 많은 면이 연결되어 있습니다.");
            adjacent.push_back(triangle);
        }
    return edges;
}

bool SamePlane(const ModelingMesh& mesh, uint32 triangle, Vector normal, Vector origin) {
    if (Dot(Normalize(FaceNormal(mesh, triangle)), normal) < 1.0 - 1e-5)
        return false;
    for (uint32 corner = 0; corner < 3; ++corner) {
        const auto position = Point(mesh.vertices[mesh.indices[triangle * 3 + corner]].position);
        if (std::abs(Dot(Subtract(position, origin), normal)) > PlaneTolerance)
            return false;
    }
    return true;
}

void RecalculateNormals(ModelingMesh& mesh) {
    const auto welded = WeldedVertices(mesh);
    // UV seams sharing an authored normal stay smooth; different authored normals stay hard.
    using NormalKey = std::tuple<uint32, int64, int64, int64>;
    std::map<NormalKey, Vector> normals;
    std::vector<NormalKey> keys;
    keys.reserve(mesh.vertices.size());
    for (uint32 index = 0; index < mesh.vertices.size(); ++index) {
        const auto normal = Normalize(Point(mesh.vertices[index].normal));
        keys.emplace_back(welded[index], static_cast<int64>(std::llround(normal.x * 100000)),
                          static_cast<int64>(std::llround(normal.y * 100000)),
                          static_cast<int64>(std::llround(normal.z * 100000)));
    }
    for (uint32 triangle = 0; triangle < mesh.indices.size() / 3; ++triangle) {
        const auto normal = FaceNormal(mesh, triangle);
        Normalize(normal);
        for (uint32 corner = 0; corner < 3; ++corner) {
            auto& sum = normals[keys[mesh.indices[triangle * 3 + corner]]];
            sum = Add(sum, normal);
        }
    }
    for (uint32 index = 0; index < mesh.vertices.size(); ++index)
        if (const auto found = normals.find(keys[index]); found != normals.end())
            mesh.vertices[index].normal = FloatVector(Normalize(found->second));
}

void RemoveUnusedVertices(ModelingMesh& mesh) {
    std::vector<bool> used(mesh.vertices.size());
    for (const auto index : mesh.indices)
        used[index] = true;
    std::vector<uint32> remap(mesh.vertices.size());
    std::vector<Vertex3D> vertices;
    vertices.reserve(mesh.vertices.size());
    for (uint32 index = 0; index < mesh.vertices.size(); ++index)
        if (used[index]) {
            remap[index] = static_cast<uint32>(vertices.size());
            vertices.push_back(mesh.vertices[index]);
        }
    for (auto& index : mesh.indices)
        index = remap[index];
    mesh.vertices = std::move(vertices);
}

ModelingMesh Cube() {
    constexpr std::array<std::array<float, 3>, 8> corners{{{{-.5f, -.5f, -.5f}},
                                                           {{.5f, -.5f, -.5f}},
                                                           {{.5f, .5f, -.5f}},
                                                           {{-.5f, .5f, -.5f}},
                                                           {{-.5f, -.5f, .5f}},
                                                           {{.5f, -.5f, .5f}},
                                                           {{.5f, .5f, .5f}},
                                                           {{-.5f, .5f, .5f}}}};
    constexpr std::array<uint32, 36> indices{{0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7,
                                              0, 4, 7, 0, 7, 3, 1, 2, 6, 1, 6, 5,
                                              0, 1, 5, 0, 5, 4, 3, 7, 6, 3, 6, 2}};
    const std::array<Vec2, 6> uvs{{{0, 1}, {1, 0}, {1, 1}, {0, 1}, {0, 0}, {1, 0}}};
    ModelingMesh mesh;
    mesh.hasTexCoords = true;
    for (size_t face = 0; face < 6; ++face) {
        const auto first = static_cast<uint32>(mesh.vertices.size());
        for (size_t corner = 0; corner < 6; ++corner) {
            const auto& point = corners[indices[face * 6 + corner]];
            mesh.indices.push_back(static_cast<uint32>(mesh.vertices.size()));
            mesh.vertices.push_back({{point[0], point[1], point[2]}, {0, 1, 0}, uvs[corner]});
        }
        const auto normal = FloatVector(Normalize(FaceNormal(mesh, first / 3)));
        for (uint32 corner = 0; corner < 6; ++corner)
            mesh.vertices[first + corner].normal = normal;
    }
    return mesh;
}

std::string Utf8(const fs::path& path) {
    const auto bytes = path.generic_u8string();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

fs::path Path(const std::string& value) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(value.data()), value.size()));
}

bool Inside(const fs::path& path, const fs::path& root) {
    const auto relative = fs::weakly_canonical(path).lexically_relative(root);
    return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
}

std::string Obj(const ModelingMesh& mesh) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "# LuxEditor generated immutable mesh\no LuxGeneratedMesh\n";
    out << std::setprecision(std::numeric_limits<float>::max_digits10);
    const auto bounded = [&] {
        Require(static_cast<size_t>(out.tellp()) <= MaxFileBytes,
                "생성한 OBJ 파일이 64 MiB를 초과했습니다.");
    };
    for (const auto& vertex : mesh.vertices) {
        out << "v " << vertex.position.x << ' ' << vertex.position.y << ' ' << vertex.position.z
            << '\n';
        bounded();
    }
    if (mesh.hasTexCoords)
        for (const auto& vertex : mesh.vertices) {
            out << "vt " << vertex.texCoords.x << ' ' << vertex.texCoords.y << '\n';
            bounded();
        }
    for (const auto& vertex : mesh.vertices) {
        out << "vn " << vertex.normal.x << ' ' << vertex.normal.y << ' ' << vertex.normal.z << '\n';
        bounded();
    }
    out << "s off\n";
    for (size_t index = 0; index < mesh.indices.size(); index += 3) {
        out << 'f';
        for (size_t corner = 0; corner < 3; ++corner) {
            const auto vertex = mesh.indices[index + corner] + 1;
            out << ' ' << vertex << '/';
            if (mesh.hasTexCoords)
                out << vertex;
            out << '/' << vertex;
        }
        out << '\n';
        bounded();
    }
    Require(static_cast<bool>(out), "OBJ 데이터를 생성하지 못했습니다.");
    return out.str();
}

std::string ContentName(const std::string& bytes, const std::string& label) {
    uint64 hash = 14695981039346656037ull;
    for (const auto byte : bytes) {
        hash ^= static_cast<unsigned char>(byte);
        hash *= 1099511628211ull;
    }
    std::string safe;
    for (const unsigned char character : label) {
        if (safe.size() >= 40)
            break;
        if (std::isalnum(character) && character < 128)
            safe += static_cast<char>(character);
        else if ((character == '-' || character == '_') && !safe.empty())
            safe += static_cast<char>(character);
    }
    if (safe.empty())
        safe = "Model";
    std::ostringstream name;
    name << safe << '_' << std::hex << std::setw(16) << std::setfill('0') << hash << ".obj";
    return name.str();
}

bool SameFile(const fs::path& path, const std::string& bytes) {
    if (fs::is_symlink(fs::symlink_status(path)) || !fs::is_regular_file(path) ||
        fs::file_size(path) != bytes.size())
        return false;
    std::ifstream input(path, std::ios::binary);
    std::array<char, 8192> buffer;
    size_t offset = 0;
    while (offset < bytes.size()) {
        const auto count = std::min(buffer.size(), bytes.size() - offset);
        if (!input.read(buffer.data(), static_cast<std::streamsize>(count)) ||
            !std::equal(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(count),
                        bytes.begin() + static_cast<std::ptrdiff_t>(offset)))
            return false;
        offset += count;
    }
    return input.peek() == std::char_traits<char>::eof();
}

} // namespace

bool LoadModelingMesh(Scene& scene, Entity entity, ModelingMesh& result, std::string& error) {
    error.clear();
    try {
        Require(entity.IsValid() && entity.GetScene() == &scene,
                "편집할 엔티티가 현재 씬에 없습니다.");
        const auto* component = entity.TryGetComponent<MeshRendererComponent>();
        Require(component != nullptr, "선택한 오브젝트에 3D 메시가 없습니다.");
        ModelingMesh mesh;
        if (component->meshPath.empty()) {
            mesh = Cube();
        } else {
            const auto path = scene.ResolveAssetPath(component->meshPath);
            auto extension = Path(path).extension().string();
            std::transform(
                extension.begin(), extension.end(), extension.begin(),
                [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
            Require(extension == ".fbx" || extension == ".obj",
                    "모델링에는 FBX 또는 OBJ 메시가 필요합니다.");
            const auto* animation = entity.TryGetComponent<ModelAnimationComponent>();
            RawMeshData raw;
            if (component->modelSubmesh < 0 &&
                (extension != ".fbx" || !animation || animation->clip < 0) &&
                AssetManager::LoadMesh(path, raw, true)) {
                Require(raw.vertices.size() % 3 == 0 && raw.normals.size() == raw.vertices.size() &&
                            raw.texCoords.size() == raw.vertices.size() / 3 * 2,
                        "모델의 정점 속성이 올바르지 않습니다.");
                mesh.hasTexCoords = raw.hasTexCoords;
                mesh.indices = std::move(raw.indices);
                for (size_t index = 0; index < raw.vertices.size() / 3; ++index)
                    mesh.vertices.push_back(
                        {{raw.vertices[index * 3], raw.vertices[index * 3 + 1],
                          raw.vertices[index * 3 + 2]},
                         {raw.normals[index * 3], raw.normals[index * 3 + 1],
                          raw.normals[index * 3 + 2]},
                         {raw.texCoords[index * 2], raw.texCoords[index * 2 + 1]}});
            } else {
                Require(extension == ".fbx", "OBJ 메시를 읽을 수 없습니다.");
                const auto model = ModelAsset::LoadFbx(path, error);
                Require(model != nullptr, "FBX 모델을 읽을 수 없습니다.");
                ModelPose pose;
                Require(model->Evaluate(animation ? animation->time : 0.0,
                                        animation ? animation->clip : -1, pose, error),
                        "FBX 모델의 현재 포즈를 평가할 수 없습니다.");
                const auto part = [&](size_t index) {
                    const auto& source = model->submeshes.at(index);
                    ModelingMesh target;
                    target.hasTexCoords = source.hasTexCoords;
                    target.indices = source.indices;
                    for (const auto& vertex : pose.submeshes.at(index))
                        target.vertices.push_back(
                            {vertex.position, vertex.normal, vertex.texCoords});
                    return target;
                };
                if (component->modelSubmesh >= 0) {
                    Require(static_cast<size_t>(component->modelSubmesh) < model->submeshes.size(),
                            "모델의 서브메시 선택이 올바르지 않습니다.");
                    mesh = part(static_cast<size_t>(component->modelSubmesh));
                } else {
                    std::vector<ModelingMesh> parts;
                    std::vector<ModelingMeshSource> sources;
                    parts.reserve(model->submeshes.size());
                    sources.reserve(model->submeshes.size());
                    for (size_t index = 0; index < model->submeshes.size(); ++index) {
                        parts.push_back(part(index));
                        sources.push_back({&parts.back(), pose.nodeWorldTransforms.at(
                                                              model->submeshes[index].node)});
                    }
                    Require(MergeModelingMeshes(sources, mesh, error),
                            "FBX 모델의 메시를 합칠 수 없습니다.");
                }
            }
        }
        Validate(mesh);
        result = std::move(mesh);
        return true;
    } catch (const std::exception& exception) {
        if (error.empty())
            error = exception.what();
        return false;
    }
}

std::vector<uint32> CoincidentVertices(const ModelingMesh& mesh, uint32 vertex) {
    std::vector<uint32> result;
    if (vertex >= mesh.vertices.size() || mesh.vertices.size() > MaxVertices)
        return result;
    const auto origin = Point(mesh.vertices[vertex].position);
    if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z))
        return result;
    for (uint32 index = 0; index < mesh.vertices.size(); ++index) {
        const auto delta = Subtract(Point(mesh.vertices[index].position), origin);
        if (Dot(delta, delta) <= WeldTolerance * WeldTolerance)
            result.push_back(index);
    }
    return result;
}

std::vector<uint32> CoplanarFaces(const ModelingMesh& mesh, uint32 triangle) {
    try {
        Validate(mesh);
        Require(triangle < mesh.indices.size() / 3, "선택한 면이 모델에 없습니다.");
        const auto welded = WeldedVertices(mesh);
        const auto edges = Adjacency(mesh, welded);
        const auto normal = Normalize(FaceNormal(mesh, triangle));
        const auto origin = Point(mesh.vertices[mesh.indices[triangle * 3]].position);
        std::vector<uint32> pending{triangle}, result;
        std::unordered_set<uint32> visited;
        while (!pending.empty()) {
            const auto current = pending.back();
            pending.pop_back();
            if (!visited.insert(current).second || !SamePlane(mesh, current, normal, origin))
                continue;
            result.push_back(current);
            for (uint32 corner = 0; corner < 3; ++corner) {
                const auto a = welded[mesh.indices[current * 3 + corner]];
                const auto b = welded[mesh.indices[current * 3 + (corner + 1) % 3]];
                for (const auto adjacent : edges.at(Edge(a, b)))
                    if (!visited.contains(adjacent))
                        pending.push_back(adjacent);
            }
        }
        std::sort(result.begin(), result.end());
        return result;
    } catch (...) {
        return {};
    }
}

bool TransformModelingVertices(ModelingMesh& mesh, std::span<const uint32> vertexIds,
                               const Mat4& localDelta, std::string& error) {
    error.clear();
    try {
        Validate(mesh);
        Affine(localDelta);
        Require(!vertexIds.empty() && vertexIds.size() <= MaxVertices,
                "이동할 정점을 선택하십시오.");
        std::unordered_set<uint32> selected;
        for (const auto index : vertexIds) {
            Require(index < mesh.vertices.size(), "선택한 정점이 모델에 없습니다.");
            selected.insert(index);
        }
        auto result = mesh;
        for (const auto index : selected)
            result.vertices[index].position =
                TransformPoint(localDelta, result.vertices[index].position);
        if (selected.size() == result.vertices.size() && Determinant(localDelta) < 0)
            for (size_t index = 0; index < result.indices.size(); index += 3)
                std::swap(result.indices[index + 1], result.indices[index + 2]);
        RecalculateNormals(result);
        Validate(result);
        mesh = std::move(result);
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

bool ExtrudeModelingFaces(ModelingMesh& mesh, std::span<const uint32> triangles, float distance,
                          std::string& error) {
    error.clear();
    try {
        Validate(mesh);
        Require(!triangles.empty() && triangles.size() <= MaxIndices / 3,
                "돌출할 면을 선택하십시오.");
        Require(std::isfinite(distance) && std::abs(distance) >= 1e-5f &&
                    std::abs(distance) <= 1e6f,
                "돌출 거리는 0이 아닌 유효한 값이어야 합니다.");
        std::set<uint32> selected(triangles.begin(), triangles.end());
        for (const auto triangle : selected)
            Require(triangle < mesh.indices.size() / 3, "선택한 면이 모델에 없습니다.");
        const auto welded = WeldedVertices(mesh);
        const auto edges = Adjacency(mesh, welded);
        const auto seed = *selected.begin();
        const auto normal = Normalize(FaceNormal(mesh, seed));
        const auto origin = Point(mesh.vertices[mesh.indices[seed * 3]].position);
        std::vector<uint32> pending{seed};
        std::set<uint32> connected;
        while (!pending.empty()) {
            const auto triangle = pending.back();
            pending.pop_back();
            if (!connected.insert(triangle).second)
                continue;
            Require(SamePlane(mesh, triangle, normal, origin),
                    "돌출할 면은 같은 평면에서 연결되어 있어야 합니다.");
            for (uint32 corner = 0; corner < 3; ++corner) {
                const auto a = welded[mesh.indices[triangle * 3 + corner]];
                const auto b = welded[mesh.indices[triangle * 3 + (corner + 1) % 3]];
                const auto& adjacent = edges.at(Edge(a, b));
                Require(adjacent.size() <= 2, "비다양체 모서리가 있는 모델은 돌출할 수 없습니다.");
                for (const auto neighbor : adjacent)
                    if (selected.contains(neighbor) && !connected.contains(neighbor))
                        pending.push_back(neighbor);
            }
        }
        Require(connected == selected, "분리된 면은 함께 돌출할 수 없습니다.");

        struct Boundary {
            uint32 a, b;
        };

        std::vector<Boundary> boundary;
        std::set<uint32> capVertices;
        std::map<uint32, std::pair<uint32, uint32>> degrees;
        for (const auto triangle : selected)
            for (uint32 corner = 0; corner < 3; ++corner) {
                const auto a = mesh.indices[triangle * 3 + corner];
                const auto b = mesh.indices[triangle * 3 + (corner + 1) % 3];
                capVertices.insert(a);
                const auto& adjacent = edges.at(Edge(welded[a], welded[b]));
                const auto count =
                    std::count_if(adjacent.begin(), adjacent.end(),
                                  [&selected](uint32 face) { return selected.contains(face); });
                if (count == 1) {
                    boundary.push_back({a, b});
                    ++degrees[welded[a]].first;
                    ++degrees[welded[b]].second;
                }
            }
        Require(!boundary.empty(), "돌출할 면에 경계 모서리가 없습니다.");
        for (const auto& [vertex, degree] : degrees) {
            static_cast<void>(vertex);
            Require(degree.first == 1 && degree.second == 1,
                    "선택한 면의 경계가 서로 겹치거나 연결 방향이 올바르지 않습니다.");
        }
        Require(capVertices.size() <= MaxVertices - mesh.vertices.size() &&
                    boundary.size() <=
                        (MaxVertices - mesh.vertices.size() - capVertices.size()) / 4 &&
                    boundary.size() <= (MaxIndices - mesh.indices.size()) / 6,
                "돌출 결과가 모델의 정점·인덱스 한도를 초과합니다.");
        auto result = mesh;
        const auto offset = Scale(normal, distance);
        std::unordered_map<uint32, uint32> cap;
        for (const auto source : capVertices) {
            auto vertex = mesh.vertices[source];
            vertex.position = FloatVector(Add(Point(vertex.position), offset));
            cap.emplace(source, static_cast<uint32>(result.vertices.size()));
            result.vertices.push_back(vertex);
        }
        for (const auto triangle : selected)
            for (uint32 corner = 0; corner < 3; ++corner)
                result.indices[triangle * 3 + corner] = cap.at(mesh.indices[triangle * 3 + corner]);
        for (const auto& edge : boundary) {
            const auto a = Point(mesh.vertices[edge.a].position);
            const auto b = Point(mesh.vertices[edge.b].position);
            const auto sideNormal = FloatVector(Normalize(Cross(Subtract(b, a), offset)));
            const float length = static_cast<float>(std::sqrt(Dot(Subtract(b, a), Subtract(b, a))));
            Vec2 uvA, uvB, uvOffset;
            if (mesh.hasTexCoords) {
                uvA = mesh.vertices[edge.a].texCoords;
                uvB = mesh.vertices[edge.b].texCoords;
                // Extend the edge's existing UV density onto the side. A palette atlas often
                // uses a single UV for an entire face: its side must sample that same color.
                const float scale = std::abs(distance) / length;
                uvOffset = {-(uvB.y - uvA.y) * scale, (uvB.x - uvA.x) * scale};
            }
            const auto first = static_cast<uint32>(result.vertices.size());
            result.vertices.push_back({FloatVector(a), sideNormal, uvA});
            result.vertices.push_back({FloatVector(b), sideNormal, uvB});
            result.vertices.push_back({FloatVector(Add(b, offset)), sideNormal, uvB + uvOffset});
            result.vertices.push_back({FloatVector(Add(a, offset)), sideNormal, uvA + uvOffset});
            result.indices.insert(result.indices.end(),
                                  {first, first + 1, first + 2, first, first + 2, first + 3});
        }
        RecalculateNormals(result);
        RemoveUnusedVertices(result);
        Validate(result);
        mesh = std::move(result);
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

bool MergeModelingMeshes(std::span<const ModelingMeshSource> sources, ModelingMesh& result,
                         std::string& error) {
    error.clear();
    try {
        Require(!sources.empty() && sources.size() <= 1024, "합칠 모델을 1개 이상 선택하십시오.");
        ModelingMesh merged;
        merged.hasTexCoords = true;
        for (const auto& source : sources) {
            Require(source.mesh != nullptr, "합칠 모델이 없습니다.");
            Validate(*source.mesh);
            Affine(source.transform);
            Require(source.mesh->vertices.size() <= MaxVertices - merged.vertices.size() &&
                        source.mesh->indices.size() <= MaxIndices - merged.indices.size(),
                    "합친 모델이 정점·인덱스 한도를 초과합니다.");
            const auto offset = static_cast<uint32>(merged.vertices.size());
            const bool mirrored = Determinant(source.transform) < 0;
            merged.hasTexCoords &= source.mesh->hasTexCoords;
            for (const auto& vertex : source.mesh->vertices)
                merged.vertices.push_back({TransformPoint(source.transform, vertex.position),
                                           TransformNormal(source.transform, vertex.normal),
                                           vertex.texCoords});
            for (size_t index = 0; index < source.mesh->indices.size(); index += 3)
                for (size_t corner = 0; corner < 3; ++corner)
                    merged.indices.push_back(
                        offset + source.mesh->indices[index + (mirrored ? 2 - corner : corner)]);
        }
        Validate(merged);
        result = std::move(merged);
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

bool RelativeModelingTransform(const Mat4& targetWorld, const Mat4& sourceWorld, Mat4& result,
                               std::string& error) {
    error.clear();
    try {
        Affine(targetWorld);
        Affine(sourceWorld);
        const double determinant = Determinant(targetWorld);
        Require(std::isfinite(determinant) && std::abs(determinant) > 1e-20,
                "기준 오브젝트의 크기가 0이어서 로컬 좌표를 계산할 수 없습니다.");
        const auto* m = targetWorld.elements;
        Mat4 inverse;
        const std::array<Vector, 3> rows{
            Scale(Cross({m[4], m[5], m[6]}, {m[8], m[9], m[10]}), 1.0 / determinant),
            Scale(Cross({m[8], m[9], m[10]}, {m[0], m[1], m[2]}), 1.0 / determinant),
            Scale(Cross({m[0], m[1], m[2]}, {m[4], m[5], m[6]}), 1.0 / determinant)};
        for (size_t row = 0; row < 3; ++row) {
            inverse.elements[row] = static_cast<float>(rows[row].x);
            inverse.elements[4 + row] = static_cast<float>(rows[row].y);
            inverse.elements[8 + row] = static_cast<float>(rows[row].z);
            inverse.elements[12 + row] = static_cast<float>(-Dot(rows[row], {m[12], m[13], m[14]}));
        }
        const auto relative = inverse * sourceWorld;
        Affine(relative);
        result = relative;
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

bool SaveModelingMesh(const ModelingMesh& mesh, const fs::path& projectRoot,
                      const std::string& label, std::string& path, std::string& error) {
    error.clear();
    fs::path temporary;
    try {
        Validate(mesh);
        const auto rootText = Utf8(projectRoot);
        Require(rootText.size() <= 4096 && rootText.find('\0') == std::string::npos,
                "프로젝트 경로가 올바르지 않습니다.");
        Require(!projectRoot.empty() && fs::is_directory(projectRoot),
                "모델을 저장할 프로젝트 폴더가 없습니다.");
        const auto root = fs::canonical(projectRoot);
        const auto directory = root / "Assets" / "Models" / "Generated";
        Require(Inside(directory, root), "모델 저장 폴더가 프로젝트 외부를 가리킵니다.");
        fs::create_directories(directory);
        Require(Inside(directory, root), "모델 저장 폴더가 프로젝트 외부를 가리킵니다.");
        const auto bytes = Obj(mesh);
        const auto target = directory / ContentName(bytes, label);
        Require(Inside(target, root), "모델 저장 경로가 프로젝트 외부를 가리킵니다.");
        Require(Utf8(target).size() <= 4096, "생성 모델 경로가 너무 깁니다.");
        if (fs::exists(fs::symlink_status(target))) {
            Require(SameFile(target, bytes),
                    "생성 모델의 해시가 충돌했거나 기존 내용이 변경되었습니다.");
            path = Utf8(target);
            return true;
        }
        static std::atomic<uint64> sequence{0};
        for (uint32 attempt = 0; attempt < 16; ++attempt) {
            const auto name =
                ".lux-model-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                std::to_string(sequence.fetch_add(1));
            temporary = directory / name;
            if (fs::create_directory(temporary))
                break;
            temporary.clear();
        }
        Require(!temporary.empty(), "모델 임시 저장 폴더를 생성하지 못했습니다.");
        const auto staged = temporary / "model.obj";
        std::ofstream output(staged, std::ios::binary);
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        output.close();
        Require(static_cast<bool>(output), "모델 파일을 저장하지 못했습니다.");
        // Publishing a hard link is atomic and cannot replace an existing target on any platform.
        std::error_code status;
        fs::create_hard_link(staged, target, status);
        if (status)
            Require(fs::exists(target) && SameFile(target, bytes),
                    "모델 파일을 원자적으로 저장하지 못했습니다. 하드 링크를 지원하는 프로젝트 "
                    "폴더가 필요합니다.");
        std::error_code ignored;
        fs::remove_all(temporary, ignored);
        temporary.clear();
        path = Utf8(target);
        return true;
    } catch (const std::exception& exception) {
        if (!temporary.empty()) {
            std::error_code ignored;
            fs::remove_all(temporary, ignored);
        }
        error = exception.what();
        return false;
    }
}

} // namespace LuxEditor
