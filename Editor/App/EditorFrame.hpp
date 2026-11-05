#pragma once

#include "Project/EditorProject.hpp"
#include "Scene/EditorDocument.hpp"
#include <array>
#include <optional>
#include <vector>
#include <wx/spinctrl.h>
#include <wx/wx.h>

namespace LuxEngine {

class SceneCanvas;

class EditorFrame final : public wxFrame {
public:
    EditorFrame();

private:
    void BuildMenus();
    void BuildLayout();
    void RefreshAll();
    void RefreshHierarchy();
    void RefreshInspector();
    void UpdateChrome();
    void UpdateRecentMenu();
    void RefreshProjectExplorer();
    void ApplyProjectPreferences();
    bool OpenProjectScene(const std::filesystem::path& file);
    void RecoverAutosaveIfNeeded();
    void AutoSaveScene();
    void NewProject();
    void OpenProjectDialog();
    void OpenProjectFile(const std::filesystem::path& file);
    void EditProjectSettings();
    void CloneCurrentProject();
    void ImportProjectDialog();
    void ExportCurrentProject();
    void BackupCurrentProject();
    void RestoreCurrentProject();
    void RescanProjectResources();
    bool ConfirmDiscardChanges();
    bool SaveScene(bool choosePath);
    void AddEntity();
    void DeleteEntity();
    void OnNameChanged();
    void OnTransformChanged();
    void OnSpriteChanged();
    void OnColorChanged();
    void SelectEntity(std::optional<uint32> id);

    EditorDocument document_;
    std::optional<EditorProject> project_;
    RecentProjects recentProjects_;
    wxMenu* recentMenu_ = nullptr;
    wxTimer autoSaveTimer_;
    SceneCanvas* canvas_ = nullptr;
    wxListBox* hierarchy_ = nullptr;
    wxListBox* projectFiles_ = nullptr;
    wxStaticText* projectLabel_ = nullptr;
    wxStaticText* selectionLabel_ = nullptr;
    wxPanel* inspectorBody_ = nullptr;
    wxTextCtrl* name_ = nullptr;
    wxCheckBox* spriteEnabled_ = nullptr;
    std::array<std::array<wxSpinCtrlDouble*, 3>, 3> transform_{};
    std::array<wxSpinCtrlDouble*, 4> color_{};
    std::vector<uint32> hierarchyIds_;
    std::vector<std::filesystem::path> projectFilePaths_;
    bool refreshingHierarchy_ = false;
    bool refreshingInspector_ = false;
};

} // namespace LuxEngine
