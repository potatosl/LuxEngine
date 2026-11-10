#pragma once

#include "Engine/Scene/Entity.hpp"

#include <memory>
#include <optional>
#include <string>

namespace LuxEditor {

class EditorDocument final {
public:
    EditorDocument();

    void NewScene();
    bool Open(const std::string& path);
    bool Recover(const std::string& originalPath, const std::string& autosavePath);
    bool Save();
    bool SaveAs(const std::string& path);
    bool ReplaceScene(const std::string& serializedPath,
                      std::optional<LuxEngine::uint32> selectedId);
    bool ReplaceSceneFromData(const std::string& serializedData,
                              std::optional<LuxEngine::uint32> selectedId);

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

    void Select(std::optional<LuxEngine::uint32> id);
    LuxEngine::Entity GetSelected() const;
    LuxEngine::Entity CreateEntity(const std::string& name = "Entity");
    bool DeleteSelected();

    LuxEngine::Scene& GetScene() noexcept {
        return *scene_;
    }
    const LuxEngine::Scene& GetScene() const noexcept {
        return *scene_;
    }

private:
    std::unique_ptr<LuxEngine::Scene> scene_;
    std::optional<LuxEngine::uint32> selectedId_;
    std::string path_;
    bool dirty_ = false;
};

} // namespace LuxEditor
