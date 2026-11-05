#include "EditorFrame.hpp"
#include "Engine/ECS/ECS.hpp"
#include "SceneCanvas.hpp"
#include <algorithm>
#include <numbers>
#include <string>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/scrolwin.h>
#include <wx/splitter.h>

namespace LuxEngine {
namespace {
enum : int { AddEntityId = wxID_HIGHEST + 100, DeleteEntityId, SnapGridId };

wxSpinCtrlDouble* MakeNumber(wxWindow* parent, double low, double high, double initial,
                             double step) {
    auto* control =
        new wxSpinCtrlDouble(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(78, -1),
                             wxSP_ARROW_KEYS | wxTE_PROCESS_ENTER, low, high, initial, step);
    control->SetDigits(3);
    return control;
}

std::string Utf8(const wxString& value) {
    return value.ToStdString(wxConvUTF8);
}

wxString Ui(const char* text) {
    return wxString::FromUTF8(text);
}
} // namespace

EditorFrame::EditorFrame()
    : wxFrame(nullptr, wxID_ANY, "LuxEditor", wxDefaultPosition, wxSize(1280, 800)) {
    SetMinSize(wxSize(960, 600));
    CreateStatusBar();
    BuildMenus();
    BuildLayout();
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& event) {
        if (event.CanVeto() && !ConfirmDiscardChanges()) {
            event.Veto();
            return;
        }
        event.Skip();
    });
    RefreshAll();
}

void EditorFrame::BuildMenus() {
    auto* file = new wxMenu;
    file->Append(wxID_NEW, Ui("새 장면\tCtrl+N"));
    file->Append(wxID_OPEN, Ui("열기...\tCtrl+O"));
    file->AppendSeparator();
    file->Append(wxID_SAVE, Ui("저장\tCtrl+S"));
    file->Append(wxID_SAVEAS, Ui("다른 이름으로 저장...\tCtrl+Shift+S"));
    file->AppendSeparator();
    file->Append(wxID_EXIT, Ui("끝내기"));

    auto* edit = new wxMenu;
    edit->Append(AddEntityId, Ui("엔티티 추가\tCtrl+E"));
    edit->Append(DeleteEntityId, Ui("선택 삭제\tDel"));

    auto* view = new wxMenu;
    view->AppendCheckItem(SnapGridId, Ui("격자에 맞춤"));
    view->Check(SnapGridId, true);

    auto* bar = new wxMenuBar;
    bar->Append(file, Ui("파일"));
    bar->Append(edit, Ui("편집"));
    bar->Append(view, Ui("보기"));
    SetMenuBar(bar);

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
            RefreshAll();
        },
        wxID_OPEN);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { SaveScene(false); }, wxID_SAVE);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { SaveScene(true); }, wxID_SAVEAS);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Close(); }, wxID_EXIT);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { AddEntity(); }, AddEntityId);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { DeleteEntity(); }, DeleteEntityId);
    Bind(
        wxEVT_MENU, [this](wxCommandEvent& event) { canvas_->SetSnapToGrid(event.IsChecked()); },
        SnapGridId);
}

void EditorFrame::BuildLayout() {
    auto* outer =
        new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE);
    outer->SetMinimumPaneSize(190);
    outer->SetSashGravity(0.0);
    auto* hierarchyPanel = new wxPanel(outer);
    auto* inner =
        new wxSplitterWindow(outer, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE);
    inner->SetMinimumPaneSize(280);
    inner->SetSashGravity(1.0);

    auto* hierarchySizer = new wxBoxSizer(wxVERTICAL);
    hierarchySizer->Add(new wxStaticText(hierarchyPanel, wxID_ANY, Ui("장면 계층")), 0, wxALL, 10);
    hierarchy_ = new wxListBox(hierarchyPanel, wxID_ANY);
    hierarchySizer->Add(hierarchy_, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);
    auto* actions = new wxBoxSizer(wxHORIZONTAL);
    auto* add = new wxButton(hierarchyPanel, wxID_ANY, Ui("추가"));
    auto* remove = new wxButton(hierarchyPanel, wxID_ANY, Ui("삭제"));
    actions->Add(add, 1, wxRIGHT, 5);
    actions->Add(remove, 1);
    hierarchySizer->Add(actions, 0, wxEXPAND | wxALL, 8);
    hierarchyPanel->SetSizer(hierarchySizer);
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

    canvas_ = new SceneCanvas(
        inner, document_, [this](std::optional<uint32> id) { SelectEntity(id); },
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
            UpdateChrome();
        });
    auto* inspector = new wxScrolledWindow(inner, wxID_ANY, wxDefaultPosition, wxDefaultSize,
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
    bodySizer->Add(spriteBox, 0, wxEXPAND);
    inspectorBody_->SetSizer(bodySizer);
    inspectorSizer->Add(inspectorBody_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
    inspector->SetSizer(inspectorSizer);

    inner->SplitVertically(canvas_, inspector, -335);
    outer->SplitVertically(hierarchyPanel, inner, 250);
    auto* rootSizer = new wxBoxSizer(wxVERTICAL);
    rootSizer->Add(outer, 1, wxEXPAND);
    SetSizer(rootSizer);
}

void EditorFrame::RefreshAll() {
    RefreshHierarchy();
    RefreshInspector();
    canvas_->RefreshScene();
    UpdateChrome();
}

void EditorFrame::RefreshHierarchy() {
    refreshingHierarchy_ = true;
    hierarchy_->Freeze();
    hierarchy_->Clear();
    hierarchyIds_.clear();
    const Entity selected = document_.GetSelected();
    for (Entity entity : document_.GetScene().GetEntities()) {
        const uint32 id = entity.GetId();
        const std::string tag = entity.HasComponent<TagComponent>()
                                    ? entity.GetComponent<TagComponent>().tag
                                    : "Entity";
        hierarchyIds_.push_back(id);
        hierarchy_->Append(wxString::Format("%u  ", static_cast<unsigned>(id)) +
                           wxString::FromUTF8(tag.c_str()));
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
    }
    refreshingInspector_ = false;
}

void EditorFrame::UpdateChrome() {
    const wxString name =
        document_.GetPath().empty()
            ? Ui("새 장면")
            : wxFileName(wxString::FromUTF8(document_.GetPath().c_str())).GetFullName();
    SetTitle(Ui("LuxEditor — ") + name + (document_.IsDirty() ? " *" : ""));
    SetStatusText(wxString::Format(Ui("엔티티 %u개"),
                                   static_cast<unsigned>(document_.GetScene().GetEntityCount())));
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
    UpdateChrome();
    return true;
}

void EditorFrame::AddEntity() {
    const std::string name = "Entity " + std::to_string(document_.GetScene().GetEntityCount() + 1);
    Entity entity = document_.CreateEntity(name);
    entity.AddComponent<SpriteRendererComponent>();
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
    UpdateChrome();
}

} // namespace LuxEngine
