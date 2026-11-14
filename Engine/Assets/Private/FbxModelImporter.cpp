#include <LuxEngine/Assets/ModelAsset.hpp>
#include <ufbx.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

namespace LuxEngine {
namespace {

constexpr size_t MaxFileBytes = 64 * 1024 * 1024;
constexpr size_t MaxParserBytes = 128 * 1024 * 1024;
constexpr size_t MaxVertices = 1'000'000;
constexpr size_t MaxIndices = 3'000'000;
constexpr size_t MaxWeights = 8'000'000;
constexpr size_t MaxFaceVertices = 1024;
constexpr size_t MaxNodes = 100'000;
constexpr size_t MaxMaterials = 8192;
constexpr size_t MaxTextures = 8192;
constexpr size_t MaxAnimations = 4096;
constexpr double DegreesToRadians = 0.017453292519943295;

using ScenePtr = std::shared_ptr<ufbx_scene>;

std::string String(ufbx_string value) {
    return value.length ? std::string(value.data, value.length) : std::string{};
}

void Require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

bool ValidPath(const std::string& value) {
    if (value.empty() || value.size() > 4096 || value.find('\0') != std::string::npos)
        return false;
    for (size_t index = 0; index < value.size();) {
        const auto first = static_cast<unsigned char>(value[index++]);
        if (first < 0x80)
            continue;
        uint32 codepoint;
        size_t remaining;
        uint32 minimum;
        if (first >= 0xc2 && first <= 0xdf) {
            codepoint = first & 0x1f;
            remaining = 1;
            minimum = 0x80;
        } else if (first >= 0xe0 && first <= 0xef) {
            codepoint = first & 0x0f;
            remaining = 2;
            minimum = 0x800;
        } else if (first >= 0xf0 && first <= 0xf4) {
            codepoint = first & 0x07;
            remaining = 3;
            minimum = 0x10000;
        } else {
            return false;
        }
        if (remaining > value.size() - index)
            return false;
        while (remaining--) {
            const auto next = static_cast<unsigned char>(value[index++]);
            if ((next & 0xc0) != 0x80)
                return false;
            codepoint = (codepoint << 6) | (next & 0x3f);
        }
        if (codepoint < minimum || codepoint > 0x10ffff ||
            (codepoint >= 0xd800 && codepoint <= 0xdfff))
            return false;
    }
    return true;
}

float Scalar(double value) {
    Require(std::isfinite(value) && std::abs(value) <= std::numeric_limits<float>::max(),
            "FBX 값이 유효한 실수 범위를 벗어났습니다.");
    return static_cast<float>(value);
}

Vec3 Vector(ufbx_vec3 value) {
    return {Scalar(value.x), Scalar(value.y), Scalar(value.z)};
}

Mat4 Matrix(const ufbx_matrix& value) {
    Mat4 result;
    for (size_t column = 0; column < 4; ++column)
        for (size_t row = 0; row < 3; ++row)
            result.elements[column * 4 + row] = Scalar(value.cols[column].v[row]);
    return result;
}

ModelTransform Transform(const ufbx_node& node) {
    ModelTransform result;
    result.position = Vector(node.local_transform.translation);
    const auto rotation =
        ufbx_quat_to_euler(node.local_transform.rotation, UFBX_ROTATION_ORDER_XYZ);
    result.rotation = Vector({rotation.x * DegreesToRadians, rotation.y * DegreesToRadians,
                              rotation.z * DegreesToRadians});
    result.scale = Vector(node.local_transform.scale);
    result.matrix = Matrix(node.node_to_parent);
    return result;
}

Vec3 Normal(const ufbx_matrix& matrix, ufbx_vec3 normal) {
    normal = ufbx_transform_direction(&matrix, normal);
    const double length = std::hypot(normal.x, normal.y, normal.z);
    Require(std::isfinite(length) && length > 1e-30, "FBX 법선을 정규화할 수 없습니다.");
    return Vector({normal.x / length, normal.y / length, normal.z / length});
}

void Invertible(const ufbx_matrix& matrix) {
    Matrix(matrix);
    const auto determinant = ufbx_matrix_determinant(&matrix);
    Require(std::isfinite(determinant) && std::abs(determinant) > 1e-30,
            "FBX 메시 변환의 역행렬을 계산할 수 없습니다.");
}

template <typename Attribute> auto AttributeValue(const Attribute& attribute, uint32 corner) {
    Require(attribute.exists && corner < attribute.indices.count,
            "FBX 정점 속성 인덱스가 올바르지 않습니다.");
    const auto index = attribute.indices.data[corner];
    Require(index < attribute.values.count, "FBX 정점 속성 데이터가 올바르지 않습니다.");
    return attribute.values.data[index];
}

std::vector<uint8> ReadSnapshot(const std::filesystem::path& path) {
    std::error_code error;
    Require(std::filesystem::is_regular_file(path, error) && !error,
            "FBX 파일을 찾을 수 없습니다.");
    const auto size = std::filesystem::file_size(path, error);
    Require(!error && size > 0 && size <= MaxFileBytes,
            "FBX 파일은 비어 있지 않아야 하며 64 MiB 이하여야 합니다.");
    const auto modified = std::filesystem::last_write_time(path, error);
    Require(!error, "FBX 수정 시간을 확인할 수 없습니다.");
    std::ifstream input(path, std::ios::binary);
    Require(static_cast<bool>(input), "FBX 파일을 열 수 없습니다.");
    std::vector<uint8> bytes(static_cast<size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    Require(static_cast<bool>(input) && input.peek() == std::char_traits<char>::eof(),
            "FBX 파일을 읽지 못했습니다.");
    Require(std::filesystem::file_size(path, error) == size && !error,
            "읽는 동안 FBX 파일이 변경되었습니다.");
    Require(std::filesystem::last_write_time(path, error) == modified && !error,
            "읽는 동안 FBX 파일이 변경되었습니다.");
    return bytes;
}

struct SubmeshSource {
    uint32 node;
    std::vector<uint32> corners;
};

struct CachedModel {
    std::weak_ptr<ModelAsset> model;
    std::filesystem::file_time_type modified;
    uintmax_t size = 0;
};

std::mutex cacheMutex;
std::unordered_map<std::string, CachedModel> modelCache;

struct Corner {
    uint32 position;
    uint32 normal;
    uint32 uv;
    bool operator==(const Corner&) const = default;
};

struct CornerHash {
    size_t operator()(const Corner& corner) const {
        size_t hash = corner.position;
        hash ^= size_t(corner.normal) + size_t{0x9e3779b9} + (hash << 6) + (hash >> 2);
        hash ^= size_t(corner.uv) + size_t{0x9e3779b9} + (hash << 6) + (hash >> 2);
        return hash;
    }
};

void SetBounds(ModelSubmesh& mesh) {
    if (mesh.vertices.empty())
        return;
    mesh.boundsMin = mesh.boundsMax = mesh.vertices.front().position;
    for (const auto& vertex : mesh.vertices) {
        mesh.boundsMin.x = std::min(mesh.boundsMin.x, vertex.position.x);
        mesh.boundsMin.y = std::min(mesh.boundsMin.y, vertex.position.y);
        mesh.boundsMin.z = std::min(mesh.boundsMin.z, vertex.position.z);
        mesh.boundsMax.x = std::max(mesh.boundsMax.x, vertex.position.x);
        mesh.boundsMax.y = std::max(mesh.boundsMax.y, vertex.position.y);
        mesh.boundsMax.z = std::max(mesh.boundsMax.z, vertex.position.z);
    }
}

int32 TextureIndex(const ufbx_material_map& map,
                   const std::unordered_map<uint32, int32>& textureIndices) {
    if (!map.texture || !map.texture_enabled || map.feature_disabled)
        return -1;
    const auto* texture = map.texture;
    if (texture->type != UFBX_TEXTURE_FILE) {
        if (texture->file_textures.count != 1)
            return -1;
        texture = texture->file_textures.data[0];
    }
    const auto found = textureIndices.find(texture->typed_id);
    return found != textureIndices.end() ? found->second : -1;
}

void ReadMaterials(const ufbx_scene& scene, ModelAsset& model) {
    Require(scene.materials.count <= MaxMaterials && scene.textures.count <= MaxTextures,
            "FBX 재질·텍스처 수가 가져오기 한도를 초과했습니다.");
    std::unordered_map<uint32, int32> textureIndices;
    size_t embeddedBytes = 0;
    for (const auto* source : scene.textures) {
        if (source->type != UFBX_TEXTURE_FILE) {
            model.warnings.emplace_back("레이어·절차적 텍스처는 원본 파일 참조만 가져옵니다.");
            continue;
        }
        ModelTexture texture;
        texture.name = String(source->name);
        texture.path = String(source->filename);
        texture.relativePath = String(source->relative_filename);
        Require((texture.path.empty() || ValidPath(texture.path)) &&
                    (texture.relativePath.empty() || ValidPath(texture.relativePath)),
                "FBX 텍스처 경로가 유효한 UTF-8 경로가 아닙니다.");
        const auto content = source->content;
        Require(content.size <= MaxFileBytes - embeddedBytes,
                "FBX 내장 텍스처의 총 크기가 64 MiB를 초과했습니다.");
        embeddedBytes += content.size;
        if (content.size) {
            const auto* bytes = static_cast<const uint8*>(content.data);
            texture.embedded.assign(bytes, bytes + content.size);
        }
        textureIndices.emplace(source->typed_id, static_cast<int32>(model.textures.size()));
        model.textures.push_back(std::move(texture));
        if (source->has_uv_transform ||
            (source->uv_set.length && String(source->uv_set) != "UVMap"))
            model.warnings.emplace_back(
                "사용자 정의 텍스처 UV 변환·추가 UV 채널은 렌더러에서 지원하지 않습니다.");
    }
    for (const auto* source : scene.materials) {
        ModelMaterial material;
        material.name = String(source->name);
        const auto& pbr = source->pbr;
        const auto& color = pbr.base_color.has_value ? pbr.base_color : source->fbx.diffuse_color;
        if (color.has_value) {
            material.baseColor = {Scalar(color.value_vec3.x), Scalar(color.value_vec3.y),
                                  Scalar(color.value_vec3.z), 1.0f};
        }
        const double factor = pbr.base_factor.has_value ? pbr.base_factor.value_real : 1.0;
        material.baseColor.x = Scalar(material.baseColor.x * factor);
        material.baseColor.y = Scalar(material.baseColor.y * factor);
        material.baseColor.z = Scalar(material.baseColor.z * factor);
        material.baseColor.w = pbr.opacity.has_value ? Scalar(pbr.opacity.value_real) : 1.0f;
        material.metallic = pbr.metalness.has_value ? Scalar(pbr.metalness.value_real) : 0.0f;
        material.roughness = pbr.roughness.has_value ? Scalar(pbr.roughness.value_real) : 1.0f;
        material.emissive =
            pbr.emission_color.has_value ? Vector(pbr.emission_color.value_vec3) : Vec3{};
        material.baseColorTexture = TextureIndex(pbr.base_color, textureIndices);
        if (material.baseColorTexture < 0)
            material.baseColorTexture = TextureIndex(source->fbx.diffuse_color, textureIndices);
        material.normalTexture = TextureIndex(pbr.normal_map, textureIndices);
        material.metallicTexture = TextureIndex(pbr.metalness, textureIndices);
        material.roughnessTexture = TextureIndex(pbr.roughness, textureIndices);
        material.emissiveTexture = TextureIndex(pbr.emission_color, textureIndices);
        material.occlusionTexture = TextureIndex(pbr.ambient_occlusion, textureIndices);
        material.doubleSided = source->features.double_sided.enabled;
        model.materials.push_back(std::move(material));
    }
    std::sort(model.warnings.begin(), model.warnings.end());
    model.warnings.erase(std::unique(model.warnings.begin(), model.warnings.end()),
                         model.warnings.end());
}

void ReadBones(const ufbx_mesh& mesh, const ufbx_node& node, ModelSubmesh& result) {
    const auto nodeToGeometry = ufbx_matrix_invert(&node.geometry_to_node);
    for (const auto* skin : mesh.skin_deformers) {
        Require(result.bones.size() + skin->clusters.count <= 4096,
                "FBX 메시의 본 수가 가져오기 한도를 초과했습니다.");
        for (const auto* cluster : skin->clusters) {
            Require(cluster->bone_node != nullptr, "FBX 스킨에 연결되지 않은 본이 있습니다.");
            ModelBone bone;
            bone.name = String(cluster->bone_node->name);
            bone.node = cluster->bone_node->typed_id;
            bone.inverseBind = Matrix(ufbx_matrix_mul(&cluster->geometry_to_bone, &nodeToGeometry));
            result.bones.push_back(std::move(bone));
        }
    }
}

void ReadWeights(const ufbx_mesh& mesh, uint32 vertex, ModelVertex& result, size_t& totalWeights) {
    uint32 offset = 0;
    for (const auto* skin : mesh.skin_deformers) {
        if (vertex < skin->vertices.count) {
            const auto info = skin->vertices.data[vertex];
            Require(info.weight_begin <= skin->weights.count &&
                        info.num_weights <= skin->weights.count - info.weight_begin,
                    "FBX 스킨 가중치 범위가 올바르지 않습니다.");
            Require(info.num_weights <= MaxWeights - totalWeights,
                    "FBX 스킨 가중치 수가 가져오기 한도를 초과했습니다.");
            totalWeights += info.num_weights;
            for (uint32 index = 0; index < info.num_weights; ++index) {
                const auto weight = skin->weights.data[info.weight_begin + index];
                Require(weight.cluster_index < skin->clusters.count && weight.weight >= 0,
                        "FBX 스킨 가중치가 올바르지 않습니다.");
                result.weights.push_back({offset + weight.cluster_index, Scalar(weight.weight)});
            }
        }
        offset += static_cast<uint32>(skin->clusters.count);
    }
}

void ReadNodeMesh(const ufbx_node& node, ModelAsset& model, std::vector<SubmeshSource>& sources,
                  size_t& totalVertices, size_t& totalIndices, size_t& totalWeights) {
    const auto& mesh = *node.mesh;
    Require(mesh.num_vertices <= MaxVertices && mesh.num_indices <= MaxIndices &&
                mesh.num_triangles <= MaxIndices / 3,
            "FBX 메시의 크기가 가져오기 한도를 초과했습니다.");
    if (!mesh.num_triangles)
        return;
    Invertible(node.geometry_to_node);
    const auto normalMatrix = ufbx_matrix_for_normals(&node.geometry_to_node);
    const bool mirrored = ufbx_matrix_determinant(&node.geometry_to_node) < 0;
    std::unordered_map<int32, size_t> materialMeshes;
    std::vector<std::unordered_map<Corner, uint32, CornerHash>> corners;
    std::array<uint32, (MaxFaceVertices - 2) * 3> triangles;
    for (size_t faceIndex = 0; faceIndex < mesh.faces.count; ++faceIndex) {
        const auto face = mesh.faces.data[faceIndex];
        Require(face.num_indices <= MaxFaceVertices && face.index_begin <= mesh.num_indices &&
                    face.num_indices <= mesh.num_indices - face.index_begin,
                "FBX 다각형 크기·인덱스가 올바르지 않습니다.");
        if (face.num_indices < 3)
            continue;
        int32 material = -1;
        if (faceIndex < mesh.face_material.count) {
            const auto slot = mesh.face_material.data[faceIndex];
            if (slot < node.materials.count)
                material = static_cast<int32>(node.materials.data[slot]->typed_id);
        }
        auto found = materialMeshes.find(material);
        if (found == materialMeshes.end()) {
            Require(model.submeshes.size() < MaxNodes,
                    "FBX 서브메시 수가 가져오기 한도를 초과했습니다.");
            const auto index = model.submeshes.size();
            ModelSubmesh submesh;
            submesh.name = String(node.name);
            submesh.node = node.typed_id;
            submesh.material = material;
            submesh.hasTexCoords = mesh.vertex_uv.exists;
            submesh.skinned = mesh.skin_deformers.count != 0;
            submesh.morphTargets = mesh.blend_deformers.count != 0;
            ReadBones(mesh, node, submesh);
            model.nodes.at(node.typed_id).submeshes.push_back(static_cast<uint32>(index));
            model.submeshes.push_back(std::move(submesh));
            sources.push_back({node.typed_id, {}});
            corners.emplace_back();
            found = materialMeshes.emplace(material, corners.size() - 1).first;
        }
        const auto localIndex = found->second;
        const auto index = model.nodes.at(node.typed_id).submeshes.at(localIndex);
        auto& target = model.submeshes.at(index);
        auto& source = sources.at(index);
        auto& unique = corners.at(localIndex);
        const size_t triangleCount =
            ufbx_triangulate_face(triangles.data(), triangles.size(), &mesh, face);
        Require(triangleCount > 0 && triangleCount <= MaxIndices / 3 &&
                    triangleCount * 3 <= MaxIndices - totalIndices,
                "FBX 삼각형 수가 가져오기 한도를 초과했습니다.");
        totalIndices += triangleCount * 3;
        for (size_t triangle = 0; triangle < triangleCount; ++triangle) {
            for (size_t cornerIndex = 0; cornerIndex < 3; ++cornerIndex) {
                const auto corner =
                    triangles[triangle * 3 + (mirrored ? 2 - cornerIndex : cornerIndex)];
                Require(corner < mesh.vertex_indices.count &&
                            corner < mesh.vertex_position.indices.count &&
                            corner < mesh.vertex_normal.indices.count &&
                            (!mesh.vertex_uv.exists || corner < mesh.vertex_uv.indices.count),
                        "FBX 다각형의 정점 인덱스가 올바르지 않습니다.");
                const Corner key{mesh.vertex_indices.data[corner],
                                 mesh.vertex_normal.indices.data[corner],
                                 mesh.vertex_uv.exists ? mesh.vertex_uv.indices.data[corner] : 0};
                auto vertex = unique.find(key);
                if (vertex == unique.end()) {
                    Require(totalVertices++ < MaxVertices,
                            "FBX 정점 수가 1,000,000개를 초과했습니다.");
                    ModelVertex value;
                    value.position = Vector(ufbx_transform_position(
                        &node.geometry_to_node, AttributeValue(mesh.vertex_position, corner)));
                    value.normal = Normal(normalMatrix, AttributeValue(mesh.vertex_normal, corner));
                    if (mesh.vertex_uv.exists) {
                        const auto uv = AttributeValue(mesh.vertex_uv, corner);
                        value.texCoords = {Scalar(uv.x), Scalar(1.0 - uv.y)};
                    }
                    ReadWeights(mesh, key.position, value, totalWeights);
                    const auto vertexIndex = static_cast<uint32>(target.vertices.size());
                    target.vertices.push_back(std::move(value));
                    source.corners.push_back(corner);
                    vertex = unique.emplace(key, vertexIndex).first;
                }
                target.indices.push_back(vertex->second);
            }
        }
    }
    if (mesh.cache_deformers.count)
        model.warnings.emplace_back(
            "외부 정점 캐시의 자동 로딩은 지원하지 않습니다. 스킨·모프 애니메이션을 사용하십시오.");
}

} // namespace

struct ModelAsset::Impl {
    ScenePtr scene;
    std::vector<SubmeshSource> sources;
    mutable std::mutex poseMutex;
    mutable int32 cachedClip = -2;
    mutable double cachedTime = 0.0;
    mutable ModelPose cachedPose;
};

Ref<ModelAsset> ModelAsset::LoadFbx(const std::string& filepath, std::string& error) {
    error.clear();
    try {
        Require(ValidPath(filepath), "FBX 파일 경로가 비어 있거나 유효한 UTF-8 경로가 아닙니다.");
        const auto path = std::filesystem::path(std::u8string(filepath.begin(), filepath.end()));
        std::error_code status;
        const auto key = std::filesystem::absolute(path).lexically_normal().generic_u8string();
        const std::string cacheKey(key.begin(), key.end());
        const auto size = std::filesystem::file_size(path, status);
        Require(!status, "FBX 파일 크기를 확인할 수 없습니다.");
        const auto modified = std::filesystem::last_write_time(path, status);
        Require(!status, "FBX 수정 시간을 확인할 수 없습니다.");
        {
            const std::lock_guard lock(cacheMutex);
            const auto found = modelCache.find(cacheKey);
            if (found != modelCache.end() && found->second.size == size &&
                found->second.modified == modified)
                if (auto cached = found->second.model.lock())
                    return cached;
        }
        const auto bytes = ReadSnapshot(path);
        ufbx_load_opts options{};
        options.temp_allocator.memory_limit = MaxParserBytes;
        options.result_allocator.memory_limit = MaxParserBytes;
        options.temp_allocator.allocation_limit = 1'000'000;
        options.result_allocator.allocation_limit = 1'000'000;
        options.node_depth_limit = 256;
        options.file_format = UFBX_FILE_FORMAT_FBX;
        options.index_error_handling = UFBX_INDEX_ERROR_HANDLING_ABORT_LOADING;
        options.generate_missing_normals = true;
        options.evaluate_skinning = true;
        options.normalize_normals = true;
        options.target_axes = ufbx_axes_right_handed_y_up;
        options.target_unit_meters = 1.0;
        options.space_conversion = UFBX_SPACE_CONVERSION_ADJUST_TRANSFORMS;
        options.inherit_mode_handling = UFBX_INHERIT_MODE_HANDLING_HELPER_NODES;
        options.load_external_files = false;
        options.filename = {filepath.data(), filepath.size()};
        ufbx_error parserError{};
        ScenePtr scene(ufbx_load_memory(bytes.data(), bytes.size(), &options, &parserError),
                       ufbx_free_scene);
        if (!scene) {
            std::array<char, 2048> detail{};
            ufbx_format_error(detail.data(), detail.size(), &parserError);
            throw std::runtime_error(std::string("FBX 파일을 해석할 수 없습니다: ") +
                                     detail.data());
        }
        Require(scene->nodes.count <= MaxNodes && scene->anim_stacks.count <= MaxAnimations,
                "FBX 노드·애니메이션 수가 가져오기 한도를 초과했습니다.");
        Ref<ModelAsset> model(new ModelAsset);
        model->sourcePath = filepath;
        model->sourceUnitMeters = scene->settings.unit_meters;
        model->framesPerSecond = scene->settings.frames_per_second;
        model->impl_ = std::make_shared<Impl>();
        model->impl_->scene = scene;
        model->nodes.resize(scene->nodes.count);
        for (const auto* node : scene->nodes) {
            auto& target = model->nodes.at(node->typed_id);
            target.name = String(node->name);
            target.parent = node->parent ? static_cast<int32>(node->parent->typed_id) : -1;
            target.localTransform = Transform(*node);
            target.worldTransform = Matrix(node->node_to_world);
            target.bone = node->bone != nullptr;
        }
        ReadMaterials(*scene, *model);
        size_t totalVertices = 0, totalIndices = 0, totalWeights = 0;
        for (const auto* node : scene->nodes)
            if (node->mesh)
                ReadNodeMesh(*node, *model, model->impl_->sources, totalVertices, totalIndices,
                             totalWeights);
        for (auto& submesh : model->submeshes) {
            SetBounds(submesh);
            for (const auto& bone : submesh.bones)
                model->nodes.at(bone.node).bone = true;
        }
        for (const auto* stack : scene->anim_stacks) {
            Require(std::isfinite(stack->time_begin) && std::isfinite(stack->time_end) &&
                        stack->time_end >= stack->time_begin,
                    "FBX 애니메이션 시간 범위가 올바르지 않습니다.");
            model->animations.push_back(
                {String(stack->name), stack->time_begin, stack->time_end - stack->time_begin});
            Require(std::isfinite(model->animations.back().duration),
                    "FBX 애니메이션 길이가 유효한 실수 범위를 벗어났습니다.");
        }
        std::sort(model->warnings.begin(), model->warnings.end());
        model->warnings.erase(std::unique(model->warnings.begin(), model->warnings.end()),
                              model->warnings.end());
        Require(!model->submeshes.empty(), "FBX 파일에 렌더링할 다각형 메시가 없습니다.");
        {
            const std::lock_guard lock(cacheMutex);
            if (modelCache.size() >= 128)
                std::erase_if(modelCache,
                              [](const auto& item) { return item.second.model.expired(); });
            if (modelCache.size() >= 128)
                modelCache.erase(modelCache.begin());
            modelCache[cacheKey] = {model, modified, size};
        }
        return model;
    } catch (const std::exception& exception) {
        error = exception.what();
        return nullptr;
    }
}

bool ModelAsset::Evaluate(double seconds, int32 clip, ModelPose& output, std::string& error) const {
    error.clear();
    try {
        Require(impl_ && impl_->scene, "FBX 모델이 로딩되지 않았습니다.");
        Require(std::isfinite(seconds) && clip >= -1 &&
                    (clip == -1 || static_cast<size_t>(clip) < animations.size()),
                "FBX 애니메이션 시간·클립 인덱스가 올바르지 않습니다.");
        const std::lock_guard poseLock(impl_->poseMutex);
        const double clipTime =
            clip >= 0 ? std::clamp(seconds, 0.0, animations[clip].duration) : 0.0;
        if (impl_->cachedClip == clip && impl_->cachedTime == clipTime) {
            ModelPose copy = impl_->cachedPose;
            output = std::move(copy);
            return true;
        }
        const auto& sourceScene = *impl_->scene;
        Require(clip == -1 || static_cast<size_t>(clip) < sourceScene.anim_stacks.count,
                "FBX 애니메이션 메타데이터와 원본 클립이 일치하지 않습니다.");
        ScenePtr evaluated;
        const ufbx_scene* scene = &sourceScene;
        if (clip >= 0) {
            ufbx_evaluate_opts options{};
            options.temp_allocator.memory_limit = MaxParserBytes;
            options.result_allocator.memory_limit = MaxParserBytes;
            options.temp_allocator.allocation_limit = 1'000'000;
            options.result_allocator.allocation_limit = 1'000'000;
            options.evaluate_skinning = true;
            const auto* animation = clip >= 0 ? sourceScene.anim_stacks.data[clip]->anim : nullptr;
            const double time = animations[clip].start + clipTime;
            ufbx_error parserError{};
            evaluated =
                ScenePtr(ufbx_evaluate_scene(&sourceScene, animation, time, &options, &parserError),
                         ufbx_free_scene);
            if (!evaluated) {
                std::array<char, 2048> detail{};
                ufbx_format_error(detail.data(), detail.size(), &parserError);
                throw std::runtime_error(std::string("FBX 애니메이션을 평가할 수 없습니다: ") +
                                         detail.data());
            }
            scene = evaluated.get();
        }
        Require(scene->nodes.count == nodes.size(),
                "FBX 노드 메타데이터와 원본 계층이 일치하지 않습니다.");
        ModelPose result;
        result.nodeLocalTransforms.reserve(nodes.size());
        result.nodeWorldTransforms.reserve(nodes.size());
        for (const auto* node : scene->nodes) {
            result.nodeLocalTransforms.push_back(Transform(*node));
            result.nodeWorldTransforms.push_back(Matrix(node->node_to_world));
        }
        result.submeshes.reserve(submeshes.size());
        for (size_t index = 0; index < submeshes.size(); ++index) {
            const auto& mapping = impl_->sources.at(index);
            Require(mapping.node < scene->nodes.count,
                    "FBX 서브메시의 노드 인덱스가 올바르지 않습니다.");
            const auto* node = scene->nodes.data[mapping.node];
            const auto& mesh = *node->mesh;
            auto vertices = submeshes[index].vertices;
            if (evaluated || submeshes[index].skinned || submeshes[index].morphTargets) {
                ufbx_matrix transform = node->geometry_to_node;
                if (!mesh.skinned_is_local) {
                    Invertible(node->node_to_world);
                    transform = ufbx_matrix_invert(&node->node_to_world);
                }
                const auto normalMatrix = ufbx_matrix_for_normals(&transform);
                for (size_t vertex = 0; vertex < vertices.size(); ++vertex) {
                    const auto corner = mapping.corners.at(vertex);
                    vertices[vertex].position = Vector(ufbx_transform_position(
                        &transform, AttributeValue(mesh.skinned_position, corner)));
                    vertices[vertex].normal =
                        Normal(normalMatrix, AttributeValue(mesh.skinned_normal, corner));
                }
            }
            result.submeshes.push_back(std::move(vertices));
        }
        impl_->cachedPose = result;
        impl_->cachedClip = clip;
        impl_->cachedTime = clipTime;
        output = std::move(result);
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

} // namespace LuxEngine
