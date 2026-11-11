#pragma once
#include "Engine/Animation/AnimationEngine.hpp"
#include "Engine/Core/Core.hpp"
#include "Engine/ECS/ECS.hpp"
#include "Engine/Physics/PhysicsEngine.hpp"
#include <algorithm>
#include <iosfwd>
#include <stdexcept>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace LuxEngine {
class Entity;
class PhysicsWorld2D;

struct SceneSettings {
    Vec4 clearColor{.12f, .14f, .16f, 1};
    float exposure = 1;
    Vec2 gravity{0, -9.81f};
    Vec3 gravity3D{0, -9.81f, 0};
    // Existing framework applications can continue setting PhysicsEngine's global gravity.
    bool overrideGravity = false;
    float fixedStep = 1.0f / 60.0f;
    uint32 physicsSubsteps = 1;
};

class LUX_API Scene {
public:
    Scene();
    ~Scene();
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&) = delete;
    Scene& operator=(Scene&&) = delete;
    std::vector<Entity> GetEntities();
    std::vector<Entity> GetEntitiesInHierarchyOrder();
    // Stable ascending Z order within each 2D pass: tilemaps, then sprites.
    std::vector<Entity> GetEntitiesIn2DLayerOrder();
    Entity CreateEntity(const std::string& name = "Entity");
    void DestroyEntity(Entity entity);
    bool SetParent(uint32 child, uint32 parent = 0, bool keepWorld = true);
    uint32 GetParent(uint32 child) const;
    std::vector<uint32> GetChildren(uint32 parent) const;
    Mat4 GetWorldMatrix(uint32 id) const;
    TransformComponent GetWorldTransform(uint32 id) const;
    bool SetWorldTransform(uint32 id, const TransformComponent& value);
    bool ValidateHierarchy() const;
    std::unordered_map<uint32, uint32>
    ImportEntities(const Scene& source, const std::vector<uint32>& roots = {}, uint32 parent = 0);
    // Replace built-in authored data while preserving the destination's hierarchy/provenance.
    bool CopyEntityComponents(const Scene& source, uint32 sourceId, uint32 targetId);
    // Commit validated authored data while retaining unchanged native physics bodies/runtime.
    // Both scenes' previously obtained entity handles are invalidated; the target origin stays.
    bool ApplyState(Scene& prepared);
    const SceneSettings& GetSettings() const noexcept {
        return settings_;
    }
    bool SetSettings(const SceneSettings& settings);
    const std::string& GetSourcePath() const noexcept {
        return sourcePath_;
    }
    void SetSourcePath(std::string path) {
        sourcePath_ = std::move(path);
    }
    void OnUpdate(double dt);
    void OnRender();
    void OnRender3D();
    std::string ResolveAssetPath(const std::string& path) const;
    // BeginContact events from the most recent 2D physics step, including sensors.
    const std::vector<Collision2DEvent>& GetCollisionEvents2D() const {
        return collisionEvents2D_;
    }
    enum class SaveFault {
        None,
        TempWriteFailure,
        DiskFull,
        ReplaceAccessDenied,
        ReplaceFailedAfterBackup,
        BackupRestoreFailure
    };
    static void InjectSaveFault(SaveFault fault, uint32 count = 1);
    static void ClearSaveFaults();
    static std::string GetLastSaveError();

    bool Serialize(const std::string& filepath);
    bool Deserialize(const std::string& filepath);
    bool SerializeToString(std::string& out);
    bool DeserializeFromString(const std::string& data);
    uint32 GetEntityCount() const {
        return static_cast<uint32>(entities_.size());
    }
    bool IsAlive(uint32 id) const {
        return entities_.contains(id);
    }
    std::weak_ptr<int> Lifetime() const {
        return lifetime_;
    }
    template <class T, class... Args> T& AddComponent(uint32 id, Args&&... args) {
        RequireAlive(id);
        auto& values = GetPool<T>().values;
        auto [it, added] = values.try_emplace(id, T{std::forward<Args>(args)...});
        if (!added)
            throw std::logic_error("Component already exists");
        return it->second;
    }
    template <class T> T& GetComponent(uint32 id) {
        return const_cast<T&>(std::as_const(*this).GetComponent<T>(id));
    }
    template <class T> const T& GetComponent(uint32 id) const {
        RequireAlive(id);
        auto it = pools_.find(typeid(T));
        if (it == pools_.end())
            throw std::out_of_range("Component does not exist");
        return static_cast<const Pool<T>&>(*it->second).values.at(id);
    }
    template <class T> T* TryGetComponent(uint32 id) {
        return const_cast<T*>(std::as_const(*this).TryGetComponent<T>(id));
    }
    template <class T> const T* TryGetComponent(uint32 id) const {
        if (!IsAlive(id))
            return nullptr;
        auto it = pools_.find(typeid(T));
        if (it == pools_.end())
            return nullptr;
        auto& values = static_cast<const Pool<T>&>(*it->second).values;
        auto component = values.find(id);
        return component == values.end() ? nullptr : &component->second;
    }
    template <class T> bool HasComponent(uint32 id) const {
        auto it = pools_.find(typeid(T));
        return IsAlive(id) && it != pools_.end() &&
               static_cast<const Pool<T>&>(*it->second).values.contains(id);
    }
    // Query a component pool directly without allocating/sorting every entity in the scene.
    template <class T> std::vector<uint32> GetComponentEntityIds() const {
        const auto found = pools_.find(typeid(T));
        if (found == pools_.end())
            return {};
        const auto& values = static_cast<const Pool<T>&>(*found->second).values;
        std::vector<uint32> result;
        result.reserve(values.size());
        for (const auto& [id, component] : values)
            if (IsAlive(id))
                result.push_back(id);
        std::sort(result.begin(), result.end());
        return result;
    }
    template <class T> void RemoveComponent(uint32 id) {
        RequireAlive(id);
        if (!HasComponent<T>(id))
            return;
        auto it = pools_.find(typeid(T));
        if (it != pools_.end()) {
            it->second->Erase(id);
            if constexpr (std::is_same_v<T, TransformComponent> ||
                          std::is_same_v<T, Rigidbody2DComponent> ||
                          std::is_same_v<T, BoxCollider2DComponent>)
                RemovePhysicsBody2D(id);
        }
    }

private:
    struct PoolBase {
        virtual ~PoolBase() = default;
        virtual void Erase(uint32 id) = 0;
        virtual bool Empty() const = 0;
        virtual bool Contains(uint32 id) const = 0;
    };
    template <class T> struct Pool final : PoolBase {
        std::unordered_map<uint32, T> values;
        void Erase(uint32 id) override {
            values.erase(id);
        }
        bool Empty() const override {
            return values.empty();
        }
        bool Contains(uint32 id) const override {
            return values.contains(id);
        }
    };
    template <class T> Pool<T>& GetPool() {
        auto it = pools_.find(typeid(T));
        if (it == pools_.end())
            it = pools_.emplace(typeid(T), std::make_unique<Pool<T>>()).first;
        return static_cast<Pool<T>&>(*it->second);
    }
    void RequireAlive(uint32 id) const {
        if (!IsAlive(id))
            throw std::out_of_range("Entity is not alive");
    }
    bool DeserializeStream(std::istream& in);
    void RepairReferenceLinks();
    void RemovePhysicsBody2D(uint32 id);
    uint32 nextEntityId_ = 1;
    std::shared_ptr<int> lifetime_ = std::make_shared<int>(0);
    std::unordered_set<uint32> entities_;
    std::unordered_map<std::type_index, std::unique_ptr<PoolBase>> pools_;
    std::unique_ptr<PhysicsWorld2D> physics2D_;
    std::vector<Collision2DEvent> collisionEvents2D_;
    std::vector<uint32> updateBuffer_;
    SceneSettings settings_;
    std::string sourcePath_;
    double physicsAccumulator_ = 0;
};
} // namespace LuxEngine
