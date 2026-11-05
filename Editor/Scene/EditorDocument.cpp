#include "EditorDocument.hpp"

namespace LuxEngine {

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
    if (path_.empty() || !scene_->Serialize(path_))
        return false;
    dirty_ = false;
    return true;
}

bool EditorDocument::SaveAs(const std::string& path) {
    if (path.empty())
        return false;
    std::string newPath = path;
    if (!scene_->Serialize(newPath))
        return false;
    path_.swap(newPath);
    dirty_ = false;
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
