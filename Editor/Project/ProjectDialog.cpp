#include "ProjectDialog.hpp"
#include "PathEncoding.hpp"

#include <array>
#include <cstdio>
#include <sstream>
#include <wx/choice.h>
#include <wx/dirdlg.h>
#include <wx/filedlg.h>
#include <wx/spinctrl.h>
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
        PathFromUtf8(Utf8(wxStandardPaths::Get().GetDocumentsDir())) / "NewLuxProject";
    auto* path = new wxTextCtrl(&dialog, wxID_ANY, wxString::FromUTF8(PathUtf8(initial)));
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
    return ProjectCreationRequest{PathFromUtf8(Utf8(path->GetValue())), Utf8(name->GetValue()),
                                  static_cast<ProjectTemplate>(templates->GetSelection())};
}

std::optional<ProjectSettingsRequest> ShowProjectSettingsDialog(wxWindow* parent,
                                                                const ProjectSettings& current) {
    wxDialog dialog(parent, wxID_ANY, Ui("프로젝트 설정"), wxDefaultPosition, wxSize(600, 690));
    auto* layout = new wxBoxSizer(wxVERTICAL);
    auto addLabel = [&dialog, layout](const char* label) {
        layout->Add(new wxStaticText(&dialog, wxID_ANY, Ui(label)), 0, wxLEFT | wxTOP, 10);
    };
    addLabel("프로젝트 이름");
    auto* name = new wxTextCtrl(&dialog, wxID_ANY, wxString::FromUTF8(current.name));
    layout->Add(name, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);
    addLabel("프로젝트 버전 (주.부.수정)");
    auto* versionRow = new wxBoxSizer(wxHORIZONTAL);
    auto* version = new wxTextCtrl(&dialog, wxID_ANY, wxString::FromUTF8(current.version));
    versionRow->Add(version, 1, wxRIGHT, 6);
    const std::array<const char*, 3> buttons{"주 +1", "부 +1", "수정 +1"};
    for (size_t part = 0; part < buttons.size(); ++part) {
        auto* button = new wxButton(&dialog, wxID_ANY, Ui(buttons[part]));
        versionRow->Add(button, 0, wxRIGHT, 4);
        button->Bind(wxEVT_BUTTON, [version, part](wxCommandEvent&) {
            unsigned values[3]{};
            char extra = 0;
            const std::string text = Utf8(version->GetValue());
            if (std::sscanf(text.c_str(), "%u.%u.%u%c", &values[0], &values[1], &values[2],
                            &extra) != 3 ||
                values[part] >= 9999)
                return;
            ++values[part];
            for (size_t index = part + 1; index < 3; ++index)
                values[index] = 0;
            version->ChangeValue(wxString::Format("%u.%u.%u", values[0], values[1], values[2]));
        });
    }
    layout->Add(versionRow, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);

    addLabel("프로젝트 아이콘 (PNG, JPEG, ICO, ICNS)");
    auto* iconRow = new wxBoxSizer(wxHORIZONTAL);
    auto* icon =
        new wxTextCtrl(&dialog, wxID_ANY, wxString::FromUTF8(GenericPathUtf8(current.icon)));
    auto* browse = new wxButton(&dialog, wxID_ANY, Ui("찾아보기..."));
    iconRow->Add(icon, 1, wxRIGHT, 6);
    iconRow->Add(browse, 0);
    layout->Add(iconRow, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);
    browse->Bind(wxEVT_BUTTON, [&dialog, icon](wxCommandEvent&) {
        wxFileDialog chooser(
            &dialog, Ui("프로젝트 아이콘 선택"), wxEmptyString, wxEmptyString,
            Ui("이미지 (*.png;*.jpg;*.jpeg;*.ico;*.icns)|*.png;*.jpg;*.jpeg;*.ico;*.icns"),
            wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (chooser.ShowModal() == wxID_OK)
            icon->ChangeValue(chooser.GetPath());
    });

    addLabel("자동 저장 간격 (초, 0은 사용 안 함)");
    auto* autosave =
        new wxSpinCtrl(&dialog, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                       wxSP_ARROW_KEYS, 0, 3600, static_cast<int>(current.autosaveSeconds));
    layout->Add(autosave, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);
    addLabel("보관할 백업 개수");
    auto* backup =
        new wxSpinCtrl(&dialog, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                       wxSP_ARROW_KEYS, 1, 100, static_cast<int>(current.backupLimit));
    layout->Add(backup, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);
    auto* snap = new wxCheckBox(&dialog, wxID_ANY, Ui("이 프로젝트에서 격자에 맞춤"));
    snap->SetValue(current.snapToGrid);
    layout->Add(snap, 0, wxALL, 10);

    addLabel("추가 CMake 종속성 (한 줄에 패키지 이름과 버전)");
    std::string dependencies;
    for (const auto& item : current.dependencies)
        dependencies += item.package + (item.version.empty() ? "" : " " + item.version) + "\n";
    auto* dependencyText = new wxTextCtrl(&dialog, wxID_ANY, wxString::FromUTF8(dependencies),
                                          wxDefaultPosition, wxSize(-1, 100), wxTE_MULTILINE);
    layout->Add(dependencyText, 1, wxEXPAND | wxLEFT | wxRIGHT, 10);
    layout->Add(dialog.CreateButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 10);
    dialog.SetSizer(layout);
    if (dialog.ShowModal() != wxID_OK)
        return std::nullopt;

    ProjectSettingsRequest request;
    request.settings = current;
    request.settings.name = Utf8(name->GetValue());
    request.settings.version = Utf8(version->GetValue());
    request.settings.autosaveSeconds = static_cast<uint32_t>(autosave->GetValue());
    request.settings.backupLimit = static_cast<uint32_t>(backup->GetValue());
    request.settings.snapToGrid = snap->GetValue();
    request.settings.dependencies.clear();
    std::istringstream lines(Utf8(dependencyText->GetValue()));
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream fields(line);
        ProjectDependency dependency;
        std::string extra;
        if (!(fields >> dependency.package))
            continue;
        fields >> dependency.version;
        if (fields >> extra) {
            wxMessageBox(Ui("종속성은 한 줄에 패키지 이름과 버전만 입력해 주세요."),
                         Ui("프로젝트 설정"), wxOK | wxICON_ERROR, parent);
            return std::nullopt;
        }
        request.settings.dependencies.push_back(std::move(dependency));
    }
    const auto iconValue = Utf8(icon->GetValue());
    if (iconValue.empty()) {
        request.settings.icon.clear();
        request.clearIcon = true;
    } else if (iconValue != GenericPathUtf8(current.icon)) {
        request.iconSource = PathFromUtf8(iconValue);
    }
    return request;
}

std::optional<std::filesystem::path>
ShowProjectDestinationDialog(wxWindow* parent, const char* title, const std::string& suggested) {
    const auto initial =
        PathFromUtf8(Utf8(wxStandardPaths::Get().GetDocumentsDir())) / PathFromUtf8(suggested);
    wxTextEntryDialog dialog(parent, Ui("새 프로젝트 폴더의 전체 경로를 입력해 주세요."), Ui(title),
                             wxString::FromUTF8(PathUtf8(initial)));
    if (dialog.ShowModal() != wxID_OK)
        return std::nullopt;
    return PathFromUtf8(Utf8(dialog.GetValue()));
}

} // namespace LuxEngine
