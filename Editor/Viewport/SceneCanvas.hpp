#pragma once

#include "Engine/Core/Core.hpp"

#include <wx/panel.h>

#include <functional>
#include <optional>
#include <utility>

namespace LuxEngine {

class EditorDocument;

class SceneCanvas final : public wxPanel {
public:
    SceneCanvas(wxWindow* parent, EditorDocument& document,
                std::function<void(std::optional<uint32>)> onSelect,
                std::function<void(uint32, double, double)> onMove);

    void RefreshScene();
    void FocusSelection();
    void SetSnapToGrid(bool enabled);

private:
    wxPoint WorldToScreen(double x, double y) const;
    std::pair<double, double> ScreenToWorld(const wxPoint& point) const;
    void OnPaint(wxPaintEvent& event);
    void OnLeftDown(wxMouseEvent& event);
    void OnLeftUp(wxMouseEvent& event);
    void OnMouseWheel(wxMouseEvent& event);
    void OnMiddleDown(wxMouseEvent& event);
    void OnMiddleUp(wxMouseEvent& event);
    void OnMouseMove(wxMouseEvent& event);
    void OnCaptureLost(wxMouseCaptureLostEvent& event);
    void StopSpriteDrag();

    EditorDocument& document_;
    std::function<void(std::optional<uint32>)> onSelect_;
    std::function<void(uint32, double, double)> onMove_;
    double pixelsPerUnit_ = 72.0;
    double panX_ = 0.0;
    double panY_ = 0.0;
    wxPoint lastPanPoint_;
    bool panning_ = false;
    bool draggingSprite_ = false;
    bool snapToGrid_ = true;
    uint32 dragEntityId_ = 0;
    double dragOffsetX_ = 0.0;
    double dragOffsetY_ = 0.0;
};

} // namespace LuxEngine
