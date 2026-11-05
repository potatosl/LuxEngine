#include "EditorFrame.hpp"

#include "Project/PathEncoding.hpp"
#include "Project/ProjectArchive.hpp"
#include "Project/ProjectDialog.hpp"
#include <wx/filedlg.h>
#include <wx/textdlg.h>

namespace LuxEngine {
namespace {
wxString Ui(const char* text) {
    return wxString::FromUTF8(text);
}

std::string Utf8(const wxString& value) {
    return value.ToStdString(wxConvUTF8);
}

void ShowProjectError(wxWindow* parent, const std::string& error) {
    wxMessageBox(wxString::FromUTF8(error), Ui("프로젝트 작업 실패"), wxOK | wxICON_ERROR, parent);
}
} // namespace

void EditorFrame::CloneCurrentProject() {
    if (!project_ || (document_.IsDirty() && !SaveScene(false)))
        return;
    wxTextEntryDialog nameDialog(this, Ui("복제할 프로젝트의 이름을 입력해 주세요."),
                                 Ui("프로젝트 복제"),
                                 wxString::FromUTF8(project_->GetName() + " 복제"));
    if (nameDialog.ShowModal() != wxID_OK)
        return;
    auto destination =
        ShowProjectDestinationDialog(this, "프로젝트 복제", Utf8(nameDialog.GetValue()));
    if (!destination)
        return;
    std::string error;
    auto copy = CloneProject(*project_, *destination, Utf8(nameDialog.GetValue()), error);
    if (!copy) {
        ShowProjectError(this, error);
        return;
    }
    OpenProjectFile(copy->GetFile());
}

void EditorFrame::ImportProjectDialog() {
    wxFileDialog dialog(this, Ui("프로젝트 가져오기"), wxEmptyString, wxEmptyString,
                        Ui("Lux 프로젝트 패키지 (*.luxpack;*.luxbackup)|*.luxpack;*.luxbackup"),
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK)
        return;
    const auto archive = PathFromUtf8(Utf8(dialog.GetPath()));
    auto destination =
        ShowProjectDestinationDialog(this, "프로젝트 가져오기", PathUtf8(archive.stem()));
    if (!destination || !ConfirmDiscardChanges())
        return;
    std::string error;
    auto imported = ImportProject(archive, *destination, error);
    if (!imported) {
        ShowProjectError(this, error);
        return;
    }
    OpenProjectFile(imported->GetFile());
}

void EditorFrame::ExportCurrentProject() {
    if (!project_ || (document_.IsDirty() && !SaveScene(false)))
        return;
    wxFileDialog dialog(this, Ui("프로젝트 내보내기"), wxEmptyString,
                        wxString::FromUTF8(project_->GetName() + ".luxpack"),
                        Ui("Lux 프로젝트 패키지 (*.luxpack)|*.luxpack"),
                        wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dialog.ShowModal() != wxID_OK)
        return;
    auto archive = PathFromUtf8(Utf8(dialog.GetPath()));
    if (archive.extension() != ".luxpack")
        archive += ".luxpack";
    std::string error;
    if (!ExportProject(*project_, archive, error)) {
        ShowProjectError(this, error);
        return;
    }
    SetStatusText(Ui("프로젝트 패키지를 내보냈습니다."));
}

void EditorFrame::BackupCurrentProject() {
    if (!project_ || (document_.IsDirty() && !SaveScene(false)))
        return;
    std::filesystem::path backup;
    std::string error;
    if (!CreateProjectBackup(*project_, backup, error)) {
        ShowProjectError(this, error);
        return;
    }
    SetStatusText(Ui("프로젝트 백업을 만들었습니다."));
}

void EditorFrame::RestoreCurrentProject() {
    if (!project_)
        return;
    const auto directory = project_->GetFile().parent_path() / ".lux" / "backups";
    wxFileDialog dialog(this, Ui("프로젝트 백업 복구"), wxString::FromUTF8(PathUtf8(directory)),
                        wxEmptyString, Ui("Lux 백업 (*.luxbackup)|*.luxbackup"),
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK || !ConfirmDiscardChanges())
        return;
    if (wxMessageBox(
            Ui("현재 프로젝트를 선택한 백업으로 복구하시겠습니까? 복구 전 상태도 별도 백업합니다."),
            Ui("프로젝트 백업 복구"), wxYES_NO | wxICON_QUESTION, this) != wxYES)
        return;
    std::string error;
    const auto projectFile = project_->GetFile();
    if (!RestoreProjectBackup(*project_, PathFromUtf8(Utf8(dialog.GetPath())), error)) {
        ShowProjectError(this, error);
        return;
    }
    auto restored = EditorProject::Open(projectFile, error);
    if (!restored || !document_.Open(PathUtf8(restored->GetSceneFile()))) {
        ShowProjectError(this, error.empty() ? "복구한 프로젝트를 열 수 없습니다." : error);
        return;
    }
    project_ = std::move(restored);
    ApplyProjectPreferences();
    RefreshAll();
    SetStatusText(Ui("프로젝트를 백업에서 복구했습니다."));
}

void EditorFrame::RescanProjectResources() {
    if (!project_)
        return;
    std::string error;
    const size_t count = project_->RescanResources(error);
    if (!error.empty()) {
        ShowProjectError(this, error);
        return;
    }
    RefreshProjectExplorer();
    SetStatusText(wxString::Format(Ui("리소스 %zu개를 재검색했습니다."), count));
}

} // namespace LuxEngine
