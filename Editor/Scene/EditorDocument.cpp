#include "EditorDocument.hpp"
#include "Project/PathEncoding.hpp"

#include <chrono>
#include <filesystem>

namespace LuxEditor {
namespace {
// Writes the scene next to the target first and then atomically replaces the target,
// so a failed save never leaves the user's scene file truncated or destroyed.
bool SaveSceneAtomically(LuxEngine::Scene& scene, const std::string& path) {
    return scene.Serialize(path);
}
} // namespace

EditorDocument::EditorDocument() : scene_(std::make_unique<LuxEngine::Scene>()) {}

void EditorDocument::NewScene() {
    lastError_.clear();
    auto replacement = std::make_unique<LuxEngine::Scene>();
    replacement->SetAssetRoot(scene_->GetAssetRoot());
    scene_.swap(replacement);
    selectedId_.reset();
    path_.clear();
    dirty_ = false;
}

bool EditorDocument::Open(const std::string& path) {
    lastError_.clear();
    if (path.empty())
        return false;
    std::string newPath = path;
    auto replacement = std::make_unique<LuxEngine::Scene>();
    if (!replacement->Deserialize(newPath))
        return false;
    scene_.swap(replacement);
    selectedId_.reset();
    path_.swap(newPath);
    dirty_ = false;
    return true;
}

bool EditorDocument::Recover(const std::string& originalPath, const std::string& autosavePath) {
    lastError_.clear();
    if (originalPath.empty() || autosavePath.empty())
        return false;
    auto replacement = std::make_unique<LuxEngine::Scene>();
    if (!replacement->Deserialize(autosavePath))
        return false;
    if (!scene_->GetAssetRoot().empty() && path_ == originalPath)
        replacement->SetAssetRoot(scene_->GetAssetRoot());
    else
        replacement->SetAssetRoot(LuxEngine::Scene::InferAssetRootForScene(originalPath));
    scene_.swap(replacement);
    selectedId_.reset();
    path_ = originalPath;
    dirty_ = true;
    return true;
}

bool EditorDocument::Save() {
    lastError_.clear();
    if (path_.empty() || !SaveSceneAtomically(*scene_, path_)) {
        lastError_ = LuxEngine::Scene::GetLastSaveError();
        return false;
    }
    dirty_ = false;
    return true;
}

bool EditorDocument::SaveAs(const std::string& path) {
    lastError_.clear();
    if (path.empty())
        return false;
    const auto targetRoot = LuxEngine::Scene::InferAssetRootForScene(path);
    if (!scene_->GetAssetRoot().empty()) {
        std::error_code error;
        const bool sameRoot = std::filesystem::equivalent(
            PathFromUtf8(scene_->GetAssetRoot()), PathFromUtf8(targetRoot), error);
        if (!sameRoot)
            for (auto entity : scene_->GetEntities()) {
                if (entity.HasComponent<LuxEngine::SpriteRendererComponent>() &&
                    !entity.GetComponent<LuxEngine::SpriteRendererComponent>().texturePath.empty()) {
                    lastError_ = "텍스처가 있는 장면은 다른 에셋 루트에 저장할 수 없습니다. "
                                 "같은 프로젝트 안에 저장하거나 프로젝트 복제를 사용해 주세요.";
                    return false;
                }
            }
    }
    std::string newPath = path;
    if (!SaveSceneAtomically(*scene_, newPath)) {
        lastError_ = LuxEngine::Scene::GetLastSaveError();
        return false;
    }
    path_.swap(newPath);
    scene_->SetAssetRoot(targetRoot);
    dirty_ = false;
    return true;
}

bool EditorDocument::ReplaceScene(const std::string& serializedPath,
                                  std::optional<LuxEngine::uint32> selectedId) {
    auto replacement = std::make_unique<LuxEngine::Scene>();
    if (!replacement->Deserialize(serializedPath))
        return false;
    if (!scene_->GetAssetRoot().empty())
        replacement->SetAssetRoot(scene_->GetAssetRoot());
    scene_.swap(replacement);
    selectedId_ = selectedId && scene_->IsAlive(*selectedId) ? selectedId : std::nullopt;
    dirty_ = true;
    return true;
}

bool EditorDocument::ReplaceSceneFromData(const std::string& serializedData,
                                          std::optional<LuxEngine::uint32> selectedId) {
    auto replacement = std::make_unique<LuxEngine::Scene>();
    replacement->SetAssetRoot(scene_->GetAssetRoot());
    if (!replacement->DeserializeFromString(serializedData))
        return false;
    scene_.swap(replacement);
    selectedId_ = selectedId && scene_->IsAlive(*selectedId) ? selectedId : std::nullopt;
    dirty_ = true;
    return true;
}

void EditorDocument::Select(std::optional<LuxEngine::uint32> id) {
    selectedId_ = id && scene_->IsAlive(*id) ? id : std::nullopt;
}

LuxEngine::Entity EditorDocument::GetSelected() const {
    if (!selectedId_ || !scene_->IsAlive(*selectedId_))
        return {};
    return {*selectedId_, scene_.get()};
}

LuxEngine::Entity EditorDocument::CreateEntity(const std::string& name) {
    LuxEngine::Entity entity = scene_->CreateEntity(name);
    selectedId_ = entity.GetId();
    dirty_ = true;
    return entity;
}

bool EditorDocument::DeleteSelected() {
    LuxEngine::Entity selected = GetSelected();
    selectedId_.reset();
    if (!selected)
        return false;
    scene_->DestroyEntity(selected);
    dirty_ = true;
    return true;
}

} // namespace LuxEditor
