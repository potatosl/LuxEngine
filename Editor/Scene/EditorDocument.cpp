#include "EditorDocument.hpp"
#include "Project/PathEncoding.hpp"

#include <chrono>
#include <filesystem>

namespace LuxEngine {
namespace {
// Writes the scene next to the target first and then atomically replaces the target,
// so a failed save never leaves the user's scene file truncated or destroyed.
bool SaveSceneAtomically(Scene& scene, const std::string& path) {
    return scene.Serialize(path);
}
} // namespace

EditorDocument::EditorDocument() : scene_(std::make_unique<Scene>()) {}

void EditorDocument::NewScene() {
    auto replacement = std::make_unique<Scene>();
    scene_.swap(replacement);
    selectedId_.reset();
    path_.clear();
    dirty_ = false;
}

bool EditorDocument::Open(const std::string& path) {
    if (path.empty())
        return false;
    std::string newPath = path;
    auto replacement = std::make_unique<Scene>();
    if (!replacement->Deserialize(newPath))
        return false;
    scene_.swap(replacement);
    selectedId_.reset();
    path_.swap(newPath);
    dirty_ = false;
    return true;
}

bool EditorDocument::Recover(const std::string& originalPath, const std::string& autosavePath) {
    if (originalPath.empty() || autosavePath.empty())
        return false;
    auto replacement = std::make_unique<Scene>();
    if (!replacement->Deserialize(autosavePath))
        return false;
    scene_.swap(replacement);
    selectedId_.reset();
    path_ = originalPath;
    dirty_ = true;
    return true;
}

bool EditorDocument::Save() {
    if (path_.empty() || !SaveSceneAtomically(*scene_, path_))
        return false;
    dirty_ = false;
    return true;
}

bool EditorDocument::SaveAs(const std::string& path) {
    if (path.empty())
        return false;
    std::string newPath = path;
    if (!SaveSceneAtomically(*scene_, newPath))
        return false;
    path_.swap(newPath);
    dirty_ = false;
    return true;
}

bool EditorDocument::ReplaceScene(const std::string& serializedPath,
                                  std::optional<uint32> selectedId) {
    auto replacement = std::make_unique<Scene>();
    if (!replacement->Deserialize(serializedPath))
        return false;
    scene_.swap(replacement);
    selectedId_ = selectedId && scene_->IsAlive(*selectedId) ? selectedId : std::nullopt;
    dirty_ = true;
    return true;
}

bool EditorDocument::ReplaceSceneFromData(const std::string& serializedData,
                                          std::optional<uint32> selectedId) {
    auto replacement = std::make_unique<Scene>();
    if (!replacement->DeserializeFromString(serializedData))
        return false;
    scene_.swap(replacement);
    selectedId_ = selectedId && scene_->IsAlive(*selectedId) ? selectedId : std::nullopt;
    dirty_ = true;
    return true;
}

void EditorDocument::Select(std::optional<uint32> id) {
    selectedId_ = id && scene_->IsAlive(*id) ? id : std::nullopt;
}

Entity EditorDocument::GetSelected() const {
    if (!selectedId_ || !scene_->IsAlive(*selectedId_))
        return {};
    return {*selectedId_, scene_.get()};
}

Entity EditorDocument::CreateEntity(const std::string& name) {
    Entity entity = scene_->CreateEntity(name);
    selectedId_ = entity.GetId();
    dirty_ = true;
    return entity;
}

bool EditorDocument::DeleteSelected() {
    Entity selected = GetSelected();
    selectedId_.reset();
    if (!selected)
        return false;
    scene_->DestroyEntity(selected);
    dirty_ = true;
    return true;
}

} // namespace LuxEngine
