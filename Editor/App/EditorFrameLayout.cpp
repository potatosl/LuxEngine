#include "EditorFrame.hpp"

#include "Engine/ECS/ECS.hpp"
#include "Viewport/SceneCanvas.hpp"
#include "Viewport/SceneCanvas3D.hpp"
#include <algorithm>
#include <wx/aui/auibook.h>
#include <wx/aui/dockart.h>
#include <wx/aui/framemanager.h>
#include <wx/aui/tabart.h>
#include <wx/config.h>
#include <wx/scrolwin.h>

namespace LuxEngine {
namespace {
wxString Ui(const char* text) {
    return wxString::FromUTF8(text);
}
wxSpinCtrlDouble* MakeNumber(wxWindow* parent, double low, double high, double initial,
                             double step) {
    auto* control =
        new wxSpinCtrlDouble(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(78, -1),
                             wxSP_ARROW_KEYS | wxTE_PROCESS_ENTER, low, high, initial, step);
    control->SetDigits(3);
    return control;
}
} // namespace

void EditorFrame::BuildLayout() {
    auto* hierarchyPanel = new wxPanel(this);

    auto* hierarchySizer = new wxBoxSizer(wxVERTICAL);
    hierarchySizer->Add(new wxStaticText(hierarchyPanel, wxID_ANY, Ui("장면 계층")), 0, wxALL, 10);
    hierarchySearch_ = new wxSearchCtrl(hierarchyPanel, wxID_ANY);
    hierarchySearch_->SetHint(Ui("엔티티 검색"));
    hierarchySearch_->ShowCancelButton(true);
    hierarchySizer->Add(hierarchySearch_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
    hierarchy_ = new wxListBox(hierarchyPanel, wxID_ANY);
    hierarchySizer->Add(hierarchy_, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);
    auto* actions = new wxBoxSizer(wxHORIZONTAL);
    auto* add = new wxButton(hierarchyPanel, wxID_ANY, Ui("추가"));
    auto* remove = new wxButton(hierarchyPanel, wxID_ANY, Ui("삭제"));
    actions->Add(add, 1, wxRIGHT, 5);
    actions->Add(remove, 1);
    hierarchySizer->Add(actions, 0, wxEXPAND | wxALL, 8);
    hierarchyPanel->SetSizer(hierarchySizer);
    auto* projectPanel = new wxPanel(this);
    auto* projectSizer = new wxBoxSizer(wxVERTICAL);
    projectSizer->Add(new wxStaticText(projectPanel, wxID_ANY, Ui("프로젝트 파일")), 0, wxALL, 10);
    projectSearch_ = new wxSearchCtrl(projectPanel, wxID_ANY);
    projectSearch_->SetHint(Ui("파일 검색"));
    projectSearch_->ShowCancelButton(true);
    projectSizer->Add(projectSearch_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
    projectFiles_ = new wxListBox(projectPanel, wxID_ANY);
    projectSizer->Add(projectFiles_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
    projectPanel->SetSizer(projectSizer);
    projectFiles_->Bind(wxEVT_LISTBOX_DCLICK, [this](wxCommandEvent&) {
        const int selection = projectFiles_->GetSelection();
        if (selection == wxNOT_FOUND ||
            static_cast<size_t>(selection) >= projectFilePaths_.size() ||
            projectFilePaths_[static_cast<size_t>(selection)].extension() != ".lux")
            return;
        OpenProjectScene(projectFilePaths_[static_cast<size_t>(selection)]);
    });
    add->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { AddEntity(); });
    remove->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { DeleteEntity(); });
    hierarchy_->Bind(wxEVT_LISTBOX, [this](wxCommandEvent&) {
        if (refreshingHierarchy_)
            return;
        const int index = hierarchy_->GetSelection();
        SelectEntity(index == wxNOT_FOUND
                         ? std::nullopt
                         : std::optional<uint32>(hierarchyIds_.at(static_cast<size_t>(index))));
    });
    hierarchySearch_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { RefreshHierarchy(); });
    projectSearch_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { RefreshProjectExplorer(); });
    hierarchy_->Bind(wxEVT_CONTEXT_MENU, [this](wxContextMenuEvent&) {
        wxMenu menu;
        menu.Append(wxID_ADD, Ui("2D 엔티티 추가"));
        menu.Append(wxID_ANY, Ui("3D 메시 엔티티 추가"));
        const int meshId = menu.FindItem(Ui("3D 메시 엔티티 추가"));
        menu.AppendSeparator();
        menu.Append(wxID_DELETE, Ui("선택 삭제"));
        menu.Bind(wxEVT_MENU, [this, meshId](wxCommandEvent& event) {
            if (event.GetId() == wxID_ADD)
                AddEntity();
            else if (event.GetId() == meshId)
                AddMeshEntity();
            else if (event.GetId() == wxID_DELETE)
                DeleteEntity();
        });
        PopupMenu(&menu);
    });

    viewportTabs_ = new wxAuiNotebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                      wxAUI_NB_TOP | wxAUI_NB_TAB_MOVE | wxAUI_NB_SCROLL_BUTTONS);
    canvas_ = new SceneCanvas(
        viewportTabs_, document_, [this](std::optional<uint32> id) { SelectEntity(id); },
        [this](uint32 id, double x, double y) {
            if (!document_.GetScene().HasComponent<TransformComponent>(id))
                return;
            auto& position = document_.GetScene().GetComponent<TransformComponent>(id).position;
            const float nextX = static_cast<float>(std::clamp(x, -100000.0, 100000.0));
            const float nextY = static_cast<float>(std::clamp(y, -100000.0, 100000.0));
            if (position.x == nextX && position.y == nextY)
                return;
            position.x = nextX;
            position.y = nextY;
            document_.MarkDirty();
            RefreshInspector();
            canvas_->RefreshScene();
            canvas3D_->RefreshScene();
            UpdateChrome();
        });
    auto* inspector = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                           wxVSCROLL | wxTAB_TRAVERSAL);
    inspector->SetScrollRate(0, 12);
    auto* inspectorSizer = new wxBoxSizer(wxVERTICAL);
    inspectorSizer->Add(new wxStaticText(inspector, wxID_ANY, Ui("속성")), 0, wxALL, 10);
    selectionLabel_ = new wxStaticText(inspector, wxID_ANY, Ui("선택된 엔티티 없음"));
    inspectorSizer->Add(selectionLabel_, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);
    inspectorBody_ = new wxPanel(inspector);
    auto* bodySizer = new wxBoxSizer(wxVERTICAL);
    bodySizer->Add(new wxStaticText(inspectorBody_, wxID_ANY, Ui("이름")), 0, wxBOTTOM, 4);
    name_ = new wxTextCtrl(inspectorBody_, wxID_ANY);
    bodySizer->Add(name_, 0, wxEXPAND | wxBOTTOM, 16);
    name_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { OnNameChanged(); });

    auto* transformBox = new wxStaticBoxSizer(wxVERTICAL, inspectorBody_, Ui("변환"));
    auto* grid = new wxFlexGridSizer(4, 5, 5);
    for (int col = 1; col < 4; ++col)
        grid->AddGrowableCol(col, 1);
    grid->Add(new wxStaticText(inspectorBody_, wxID_ANY, wxEmptyString));
    for (const char* axis : {"X", "Y", "Z"})
        grid->Add(new wxStaticText(inspectorBody_, wxID_ANY, axis), 0, wxALIGN_CENTER);
    const std::array<wxString, 3> labels{Ui("위치"), Ui("회전 °"), Ui("크기")};
    for (size_t row = 0; row < labels.size(); ++row) {
        grid->Add(new wxStaticText(inspectorBody_, wxID_ANY, labels[row]), 0,
                  wxALIGN_CENTER_VERTICAL);
        for (size_t col = 0; col < 3; ++col) {
            const double initial = row == 2 ? 1.0 : 0.0;
            auto* control =
                MakeNumber(inspectorBody_, -100000.0, 100000.0, initial, row == 1 ? 5.0 : 0.1);
            transform_[row][col] = control;
            grid->Add(control, 1, wxEXPAND);
            control->Bind(wxEVT_SPINCTRLDOUBLE,
                          [this](wxSpinDoubleEvent&) { OnTransformChanged(); });
            control->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { OnTransformChanged(); });
        }
    }
    transformBox->Add(grid, 1, wxEXPAND | wxALL, 6);
    bodySizer->Add(transformBox, 0, wxEXPAND | wxBOTTOM, 15);

    auto* spriteBox = new wxStaticBoxSizer(wxVERTICAL, inspectorBody_, Ui("2D 스프라이트"));
    spriteEnabled_ = new wxCheckBox(inspectorBody_, wxID_ANY, Ui("스프라이트 사용"));
    spriteBox->Add(spriteEnabled_, 0, wxALL, 6);
    spriteEnabled_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { OnSpriteChanged(); });
    auto* colorGrid = new wxFlexGridSizer(4, 5, 5);
    for (int col = 0; col < 4; ++col)
        colorGrid->AddGrowableCol(col, 1);
    for (const char* channel : {"R", "G", "B", "A"})
        colorGrid->Add(new wxStaticText(inspectorBody_, wxID_ANY, channel), 0, wxALIGN_CENTER);
    for (auto& control : color_) {
        control = MakeNumber(inspectorBody_, 0.0, 1.0, 1.0, 0.05);
        colorGrid->Add(control, 1, wxEXPAND);
        control->Bind(wxEVT_SPINCTRLDOUBLE, [this](wxSpinDoubleEvent&) { OnColorChanged(); });
        control->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { OnColorChanged(); });
    }
    spriteBox->Add(colorGrid, 0, wxEXPAND | wxALL, 6);
    bodySizer->Add(spriteBox, 0, wxEXPAND | wxBOTTOM, 12);
    auto* meshBox = new wxStaticBoxSizer(wxVERTICAL, inspectorBody_, Ui("3D 메시"));
    meshEnabled_ = new wxCheckBox(inspectorBody_, wxID_ANY, Ui("메시 사용"));
    meshBox->Add(meshEnabled_, 0, wxALL, 6);
    meshEnabled_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { OnMeshChanged(); });
    auto* materialGrid = new wxFlexGridSizer(2, 2, 5, 6);
    materialGrid->AddGrowableCol(1, 1);
    materialGrid->Add(new wxStaticText(inspectorBody_, wxID_ANY, Ui("금속성")), 0,
                      wxALIGN_CENTER_VERTICAL);
    metallic_ = MakeNumber(inspectorBody_, 0.0, 1.0, 0.0, 0.05);
    materialGrid->Add(metallic_, 1, wxEXPAND);
    materialGrid->Add(new wxStaticText(inspectorBody_, wxID_ANY, Ui("거칠기")), 0,
                      wxALIGN_CENTER_VERTICAL);
    roughness_ = MakeNumber(inspectorBody_, 0.0, 1.0, 0.5, 0.05);
    materialGrid->Add(roughness_, 1, wxEXPAND);
    meshBox->Add(materialGrid, 0, wxEXPAND | wxALL, 6);
    metallic_->Bind(wxEVT_SPINCTRLDOUBLE, [this](wxSpinDoubleEvent&) { OnMeshChanged(); });
    roughness_->Bind(wxEVT_SPINCTRLDOUBLE, [this](wxSpinDoubleEvent&) { OnMeshChanged(); });
    metallic_->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { OnMeshChanged(); });
    roughness_->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { OnMeshChanged(); });
    bodySizer->Add(meshBox, 0, wxEXPAND);
    inspectorBody_->SetSizer(bodySizer);
    inspectorSizer->Add(inspectorBody_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
    inspector->SetSizer(inspectorSizer);

    canvas3D_ = new SceneCanvas3D(viewportTabs_, document_,
                                  [this](std::optional<uint32> id) { SelectEntity(id); });
    viewportTabs_->AddPage(canvas_, Ui("2D 장면"), true);
    viewportTabs_->AddPage(canvas3D_, Ui("3D 배치"));
    viewportTabs_->Bind(wxEVT_AUINOTEBOOK_PAGE_CHANGED, [this](wxAuiNotebookEvent& event) {
        if (viewMode_)
            viewMode_->SetSelection(event.GetSelection());
        SetStatusText(event.GetSelection() == 1 ? Ui("3D 배치 미리보기") : Ui("2D 장면"), 1);
        event.Skip();
    });

    auto* commandBar = new wxPanel(this);
    commandBar->SetBackgroundColour(wxColour(32, 38, 48));
    auto* commands = new wxBoxSizer(wxHORIZONTAL);
    auto addCommand = [commandBar, commands](const char* label, auto action) {
        auto* button = new wxButton(commandBar, wxID_ANY, Ui(label), wxDefaultPosition,
                                    wxSize(-1, 30), wxBORDER_NONE);
        button->SetBackgroundColour(wxColour(53, 65, 81));
        button->SetForegroundColour(wxColour(234, 240, 246));
        button->Bind(wxEVT_BUTTON, action);
        commands->Add(button, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, 6);
    };
    addCommand("새 프로젝트", [this](wxCommandEvent&) { NewProject(); });
    addCommand("프로젝트 열기", [this](wxCommandEvent&) { OpenProjectDialog(); });
    addCommand("장면 저장", [this](wxCommandEvent&) { SaveScene(false); });
    addCommand("엔티티 추가", [this](wxCommandEvent&) { AddEntity(); });
    addCommand("3D 메시 추가", [this](wxCommandEvent&) { AddMeshEntity(); });
    commands->AddSpacer(16);
    viewMode_ = new wxChoice(commandBar, wxID_ANY);
    viewMode_->Append(Ui("2D 작업"));
    viewMode_->Append(Ui("3D 배치"));
    viewMode_->SetSelection(0);
    viewMode_->SetMinSize(wxSize(105, 30));
    viewMode_->Bind(wxEVT_CHOICE, [this](wxCommandEvent& event) {
        viewportTabs_->SetSelection(event.GetSelection());
    });
    commands->Add(viewMode_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 14);
    projectLabel_ = new wxStaticText(commandBar, wxID_ANY, Ui("프로젝트 없음"), wxDefaultPosition,
                                     wxSize(240, -1), wxST_ELLIPSIZE_END);
    projectLabel_->SetForegroundColour(wxColour(232, 235, 240));
    commands->AddStretchSpacer();
    commands->Add(projectLabel_, 0, wxALIGN_CENTER_VERTICAL);
    commandBar->SetSizer(commands);

    auto* console = new wxPanel(this);
    auto* consoleSizer = new wxBoxSizer(wxVERTICAL);
    consoleOutput_ =
        new wxTextCtrl(console, wxID_ANY, Ui("작업 기록을 표시합니다.\n"), wxDefaultPosition,
                       wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2 | wxBORDER_NONE);
    consoleSizer->Add(consoleOutput_, 1, wxEXPAND | wxALL, 4);
    console->SetSizer(consoleSizer);

    aui_.SetManagedWindow(this);
    aui_.SetFlags(wxAUI_MGR_DEFAULT | wxAUI_MGR_LIVE_RESIZE);
    aui_.AddPane(commandBar, wxAuiPaneInfo()
                                 .Name("commands")
                                 .Top()
                                 .CaptionVisible(false)
                                 .PaneBorder(false)
                                 .Resizable(false)
                                 .Floatable(false)
                                 .DockFixed()
                                 .BestSize(-1, 42));
    aui_.AddPane(
        viewportTabs_,
        wxAuiPaneInfo().Name("viewport").CenterPane().CaptionVisible(false).CloseButton(false));
    aui_.AddPane(hierarchyPanel, wxAuiPaneInfo()
                                     .Name("hierarchy")
                                     .Caption(Ui("장면 계층"))
                                     .Left()
                                     .BestSize(270, 400)
                                     .MinSize(190, 160)
                                     .CloseButton(true)
                                     .Floatable(true));
    aui_.AddPane(inspector, wxAuiPaneInfo()
                                .Name("inspector")
                                .Caption(Ui("속성"))
                                .Right()
                                .BestSize(340, 500)
                                .MinSize(260, 180)
                                .CloseButton(true)
                                .Floatable(true));
    aui_.AddPane(projectPanel, wxAuiPaneInfo()
                                   .Name("project")
                                   .Caption(Ui("프로젝트 파일"))
                                   .Bottom()
                                   .BestSize(620, 210)
                                   .MinSize(250, 120)
                                   .CloseButton(true)
                                   .Floatable(true));
    aui_.AddPane(console, wxAuiPaneInfo()
                              .Name("console")
                              .Caption(Ui("콘솔"))
                              .Bottom()
                              .BestSize(450, 210)
                              .MinSize(250, 120)
                              .CloseButton(true)
                              .Floatable(true));
    aui_.Update();
    defaultPerspective_ = aui_.SavePerspective();
    ApplyWorkspaceTheme();
    RestoreWorkspaceLayout();
}

void EditorFrame::ApplyWorkspaceTheme() {
    const wxColour background(36, 43, 53);
    const wxColour surface(43, 52, 64);
    const wxColour text(225, 234, 243);
    auto* art = aui_.GetArtProvider();
    art->SetColour(wxAUI_DOCKART_BACKGROUND_COLOUR, background);
    art->SetColour(wxAUI_DOCKART_SASH_COLOUR, wxColour(25, 30, 38));
    art->SetColour(wxAUI_DOCKART_ACTIVE_CAPTION_COLOUR, wxColour(49, 67, 87));
    art->SetColour(wxAUI_DOCKART_ACTIVE_CAPTION_GRADIENT_COLOUR, wxColour(49, 67, 87));
    art->SetColour(wxAUI_DOCKART_INACTIVE_CAPTION_COLOUR, surface);
    art->SetColour(wxAUI_DOCKART_INACTIVE_CAPTION_GRADIENT_COLOUR, surface);
    art->SetColour(wxAUI_DOCKART_ACTIVE_CAPTION_TEXT_COLOUR, text);
    art->SetColour(wxAUI_DOCKART_INACTIVE_CAPTION_TEXT_COLOUR, wxColour(178, 193, 209));
    art->SetColour(wxAUI_DOCKART_BORDER_COLOUR, wxColour(24, 30, 38));
    auto* tabArt = new wxAuiSimpleTabArt();
    tabArt->SetColour(surface);
    tabArt->SetActiveColour(wxColour(60, 81, 105));
    viewportTabs_->SetArtProvider(tabArt);
    for (const char* name : {"hierarchy", "project", "inspector", "console"}) {
        auto& pane = aui_.GetPane(name);
        if (pane.IsOk()) {
            pane.window->SetBackgroundColour(background);
            pane.window->SetForegroundColour(text);
        }
    }
    inspectorBody_->SetBackgroundColour(background);
    inspectorBody_->SetForegroundColour(text);
    for (wxWindow* control :
         std::array<wxWindow*, 4>{hierarchy_, projectFiles_, consoleOutput_, name_}) {
        control->SetBackgroundColour(wxColour(29, 36, 46));
        control->SetForegroundColour(text);
    }
    aui_.Update();
}

void EditorFrame::RestoreWorkspaceLayout() {
    wxConfig config("LuxEditor");
    wxString perspective;
    if (config.Read("Workspace/Perspective", &perspective) && !perspective.empty() &&
        !aui_.LoadPerspective(perspective, true))
        aui_.LoadPerspective(defaultPerspective_, true);
}

void EditorFrame::SaveWorkspaceLayout() {
    wxConfig config("LuxEditor");
    config.Write("Workspace/Perspective", aui_.SavePerspective());
    config.Flush();
}

void EditorFrame::ResetWorkspaceLayout() {
    aui_.LoadPerspective(defaultPerspective_, true);
    SetStatusText(Ui("기본 작업 공간 배치를 복원했습니다."));
}

void EditorFrame::ShowWorkspacePane(const char* name) {
    auto& pane = aui_.GetPane(name);
    if (pane.IsOk()) {
        pane.Show(!pane.IsShown());
        aui_.Update();
    }
}

} // namespace LuxEngine
