#include <LuxEngine/Scene/Entity.hpp>
#include <LuxEngine/Scene/PrefabLibrary.hpp>
#include <LuxEngine/Scene/SceneManager.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace LuxEngine {
namespace {
namespace fs = std::filesystem;
constexpr std::uintmax_t MaxFileBytes = 64 * 1024 * 1024;
constexpr std::uintmax_t MaxLoadBytes = 128 * 1024 * 1024;
constexpr size_t MaxCacheBytes = 64 * 1024 * 1024;
constexpr size_t MaxReferences = 128;
constexpr size_t MaxDepth = 16;
constexpr uint32 MaxEntities = 100000;
void ValidateInheritance(Scene& scene);

std::string Utf8(const fs::path& path) {
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

fs::path Path(const std::string& value) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(value.data()), value.size()));
}

fs::path Canonical(const fs::path& path, const fs::path& owner = {}) {
    if (path.empty() || path.native().find(fs::path::value_type{}) != fs::path::string_type::npos)
        throw std::runtime_error("씬 경로가 비어 있습니다.");
    std::error_code error;
    const auto joined = path.is_absolute() ? path : owner.parent_path() / path;
    const auto absolute = fs::absolute(joined, error);
    if (error)
        throw std::runtime_error("씬 경로를 확인할 수 없습니다: " + Utf8(path));
    const auto canonical = fs::weakly_canonical(absolute, error);
    if (error)
        throw std::runtime_error("씬 경로를 확인할 수 없습니다: " + Utf8(path));
    return canonical.lexically_normal();
}

bool SamePath(const fs::path& first, const fs::path& second) {
    if (first == second)
        return true;
    std::error_code error;
    return fs::equivalent(first, second, error) && !error;
}

std::shared_ptr<Scene> Clone(Scene& source) {
    std::string data;
    if (!source.SerializeToString(data))
        throw std::runtime_error("씬 상태를 복사할 수 없습니다.");
    auto result = std::make_shared<Scene>();
    if (!result->DeserializeFromString(data))
        throw std::runtime_error("씬 상태를 복원할 수 없습니다.");
    result->SetSourcePath(source.GetSourcePath());
    for (auto entity : source.GetEntities()) {
        if (const auto* animation = entity.TryGetComponent<AnimationComponent>())
            result->GetComponent<AnimationComponent>(entity.GetId()).currentTime =
                animation->currentTime;
        if (const auto* body = entity.TryGetComponent<Rigidbody2DComponent>())
            result->GetComponent<Rigidbody2DComponent>(entity.GetId()).isGrounded =
                body->isGrounded;
    }
    return result;
}

void Commit(Scene& target, Scene& staged) {
    if (staged.GetEntityCount() > MaxEntities)
        throw std::runtime_error("합성 씬의 엔티티 수가 100000개를 넘었습니다.");
    ValidateInheritance(staged);
    if (!target.ApplyState(staged))
        throw std::runtime_error("합성 씬 상태를 적용할 수 없습니다.");
}

std::vector<uint32> ReferenceRoots(Scene& scene) {
    auto result = scene.GetComponentEntityIds<SceneReferenceComponent>();
    if (result.empty())
        return result;
    if (result.size() > MaxReferences)
        throw std::runtime_error("씬 참조 수가 128개를 넘었습니다.");
    std::unordered_map<uint32, size_t> depths{{0, 0}};
    for (const auto id : result) {
        std::vector<uint32> chain;
        auto cursor = id;
        while (!depths.contains(cursor)) {
            if (!scene.IsAlive(cursor) || chain.size() >= MaxEntities)
                throw std::runtime_error("씬 참조의 부모 계층이 올바르지 않습니다.");
            chain.push_back(cursor);
            cursor = scene.GetParent(cursor);
        }
        auto depth = depths.at(cursor);
        for (auto it = chain.rbegin(); it != chain.rend(); ++it)
            depths[*it] = ++depth;
    }
    std::stable_sort(result.begin(), result.end(),
                     [&](uint32 a, uint32 b) { return depths.at(a) < depths.at(b); });
    return result;
}

bool IsDescendant(Scene& scene, uint32 id, uint32 root) {
    for (size_t depth = 0; id != 0 && depth <= MaxEntities; ++depth) {
        if (id == root)
            return true;
        id = scene.GetParent(id);
    }
    return false;
}

bool HasReferenceAncestor(Scene& scene, uint32 id) {
    for (auto parent = scene.GetParent(id); parent != 0; parent = scene.GetParent(parent))
        if (scene.HasComponent<SceneReferenceComponent>(parent))
            return true;
    return false;
}

void ValidateInheritance(Scene& scene) {
    unsigned bases = 0;
    for (const auto root : ReferenceRoots(scene))
        if (scene.GetComponent<SceneReferenceComponent>(root).mode ==
                SceneReferenceMode::Inherited &&
            !HasReferenceAncestor(scene, root) && ++bases > 1)
            throw std::runtime_error("씬 전체에 적용되는 상속 기준은 하나만 지정할 수 있습니다.");
}

void PruneMaps(Scene& scene) {
    for (auto entity : scene.GetEntities()) {
        if (auto* reference = entity.TryGetComponent<SceneReferenceComponent>())
            std::erase_if(reference->entityMap,
                          [&](const auto& entry) { return !scene.IsAlive(entry.second); });
        if (const auto* source = entity.TryGetComponent<InstanceSourceComponent>();
            source && !scene.IsAlive(source->instanceRoot))
            entity.RemoveComponent<InstanceSourceComponent>();
    }
}

using IdMap = std::unordered_map<uint32, uint32>;

IdMap ReadMap(const SceneReferenceComponent& reference) {
    return {reference.entityMap.begin(), reference.entityMap.end()};
}

void WriteMap(SceneReferenceComponent& reference, const IdMap& mapping) {
    reference.entityMap.assign(mapping.begin(), mapping.end());
    std::sort(reference.entityMap.begin(), reference.entityMap.end());
}

void RemapReference(SceneReferenceComponent& reference, const IdMap& mapping) {
    std::vector<std::pair<uint32, uint32>> next;
    for (const auto& [source, local] : reference.entityMap)
        if (const auto found = mapping.find(local); found != mapping.end())
            next.emplace_back(source, found->second);
    reference.entityMap = std::move(next);
}

SceneSettings InheritedSettings(const SceneSettings& current, const SceneSettings& previous,
                                const SceneSettings& source) {
    auto result = current;
    float* fields[]{&result.clearColor.x, &result.clearColor.y, &result.clearColor.z,
                    &result.clearColor.w, &result.exposure,     &result.gravity.x,
                    &result.gravity.y,    &result.gravity3D.x,  &result.gravity3D.y,
                    &result.gravity3D.z,  &result.fixedStep};
    const float before[]{previous.clearColor.x, previous.clearColor.y, previous.clearColor.z,
                         previous.clearColor.w, previous.exposure,     previous.gravity.x,
                         previous.gravity.y,    previous.gravity3D.x,  previous.gravity3D.y,
                         previous.gravity3D.z,  previous.fixedStep};
    const float after[]{source.clearColor.x, source.clearColor.y, source.clearColor.z,
                        source.clearColor.w, source.exposure,     source.gravity.x,
                        source.gravity.y,    source.gravity3D.x,  source.gravity3D.y,
                        source.gravity3D.z,  source.fixedStep};
    for (size_t index = 0; index < std::size(fields); ++index)
        if (*fields[index] == before[index])
            *fields[index] = after[index];
    if (result.overrideGravity == previous.overrideGravity)
        result.overrideGravity = source.overrideGravity;
    if (result.physicsSubsteps == previous.physicsSubsteps)
        result.physicsSubsteps = source.physicsSubsteps;
    return result;
}

std::string EntitySignature(Scene& scene, uint32 id, const IdMap& referenceIds) {
    Scene single;
    auto entity = single.CreateEntity();
    if (!single.CopyEntityComponents(scene, id, entity.GetId()))
        throw std::runtime_error("인스턴스 속성을 비교할 수 없습니다.");
    std::string externalReferences;
    if (auto* script = entity.TryGetComponent<ScriptComponent>())
        for (auto& [name, value] : script->fields)
            if (auto* reference = std::get_if<ScriptEntityReference>(&value)) {
                const auto found = referenceIds.find(reference->id);
                if (reference->id && found == referenceIds.end())
                    externalReferences += std::to_string(name.size()) + ":" + name + ":" +
                                          std::to_string(reference->id) + ";";
                reference->id = found == referenceIds.end() ? 0 : found->second;
            }
    if (auto* reference = entity.TryGetComponent<SceneReferenceComponent>()) {
        reference->baseline.clear();
        reference->suspended.clear();
        reference->entityMap.clear();
        reference->loaded = true;
    }
    std::string signature;
    if (!single.SerializeToString(signature))
        throw std::runtime_error("인스턴스 속성을 비교할 수 없습니다.");
    return signature + "\nexternal:" + externalReferences;
}

bool SameEntity(Scene& current, uint32 local, Scene& previous, uint32 source, const IdMap& map,
                const IdMap& localReferences, const IdMap& sourceReferences, uint32 container) {
    if (!current.IsAlive(local) || !previous.IsAlive(source))
        return false;
    const auto oldParent = previous.GetParent(source);
    const auto found = map.find(oldParent);
    const auto expectedParent = oldParent == 0 ? container : found == map.end() ? 0 : found->second;
    const auto* oldHierarchy = previous.TryGetComponent<HierarchyComponent>(source);
    const auto* localHierarchy = current.TryGetComponent<HierarchyComponent>(local);
    return current.GetParent(local) == expectedParent &&
           (oldHierarchy && oldHierarchy->group) == (localHierarchy && localHierarchy->group) &&
           EntitySignature(current, local, localReferences) ==
               EntitySignature(previous, source, sourceReferences);
}

// Persisted metadata can contain previous source and unloaded-subtree snapshots. Their reference
// paths use the same owning-file context as the outer scene, including after project relocation.
void RewritePaths(Scene& scene, const fs::path& owner, const fs::path* destination,
                  size_t depth = 0) {
    if (depth > MaxDepth)
        throw std::runtime_error("씬 참조 스냅샷의 중첩 한도를 넘었습니다.");
    auto settings = scene.GetSettings();
    if (!settings.render3D.environmentPath.empty()) {
        const auto absolute = Canonical(Path(settings.render3D.environmentPath), owner);
        const auto relative =
            destination ? absolute.lexically_relative(destination->parent_path()) : fs::path{};
        settings.render3D.environmentPath =
            Utf8(destination && !relative.empty() ? relative : absolute);
        if (!scene.SetSettings(settings))
            throw std::runtime_error("Invalid rebased environment path");
    }
    for (auto entity : scene.GetEntities()) {
        if (auto* animation = entity.TryGetComponent<ModelAnimationComponent>()) {
            const auto absolute = Canonical(Path(animation->modelPath), owner);
            const auto relative =
                destination ? absolute.lexically_relative(destination->parent_path()) : fs::path{};
            animation->modelPath = Utf8(destination && !relative.empty() ? relative : absolute);
        }
        if (auto* mesh = entity.TryGetComponent<MeshRendererComponent>())
            for (auto* resource :
                 {&mesh->meshPath, &mesh->material.baseColorTexture, &mesh->material.normalTexture,
                  &mesh->material.metallicTexture, &mesh->material.roughnessTexture,
                  &mesh->material.occlusionTexture}) {
                if (resource->empty())
                    continue;
                const auto absolute = Canonical(Path(*resource), owner);
                const auto relative = destination
                                          ? absolute.lexically_relative(destination->parent_path())
                                          : fs::path{};
                *resource = Utf8(destination && !relative.empty() ? relative : absolute);
            }
        auto* reference = entity.TryGetComponent<SceneReferenceComponent>();
        if (!reference)
            continue;
        const auto absolute = Canonical(Path(reference->path), owner);
        if (destination) {
            if (SamePath(absolute, *destination))
                throw std::runtime_error("씬 자신을 참조하는 경로에 저장할 수 없습니다.");
            std::error_code error;
            const auto relative = fs::relative(absolute, destination->parent_path(), error);
            reference->path = !error && !relative.empty() ? Utf8(relative) : Utf8(absolute);
        } else
            reference->path = Utf8(absolute);
        for (std::string* snapshot : {&reference->baseline, &reference->suspended}) {
            if (snapshot->empty())
                continue;
            Scene nested;
            if (!nested.DeserializeFromString(*snapshot))
                throw std::runtime_error("씬 참조 스냅샷 형식이 올바르지 않습니다.");
            RewritePaths(nested, owner, destination, depth + 1);
            if (!nested.SerializeToString(*snapshot))
                throw std::runtime_error("씬 참조 경로를 저장할 수 없습니다.");
        }
    }
}

std::string CaptureSubtree(Scene& target, uint32 root) {
    auto snapshot = Clone(target);
    std::unordered_set<uint32> retained;
    for (auto entity : snapshot->GetEntities())
        if (IsDescendant(*snapshot, entity.GetId(), root))
            retained.insert(entity.GetId());
    for (uint32 parent = snapshot->GetParent(root); parent != 0;
         parent = snapshot->GetParent(parent))
        retained.insert(parent);
    snapshot->GetComponent<SceneReferenceComponent>(root).suspended.clear();
    for (auto entity : snapshot->GetEntities())
        if (!retained.contains(entity.GetId()) && entity.IsValid())
            snapshot->DestroyEntity(entity);
    PruneMaps(*snapshot);
    std::string data;
    if (!snapshot->SerializeToString(data))
        throw std::runtime_error("언로드할 씬의 상태를 보관할 수 없습니다.");
    return data;
}

void RestoreSubtree(Scene& target, uint32 root) {
    auto reference = target.GetComponent<SceneReferenceComponent>(root);
    if (reference.suspended.empty())
        return;
    Scene snapshot;
    if (!snapshot.DeserializeFromString(reference.suspended) || !snapshot.IsAlive(root))
        throw std::runtime_error("언로드된 씬 상태를 복원할 수 없습니다.");
    const auto mapping = target.ImportEntities(snapshot, {root});
    const auto newRoot = mapping.at(root);
    const auto restoredReference = target.GetComponent<SceneReferenceComponent>(newRoot);
    // These already-validated subtrees keep their local transforms. SetParent would detach their
    // provenance while they temporarily leave the imported placeholder; publish the complete
    // hierarchy and mapping together instead.
    for (const auto child : target.GetChildren(newRoot))
        target.GetComponent<HierarchyComponent>(child).parent = root;
    for (const auto& [old, local] : mapping) {
        (void)old;
        if (auto* source = target.TryGetComponent<InstanceSourceComponent>(local);
            source && source->instanceRoot == newRoot)
            source->instanceRoot = root;
    }
    // An inner streamed instance also belongs to its outer instance's mapping. Restore those
    // associations after new local IDs have been allocated, preserving nested reload behavior.
    for (auto entity : snapshot.GetEntities()) {
        if (mapping.contains(entity.GetId()))
            continue;
        const auto* saved = entity.TryGetComponent<SceneReferenceComponent>();
        auto* ancestor = target.TryGetComponent<SceneReferenceComponent>(entity.GetId());
        if (!saved || !ancestor)
            continue;
        auto ancestorMap = ReadMap(*ancestor);
        for (const auto& [source, oldLocal] : saved->entityMap)
            if (const auto found = mapping.find(oldLocal); found != mapping.end())
                ancestorMap[source] = oldLocal == root ? root : found->second;
        WriteMap(*ancestor, ancestorMap);
    }
    target.DestroyEntity(Entity(newRoot, &target));
    reference.entityMap = restoredReference.entityMap;
    reference.suspended.clear();
    reference.loaded = true;
    target.GetComponent<SceneReferenceComponent>(root) = std::move(reference);
    PruneMaps(target);
}

void UnloadSubtree(Scene& target, uint32 root) {
    auto& reference = target.GetComponent<SceneReferenceComponent>(root);
    if (!reference.loaded)
        return;
    const auto suspended = CaptureSubtree(target, root);
    for (const auto child : target.GetChildren(root))
        target.DestroyEntity(Entity(child, &target));
    auto& current = target.GetComponent<SceneReferenceComponent>(root);
    current.suspended = suspended;
    current.loaded = false;
    current.entityMap.clear();
    PruneMaps(target);
}

struct Stamp {
    fs::path path;
    fs::file_time_type writeTime{};
    std::uintmax_t bytes = 0;
};

Stamp FileStamp(const fs::path& path) {
    std::error_code error;
    if (!fs::is_regular_file(path, error) || error)
        throw std::runtime_error("씬 파일을 찾을 수 없습니다: " + Utf8(path));
    Stamp result{path};
    result.bytes = fs::file_size(path, error);
    if (error || result.bytes > MaxFileBytes)
        throw std::runtime_error("씬 파일이 읽기 한도 64 MiB를 넘었거나 읽을 수 없습니다.");
    result.writeTime = fs::last_write_time(path, error);
    if (error)
        throw std::runtime_error("씬 파일 변경 시각을 확인할 수 없습니다.");
    return result;
}

bool Matches(const Stamp& expected) {
    try {
        const auto actual = FileStamp(expected.path);
        return actual.bytes == expected.bytes && actual.writeTime == expected.writeTime;
    } catch (...) {
        return false;
    }
}

struct LoadContext {
    std::vector<fs::path> stack;
    std::vector<Stamp> dependencies;
    std::uintmax_t bytes = 0;
    size_t references = 0;
    bool force = false;
    std::optional<uint64> cacheGeneration;
};

LoadContext ContextFor(Scene& scene, bool force = true) {
    LoadContext context;
    context.force = force;
    if (!scene.GetSourcePath().empty())
        context.stack.push_back(Canonical(Path(scene.GetSourcePath())));
    return context;
}

void CheckSaveDependencies(Scene& scene, const fs::path& owner, std::vector<fs::path>& stack,
                           std::uintmax_t& bytes, size_t& references,
                           std::set<std::string>& validated) {
    for (const auto root : ReferenceRoots(scene)) {
        const auto path =
            Canonical(Path(scene.GetComponent<SceneReferenceComponent>(root).path), owner);
        for (const auto& ancestor : stack)
            if (SamePath(path, ancestor))
                throw std::runtime_error(
                    "씬 참조에 순환 관계가 생기는 경로에는 저장할 수 없습니다.");
        const auto key = Utf8(path);
        if (validated.contains(key))
            continue;
        if (++references > MaxReferences || stack.size() >= MaxDepth)
            throw std::runtime_error("씬 저장 종속성의 중첩 또는 참조 수 한도를 넘었습니다.");
        const auto stamp = FileStamp(path);
        bytes += stamp.bytes;
        if (bytes > MaxLoadBytes)
            throw std::runtime_error("씬 저장 종속성의 합계 읽기 한도 128 MiB를 넘었습니다.");
        Scene dependency;
        if (!dependency.Deserialize(Utf8(path)))
            throw std::runtime_error("참조된 씬 파일을 읽을 수 없습니다: " + Utf8(path));
        stack.push_back(path);
        CheckSaveDependencies(dependency, path, stack, bytes, references, validated);
        stack.pop_back();
        validated.insert(key);
    }
}
} // namespace

struct SceneManager::State {
    struct CacheEntry {
        std::string data;
        std::vector<Stamp> dependencies;
        uint64 used = 0;
    };

    struct StreamJob {
        Scene* owner = nullptr;
        std::weak_ptr<int> lifetime;
        uint32 root = 0;
        std::string path;
        std::future<SceneLoadResult> future;
    };

    struct FailedStream {
        Scene* owner = nullptr;
        std::weak_ptr<int> lifetime;
        uint32 root = 0;
        std::string path;
        std::chrono::steady_clock::time_point retry;
    };

    mutable std::mutex mutex;
    std::map<std::string, CacheEntry> rawCache, preloadCache;
    uint64 sequence = 0, generation = 0;
    size_t cacheBytes = 0;
    std::atomic<unsigned> activeJobs{0};
    // Streaming state is accessed only by the caller's scene-update thread.
    std::vector<StreamJob> streams;
    std::vector<FailedStream> failedStreams;

    void Store(std::map<std::string, CacheEntry>& cache, const std::string& key, std::string data,
               std::vector<Stamp> dependencies, uint64 expectedGeneration) {
        std::lock_guard lock(mutex);
        if (generation != expectedGeneration || data.size() > MaxCacheBytes)
            return;
        if (const auto found = cache.find(key); found != cache.end()) {
            cacheBytes -= found->second.data.size();
            cache.erase(found);
        }
        cacheBytes += data.size();
        cache[key] = {std::move(data), std::move(dependencies), ++sequence};
        while (cacheBytes > MaxCacheBytes || rawCache.size() + preloadCache.size() > 32) {
            auto* oldestCache = &rawCache;
            auto oldest = rawCache.end();
            for (auto* candidate : {&rawCache, &preloadCache})
                for (auto it = candidate->begin(); it != candidate->end(); ++it)
                    if (oldest == oldestCache->end() || it->second.used < oldest->second.used) {
                        oldestCache = candidate;
                        oldest = it;
                    }
            if (oldest == oldestCache->end())
                break;
            cacheBytes -= oldest->second.data.size();
            oldestCache->erase(oldest);
        }
    }

    std::string Read(const fs::path& path, LoadContext& context) {
        const auto stamp = FileStamp(path);
        context.bytes += stamp.bytes;
        if (context.bytes > MaxLoadBytes)
            throw std::runtime_error("씬 종속성의 합계 읽기 한도 128 MiB를 넘었습니다.");
        context.dependencies.push_back(stamp);
        const auto key = Utf8(path);
        uint64 currentGeneration = 0;
        {
            std::lock_guard lock(mutex);
            if (!context.cacheGeneration)
                context.cacheGeneration = generation;
            currentGeneration = *context.cacheGeneration;
            const auto found = rawCache.find(key);
            if (!context.force && found != rawCache.end() &&
                found->second.dependencies.front().writeTime == stamp.writeTime &&
                found->second.dependencies.front().bytes == stamp.bytes) {
                found->second.used = ++sequence;
                return found->second.data;
            }
        }
        std::ifstream file(path, std::ios::binary);
        if (!file)
            throw std::runtime_error("씬 파일을 읽을 수 없습니다: " + key);
        std::string data(static_cast<size_t>(stamp.bytes), '\0');
        if (!data.empty())
            file.read(data.data(), static_cast<std::streamsize>(data.size()));
        if (!file || file.peek() != std::char_traits<char>::eof())
            throw std::runtime_error("씬 파일을 읽는 동안 파일이 변경되었습니다.");
        if (Matches(stamp))
            Store(rawCache, key, data, {stamp}, currentGeneration);
        return data;
    }

    std::shared_ptr<Scene> LoadResolved(const fs::path& requested, LoadContext& context);
    void RefreshOne(Scene& target, uint32 root, Scene& source, bool reset = false);
    SceneLoadResult LoadTop(const fs::path& requested, bool preload,
                            std::optional<uint64> submittedGeneration = std::nullopt);
};

void SceneManager::State::RefreshOne(Scene& target, uint32 root, Scene& source, bool reset) {
    if (!target.HasComponent<SceneReferenceComponent>(root))
        throw std::runtime_error("씬 참조 루트를 찾을 수 없습니다.");
    if (!target.GetComponent<SceneReferenceComponent>(root).loaded)
        RestoreSubtree(target, root);
    auto reference = target.GetComponent<SceneReferenceComponent>(root);
    const auto oldMap = ReadMap(reference);
    if (reset) {
        std::unordered_set<uint32> retained;
        for (const auto& [sourceId, localId] : oldMap)
            if (source.IsAlive(sourceId) && target.IsAlive(localId) &&
                IsDescendant(target, localId, root))
                retained.insert(localId);
        std::vector<uint32> removed;
        for (const auto entity : target.GetEntities())
            if (entity.GetId() != root && IsDescendant(target, entity.GetId(), root) &&
                !retained.contains(entity.GetId()))
                removed.push_back(entity.GetId());
        for (const auto localId : retained)
            if (!target.SetParent(localId, root, false))
                throw std::runtime_error("프리팹 계층을 초기화할 수 없습니다.");
        for (const auto localId : removed)
            if (target.IsAlive(localId))
                target.DestroyEntity(Entity(localId, &target));
    }
    Scene previous;
    const bool hasBaseline = !reference.baseline.empty();
    if (hasBaseline && !previous.DeserializeFromString(reference.baseline))
        throw std::runtime_error("씬 상속의 이전 기준 상태를 읽을 수 없습니다.");
    IdMap localReferences, sourceReferences;
    for (const auto& [sourceId, localId] : oldMap) {
        localReferences[localId] = sourceId;
        sourceReferences[sourceId] = sourceId;
    }
    std::unordered_map<uint32, bool> untouched;
    std::unordered_set<uint32> protectedReferences;
    for (const auto& [sourceId, localId] : oldMap) {
        if (!target.IsAlive(localId) || !previous.IsAlive(sourceId))
            continue;
        const bool same = SameEntity(target, localId, previous, sourceId, oldMap, localReferences,
                                     sourceReferences, root);
        untouched[sourceId] = same;
        const auto* nested = target.TryGetComponent<SceneReferenceComponent>(localId);
        if (nested && (!nested->loaded || !same))
            protectedReferences.insert(sourceId);
    }
    // Deletions are overrides: an entity that existed in the old source but no longer has a
    // mapping was deliberately removed locally. New descendants of such a parent stay removed.
    std::unordered_set<uint32> skipped;
    for (auto entity : source.GetEntities()) {
        const auto id = entity.GetId();
        for (auto parent = id; parent != 0; parent = source.GetParent(parent)) {
            if (!reset && ((previous.IsAlive(parent) && !oldMap.contains(parent)) ||
                           (parent != id && protectedReferences.contains(parent)))) {
                skipped.insert(id);
                break;
            }
        }
    }
    std::unordered_set<uint32> removeLocals;
    for (const auto& [sourceId, localId] : oldMap)
        if (!source.IsAlive(sourceId) && target.IsAlive(localId) && untouched[sourceId])
            removeLocals.insert(localId);
    for (const auto local : removeLocals) {
        if (!target.IsAlive(local))
            continue;
        auto parent = target.GetParent(local);
        while (parent != 0 && removeLocals.contains(parent))
            parent = target.GetParent(parent);
        for (const auto child : target.GetChildren(local))
            if (!removeLocals.contains(child) && !target.SetParent(child, parent, true))
                throw std::runtime_error(
                    "상속된 부모를 제거하는 동안 자식 상태를 보존할 수 없습니다.");
        target.DestroyEntity(Entity(local, &target));
    }
    IdMap mapping;
    for (const auto& [sourceId, localId] : oldMap)
        if (source.IsAlive(sourceId) && target.IsAlive(localId))
            mapping[sourceId] = localId;
        else if (target.IsAlive(localId)) {
            if (const auto* provenance = target.TryGetComponent<InstanceSourceComponent>(localId);
                provenance && provenance->instanceRoot == root)
                target.RemoveComponent<InstanceSourceComponent>(localId);
        }
    for (auto entity : source.GetEntities())
        if (!mapping.contains(entity.GetId()) && !skipped.contains(entity.GetId()))
            mapping[entity.GetId()] = target.CreateEntity().GetId();

    std::vector<uint32> copied;
    for (auto entity : source.GetEntities()) {
        const auto sourceId = entity.GetId();
        if (skipped.contains(sourceId) || !mapping.contains(sourceId))
            continue;
        const bool added = !oldMap.contains(sourceId);
        if (!reset && !added && !untouched[sourceId])
            continue;
        const auto localId = mapping.at(sourceId);
        std::optional<SceneReferenceComponent> unloaded;
        if (const auto* nested = target.TryGetComponent<SceneReferenceComponent>(localId);
            nested && !nested->loaded)
            unloaded = *nested;
        if (!target.CopyEntityComponents(source, sourceId, localId))
            throw std::runtime_error("참조된 씬의 엔티티를 복사할 수 없습니다.");
        if (auto* script = target.TryGetComponent<ScriptComponent>(localId))
            for (auto& [name, value] : script->fields)
                if (auto* reference = std::get_if<ScriptEntityReference>(&value)) {
                    const auto found = mapping.find(reference->id);
                    reference->id = found == mapping.end() ? 0 : found->second;
                }
        if (unloaded) {
            auto& nested = target.GetComponent<SceneReferenceComponent>(localId);
            nested.loaded = false;
            nested.baseline = std::move(unloaded->baseline);
            nested.suspended = std::move(unloaded->suspended);
            nested.entityMap = std::move(unloaded->entityMap);
        }
        const auto parent = source.GetParent(sourceId);
        const auto* sourceHierarchy = source.TryGetComponent<HierarchyComponent>(sourceId);
        auto* hierarchy = target.TryGetComponent<HierarchyComponent>(localId);
        if (!hierarchy)
            hierarchy = &target.AddComponent<HierarchyComponent>(localId);
        hierarchy->parent = parent != 0 && mapping.contains(parent) ? mapping.at(parent) : root;
        hierarchy->group = sourceHierarchy && sourceHierarchy->group;
        auto* provenance = target.TryGetComponent<InstanceSourceComponent>(localId);
        if (!provenance)
            provenance = &target.AddComponent<InstanceSourceComponent>(localId);
        const auto* sourceProvenance = source.TryGetComponent<InstanceSourceComponent>(sourceId);
        if (sourceProvenance && mapping.contains(sourceProvenance->instanceRoot))
            *provenance = {mapping.at(sourceProvenance->instanceRoot),
                           sourceProvenance->sourceEntity};
        else
            *provenance = {root, sourceId};
        copied.push_back(localId);
    }
    for (const auto local : copied)
        if (auto* nested = target.TryGetComponent<SceneReferenceComponent>(local))
            RemapReference(*nested, mapping);
    if (reference.mode == SceneReferenceMode::Inherited && !HasReferenceAncestor(target, root))
        if (!target.SetSettings(hasBaseline ? InheritedSettings(target.GetSettings(),
                                                                previous.GetSettings(),
                                                                source.GetSettings())
                                            : source.GetSettings()))
            throw std::runtime_error("상속된 씬 설정을 적용할 수 없습니다.");
    if (!source.SerializeToString(reference.baseline))
        throw std::runtime_error("참조된 씬의 기준 상태를 저장할 수 없습니다.");
    reference.loaded = true;
    reference.suspended.clear();
    std::erase_if(mapping, [&](const auto& item) {
        return !target.IsAlive(item.second) || !IsDescendant(target, item.second, root);
    });
    WriteMap(reference, mapping);
    target.GetComponent<SceneReferenceComponent>(root) = std::move(reference);
    PruneMaps(target);
}

std::shared_ptr<Scene> SceneManager::State::LoadResolved(const fs::path& requested,
                                                         LoadContext& context) {
    const auto path = Canonical(requested);
    if (context.stack.size() >= MaxDepth)
        throw std::runtime_error("씬 참조의 중첩 한도 16단계를 넘었습니다.");
    for (const auto& ancestor : context.stack)
        if (SamePath(ancestor, path))
            throw std::runtime_error("씬 참조에 순환 관계가 있습니다: " + Utf8(path));
    context.stack.push_back(path);
    try {
        auto result = std::make_shared<Scene>();
        if (!result->DeserializeFromString(Read(path, context)))
            throw std::runtime_error("씬 파일 형식이 올바르지 않습니다: " + Utf8(path));
        if (PrefabLibrary::IsPrefabPath(path) && result->GetChildren(0).size() != 1)
            throw std::runtime_error("프리팹은 최상위 오브젝트 하나를 포함해야 합니다.");
        result->SetSourcePath(Utf8(path));
        RewritePaths(*result, path, nullptr);
        ValidateInheritance(*result);
        for (const auto root : ReferenceRoots(*result)) {
            if (!result->IsAlive(root))
                continue;
            const auto reference = result->GetComponent<SceneReferenceComponent>(root);
            if (HasReferenceAncestor(*result, root) || !reference.loaded)
                continue;
            if (++context.references > MaxReferences)
                throw std::runtime_error("씬 참조 수가 128개를 넘었습니다.");
            const auto source = LoadResolved(Path(reference.path), context);
            RefreshOne(*result, root, *source);
        }
        std::string verify;
        if (result->GetEntityCount() > MaxEntities || !result->SerializeToString(verify))
            throw std::runtime_error("합성 씬이 저장 가능한 크기 한도를 넘었습니다.");
        context.stack.pop_back();
        return result;
    } catch (...) {
        context.stack.pop_back();
        throw;
    }
}

SceneLoadResult SceneManager::State::LoadTop(const fs::path& requested, bool preload,
                                             std::optional<uint64> submittedGeneration) {
    try {
        uint64 currentGeneration = 0;
        {
            std::lock_guard lock(mutex);
            currentGeneration = submittedGeneration.value_or(generation);
        }
        const auto path = Canonical(requested);
        const auto key = Utf8(path);
        if (!preload) {
            CacheEntry candidate;
            {
                std::lock_guard lock(mutex);
                if (const auto found = preloadCache.find(key); found != preloadCache.end()) {
                    candidate = found->second;
                    found->second.used = ++sequence;
                }
            }
            if (!candidate.data.empty() && std::all_of(candidate.dependencies.begin(),
                                                       candidate.dependencies.end(), Matches)) {
                auto scene = std::make_shared<Scene>();
                if (scene->DeserializeFromString(candidate.data)) {
                    scene->SetSourcePath(key);
                    return {std::move(scene), {}};
                }
            }
        }
        LoadContext context;
        context.force = preload;
        context.cacheGeneration = currentGeneration;
        auto scene = LoadResolved(path, context);
        if (preload) {
            std::string snapshot;
            if (!scene->SerializeToString(snapshot))
                throw std::runtime_error("프리로드한 씬 상태를 보관할 수 없습니다.");
            Store(preloadCache, key, std::move(snapshot), std::move(context.dependencies),
                  currentGeneration);
        }
        return {std::move(scene), {}};
    } catch (const std::exception& error) {
        return {nullptr, error.what()};
    } catch (...) {
        return {nullptr, "씬 로드 중 알 수 없는 오류가 발생했습니다."};
    }
}

SceneManager::SceneManager() : state_(std::make_shared<State>()) {}

SceneManager::~SceneManager() = default;

SceneLoadResult SceneManager::Load(const fs::path& path) {
    return state_->LoadTop(path, false);
}

std::shared_ptr<Scene> SceneManager::Load(const fs::path& path, std::string& error) {
    auto result = Load(path);
    error = std::move(result.error);
    return std::move(result.scene);
}

std::future<SceneLoadResult> SceneManager::LoadAsync(const fs::path& path) {
    auto promise = std::make_shared<std::promise<SceneLoadResult>>();
    auto future = promise->get_future();
    fs::path submitted;
    try {
        if (path.native().find(fs::path::value_type{}) != fs::path::string_type::npos)
            throw std::runtime_error("씬 경로에 NUL 문자를 사용할 수 없습니다.");
        submitted = fs::absolute(path).lexically_normal();
    } catch (const std::exception& error) {
        promise->set_value({nullptr, error.what()});
        return future;
    }
    const auto state = state_;
    uint64 submittedGeneration = 0;
    {
        std::lock_guard lock(state->mutex);
        submittedGeneration = state->generation;
    }
    if (state->activeJobs.fetch_add(1) >= 8) {
        --state->activeJobs;
        promise->set_value({nullptr, "동시에 로드할 수 있는 씬은 8개입니다.", true});
        return future;
    }
    try {
        std::thread([state, submitted, promise, submittedGeneration] {
            auto result = state->LoadTop(submitted, false, submittedGeneration);
            --state->activeJobs;
            promise->set_value(std::move(result));
        }).detach();
    } catch (const std::exception& error) {
        --state->activeJobs;
        promise->set_value({nullptr, error.what()});
    }
    return future;
}

bool SceneManager::Preload(const fs::path& path, std::string& error) {
    const auto result = state_->LoadTop(path, true);
    error = result.error;
    return static_cast<bool>(result);
}

std::future<SceneLoadResult> SceneManager::PreloadAsync(const fs::path& path) {
    auto promise = std::make_shared<std::promise<SceneLoadResult>>();
    auto future = promise->get_future();
    fs::path submitted;
    try {
        if (path.native().find(fs::path::value_type{}) != fs::path::string_type::npos)
            throw std::runtime_error("씬 경로에 NUL 문자를 사용할 수 없습니다.");
        submitted = fs::absolute(path).lexically_normal();
    } catch (const std::exception& error) {
        promise->set_value({nullptr, error.what()});
        return future;
    }
    const auto state = state_;
    uint64 submittedGeneration = 0;
    {
        std::lock_guard lock(state->mutex);
        submittedGeneration = state->generation;
    }
    if (state->activeJobs.fetch_add(1) >= 8) {
        --state->activeJobs;
        promise->set_value({nullptr, "동시에 로드할 수 있는 씬은 8개입니다.", true});
        return future;
    }
    try {
        std::thread([state, submitted, promise, submittedGeneration] {
            auto result = state->LoadTop(submitted, true, submittedGeneration);
            --state->activeJobs;
            promise->set_value(std::move(result));
        }).detach();
    } catch (const std::exception& error) {
        --state->activeJobs;
        promise->set_value({nullptr, error.what()});
    }
    return future;
}

bool SceneManager::IsPreloaded(const fs::path& path) const {
    try {
        const auto key = Utf8(Canonical(path));
        std::vector<Stamp> dependencies;
        {
            std::lock_guard lock(state_->mutex);
            const auto found = state_->preloadCache.find(key);
            if (found == state_->preloadCache.end())
                return false;
            dependencies = found->second.dependencies;
        }
        return std::all_of(dependencies.begin(), dependencies.end(), Matches);
    } catch (...) {
        return false;
    }
}

bool SceneManager::Unload(const fs::path& path) {
    try {
        const auto canonical = Canonical(path);
        std::lock_guard lock(state_->mutex);
        ++state_->generation;
        bool removed = false;
        for (auto* cache : {&state_->rawCache, &state_->preloadCache})
            for (auto it = cache->begin(); it != cache->end();) {
                const bool contains =
                    it->first == Utf8(canonical) ||
                    std::any_of(it->second.dependencies.begin(), it->second.dependencies.end(),
                                [&](const auto& dependency) {
                                    return SamePath(dependency.path, canonical);
                                });
                if (contains) {
                    state_->cacheBytes -= it->second.data.size();
                    it = cache->erase(it);
                    removed = true;
                } else
                    ++it;
            }
        return removed;
    } catch (...) {
        return false;
    }
}

void SceneManager::ClearCache() {
    std::lock_guard lock(state_->mutex);
    ++state_->generation;
    state_->cacheBytes = 0;
    state_->rawCache.clear();
    state_->preloadCache.clear();
}

uint32 SceneManager::AddSceneInstance(Scene& target, const fs::path& path, SceneReferenceMode mode,
                                      std::string& error, uint32 parent) {
    error.clear();
    try {
        if (mode != SceneReferenceMode::Nested && mode != SceneReferenceMode::Inherited &&
            mode != SceneReferenceMode::Additive)
            throw std::runtime_error("씬 참조 방식이 올바르지 않습니다.");
        if (parent != 0 && !target.IsAlive(parent))
            throw std::runtime_error("씬 인스턴스의 부모를 찾을 수 없습니다.");
        if (mode == SceneReferenceMode::Inherited) {
            if (parent != 0)
                throw std::runtime_error("상속 씬의 기준은 최상위에 배치해야 합니다.");
            for (const auto root : ReferenceRoots(target))
                if (!HasReferenceAncestor(target, root) &&
                    target.GetComponent<SceneReferenceComponent>(root).mode == mode)
                    throw std::runtime_error("상속 씬의 기준은 하나만 지정할 수 있습니다.");
        }
        auto context = ContextFor(target);
        {
            std::lock_guard lock(state_->mutex);
            context.cacheGeneration = state_->generation;
        }
        const auto sourcePath = Canonical(path, Path(target.GetSourcePath()));
        auto source = state_->LoadResolved(sourcePath, context);
        auto staged = Clone(target);
        const auto root = staged->CreateEntity(Utf8(sourcePath.stem())).GetId();
        auto& hierarchy = staged->AddComponent<HierarchyComponent>(root);
        hierarchy.group = true;
        hierarchy.parent = parent;
        SceneReferenceComponent reference;
        reference.path = Utf8(sourcePath);
        reference.mode = mode;
        staged->AddComponent<SceneReferenceComponent>(root, std::move(reference));
        state_->RefreshOne(*staged, root, *source);
        Commit(target, *staged);
        return root;
    } catch (const std::exception& failure) {
        error = failure.what();
        return 0;
    }
}

bool SceneManager::RefreshInstances(Scene& target, std::string& error) {
    error.clear();
    try {
        const auto roots = ReferenceRoots(target);
        if (std::none_of(roots.begin(), roots.end(), [&](uint32 root) {
                return target.GetComponent<SceneReferenceComponent>(root).loaded;
            }))
            return true;
        auto context = ContextFor(target);
        {
            std::lock_guard lock(state_->mutex);
            context.cacheGeneration = state_->generation;
        }
        auto staged = Clone(target);
        for (const auto root : ReferenceRoots(*staged)) {
            if (!staged->IsAlive(root))
                continue;
            const auto reference = staged->GetComponent<SceneReferenceComponent>(root);
            if (!reference.loaded)
                continue;
            if (++context.references > MaxReferences)
                throw std::runtime_error("씬 참조 수가 128개를 넘었습니다.");
            auto source = state_->LoadResolved(
                Canonical(Path(reference.path), Path(target.GetSourcePath())), context);
            state_->RefreshOne(*staged, root, *source);
        }
        Commit(target, *staged);
        return true;
    } catch (const std::exception& failure) {
        error = failure.what();
        return false;
    }
}

bool SceneManager::RefreshInstance(Scene& target, uint32 root, std::string& error) {
    error.clear();
    try {
        const auto* reference = target.TryGetComponent<SceneReferenceComponent>(root);
        if (!reference)
            throw std::runtime_error("씬 참조 루트를 선택해 주세요.");
        auto context = ContextFor(target);
        {
            std::lock_guard lock(state_->mutex);
            context.cacheGeneration = state_->generation;
        }
        const auto path = Canonical(Path(reference->path), Path(target.GetSourcePath()));
        auto source = state_->LoadResolved(path, context);
        auto staged = Clone(target);
        state_->RefreshOne(*staged, root, *source);
        Commit(target, *staged);
        return true;
    } catch (const std::exception& failure) {
        error = failure.what();
        return false;
    }
}

bool SceneManager::RevertInstance(Scene& target, uint32 root, std::string& error) {
    error.clear();
    try {
        const auto* reference = target.TryGetComponent<SceneReferenceComponent>(root);
        if (!reference)
            throw std::runtime_error("씬 참조 루트를 선택해 주세요.");
        auto context = ContextFor(target);
        {
            std::lock_guard lock(state_->mutex);
            context.cacheGeneration = state_->generation;
        }
        const auto path = Canonical(Path(reference->path), Path(target.GetSourcePath()));
        auto source = state_->LoadResolved(path, context);
        auto staged = Clone(target);
        state_->RefreshOne(*staged, root, *source, true);
        Commit(target, *staged);
        return true;
    } catch (const std::exception& failure) {
        error = failure.what();
        return false;
    }
}

bool SceneManager::UnloadInstance(Scene& target, uint32 root, std::string& error) {
    error.clear();
    try {
        const auto* reference = target.TryGetComponent<SceneReferenceComponent>(root);
        if (!reference)
            throw std::runtime_error("씬 참조 루트를 선택해 주세요.");
        if (!reference->loaded)
            return true;
        auto staged = Clone(target);
        UnloadSubtree(*staged, root);
        Commit(target, *staged);
        return true;
    } catch (const std::exception& failure) {
        error = failure.what();
        return false;
    }
}

bool SceneManager::ConfigureStreaming(Scene& target, uint32 root, Vec2 center, float loadRadius,
                                      float unloadRadius, std::string& error) {
    error.clear();
    try {
        if (!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(loadRadius) ||
            !std::isfinite(unloadRadius) || loadRadius < 0 || unloadRadius < loadRadius ||
            unloadRadius > 1000000 || (loadRadius == 0 && unloadRadius != 0))
            throw std::runtime_error(
                "스트리밍 반경은 0 이상이며 언로드 반경은 로드 반경 이상이어야 합니다.");
        if (!target.HasComponent<SceneReferenceComponent>(root))
            throw std::runtime_error("씬 참조 루트를 선택해 주세요.");
        auto staged = Clone(target);
        auto& reference = staged->GetComponent<SceneReferenceComponent>(root);
        reference.streamCenter = center;
        reference.streamLoadRadius = loadRadius;
        reference.streamUnloadRadius = unloadRadius;
        Commit(target, *staged);
        return true;
    } catch (const std::exception& failure) {
        error = failure.what();
        return false;
    }
}

bool SceneManager::UpdateStreaming(Scene& target, Vec2 observer, std::string& error,
                                   bool* changed) {
    error.clear();
    if (changed)
        *changed = false;
    try {
        if (!std::isfinite(observer.x) || !std::isfinite(observer.y))
            throw std::runtime_error("스트리밍 관찰 좌표가 올바르지 않습니다.");
        std::erase_if(state_->streams, [&](const auto& job) {
            return job.lifetime.expired() ||
                   (job.owner == &target &&
                    !target.HasComponent<SceneReferenceComponent>(job.root));
        });
        std::erase_if(state_->failedStreams, [&](const auto& failure) {
            return failure.lifetime.expired() ||
                   (failure.owner == &target &&
                    !target.HasComponent<SceneReferenceComponent>(failure.root));
        });
        std::vector<uint32> unloads;
        std::vector<std::pair<uint32, std::shared_ptr<Scene>>> loads;
        for (const auto root : ReferenceRoots(target)) {
            const auto& reference = target.GetComponent<SceneReferenceComponent>(root);
            const auto streamMatches = [&](const auto& job) {
                return job.owner == &target && job.root == root;
            };
            const double dx = static_cast<double>(reference.streamCenter.x) - observer.x;
            const double dy = static_cast<double>(reference.streamCenter.y) - observer.y;
            const double distanceSquared = dx * dx + dy * dy;
            const bool enabled = reference.streamLoadRadius > 0;
            const bool inside =
                enabled && distanceSquared <= static_cast<double>(reference.streamLoadRadius) *
                                                  reference.streamLoadRadius;
            const bool outside =
                enabled && distanceSquared > static_cast<double>(reference.streamUnloadRadius) *
                                                 reference.streamUnloadRadius;
            if (!enabled || (!reference.loaded && !inside)) {
                std::erase_if(state_->streams, streamMatches);
                std::erase_if(state_->failedStreams, streamMatches);
                continue;
            }
            if (reference.loaded) {
                if (outside)
                    unloads.push_back(root);
                continue;
            }
            const auto path = Canonical(Path(reference.path), Path(target.GetSourcePath()));
            const auto pathText = Utf8(path);
            auto pending =
                std::find_if(state_->streams.begin(), state_->streams.end(), streamMatches);
            if (pending != state_->streams.end() && pending->path != pathText) {
                state_->streams.erase(pending);
                pending = state_->streams.end();
            }
            if (pending == state_->streams.end()) {
                const auto failed = std::find_if(state_->failedStreams.begin(),
                                                 state_->failedStreams.end(), streamMatches);
                if (failed != state_->failedStreams.end() && failed->path == pathText &&
                    std::chrono::steady_clock::now() < failed->retry)
                    continue;
                std::erase_if(state_->failedStreams, streamMatches);
                if (state_->activeJobs.load() >= 8)
                    continue;
                state_->streams.push_back(
                    {&target, target.Lifetime(), root, pathText, LoadAsync(path)});
                continue;
            }
            if (pending->future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                continue;
            auto loaded = pending->future.get();
            state_->streams.erase(pending);
            if (!loaded) {
                if (loaded.retryable)
                    continue;
                state_->failedStreams.push_back(
                    {&target, target.Lifetime(), root, pathText,
                     std::chrono::steady_clock::now() + std::chrono::seconds(3)});
                error = loaded.error;
                continue;
            }
            // Async results cannot know the caller's scene graph. Check its origin again before
            // attaching the result, including an indirect source reference back to this scene.
            if (!target.GetSourcePath().empty()) {
                const auto origin = Canonical(Path(target.GetSourcePath()));
                if (SamePath(origin, path))
                    throw std::runtime_error("씬 스트리밍 참조에 순환 관계가 있습니다.");
                for (const auto dependency : ReferenceRoots(*loaded.scene))
                    if (SamePath(origin,
                                 Canonical(Path(
                                     loaded.scene->GetComponent<SceneReferenceComponent>(dependency)
                                         .path))))
                        throw std::runtime_error("씬 스트리밍 참조에 순환 관계가 있습니다.");
            }
            loads.emplace_back(root, std::move(loaded.scene));
        }
        if (!error.empty())
            return false;
        if (loads.empty() && unloads.empty())
            return true;
        auto staged = Clone(target);
        for (const auto root : unloads)
            if (staged->IsAlive(root))
                UnloadSubtree(*staged, root);
        for (const auto& [root, source] : loads)
            if (staged->IsAlive(root))
                state_->RefreshOne(*staged, root, *source);
        Commit(target, *staged);
        if (changed)
            *changed = true;
        return true;
    } catch (const std::exception& failure) {
        error = failure.what();
        return false;
    }
}

bool SceneManager::Save(Scene& scene, const fs::path& targetPath, const fs::path& previousPath,
                        std::string& error) {
    error.clear();
    try {
        const auto destination = Canonical(targetPath);
        const auto owner = !previousPath.empty()            ? Canonical(previousPath)
                           : !scene.GetSourcePath().empty() ? Canonical(Path(scene.GetSourcePath()))
                                                            : destination;
        ValidateInheritance(scene);
        std::vector<fs::path> dependencies{destination};
        std::uintmax_t bytes = 0;
        size_t references = 0;
        std::set<std::string> validated;
        CheckSaveDependencies(scene, owner, dependencies, bytes, references, validated);
        auto diskCopy = Clone(scene);
        RewritePaths(*diskCopy, owner, &destination);
        if (!diskCopy->Serialize(Utf8(destination))) {
            error = Scene::GetLastSaveError();
            if (error.empty())
                error = "씬 파일을 저장할 수 없습니다.";
            return false;
        }
        return true;
    } catch (const std::exception& failure) {
        error = failure.what();
        return false;
    }
}

SceneLoadResult::operator bool() const noexcept {
    return scene != nullptr;
}
} // namespace LuxEngine
