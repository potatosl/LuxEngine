#include "ProjectDialog.hpp"

#include <wx/choice.h>
#include <wx/dirdlg.h>
#include <wx/stdpaths.h>
#include <wx/textdlg.h>
#include <wx/wx.h>

namespace LuxEngine {
namespace {
wxString Ui(const char* text) {
    return wxString::FromUTF8(text);
}

std::string Utf8(const wxString& value) {
    return value.ToStdString(wxConvUTF8);
}
} // namespace

std::optional<ProjectCreationRequest> ShowNewProjectDialog(wxWindow* parent) {
    wxDialog dialog(parent, wxID_ANY, Ui("새 프로젝트"), wxDefaultPosition, wxSize(560, 360));
    auto* layout = new wxBoxSizer(wxVERTICAL);
    layout->Add(new wxStaticText(&dialog, wxID_ANY, Ui("프로젝트 이름")), 0, wxLEFT | wxTOP, 12);
    auto* name = new wxTextCtrl(&dialog, wxID_ANY, Ui("새 프로젝트"));
    layout->Add(name, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
    layout->Add(new wxStaticText(&dialog, wxID_ANY, Ui("프로젝트 폴더 (새 폴더 경로)")), 0, wxLEFT,
                12);
    auto* pathRow = new wxBoxSizer(wxHORIZONTAL);
    const auto initial =
        std::filesystem::path(Utf8(wxStandardPaths::Get().GetDocumentsDir())) / "NewLuxProject";
    auto* path = new wxTextCtrl(&dialog, wxID_ANY, wxString::FromUTF8(initial.string().c_str()));
    auto* browse = new wxButton(&dialog, wxID_ANY, Ui("찾아보기..."));
    pathRow->Add(path, 1, wxRIGHT, 6);
    pathRow->Add(browse, 0);
    layout->Add(pathRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
    browse->Bind(wxEVT_BUTTON, [parent, path, name](wxCommandEvent&) {
        wxDirDialog chooser(parent, Ui("프로젝트 상위 폴더 선택"),
                            wxStandardPaths::Get().GetDocumentsDir(), wxDD_DEFAULT_STYLE);
        if (chooser.ShowModal() == wxID_OK)
            path->ChangeValue(chooser.GetPath() + wxFILE_SEP_PATH + name->GetValue());
    });

    layout->Add(new wxStaticText(&dialog, wxID_ANY, Ui("템플릿")), 0, wxLEFT, 12);
    auto* templates = new wxChoice(&dialog, wxID_ANY);
    for (const char* label : {"빈 프로젝트", "플랫폼 게임", "탑다운 게임", "RPG", "퍼즐 게임"})
        templates->Append(Ui(label));
    templates->SetSelection(0);
    layout->Add(templates, 0, wxEXPAND | wxLEFT | wxRIGHT, 12);
    auto* description =
        new wxStaticText(&dialog, wxID_ANY, Ui("빈 장면과 Assets 폴더를 만듭니다."));
    layout->Add(description, 0, wxEXPAND | wxALL, 12);
    templates->Bind(wxEVT_CHOICE, [description](wxCommandEvent& event) {
        constexpr const char* descriptions[] = {
            "빈 장면과 Assets 폴더를 만듭니다.", "바닥과 플레이어가 있는 2D 시작 장면을 만듭니다.",
            "지면과 플레이어가 있는 2D 시작 장면을 만듭니다.",
            "마을과 주인공, NPC가 있는 2D 시작 장면을 만듭니다.",
            "보드와 블록이 있는 2D 시작 장면을 만듭니다."};
        const int selection = event.GetSelection();
        if (selection >= 0 && selection < 5)
            description->SetLabel(Ui(descriptions[selection]));
    });
    layout->Add(dialog.CreateButtonSizer(wxOK | wxCANCEL), 0,
                wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
    dialog.SetSizerAndFit(layout);
    if (dialog.ShowModal() != wxID_OK)
        return std::nullopt;
    return ProjectCreationRequest{Utf8(path->GetValue()), Utf8(name->GetValue()),
                                  static_cast<ProjectTemplate>(templates->GetSelection())};
}

std::optional<std::string> ShowProjectNameDialog(wxWindow* parent, const std::string& currentName) {
    wxTextEntryDialog dialog(
        parent, Ui("프로젝트 이름을 변경합니다.\n프로젝트 파일과 폴더 위치는 그대로 유지됩니다."),
        Ui("프로젝트 설정"), wxString::FromUTF8(currentName.c_str()));
    if (dialog.ShowModal() != wxID_OK)
        return std::nullopt;
    return Utf8(dialog.GetValue());
}

} // namespace LuxEngine
