#include "Editor/EditorFrame.hpp"

class LuxEditorApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new LuxEngine::EditorFrame();
        frame->Show(true);
        return true;
    }
};

wxIMPLEMENT_APP(LuxEditorApp);
