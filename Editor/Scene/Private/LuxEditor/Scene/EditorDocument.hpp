#pragma once

#include <LuxEngine/Scene/Entity.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace LuxEditor {

enum class SelectionMode { Replace, Add, Toggle };
enum class TransformSpace { World, Local };
enum class SelectionPivot { Median, Active, Individual };

class EditorDocument final {
public:
    using TabId = std::uint64_t;

    struct TabInfo {
        TabId id = 0;
        std::string path;
        bool dirty = false;
        std::optional<LuxEngine::uint32> selectedId;
    };

    EditorDocument();

    // Existing operations replace only the active scene; NewTab/OpenTab preserve other scenes.
    void NewScene();
    bool Open(const std::string& path);
    bool Recover(const std::string& originalPath, const std::string& autosavePath);
    bool Save();
    bool SaveAs(const std::string& path);
    bool ReplaceScene(const std::string& serializedPath,
                      std::optional<LuxEngine::uint32> selectedId);
    bool ReplaceSceneFromData(const std::string& serializedData,
                              std::optional<LuxEngine::uint32> selectedId);
    bool ReplaceSceneFromData(TabId tab, const std::string& serializedData,
                              std::optional<LuxEngine::uint32> selectedId);

    TabId NewTab();
    TabId OpenTab(const std::string& path);
    TabId AdoptTab(const std::string& path, std::shared_ptr<LuxEngine::Scene> scene,
                   bool dirty = false, std::string warning = {});
    TabId DuplicateTab(TabId tab, const std::string& path);
    bool ActivateTab(TabId tab) noexcept;
    bool CloseTab(TabId tab, bool discardDirty = false);
    void ResetTabs();
    std::vector<TabInfo> ListTabs() const;
    TabId GetActiveTabId() const noexcept;
    TabId FindTabByPath(const std::string& path) const;
    LuxEngine::Scene* GetScene(TabId tab) noexcept;
    const LuxEngine::Scene* GetScene(TabId tab) const noexcept;
    std::string GetPath(TabId tab) const;
    const std::string& GetLoadWarning(TabId tab) const noexcept;

    const std::string& GetLoadWarning() const noexcept;

    bool IsDirty(TabId tab) const noexcept;
    bool SetDirty(TabId tab, bool dirty) noexcept;
    bool Save(TabId tab);
    std::optional<LuxEngine::uint32> GetSelectedId(TabId tab) const noexcept;
    void Select(TabId tab, std::optional<LuxEngine::uint32> id);
    std::vector<LuxEngine::uint32> GetSelection() const;
    std::vector<LuxEngine::uint32> GetSelection(TabId tab) const;
    void SetSelection(std::vector<LuxEngine::uint32> ids,
                      std::optional<LuxEngine::uint32> primary = {});
    void SetSelection(TabId tab, std::vector<LuxEngine::uint32> ids,
                      std::optional<LuxEngine::uint32> primary = {});
    void ModifySelection(std::optional<LuxEngine::uint32> id, SelectionMode mode);
    void ModifySelection(TabId tab, std::optional<LuxEngine::uint32> id, SelectionMode mode);
    bool IsSelected(LuxEngine::uint32 id) const;
    TransformSpace GetTransformSpace() const noexcept;
    void SetTransformSpace(TransformSpace space) noexcept;
    SelectionPivot GetSelectionPivotMode() const noexcept;
    void SetSelectionPivotMode(SelectionPivot pivot) noexcept;

    const std::string& GetLastError() const noexcept;

    bool IsDirty() const noexcept;

    void MarkDirty() noexcept;

    void SetDirty(bool dirty) noexcept;

    const std::string& GetPath() const noexcept;

    void Select(std::optional<LuxEngine::uint32> id);
    LuxEngine::Entity GetSelected() const;
    LuxEngine::Entity CreateEntity(const std::string& name = "Entity");
    bool DeleteSelected();

    LuxEngine::Scene& GetScene() noexcept;

    const LuxEngine::Scene& GetScene() const noexcept;

private:
    struct TabState {
        TabId id;
        std::shared_ptr<LuxEngine::Scene> scene;
        std::optional<LuxEngine::uint32> selectedId;
        std::string path;
        bool dirty = false;
        std::string loadWarning;
        std::vector<LuxEngine::uint32> selection{};
        std::unordered_set<LuxEngine::uint32> selectionSet{};
        TransformSpace transformSpace = TransformSpace::World;
        SelectionPivot selectionPivot = SelectionPivot::Median;
    };

    TabState& Active() noexcept;

    const TabState& Active() const noexcept;

    TabState* Find(TabId tab) noexcept;
    const TabState* Find(TabId tab) const noexcept;
    TabId NextId();
    bool ConflictsWithOpenTab(const std::string& path, TabId except) const;
    std::vector<TabState> tabs_;
    size_t activeIndex_ = 0;
    TabId nextId_ = 1;
    std::string error_;
};

} // namespace LuxEditor
