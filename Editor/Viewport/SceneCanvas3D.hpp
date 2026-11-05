#pragma once

#include "Engine/Core/Math.hpp"
#include <functional>
#include <optional>
#include <wx/panel.h>

namespace LuxEngine {
class EditorDocument;

class SceneCanvas3D final : public wxPanel {
public:
    SceneCanvas3D(wxWindow* parent, EditorDocument& document,
                  std::function<void(std::optional<uint32>)> onSelect);
    void RefreshScene();
    void FocusSelection();

private:
    std::optional<wxPoint> Project(const Vec3& position) const;
    void OnPaint(wxPaintEvent& event);
    void OnLeftDown(wxMouseEvent& event);
    void OnMouseMove(wxMouseEvent& event);
    void OnMouseWheel(wxMouseEvent& event);
    void OnCaptureLost(wxMouseCaptureLostEvent& event);

    EditorDocument& document_;
    std::function<void(std::optional<uint32>)> onSelect_;
    Vec3 focus_{0.0f, 0.0f, 0.0f};
    double yaw_ = 0.65;
    double pitch_ = 0.48;
    double distance_ = 22.0;
    wxPoint lastMouse_;
    bool orbiting_ = false;
    bool panning_ = false;
};
} // namespace LuxEngine
