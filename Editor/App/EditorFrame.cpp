#include "EditorFrame.hpp"
#include "Engine/ECS/ECS.hpp"
#include "Project/PathEncoding.hpp"
#include "Project/ProjectArchive.hpp"
#include "Project/ProjectDialog.hpp"
#include "Viewport/SceneCanvas.hpp"
#include "Viewport/SceneCanvas3D.hpp"
#include <algorithm>
#include <functional>
#include <numbers>
#include <string>
#include <wx/choicdlg.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/image.h>
#include <wx/stdpaths.h>
#include <wx/textdlg.h>

namespace LuxEngine {
namespace {
enum : int {
    AddEntityId = wxID_HIGHEST + 100,
    AddMeshEntityId,
    DeleteEntityId,
    SnapGridId,
    NewProjectId,
    OpenProjectId,
    ProjectSettingsId,
    CloneProjectId,
    ImportProjectId,
    ExportProjectId,
    BackupProjectId,
    RestoreProjectId,
    RescanResourcesId,
    View2DId,
    View3DId,
    FocusSelectionId,
    FullScreenId,
    CommandPaletteId,
    ShowHierarchyId,
    ShowProjectId,
    ShowInspectorId,
    ShowConsoleId,
    ResetLayoutId,
    RecentProjectFirstId
};
constexpr int MaxRecentProjects = 10;
constexpr int AutoSaveTimerId = wxID_HIGHEST + 150;

std::string Utf8(const wxString& value) {
    return value.ToStdString(wxConvUTF8);
}

wxString Ui(const char* text) {
    return wxString::FromUTF8(text);
}
} // namespace

EditorFrame::EditorFrame()
    : wxFrame(nullptr, wxID_ANY, "LuxEditor", wxDefaultPosition, wxSize(1280, 800)),
      recentProjects_(PathFromUtf8(Utf8(wxStandardPaths::Get().GetUserDataDir())) /
                      "recent-projects.txt") {
    SetMinSize(wxSize(960, 600));
    CreateStatusBar(2);
    recentProjects_.Load();
    autoSaveTimer_.SetOwner(this, AutoSaveTimerId);
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) { AutoSaveScene(); }, AutoSaveTimerId);
    BuildMenus();
    BuildLayout();
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& event) {
        if (event.CanVeto() && !ConfirmDiscardChanges()) {
            event.Veto();
            return;
        }
        SaveWorkspaceLayout();
        event.Skip();
    });
    RefreshAll();
}

EditorFrame::~EditorFrame() {
    aui_.UnInit();
}

void EditorFrame::BuildMenus() {
    auto* file = new wxMenu;
    file->Append(NewProjectId, Ui("새 프로젝트..."));
    file->Append(OpenProjectId, Ui("프로젝트 열기..."));
    recentMenu_ = new wxMenu;
    file->AppendSubMenu(recentMenu_, Ui("최근 프로젝트"));
    file->Append(ProjectSettingsId, Ui("프로젝트 설정..."));
    file->Append(CloneProjectId, Ui("프로젝트 복제..."));
    file->Append(ImportProjectId, Ui("프로젝트 가져오기..."));
    file->Append(ExportProjectId, Ui("프로젝트 내보내기..."));
    file->Append(BackupProjectId, Ui("프로젝트 백업"));
    file->Append(RestoreProjectId, Ui("프로젝트 백업 복구..."));
    file->Append(RescanResourcesId, Ui("프로젝트 리소스 재검색"));
    file->AppendSeparator();
    file->Append(wxID_NEW, Ui("새 장면\tCtrl+N"));
    file->Append(wxID_OPEN, Ui("장면 열기...\tCtrl+O"));
    file->AppendSeparator();
    file->Append(wxID_SAVE, Ui("저장\tCtrl+S"));
    file->Append(wxID_SAVEAS, Ui("다른 이름으로 저장...\tCtrl+Shift+S"));
    file->AppendSeparator();
    file->Append(wxID_EXIT, Ui("끝내기"));

    auto* edit = new wxMenu;
    edit->Append(AddEntityId, Ui("엔티티 추가\tCtrl+E"));
    edit->Append(AddMeshEntityId, Ui("3D 메시 엔티티 추가"));
    edit->Append(DeleteEntityId, Ui("선택 삭제\tDel"));

    auto* view = new wxMenu;
    view->AppendCheckItem(SnapGridId, Ui("격자에 맞춤"));
    view->Check(SnapGridId, true);
    view->AppendSeparator();
    view->Append(View2DId, Ui("2D 작업 화면"));
    view->Append(View3DId, Ui("3D 배치 화면"));
    view->Append(FocusSelectionId, Ui("선택 항목에 초점\tF"));
    view->Append(FullScreenId, Ui("전체 화면\tF11"));

    auto* window = new wxMenu;
    window->Append(CommandPaletteId, Ui("명령 검색...\tCtrl+P"));
    window->AppendSeparator();
    window->Append(ShowHierarchyId, Ui("장면 계층 표시/숨기기"));
    window->Append(ShowProjectId, Ui("프로젝트 파일 표시/숨기기"));
    window->Append(ShowInspectorId, Ui("속성 표시/숨기기"));
    window->Append(ShowConsoleId, Ui("콘솔 표시/숨기기"));
    window->AppendSeparator();
    window->Append(ResetLayoutId, Ui("기본 배치 복원"));

    auto* bar = new wxMenuBar;
    bar->Append(file, Ui("파일"));
    bar->Append(edit, Ui("편집"));
    bar->Append(view, Ui("보기"));
    bar->Append(window, Ui("창"));
    SetMenuBar(bar);
    UpdateRecentMenu();

    Bind(wxEVT_MENU, [this](wxCommandEvent&) { NewProject(); }, NewProjectId);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { OpenProjectDialog(); }, OpenProjectId);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { EditProjectSettings(); }, ProjectSettingsId);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { CloneCurrentProject(); }, CloneProjectId);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ImportProjectDialog(); }, ImportProjectId);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ExportCurrentProject(); }, ExportProjectId);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { BackupCurrentProject(); }, BackupProjectId);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { RestoreCurrentProject(); }, RestoreProjectId);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { RescanProjectResources(); }, RescanResourcesId);
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent& event) {
            const size_t index = static_cast<size_t>(event.GetId() - RecentProjectFirstId);
            if (index < recentProjects_.Get().size())
                OpenProjectFile(recentProjects_.Get()[index]);
        },
        RecentProjectFirstId, RecentProjectFirstId + MaxRecentProjects - 1);

    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            if (!ConfirmDiscardChanges())
                return;
            document_.NewScene();
            RefreshAll();
        },
        wxID_NEW);
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            if (!ConfirmDiscardChanges())
                return;
            wxFileDialog dialog(this, Ui("장면 열기"), wxEmptyString, wxEmptyString,
                                Ui("Lux 장면 (*.lux)|*.lux|모든 파일 (*.*)|*.*"),
                                wxFD_OPEN | wxFD_FILE_MUST_EXIST);
            if (dialog.ShowModal() != wxID_OK)
                return;
            if (!document_.Open(Utf8(dialog.GetPath()))) {
                wxMessageBox(Ui("장면 파일을 열 수 없습니다. 파일 형식과 경로를 확인해 주세요."),
                             Ui("열기 실패"), wxOK | wxICON_ERROR, this);
                return;
            }
            project_.reset();
            ApplyProjectPreferences();
            RefreshAll();
        },
        wxID_OPEN);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { SaveScene(false); }, wxID_SAVE);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { SaveScene(true); }, wxID_SAVEAS);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Close(); }, wxID_EXIT);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { AddEntity(); }, AddEntityId);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { AddMeshEntity(); }, AddMeshEntityId);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { DeleteEntity(); }, DeleteEntityId);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { viewportTabs_->SetSelection(0); }, View2DId);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { viewportTabs_->SetSelection(1); }, View3DId);
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            if (viewportTabs_->GetSelection() == 1)
                canvas3D_->FocusSelection();
            else
                canvas_->FocusSelection();
        },
        FocusSelectionId);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowFullScreen(!IsFullScreen()); }, FullScreenId);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { OpenCommandPalette(); }, CommandPaletteId);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowWorkspacePane("hierarchy"); }, ShowHierarchyId);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowWorkspacePane("project"); }, ShowProjectId);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowWorkspacePane("inspector"); }, ShowInspectorId);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowWorkspacePane("console"); }, ShowConsoleId);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ResetWorkspaceLayout(); }, ResetLayoutId);
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent& event) {
            canvas_->SetSnapToGrid(event.IsChecked());
            if (project_) {
                auto settings = project_->GetSettings();
                settings.snapToGrid = event.IsChecked();
                std::string error;
                if (!project_->UpdateSettings(settings, error))
                    wxMessageBox(wxString::FromUTF8(error), Ui("프로젝트 설정 저장 실패"),
                                 wxOK | wxICON_ERROR, this);
            }
        },
        SnapGridId);
}

void EditorFrame::UpdateRecentMenu() {
    while (recentMenu_->GetMenuItemCount() > 0)
        recentMenu_->Destroy(recentMenu_->FindItemByPosition(0));
    const auto& files = recentProjects_.Get();
    if (files.empty()) {
        recentMenu_->Append(wxID_ANY, Ui("최근 프로젝트가 없습니다."))->Enable(false);
        return;
    }
    for (size_t index = 0; index < files.size(); ++index) {
        const auto label = wxString::FromUTF8(PathUtf8(files[index]));
        recentMenu_->Append(RecentProjectFirstId + static_cast<int>(index), label);
    }
}

void EditorFrame::OpenCommandPalette() {
    wxTextEntryDialog search(this, Ui("실행할 명령 이름을 입력해 주세요."), Ui("명령 검색"));
    if (search.ShowModal() != wxID_OK)
        return;
    struct Command {
        wxString label;
        std::function<void()> run;
    };
    const std::vector<Command> commands{{Ui("새 프로젝트"), [this] { NewProject(); }},
                                        {Ui("프로젝트 열기"), [this] { OpenProjectDialog(); }},
                                        {Ui("장면 저장"), [this] { SaveScene(false); }},
                                        {Ui("엔티티 추가"), [this] { AddEntity(); }},
                                        {Ui("3D 메시 엔티티 추가"), [this] { AddMeshEntity(); }},
                                        {Ui("선택 삭제"), [this] { DeleteEntity(); }},
                                        {Ui("2D 작업"), [this] { viewportTabs_->SetSelection(0); }},
                                        {Ui("3D 배치"), [this] { viewportTabs_->SetSelection(1); }},
                                        {Ui("기본 배치 복원"), [this] { ResetWorkspaceLayout(); }}};
    wxArrayString matches;
    std::vector<size_t> indices;
    const auto query = search.GetValue().Lower();
    for (size_t index = 0; index < commands.size(); ++index)
        if (query.empty() || commands[index].label.Lower().Find(query) != wxNOT_FOUND) {
            matches.Add(commands[index].label);
            indices.push_back(index);
        }
    if (matches.empty()) {
        wxMessageBox(Ui("일치하는 명령이 없습니다."), Ui("명령 검색"), wxOK | wxICON_INFORMATION,
                     this);
        return;
    }
    if (matches.size() == 1) {
        commands[indices.front()].run();
        return;
    }
    wxSingleChoiceDialog choose(this, Ui("실행할 명령을 선택해 주세요."), Ui("명령 검색"), matches);
    if (choose.ShowModal() == wxID_OK)
        commands[indices[static_cast<size_t>(choose.GetSelection())]].run();
}

void EditorFrame::NewProject() {
    auto request = ShowNewProjectDialog(this);
    if (!request || !ConfirmDiscardChanges())
        return;
    std::string error;
    auto created =
        EditorProject::Create(request->directory, request->name, request->projectTemplate, error);
    if (!created) {
        wxMessageBox(wxString::FromUTF8(error.c_str()), Ui("프로젝트 생성 실패"),
                     wxOK | wxICON_ERROR, this);
        return;
    }
    if (!document_.Open(PathUtf8(created->GetSceneFile()))) {
        wxMessageBox(Ui("생성한 시작 장면을 열 수 없습니다."), Ui("프로젝트 열기 실패"),
                     wxOK | wxICON_ERROR, this);
        return;
    }
    project_ = std::move(created);
    ApplyProjectPreferences();
    recentProjects_.Add(project_->GetFile());
    UpdateRecentMenu();
    RefreshAll();
}

void EditorFrame::OpenProjectDialog() {
    wxFileDialog dialog(this, Ui("프로젝트 열기"), wxEmptyString, wxEmptyString,
                        Ui("Lux 프로젝트 (*.luxproj)|*.luxproj|모든 파일 (*.*)|*.*"),
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() == wxID_OK)
        OpenProjectFile(PathFromUtf8(Utf8(dialog.GetPath())));
}

void EditorFrame::OpenProjectFile(const std::filesystem::path& file) {
    std::string error;
    auto opened = EditorProject::Open(file, error);
    if (!opened) {
        wxMessageBox(wxString::FromUTF8(error.c_str()), Ui("프로젝트 열기 실패"),
                     wxOK | wxICON_ERROR, this);
        recentProjects_.Load();
        UpdateRecentMenu();
        return;
    }
    if (!ConfirmDiscardChanges())
        return;
    if (!document_.Open(PathUtf8(opened->GetSceneFile()))) {
        wxMessageBox(Ui("시작 장면을 열 수 없습니다."), Ui("프로젝트 열기 실패"),
                     wxOK | wxICON_ERROR, this);
        return;
    }
    project_ = std::move(opened);
    ApplyProjectPreferences();
    RecoverAutosaveIfNeeded();
    recentProjects_.Add(project_->GetFile());
    UpdateRecentMenu();
    RefreshAll();
}

void EditorFrame::EditProjectSettings() {
    if (!project_)
        return;
    auto request = ShowProjectSettingsDialog(this, project_->GetSettings());
    if (!request)
        return;
    std::string error;
    if (!request->iconSource.empty() && !project_->SetIcon(request->iconSource, error)) {
        wxMessageBox(wxString::FromUTF8(error.c_str()), Ui("프로젝트 설정 저장 실패"),
                     wxOK | wxICON_ERROR, this);
        return;
    }
    if (!request->iconSource.empty())
        request->settings.icon = project_->GetIcon();
    if (!project_->UpdateSettings(request->settings, error)) {
        wxMessageBox(wxString::FromUTF8(error.c_str()), Ui("프로젝트 설정 저장 실패"),
                     wxOK | wxICON_ERROR, this);
        return;
    }
    ApplyProjectPreferences();
    RefreshAll();
}

void EditorFrame::ApplyProjectPreferences() {
    autoSaveTimer_.Stop();
    const bool snap = project_ ? project_->GetSettings().snapToGrid : true;
    GetMenuBar()->Check(SnapGridId, snap);
    if (canvas_)
        canvas_->SetSnapToGrid(snap);
    if (!project_) {
        SetIcon(wxIcon());
        return;
    }
    const auto seconds = project_->GetSettings().autosaveSeconds;
    if (seconds > 0)
        autoSaveTimer_.Start(static_cast<int>(seconds * 1000));
    if (!project_->GetIcon().empty()) {
        wxImage image;
        const auto path = project_->GetFile().parent_path() / project_->GetIcon();
        if (image.LoadFile(wxString::FromUTF8(PathUtf8(path)))) {
            wxIcon icon;
            icon.CopyFromBitmap(wxBitmap(image.Scale(32, 32)));
            SetIcon(icon);
        } else {
            SetIcon(wxIcon());
        }
    } else {
        SetIcon(wxIcon());
    }
}

bool EditorFrame::OpenProjectScene(const std::filesystem::path& file) {
    if (!ConfirmDiscardChanges())
        return false;
    if (!document_.Open(PathUtf8(file))) {
        wxMessageBox(Ui("장면 파일을 열 수 없습니다."), Ui("장면 열기 실패"), wxOK | wxICON_ERROR,
                     this);
        return false;
    }
    RecoverAutosaveIfNeeded();
    RefreshAll();
    return true;
}

void EditorFrame::RecoverAutosaveIfNeeded() {
    if (!project_ || !project_->HasNewerAutosave(PathFromUtf8(document_.GetPath())))
        return;
    const auto path = project_->GetAutosaveFile(PathFromUtf8(document_.GetPath()));
    const int answer =
        wxMessageBox(Ui("저장된 장면보다 새 자동 저장본이 있습니다. 복구하시겠습니까?"),
                     Ui("장면 자동 저장 복구"), wxYES_NO | wxICON_QUESTION, this);
    if (answer == wxYES) {
        if (!document_.Recover(document_.GetPath(), PathUtf8(path)))
            wxMessageBox(Ui("자동 저장본을 읽을 수 없습니다."), Ui("복구 실패"),
                         wxOK | wxICON_ERROR, this);
    } else {
        project_->ClearAutosave(PathFromUtf8(document_.GetPath()));
    }
}

void EditorFrame::AutoSaveScene() {
    if (!project_ || !document_.IsDirty() || document_.GetPath().empty())
        return;
    std::string error;
    if (project_->SaveAutosave(PathFromUtf8(document_.GetPath()), document_.GetScene(), error))
        SetStatusText(Ui("장면 자동 저장본을 만들었습니다."));
    else
        SetStatusText(wxString::FromUTF8(error));
}

void EditorFrame::RefreshAll() {
    RefreshHierarchy();
    RefreshProjectExplorer();
    RefreshInspector();
    canvas_->RefreshScene();
    canvas3D_->RefreshScene();
    UpdateChrome();
}

void EditorFrame::RefreshProjectExplorer() {
    projectFiles_->Clear();
    projectFilePaths_.clear();
    if (!project_) {
        projectFiles_->Append(Ui("프로젝트를 열어 주세요."));
        return;
    }
    const auto root = project_->GetFile().parent_path();
    std::error_code error;
    std::filesystem::recursive_directory_iterator iterator(
        root, std::filesystem::directory_options::skip_permission_denied, error);
    const std::filesystem::recursive_directory_iterator end;
    const wxString filter = projectSearch_->GetValue().Lower();
    while (!error && iterator != end && projectFilePaths_.size() < 500) {
        const auto relative = iterator->path().lexically_relative(root);
        if (!relative.empty() && (*relative.begin() == ".git" || *relative.begin() == ".lux")) {
            if (iterator->is_directory(error))
                iterator.disable_recursion_pending();
            iterator.increment(error);
            continue;
        }
        if (iterator->is_regular_file(error)) {
            const auto path = iterator->path();
            const wxString label = wxString::FromUTF8(GenericPathUtf8(relative));
            if (filter.empty() || label.Lower().Find(filter) != wxNOT_FOUND) {
                projectFilePaths_.push_back(path);
                projectFiles_->Append(label);
            }
        }
        iterator.increment(error);
    }
}

void EditorFrame::RefreshHierarchy() {
    refreshingHierarchy_ = true;
    hierarchy_->Freeze();
    hierarchy_->Clear();
    hierarchyIds_.clear();
    const Entity selected = document_.GetSelected();
    const wxString filter = hierarchySearch_->GetValue().Lower();
    for (Entity entity : document_.GetScene().GetEntities()) {
        const uint32 id = entity.GetId();
        const std::string tag = entity.HasComponent<TagComponent>()
                                    ? entity.GetComponent<TagComponent>().tag
                                    : "Entity";
        const wxString label = wxString::FromUTF8(tag.c_str());
        if (!filter.empty() && label.Lower().Find(filter) == wxNOT_FOUND)
            continue;
        hierarchyIds_.push_back(id);
        hierarchy_->Append(wxString::Format("%u  ", static_cast<unsigned>(id)) + label);
        if (selected && selected.GetId() == id)
            hierarchy_->SetSelection(static_cast<int>(hierarchyIds_.size() - 1));
    }
    hierarchy_->Thaw();
    refreshingHierarchy_ = false;
}

void EditorFrame::RefreshInspector() {
    refreshingInspector_ = true;
    const Entity selected = document_.GetSelected();
    selectionLabel_->SetLabel(
        selected ? wxString::Format(Ui("엔티티 #%u"), static_cast<unsigned>(selected.GetId()))
                 : Ui("선택된 엔티티 없음"));
    inspectorBody_->Enable(static_cast<bool>(selected));
    if (selected) {
        const wxString tag =
            selected.HasComponent<TagComponent>()
                ? wxString::FromUTF8(selected.GetComponent<TagComponent>().tag.c_str())
                : wxString{};
        name_->ChangeValue(tag);
        const TransformComponent value = selected.HasComponent<TransformComponent>()
                                             ? selected.GetComponent<TransformComponent>()
                                             : TransformComponent{};
        const float radiansToDegrees = 180.0f / std::numbers::pi_v<float>;
        const std::array<std::array<double, 3>, 3> rows{
            {{value.position.x, value.position.y, value.position.z},
             {value.rotation.x * radiansToDegrees, value.rotation.y * radiansToDegrees,
              value.rotation.z * radiansToDegrees},
             {value.scale.x, value.scale.y, value.scale.z}}};
        for (size_t row = 0; row < rows.size(); ++row)
            for (size_t col = 0; col < rows[row].size(); ++col)
                transform_[row][col]->SetValue(rows[row][col]);
        const bool hasSprite = selected.HasComponent<SpriteRendererComponent>();
        spriteEnabled_->SetValue(hasSprite);
        const Vec4 color =
            hasSprite ? selected.GetComponent<SpriteRendererComponent>().color : Vec4{1, 1, 1, 1};
        const std::array<float, 4> channels{color.x, color.y, color.z, color.w};
        for (size_t i = 0; i < color_.size(); ++i) {
            color_[i]->SetValue(std::clamp(static_cast<double>(channels[i]), 0.0, 1.0));
            color_[i]->Enable(hasSprite);
        }
        const bool hasMesh = selected.HasComponent<MeshRendererComponent>();
        meshEnabled_->SetValue(hasMesh);
        const Material3D material =
            hasMesh ? selected.GetComponent<MeshRendererComponent>().material : Material3D{};
        metallic_->SetValue(std::clamp(static_cast<double>(material.metallic), 0.0, 1.0));
        roughness_->SetValue(std::clamp(static_cast<double>(material.roughness), 0.0, 1.0));
        metallic_->Enable(hasMesh);
        roughness_->Enable(hasMesh);
    }
    refreshingInspector_ = false;
}

void EditorFrame::UpdateChrome() {
    const wxString name =
        document_.GetPath().empty()
            ? Ui("새 장면")
            : wxFileName(wxString::FromUTF8(document_.GetPath().c_str())).GetFullName();
    const wxString projectName =
        project_ ? wxString::FromUTF8(project_->GetName().c_str()) + Ui(" — ") : wxString{};
    SetTitle(Ui("LuxEditor — ") + projectName + name + (document_.IsDirty() ? " *" : ""));
    projectLabel_->SetLabel(project_ ? wxString::FromUTF8(project_->GetName().c_str())
                                     : Ui("프로젝트 없음"));
    SetStatusText(wxString::Format(Ui("%s엔티티 %u개"), projectName.c_str(),
                                   static_cast<unsigned>(document_.GetScene().GetEntityCount())));
    SetStatusText(viewportTabs_->GetSelection() == 1 ? Ui("3D 배치 미리보기") : Ui("2D 장면"), 1);
    GetMenuBar()->Enable(ProjectSettingsId, project_.has_value());
    for (int id :
         {CloneProjectId, ExportProjectId, BackupProjectId, RestoreProjectId, RescanResourcesId})
        GetMenuBar()->Enable(id, project_.has_value());
}

bool EditorFrame::ConfirmDiscardChanges() {
    if (!document_.IsDirty())
        return true;
    const int answer = wxMessageBox(Ui("저장하지 않은 변경 사항이 있습니다. 저장하시겠습니까?"),
                                    "LuxEditor", wxYES_NO | wxCANCEL | wxICON_QUESTION, this);
    if (answer == wxCANCEL)
        return false;
    return answer != wxYES || SaveScene(false);
}

bool EditorFrame::SaveScene(bool choosePath) {
    const auto previousPath = document_.GetPath();
    bool saved = false;
    if (choosePath || document_.GetPath().empty()) {
        wxFileDialog dialog(this, Ui("장면 저장"), wxEmptyString, wxEmptyString,
                            Ui("Lux 장면 (*.lux)|*.lux|모든 파일 (*.*)|*.*"),
                            wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
        if (dialog.ShowModal() != wxID_OK)
            return false;
        wxFileName filename(dialog.GetPath());
        if (!filename.HasExt())
            filename.SetExt("lux");
        const wxString path = filename.GetFullPath();
        if (path != dialog.GetPath() && wxFileExists(path) &&
            wxMessageBox(Ui("기존 파일을 덮어쓰시겠습니까?"), Ui("장면 저장"),
                         wxYES_NO | wxICON_QUESTION, this) != wxYES)
            return false;
        saved = document_.SaveAs(Utf8(path));
    } else {
        saved = document_.Save();
    }
    if (!saved) {
        wxMessageBox(Ui("장면을 저장하지 못했습니다. 파일 경로와 쓰기 권한을 확인해 주세요."),
                     Ui("저장 실패"), wxOK | wxICON_ERROR, this);
        return false;
    }
    RefreshProjectExplorer();
    if (project_) {
        if (!previousPath.empty())
            project_->ClearAutosave(PathFromUtf8(previousPath));
        project_->ClearAutosave(PathFromUtf8(document_.GetPath()));
    }
    UpdateChrome();
    return true;
}

void EditorFrame::AddEntity() {
    const std::string name = "Entity " + std::to_string(document_.GetScene().GetEntityCount() + 1);
    Entity entity = document_.CreateEntity(name);
    entity.AddComponent<SpriteRendererComponent>();
    RefreshAll();
}

void EditorFrame::AddMeshEntity() {
    const std::string name =
        "3D Entity " + std::to_string(document_.GetScene().GetEntityCount() + 1);
    Entity entity = document_.CreateEntity(name);
    entity.AddComponent<MeshRendererComponent>();
    viewportTabs_->SetSelection(1);
    RefreshAll();
}

void EditorFrame::DeleteEntity() {
    if (document_.DeleteSelected())
        RefreshAll();
}

void EditorFrame::SelectEntity(std::optional<uint32> id) {
    document_.Select(id);
    RefreshHierarchy();
    RefreshInspector();
    canvas_->RefreshScene();
    canvas3D_->RefreshScene();
}

void EditorFrame::OnNameChanged() {
    if (refreshingInspector_)
        return;
    Entity selected = document_.GetSelected();
    if (!selected)
        return;
    const std::string value = Utf8(name_->GetValue());
    if (selected.HasComponent<TagComponent>())
        selected.GetComponent<TagComponent>().tag = value;
    else
        selected.AddComponent<TagComponent>(value);
    document_.MarkDirty();
    RefreshHierarchy();
    canvas_->RefreshScene();
    canvas3D_->RefreshScene();
    UpdateChrome();
}

void EditorFrame::OnTransformChanged() {
    if (refreshingInspector_)
        return;
    Entity selected = document_.GetSelected();
    if (!selected)
        return;
    auto& value = selected.HasComponent<TransformComponent>()
                      ? selected.GetComponent<TransformComponent>()
                      : selected.AddComponent<TransformComponent>();
    value.position = {static_cast<float>(transform_[0][0]->GetValue()),
                      static_cast<float>(transform_[0][1]->GetValue()),
                      static_cast<float>(transform_[0][2]->GetValue())};
    value.rotation = {ToRadians(static_cast<float>(transform_[1][0]->GetValue())),
                      ToRadians(static_cast<float>(transform_[1][1]->GetValue())),
                      ToRadians(static_cast<float>(transform_[1][2]->GetValue()))};
    value.scale = {static_cast<float>(transform_[2][0]->GetValue()),
                   static_cast<float>(transform_[2][1]->GetValue()),
                   static_cast<float>(transform_[2][2]->GetValue())};
    document_.MarkDirty();
    canvas_->RefreshScene();
    canvas3D_->RefreshScene();
    UpdateChrome();
}

void EditorFrame::OnSpriteChanged() {
    if (refreshingInspector_)
        return;
    Entity selected = document_.GetSelected();
    if (!selected)
        return;
    if (spriteEnabled_->GetValue() && !selected.HasComponent<SpriteRendererComponent>())
        selected.AddComponent<SpriteRendererComponent>();
    else if (!spriteEnabled_->GetValue() && selected.HasComponent<SpriteRendererComponent>())
        selected.RemoveComponent<SpriteRendererComponent>();
    document_.MarkDirty();
    RefreshInspector();
    canvas_->RefreshScene();
    canvas3D_->RefreshScene();
    UpdateChrome();
}

void EditorFrame::OnColorChanged() {
    if (refreshingInspector_)
        return;
    Entity selected = document_.GetSelected();
    if (!selected || !selected.HasComponent<SpriteRendererComponent>())
        return;
    selected.GetComponent<SpriteRendererComponent>().color = {
        static_cast<float>(color_[0]->GetValue()), static_cast<float>(color_[1]->GetValue()),
        static_cast<float>(color_[2]->GetValue()), static_cast<float>(color_[3]->GetValue())};
    document_.MarkDirty();
    canvas_->RefreshScene();
    canvas3D_->RefreshScene();
    UpdateChrome();
}

void EditorFrame::OnMeshChanged() {
    if (refreshingInspector_)
        return;
    Entity selected = document_.GetSelected();
    if (!selected)
        return;
    if (meshEnabled_->GetValue() && !selected.HasComponent<MeshRendererComponent>())
        selected.AddComponent<MeshRendererComponent>();
    else if (!meshEnabled_->GetValue() && selected.HasComponent<MeshRendererComponent>())
        selected.RemoveComponent<MeshRendererComponent>();
    if (selected.HasComponent<MeshRendererComponent>()) {
        auto& material = selected.GetComponent<MeshRendererComponent>().material;
        material.metallic = static_cast<float>(metallic_->GetValue());
        material.roughness = static_cast<float>(roughness_->GetValue());
    }
    document_.MarkDirty();
    RefreshInspector();
    canvas3D_->RefreshScene();
    UpdateChrome();
}

} // namespace LuxEngine
