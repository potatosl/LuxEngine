#include <wx/wx.h>

class LuxEditorApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new wxFrame(nullptr, wxID_ANY, "LuxEditor", wxDefaultPosition, wxSize(1280, 800));
        frame->CreateStatusBar();
        frame->SetStatusText("Ready");
        frame->Show(true);
        return true;
    }
};

wxIMPLEMENT_APP(LuxEditorApp);
