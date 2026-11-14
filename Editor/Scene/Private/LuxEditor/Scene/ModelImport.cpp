#include <LuxEditor/Project/PathEncoding.hpp>
#include <LuxEditor/Scene/ModelImport.hpp>
#include <LuxEngine/Assets/AssetManager.hpp>
#include <LuxEngine/Assets/ModelAsset.hpp>
#include <LuxEngine/Scene/Entity.hpp>
#include <LuxEngine/Scene/Scene.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace LuxEditor {
namespace {
namespace fs = std::filesystem;
using namespace LuxEngine;
constexpr size_t MaxEntities = 100000;

bool Inside(const fs::path& file, const fs::path& root) {
    std::error_code error;
    const auto resolved = fs::weakly_canonical(file, error);
    if (error)
        return false;
    const auto relative = resolved.lexically_relative(root);
    return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
}

void Warn(ModelImportResult& result, std::string message) {
    if (result.warnings.size() < 128 &&
        std::find(result.warnings.begin(), result.warnings.end(), message) == result.warnings.end())
        result.warnings.push_back(std::move(message));
}

std::string EmbeddedExtension(const std::vector<uint8>& bytes) {
    constexpr std::array<uint8, 8> png{137, 80, 78, 71, 13, 10, 26, 10};
    if (bytes.size() >= png.size() && std::equal(png.begin(), png.end(), bytes.begin()))
        return ".png";
    if (bytes.size() >= 3 && bytes[0] == 255 && bytes[1] == 216 && bytes[2] == 255)
        return ".jpg";
    if (bytes.size() >= 2 && bytes[0] == 'B' && bytes[1] == 'M')
        return ".bmp";
    if (bytes.size() >= 18 && (bytes[2] == 2 || bytes[2] == 10) &&
        (bytes[16] == 24 || bytes[16] == 32))
        return ".tga";
    return {};
}

fs::path ExtractTexture(const ModelTexture& texture, const fs::path& root) {
    if (texture.embedded.empty() || texture.embedded.size() > 64 * 1024 * 1024)
        return {};
    const auto extension = EmbeddedExtension(texture.embedded);
    if (extension.empty())
        return {};
    uint64 hash = 14695981039346656037ull;
    for (const auto byte : texture.embedded) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    std::ostringstream name;
    name << std::hex << std::setw(16) << std::setfill('0') << hash << extension;
    const auto directory = root / "Assets" / "ImportedTextures";
    if (!Inside(directory, root))
        throw std::runtime_error("모델 텍스처 폴더가 프로젝트 외부를 가리킵니다.");
    fs::create_directories(directory);
    const auto file = directory / name.str();
    if (!Inside(file, root))
        throw std::runtime_error("모델 텍스처 경로가 프로젝트 외부를 가리킵니다.");
    if (fs::exists(file)) {
        if (fs::file_size(file) != texture.embedded.size())
            throw std::runtime_error("모델 텍스처 캐시 이름이 충돌했습니다.");
        std::ifstream input(file, std::ios::binary);
        std::vector<uint8> existing(texture.embedded.size());
        if (!input.read(reinterpret_cast<char*>(existing.data()),
                        static_cast<std::streamsize>(existing.size())) ||
            existing != texture.embedded)
            throw std::runtime_error("모델 텍스처 캐시 내용이 변경되었습니다.");
        return file;
    }
    auto temporary = file;
    temporary += ".tmp";
    if (fs::exists(temporary))
        throw std::runtime_error("모델 텍스처 추출이 이미 진행 중입니다.");
    try {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(texture.embedded.data()),
                     static_cast<std::streamsize>(texture.embedded.size()));
        output.close();
        if (!output)
            throw std::runtime_error("내장 텍스처를 저장하지 못했습니다.");
        fs::rename(temporary, file);
        if (!AssetManager::ValidateTextureFile(GenericPathUtf8(file))) {
            fs::remove(file);
            return {};
        }
    } catch (...) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        throw;
    }
    return file;
}

std::vector<std::string> ResolveTextures(const ModelAsset& model, const fs::path& root,
                                         ModelImportResult& result) {
    std::vector<std::string> paths(model.textures.size());
    const auto source = PathFromUtf8(model.sourcePath);
    for (size_t index = 0; index < model.textures.size(); ++index) {
        const auto& texture = model.textures[index];
        fs::path resolved;
        for (const auto& text : {texture.path, texture.relativePath}) {
            if (text.empty() || text.find('\0') != std::string::npos)
                continue;
            auto candidate = PathFromUtf8(text);
            if (candidate.is_relative())
                candidate = source.parent_path() / candidate;
            std::error_code error;
            if (Inside(candidate, root) && fs::is_regular_file(candidate, error) && !error &&
                AssetManager::ValidateTextureFile(GenericPathUtf8(candidate))) {
                resolved = fs::canonical(candidate);
                break;
            }
        }
        if (resolved.empty())
            resolved = ExtractTexture(texture, root);
        if (!resolved.empty())
            paths[index] = GenericPathUtf8(resolved);
        else
            Warn(result,
                 "모델 텍스처를 연결하지 못했습니다. 프로젝트 내부의 이미지가 필요합니다: " +
                     texture.name);
    }
    return paths;
}

Material3D Material(const ModelAsset& model, const ModelSubmesh& part,
                    const std::vector<std::string>& textures, ModelImportResult& result) {
    Material3D value;
    if (part.material < 0)
        return value;
    if (static_cast<size_t>(part.material) >= model.materials.size())
        throw std::runtime_error("모델의 재질 슬롯이 올바르지 않습니다.");
    const auto& source = model.materials[part.material];
    value.albedoColor = source.baseColor;
    value.metallic = source.metallic;
    value.roughness = source.roughness;
    value.alphaMode =
        value.albedoColor.w < .999f ? MaterialAlphaMode::Blend : MaterialAlphaMode::Mask;
    value.alphaCutoff = .5f;
    const auto texture = [&](int32 index) {
        if (index < 0)
            return std::string{};
        if (static_cast<size_t>(index) >= textures.size())
            throw std::runtime_error("모델의 텍스처 슬롯이 올바르지 않습니다.");
        if (!part.hasTexCoords) {
            Warn(result, "UV가 없는 서브메시의 텍스처를 생략했습니다: " + part.name);
            return std::string{};
        }
        return textures[index];
    };
    value.baseColorTexture = texture(source.baseColorTexture);
    value.normalTexture = texture(source.normalTexture);
    value.metallicTexture = texture(source.metallicTexture);
    value.roughnessTexture = texture(source.roughnessTexture);
    value.occlusionTexture = texture(source.occlusionTexture);
    value.doubleSided = source.doubleSided;
    if (source.emissiveTexture >= 0 || source.emissive.Dot(source.emissive) > 0)
        Warn(result, "현재 렌더러에 없는 FBX 발광 재질 속성은 적용하지 않았습니다: " + source.name);
    return value;
}

void Validate(const ModelAsset& model) {
    if (model.sourcePath.empty() || model.nodes.empty() ||
        model.nodes.size() + model.submeshes.size() + 1 > MaxEntities)
        throw std::runtime_error("모델의 노드 수 또는 원본 경로가 올바르지 않습니다.");
    std::vector<uint8> visited(model.nodes.size());
    for (uint32 node = 0; node < model.nodes.size(); ++node) {
        std::vector<uint32> chain;
        int32 cursor = static_cast<int32>(node);
        while (cursor >= 0 && visited[cursor] != 2) {
            if (visited[cursor] == 1 || chain.size() >= 256)
                throw std::runtime_error("모델 계층이 순환하거나 너무 깊습니다.");
            visited[cursor] = 1;
            chain.push_back(static_cast<uint32>(cursor));
            const auto parent = model.nodes[cursor].parent;
            if (parent >= static_cast<int32>(model.nodes.size()) || parent < -1)
                throw std::runtime_error("모델 부모 노드가 올바르지 않습니다.");
            cursor = parent;
        }
        for (const auto id : chain)
            visited[id] = 2;
        const auto& transform = model.nodes[node].localTransform;
        for (const auto component :
             {transform.position.x, transform.position.y, transform.position.z,
              transform.rotation.x, transform.rotation.y, transform.rotation.z, transform.scale.x,
              transform.scale.y, transform.scale.z})
            if (!std::isfinite(component))
                throw std::runtime_error("모델 변환 값이 올바르지 않습니다.");
        for (const auto part : model.nodes[node].submeshes)
            if (part >= model.submeshes.size() || model.submeshes[part].node != node)
                throw std::runtime_error("모델 서브메시의 노드 연결이 올바르지 않습니다.");
    }
}

uint32 Build(Scene& prepared, const ModelAsset& model, const fs::path& root,
             ModelImportResult& result) {
    Validate(model);
    ModelPose initialPose;
    if (!model.animations.empty()) {
        std::string error;
        if (!model.Evaluate(0, 0, initialPose, error))
            throw std::runtime_error("모델의 첫 애니메이션 포즈를 평가하지 못했습니다: " + error);
        if (initialPose.nodeLocalTransforms.size() != model.nodes.size())
            throw std::runtime_error("모델의 첫 애니메이션 포즈와 노드 계층이 일치하지 않습니다.");
    }
    for (const auto& warning : model.warnings)
        Warn(result, warning);
    const auto textures = ResolveTextures(model, root, result);
    auto wrapper = prepared.CreateEntity(PathUtf8(PathFromUtf8(model.sourcePath).stem()));
    const auto wrapperId = wrapper.GetId();
    const auto animation = [&](uint32 node, bool apply) {
        ModelAnimationComponent value;
        value.modelPath = model.sourcePath;
        value.nodeIndex = node;
        value.clip = model.animations.empty() ? -1 : 0;
        value.playing = value.clip >= 0;
        value.applyNodeTransform = apply;
        return value;
    };
    wrapper.AddComponent<ModelAnimationComponent>(animation(0, false));
    std::vector<uint32> nodes;
    nodes.reserve(model.nodes.size());
    for (uint32 index = 0; index < model.nodes.size(); ++index) {
        const auto& source = model.nodes[index];
        // The renderer samples clip zero at time zero even before play starts. Use that same
        // pose for scene nodes so an authored first key differing from rest cannot offset the
        // rendered geometry. Vertices remain node-local; no geometric transform is reapplied.
        const auto& transform = model.animations.empty() ? source.localTransform
                                                         : initialPose.nodeLocalTransforms[index];
        auto entity = prepared.CreateEntity(source.name.empty() ? "Node " + std::to_string(index)
                                                                : source.name);
        entity.GetComponent<TransformComponent>() = {transform.position, transform.rotation,
                                                     transform.scale};
        entity.AddComponent<ModelAnimationComponent>(animation(index, true));
        nodes.push_back(entity.GetId());
    }
    for (uint32 index = 0; index < model.nodes.size(); ++index) {
        const auto& source = model.nodes[index];
        if (!prepared.SetParent(nodes[index], source.parent < 0 ? wrapperId : nodes[source.parent],
                                false))
            throw std::runtime_error("모델 노드 계층을 구성하지 못했습니다.");
        for (const auto partIndex : source.submeshes) {
            const auto& part = model.submeshes[partIndex];
            auto entity = source.submeshes.size() == 1
                              ? Entity{nodes[index], &prepared}
                              : prepared.CreateEntity(part.name.empty()
                                                          ? "Material " + std::to_string(partIndex)
                                                          : part.name);
            if (source.submeshes.size() != 1) {
                if (!prepared.SetParent(entity.GetId(), nodes[index], false))
                    throw std::runtime_error("모델 재질 계층을 구성하지 못했습니다.");
                entity.AddComponent<ModelAnimationComponent>(animation(index, false));
            }
            auto& mesh = entity.AddComponent<MeshRendererComponent>();
            mesh.meshPath = model.sourcePath;
            mesh.modelSubmesh = static_cast<int32>(partIndex);
            mesh.material = Material(model, part, textures, result);
        }
    }
    return wrapperId;
}

std::string Key(const Scene& scene, uint32 id) {
    const auto* animation = scene.TryGetComponent<ModelAnimationComponent>(id);
    if (!animation)
        return {};
    if (animation->applyNodeTransform)
        return "node:" + std::to_string(animation->nodeIndex);
    if (const auto* mesh = scene.TryGetComponent<MeshRendererComponent>(id))
        return "mesh:" + std::to_string(mesh->modelSubmesh);
    return "root";
}
} // namespace

bool ImportModelAsset(Scene& scene, const ModelAsset& model, const fs::path& requestedRoot,
                      ModelImportResult& result, std::string& error) {
    result = {};
    error.clear();
    try {
        const auto root = fs::canonical(requestedRoot);
        if (!Inside(PathFromUtf8(model.sourcePath), root))
            throw std::runtime_error("모델 원본은 프로젝트 내부에 있어야 합니다.");
        Scene prepared;
        const auto wrapper = Build(prepared, model, root, result);
        const auto mapping = scene.ImportEntities(prepared);
        if (mapping.size() != prepared.GetEntityCount() || !mapping.contains(wrapper))
            throw std::runtime_error("모델 계층을 씬에 반영하지 못했습니다.");
        result.root = mapping.at(wrapper);
        for (const auto& [source, destination] : mapping)
            result.entities.push_back(destination);
        std::sort(result.entities.begin(), result.entities.end());
        return true;
    } catch (const std::exception& failure) {
        error = failure.what();
        result.root = 0;
        result.entities.clear();
        return false;
    }
}

uint32 FindImportedModelRoot(const Scene& scene, uint32 selected) {
    const auto* source = scene.TryGetComponent<ModelAnimationComponent>(selected);
    if (!source)
        return 0;
    const auto path = source->modelPath;
    uint32 root = selected;
    std::set<uint32> visited;
    while (visited.insert(root).second) {
        const auto& current = scene.GetComponent<ModelAnimationComponent>(root);
        if (!current.applyNodeTransform && !scene.HasComponent<MeshRendererComponent>(root))
            return root;
        const auto parent = scene.GetParent(root);
        const auto* animation = scene.TryGetComponent<ModelAnimationComponent>(parent);
        if (!animation || animation->modelPath != path)
            return root;
        root = parent;
    }
    return 0;
}

bool ReimportModelAsset(Scene& scene, uint32 selected, const fs::path& requestedRoot,
                        ModelImportResult& result, std::string& error,
                        const fs::path& replacementSource) {
    result = {};
    error.clear();
    try {
        const auto rootId = FindImportedModelRoot(scene, selected);
        if (!rootId)
            throw std::runtime_error("다시 가져올 모델 계층을 선택해 주세요.");
        const auto previousSource =
            scene.ResolveAssetPath(scene.GetComponent<ModelAnimationComponent>(rootId).modelPath);
        const auto sourcePath =
            replacementSource.empty() ? previousSource : GenericPathUtf8(replacementSource);
        auto model = ModelAsset::LoadFbx(sourcePath, error);
        if (!model)
            return false;
        const auto root = fs::canonical(requestedRoot);
        if (!Inside(PathFromUtf8(sourcePath), root))
            throw std::runtime_error("모델 원본은 프로젝트 내부에 있어야 합니다.");
        Scene replacement;
        const auto sourceRoot = Build(replacement, *model, root, result);
        std::string snapshot;
        Scene staged;
        if (!scene.SerializeToString(snapshot) || !staged.DeserializeFromString(snapshot))
            throw std::runtime_error("모델 재가져오기 전 장면을 복사하지 못했습니다.");
        staged.SetSourcePath(scene.GetSourcePath());
        const auto placement = staged.GetComponent<TransformComponent>(rootId);
        const auto parent = staged.GetParent(rootId);
        std::map<std::string, uint32> existing;
        std::vector<uint32> pending{rootId};
        std::vector<uint32> imported;
        while (!pending.empty()) {
            const auto id = pending.back();
            pending.pop_back();
            const auto* animation = staged.TryGetComponent<ModelAnimationComponent>(id);
            if (!animation || staged.ResolveAssetPath(animation->modelPath) != previousSource)
                continue;
            if (id != rootId && !animation->applyNodeTransform &&
                !staged.HasComponent<MeshRendererComponent>(id))
                continue;
            if (!existing.emplace(Key(staged, id), id).second)
                throw std::runtime_error("중복된 모델 노드 연결을 확인해 주세요.");
            imported.push_back(id);
            for (const auto child : staged.GetChildren(id))
                pending.push_back(child);
        }
        std::map<uint32, uint32> mapping;
        for (const auto& entity : replacement.GetEntitiesInHierarchyOrder()) {
            const auto id = entity.GetId();
            const auto old = existing.find(Key(replacement, id));
            const auto destination =
                old == existing.end() ? staged.CreateEntity().GetId() : old->second;
            mapping.emplace(id, destination);
        }
        std::set<uint32> retained;
        for (const auto& [source, destination] : mapping)
            retained.insert(destination);
        const std::set<uint32> importedIds(imported.begin(), imported.end());
        // Preserve authored children, including separately imported model hierarchies. Move
        // them while the old parent transforms still exist; then detach imported nodes before
        // deletion so removing an obsolete ancestor cannot destroy another retained node.
        for (const auto id : imported) {
            if (retained.contains(id))
                continue;
            for (const auto child : staged.GetChildren(id))
                if (!importedIds.contains(child))
                    if (!staged.SetParent(child, rootId, true))
                        throw std::runtime_error("사용자 오브젝트를 보존하지 못했습니다.");
        }
        for (const auto id : imported)
            if (!staged.SetParent(id, 0, false))
                throw std::runtime_error("기존 모델 계층을 분리하지 못했습니다.");
        for (const auto& [source, destination] : mapping)
            if (!staged.CopyEntityComponents(replacement, source, destination))
                throw std::runtime_error("모델 노드를 갱신하지 못했습니다.");
        for (const auto id : imported) {
            if (retained.contains(id))
                continue;
            staged.DestroyEntity(Entity{id, &staged});
        }
        for (const auto& entity : replacement.GetEntitiesInHierarchyOrder()) {
            const auto id = entity.GetId();
            const auto targetParent = replacement.GetParent(id);
            if (!staged.SetParent(mapping.at(id), targetParent ? mapping.at(targetParent) : parent,
                                  false))
                throw std::runtime_error("갱신한 모델 계층을 연결하지 못했습니다.");
        }
        staged.GetComponent<TransformComponent>(mapping.at(sourceRoot)) = placement;
        if (!scene.ApplyState(staged))
            throw std::runtime_error("모델 재가져오기 결과를 반영하지 못했습니다.");
        result.root = mapping.at(sourceRoot);
        result.entities.assign(retained.begin(), retained.end());
        return true;
    } catch (const std::exception& failure) {
        error = failure.what();
        result.root = 0;
        result.entities.clear();
        return false;
    }
}
} // namespace LuxEditor
