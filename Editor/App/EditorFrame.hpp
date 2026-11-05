#pragma once

#include "Project/EditorProject.hpp"
#include "Scene/EditorDocument.hpp"
#include <array>
#include <optional>
#include <vector>
#include <wx/aui/auibook.h>
#include <wx/aui/framemanager.h>
#include <wx/spinctrl.h>
#include <wx/srchctrl.h>
#include <wx/wx.h>

namespace LuxEngine {

class SceneCanvas;
class SceneCanvas3D;

class EditorFrame final : public wxFrame {
public:
    EditorFrame();
    ~EditorFrame() override;

private:
    void BuildMenus();
    void BuildLayout();
    void ApplyWorkspaceTheme();
    void RestoreWorkspaceLayout();
    void SaveWorkspaceLayout();
    void ResetWorkspaceLayout();
    void ShowWorkspacePane(const char* name);
    void OpenCommandPalette();
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
    void AddMeshEntity();
    void DeleteEntity();
    void OnNameChanged();
    void OnTransformChanged();
    void OnSpriteChanged();
    void OnColorChanged();
    void OnMeshChanged();
    void SelectEntity(std::optional<uint32> id);

    EditorDocument document_;
    std::optional<EditorProject> project_;
    RecentProjects recentProjects_;
    wxMenu* recentMenu_ = nullptr;
    wxTimer autoSaveTimer_;
    wxAuiManager aui_;
    wxString defaultPerspective_;
    SceneCanvas* canvas_ = nullptr;
    SceneCanvas3D* canvas3D_ = nullptr;
    wxAuiNotebook* viewportTabs_ = nullptr;
    wxChoice* viewMode_ = nullptr;
    wxTextCtrl* consoleOutput_ = nullptr;
    wxListBox* hierarchy_ = nullptr;
    wxSearchCtrl* hierarchySearch_ = nullptr;
    wxListBox* projectFiles_ = nullptr;
    wxSearchCtrl* projectSearch_ = nullptr;
    wxStaticText* projectLabel_ = nullptr;
    wxStaticText* selectionLabel_ = nullptr;
    wxPanel* inspectorBody_ = nullptr;
    wxTextCtrl* name_ = nullptr;
    wxCheckBox* spriteEnabled_ = nullptr;
    wxCheckBox* meshEnabled_ = nullptr;
    wxSpinCtrlDouble* metallic_ = nullptr;
    wxSpinCtrlDouble* roughness_ = nullptr;
    std::array<std::array<wxSpinCtrlDouble*, 3>, 3> transform_{};
    std::array<wxSpinCtrlDouble*, 4> color_{};
    std::vector<uint32> hierarchyIds_;
    std::vector<std::filesystem::path> projectFilePaths_;
    bool refreshingHierarchy_ = false;
    bool refreshingInspector_ = false;
};

} // namespace LuxEngine
