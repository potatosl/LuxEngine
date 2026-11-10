#pragma once

#include "Engine/Scene/Scene.hpp"
#include <filesystem>
#include <future>
#include <memory>
#include <string>

namespace LuxEngine {

struct SceneLoadResult {
    std::shared_ptr<Scene> scene;
    std::string error;
    bool retryable = false;
    explicit operator bool() const noexcept {
        return scene != nullptr;
    }
};

// File loading and composition are CPU-only. Async jobs own their inputs and never access Qt,
// a GPU device, or an editor document. Returned scenes are independent mutable instances.
// Futures are promise-backed: discarding one never joins a worker on the UI thread.
class LUX_API SceneManager final {
public:
    SceneManager();
    ~SceneManager();
    SceneManager(const SceneManager&) = delete;
    SceneManager& operator=(const SceneManager&) = delete;

    SceneLoadResult Load(const std::filesystem::path& path);
    std::shared_ptr<Scene> Load(const std::filesystem::path& path, std::string& error);
    std::future<SceneLoadResult> LoadAsync(const std::filesystem::path& path);
    bool Preload(const std::filesystem::path& path, std::string& error);
    std::future<SceneLoadResult> PreloadAsync(const std::filesystem::path& path);
    bool IsPreloaded(const std::filesystem::path& path) const;
    bool Unload(const std::filesystem::path& path);
    void ClearCache();

    // A stable container owns imported entities. Repeating Nested creates independent instances;
    // Additive containers can be unloaded independently. Inherited propagates untouched entities
    // and scene settings while retaining locally modified entities and explicit deletions.
    uint32 AddSceneInstance(Scene& target, const std::filesystem::path& source,
                            SceneReferenceMode mode, std::string& error, uint32 parent = 0);
    bool RefreshInstances(Scene& target, std::string& error);
    bool RefreshInstance(Scene& target, uint32 root, std::string& error);
    bool UnloadInstance(Scene& target, uint32 root, std::string& error);
    bool ConfigureStreaming(Scene& target, uint32 root, Vec2 center, float loadRadius,
                            float unloadRadius, std::string& error);
    // A zero load radius disables streaming. Center and observer are absolute world XY coordinates;
    // a wider unload radius supplies hysteresis. Source loading runs asynchronously; changed is set
    // only after a ready result or an unload is atomically applied. Unchanged ticks do not clone
    // scenes.
    bool UpdateStreaming(Scene& target, Vec2 observer, std::string& error, bool* changed = nullptr);

    // Canonical absolute paths are kept in memory. Disk copies use paths relative to their scene
    // file so Save As and project relocation retain reference meaning. Saving never rewrites the
    // live scene or its undo history, and a failed save leaves the original file intact.
    static bool Save(Scene& scene, const std::filesystem::path& targetPath,
                     const std::filesystem::path& previousPath, std::string& error);

private:
    struct State;
    std::shared_ptr<State> state_;
};

} // namespace LuxEngine
