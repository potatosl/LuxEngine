#pragma once

#include "Engine/Scene/Entity.hpp"

#include <memory>
#include <optional>
#include <string>

namespace LuxEngine {

class EditorDocument final {
public:
    EditorDocument();

    void NewScene();
    bool Open(const std::string& path);
    bool Recover(const std::string& originalPath, const std::string& autosavePath);
    bool Save();
    bool SaveAs(const std::string& path);
    bool ReplaceScene(const std::string& serializedPath, std::optional<uint32> selectedId);
    bool ReplaceSceneFromData(const std::string& serializedData, std::optional<uint32> selectedId);

    bool IsDirty() const noexcept {
        return dirty_;
    }
    void MarkDirty() noexcept {
        dirty_ = true;
    }
    void SetDirty(bool dirty) noexcept {
        dirty_ = dirty;
    }
    const std::string& GetPath() const noexcept {
        return path_;
    }

    void Select(std::optional<uint32> id);
    Entity GetSelected() const;
    Entity CreateEntity(const std::string& name = "Entity");
    bool DeleteSelected();

    Scene& GetScene() noexcept {
        return *scene_;
    }
    const Scene& GetScene() const noexcept {
        return *scene_;
    }

private:
    std::unique_ptr<Scene> scene_;
    std::optional<uint32> selectedId_;
    std::string path_;
    bool dirty_ = false;
};

} // namespace LuxEngine
