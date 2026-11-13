#include <LuxEngine/Assets/AssetManager.hpp>
#include <LuxEngine/Assets/FbxImporter.hpp>
#include <ufbx.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace LuxEngine::Detail {
namespace {
constexpr uint64 MaxFileBytes = 64ull * 1024ull * 1024ull;
constexpr size_t MaxParserBytes = 128ull * 1024ull * 1024ull;
constexpr size_t MaxVertices = 1'000'000;
constexpr size_t MaxIndices = 3'000'000;
constexpr size_t MaxFaceVertices = 1024;
constexpr size_t MaxNodes = 100'000;
constexpr uint32 MaxNodeDepth = 256;

struct Corner {
    uint32 position;
    uint32 normal;
    uint32 uv;

    bool operator==(const Corner&) const = default;
};

struct CornerHash {
    size_t operator()(const Corner& corner) const {
        auto hash = std::hash<uint32>{}(corner.position);
        hash ^= std::hash<uint32>{}(corner.normal) + size_t{0x9e3779b9} + (hash << 6) + (hash >> 2);
        hash ^= std::hash<uint32>{}(corner.uv) + size_t{0x9e3779b9} + (hash << 6) + (hash >> 2);
        return hash;
    }
};

bool Finite(ufbx_vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool Representable(ufbx_real value) {
    return std::isfinite(value) && std::abs(value) <= std::numeric_limits<float>::max();
}

bool FiniteMatrix(const ufbx_matrix& matrix) {
    return std::all_of(std::begin(matrix.v), std::end(matrix.v),
                       [](ufbx_real value) { return std::isfinite(value); });
}

bool StableFile(const std::filesystem::path& file, uint64 size,
                std::filesystem::file_time_type modified) {
    std::error_code error;
    if (std::filesystem::file_size(file, error) != size || error)
        return false;
    return std::filesystem::last_write_time(file, error) == modified && !error;
}

bool ReadSnapshot(const std::filesystem::path& file, std::vector<uint8>& bytes, uint64& size,
                  std::filesystem::file_time_type& modified, std::string& error) {
    std::error_code status;
    if (!std::filesystem::is_regular_file(file, status) || status) {
        error = "FBX 파일을 찾을 수 없거나 일반 파일이 아닙니다.";
        return false;
    }
    size = std::filesystem::file_size(file, status);
    if (status || size == 0 || size > MaxFileBytes) {
        error = "FBX 파일은 비어 있지 않아야 하며 64 MiB 이하여야 합니다.";
        return false;
    }
    modified = std::filesystem::last_write_time(file, status);
    if (status) {
        error = "FBX 파일의 수정 시간을 확인할 수 없습니다.";
        return false;
    }
    std::ifstream input(file, std::ios::binary);
    if (!input) {
        error = "FBX 파일을 열 수 없습니다.";
        return false;
    }
    bytes.resize(static_cast<size_t>(size));
    if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size)) ||
        input.peek() != std::char_traits<char>::eof() || !StableFile(file, size, modified)) {
        error = "FBX 파일을 읽지 못했거나 읽는 동안 파일이 변경되었습니다.";
        return false;
    }
    return true;
}

bool AttributeIndex(const ufbx_vertex_vec3& attribute, uint32 corner, uint32& index) {
    if (!attribute.exists || corner >= attribute.indices.count)
        return false;
    index = attribute.indices.data[corner];
    return index < attribute.values.count;
}

bool AttributeIndex(const ufbx_vertex_vec2& attribute, uint32 corner, uint32& index) {
    if (!attribute.exists || corner >= attribute.indices.count)
        return false;
    index = attribute.indices.data[corner];
    return index < attribute.values.count;
}

bool AppendNode(const ufbx_node& node, RawMeshData& result, bool& allUvs, std::string& error) {
    const auto& mesh = *node.mesh;
    if (mesh.skin_deformers.count || mesh.blend_deformers.count || mesh.cache_deformers.count) {
        error = "스킨·모프·캐시 변형이 있는 FBX는 정적 메시로 가져올 수 없습니다.";
        return false;
    }
    if (mesh.num_faces == 0)
        return true;
    if (mesh.num_vertices > MaxVertices || mesh.num_indices > MaxIndices ||
        mesh.num_triangles > MaxIndices / 3 || mesh.faces.count != mesh.num_faces ||
        mesh.max_face_triangles > MaxFaceVertices - 2) {
        error = "FBX 메시의 정점·삼각형 또는 다각형 크기가 가져오기 한도를 초과했습니다.";
        return false;
    }
    const auto& transform = node.geometry_to_world;
    const auto determinant = ufbx_matrix_determinant(&transform);
    const auto normals = ufbx_matrix_for_normals(&transform);
    if (!FiniteMatrix(transform) || !FiniteMatrix(normals) || !std::isfinite(determinant) ||
        determinant == 0) {
        error = "FBX 메시의 변환 행렬이 유효하지 않거나 역행렬을 계산할 수 없습니다.";
        return false;
    }
    const bool mirrored = determinant < 0;
    const bool hasUvs = mesh.vertex_uv.exists;
    allUvs = allUvs && hasUvs;
    std::unordered_map<Corner, uint32, CornerHash> vertices;
    std::array<uint32, (MaxFaceVertices - 2) * 3> triangles;
    const auto append = [&](uint32 source, uint32& target) {
        Corner corner{};
        if (!AttributeIndex(mesh.vertex_position, source, corner.position) ||
            !AttributeIndex(mesh.vertex_normal, source, corner.normal) ||
            (hasUvs && !AttributeIndex(mesh.vertex_uv, source, corner.uv))) {
            error = "FBX 메시의 정점 속성 인덱스가 올바르지 않습니다.";
            return false;
        }
        if (const auto found = vertices.find(corner); found != vertices.end()) {
            target = found->second;
            return true;
        }
        if (result.vertices.size() / 3 >= MaxVertices) {
            error = "FBX 인스턴스를 합친 정점 수가 1,000,000개를 초과했습니다.";
            return false;
        }
        const auto position =
            ufbx_transform_position(&transform, mesh.vertex_position.values.data[corner.position]);
        auto normal =
            ufbx_transform_direction(&normals, mesh.vertex_normal.values.data[corner.normal]);
        const auto uv = hasUvs ? mesh.vertex_uv.values.data[corner.uv] : ufbx_vec2{};
        // FBX UVs start at the bottom left; decoded engine textures start at the top left.
        // Missing UV attributes keep their existing zero-coordinate fallback.
        const auto textureV = hasUvs ? 1.0 - uv.y : 0.0;
        const auto length = std::hypot(normal.x, normal.y, normal.z);
        if (!Representable(position.x) || !Representable(position.y) ||
            !Representable(position.z) || !Finite(normal) || !std::isfinite(length) ||
            length <= 0 || !Representable(uv.x) || !Representable(textureV)) {
            error = "FBX 메시에 유효하지 않거나 표현 범위를 벗어난 정점·법선·UV가 있습니다.";
            return false;
        }
        normal.x /= length;
        normal.y /= length;
        normal.z /= length;
        target = static_cast<uint32>(result.vertices.size() / 3);
        result.vertices.insert(result.vertices.end(),
                               {static_cast<float>(position.x), static_cast<float>(position.y),
                                static_cast<float>(position.z)});
        result.normals.insert(result.normals.end(),
                              {static_cast<float>(normal.x), static_cast<float>(normal.y),
                               static_cast<float>(normal.z)});
        result.texCoords.insert(result.texCoords.end(),
                                {static_cast<float>(uv.x), static_cast<float>(textureV)});
        vertices.emplace(corner, target);
        return true;
    };
    for (const auto face : mesh.faces) {
        if (face.num_indices < 3 || face.num_indices > MaxFaceVertices ||
            face.index_begin > mesh.num_indices ||
            face.num_indices > mesh.num_indices - face.index_begin) {
            error = "FBX 메시에 유효하지 않거나 너무 큰 다각형이 있습니다.";
            return false;
        }
        const auto count = ufbx_triangulate_face(triangles.data(), triangles.size(), &mesh, face);
        if (count == 0 || count > triangles.size() / 3 ||
            result.indices.size() > MaxIndices - static_cast<size_t>(count) * 3) {
            error = "FBX 다각형을 삼각형으로 변환하지 못했거나 인덱스 한도를 초과했습니다.";
            return false;
        }
        for (uint32 index = 0; index < count * 3; index += 3) {
            std::array<uint32, 3> triangle{};
            for (uint32 corner = 0; corner < 3; ++corner)
                if (!append(triangles[index + corner], triangle[corner]))
                    return false;
            if (mirrored)
                std::swap(triangle[1], triangle[2]);
            result.indices.insert(result.indices.end(), triangle.begin(), triangle.end());
        }
    }
    return true;
}
} // namespace

bool ReadFbxMesh(const std::string& filepath, RawMeshData& outMesh, std::string& error) {
    outMesh = {};
    error.clear();
    if (filepath.empty() || filepath.find('\0') != std::string::npos) {
        error = "FBX 파일 경로가 올바르지 않습니다.";
        return false;
    }
    try {
        const std::filesystem::path file(
            std::u8string(reinterpret_cast<const char8_t*>(filepath.data()), filepath.size()));
        std::vector<uint8> bytes;
        uint64 size = 0;
        std::filesystem::file_time_type modified;
        if (!ReadSnapshot(file, bytes, size, modified, error))
            return false;
        ufbx_load_opts options{};
        options.temp_allocator.memory_limit = MaxParserBytes;
        options.result_allocator.memory_limit = MaxParserBytes;
        options.temp_allocator.allocation_limit = 1'000'000;
        options.result_allocator.allocation_limit = 1'000'000;
        options.node_depth_limit = MaxNodeDepth;
        options.file_format = UFBX_FILE_FORMAT_FBX;
        options.index_error_handling = UFBX_INDEX_ERROR_HANDLING_ABORT_LOADING;
        options.load_external_files = false;
        options.generate_missing_normals = true;
        options.normalize_normals = true;
        options.target_axes = ufbx_axes_right_handed_y_up;
        options.target_unit_meters = 1.0;
        options.filename = {filepath.data(), filepath.size()};
        ufbx_error parseError{};
        std::unique_ptr<ufbx_scene, decltype(&ufbx_free_scene)> scene(
            ufbx_load_memory(bytes.data(), bytes.size(), &options, &parseError), ufbx_free_scene);
        if (!scene) {
            error = "FBX 파일을 해석하지 못했습니다: ";
            if (parseError.description.data)
                error.append(parseError.description.data,
                             std::min(parseError.description.length, size_t{1024}));
            return false;
        }
        if (scene->nodes.count > MaxNodes) {
            error = "FBX 노드 수가 가져오기 한도를 초과했습니다.";
            return false;
        }
        RawMeshData result;
        bool allUvs = true;
        for (const auto* node : scene->nodes)
            if (node->mesh && !AppendNode(*node, result, allUvs, error))
                return false;
        if (result.vertices.empty() || result.indices.empty()) {
            error = "FBX 파일에 가져올 수 있는 폴리곤 메시가 없습니다.";
            return false;
        }
        result.hasTexCoords = allUvs;
        result.boundsMin =
            result.boundsMax = {result.vertices[0], result.vertices[1], result.vertices[2]};
        for (size_t index = 0; index < result.vertices.size(); index += 3) {
            result.boundsMin.x = std::min(result.boundsMin.x, result.vertices[index]);
            result.boundsMin.y = std::min(result.boundsMin.y, result.vertices[index + 1]);
            result.boundsMin.z = std::min(result.boundsMin.z, result.vertices[index + 2]);
            result.boundsMax.x = std::max(result.boundsMax.x, result.vertices[index]);
            result.boundsMax.y = std::max(result.boundsMax.y, result.vertices[index + 1]);
            result.boundsMax.z = std::max(result.boundsMax.z, result.vertices[index + 2]);
        }
        if (!StableFile(file, size, modified)) {
            error = "FBX 파일을 가져오는 동안 원본이 변경되었습니다.";
            return false;
        }
        outMesh = std::move(result);
        return true;
    } catch (const std::bad_alloc&) {
        error = "FBX 메시를 가져올 메모리가 부족합니다.";
    } catch (const std::exception&) {
        error = "FBX 파일 경로 또는 메시 데이터를 처리하지 못했습니다.";
    }
    return false;
}
} // namespace LuxEngine::Detail
