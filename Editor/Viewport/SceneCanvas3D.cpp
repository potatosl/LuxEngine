#include "SceneCanvas3D.hpp"

#include "Engine/ECS/ECS.hpp"
#include "Scene/EditorDocument.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <wx/dcbuffer.h>

namespace LuxEngine {
namespace {
const wxColour Background(27, 33, 42);
const wxColour Grid(56, 66, 78);
const wxColour Highlight(248, 197, 91);

Vec3 Add(const Vec3& a, const Vec3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 Subtract(const Vec3& a, const Vec3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

bool Finite(const Vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
} // namespace

SceneCanvas3D::SceneCanvas3D(wxWindow* parent, EditorDocument& document,
                             std::function<void(std::optional<uint32>)> onSelect)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
              wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE),
      document_(document), onSelect_(std::move(onSelect)) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Bind(wxEVT_PAINT, &SceneCanvas3D::OnPaint, this);
    Bind(wxEVT_LEFT_DOWN, &SceneCanvas3D::OnLeftDown, this);
    Bind(wxEVT_RIGHT_DOWN, [this](wxMouseEvent& event) {
        SetFocus();
        lastMouse_ = event.GetPosition();
        orbiting_ = true;
        if (!HasCapture())
            CaptureMouse();
    });
    Bind(wxEVT_RIGHT_UP, [this](wxMouseEvent&) {
        orbiting_ = false;
        if (HasCapture() && !panning_)
            ReleaseMouse();
    });
    Bind(wxEVT_MIDDLE_DOWN, [this](wxMouseEvent& event) {
        lastMouse_ = event.GetPosition();
        panning_ = true;
        if (!HasCapture())
            CaptureMouse();
    });
    Bind(wxEVT_MIDDLE_UP, [this](wxMouseEvent&) {
        panning_ = false;
        if (HasCapture() && !orbiting_)
            ReleaseMouse();
    });
    Bind(wxEVT_MOTION, &SceneCanvas3D::OnMouseMove, this);
    Bind(wxEVT_MOUSEWHEEL, &SceneCanvas3D::OnMouseWheel, this);
    Bind(wxEVT_MOUSE_CAPTURE_LOST, &SceneCanvas3D::OnCaptureLost, this);
    Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& event) {
        if (event.GetKeyCode() == 'F' && !event.HasModifiers()) {
            FocusSelection();
            return;
        }
        event.Skip();
    });
}

void SceneCanvas3D::RefreshScene() {
    Refresh(false);
}

void SceneCanvas3D::FocusSelection() {
    const Entity selected = document_.GetSelected();
    if (selected && selected.HasComponent<TransformComponent>()) {
        focus_ = selected.GetComponent<TransformComponent>().position;
        RefreshScene();
    }
}

std::optional<wxPoint> SceneCanvas3D::Project(const Vec3& position) const {
    const Vec3 relative = Subtract(position, focus_);
    const double cy = std::cos(yaw_), sy = std::sin(yaw_);
    const double cp = std::cos(pitch_), sp = std::sin(pitch_);
    const double horizontal = relative.x * cy - relative.z * sy;
    const double vertical = relative.x * sy * sp + relative.y * cp + relative.z * cy * sp;
    const double depth = distance_ + relative.x * sy * cp - relative.y * sp + relative.z * cy * cp;
    if (depth < 0.2 || !std::isfinite(depth))
        return std::nullopt;
    const wxSize size = GetClientSize();
    const double focal = std::max(1, size.GetHeight()) * 0.95;
    const double x = size.GetWidth() * 0.5 + horizontal * focal / depth;
    const double y = size.GetHeight() * 0.5 - vertical * focal / depth;
    if (!std::isfinite(x) || !std::isfinite(y) || std::abs(x) > 1000000 || std::abs(y) > 1000000)
        return std::nullopt;
    return wxPoint(static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y)));
}

void SceneCanvas3D::OnPaint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(Background));
    dc.Clear();
    const wxSize size = GetClientSize();
    if (size.GetWidth() <= 0 || size.GetHeight() <= 0)
        return;

    for (int unit = -20; unit <= 20; ++unit) {
        const wxColour line = unit == 0       ? wxColour(95, 108, 124)
                              : unit % 5 == 0 ? wxColour(70, 81, 95)
                                              : Grid;
        dc.SetPen(wxPen(line, unit == 0 ? 2 : 1));
        auto a = Project({static_cast<float>(unit), 0.0f, -20.0f});
        auto b = Project({static_cast<float>(unit), 0.0f, 20.0f});
        if (a && b)
            dc.DrawLine(*a, *b);
        a = Project({-20.0f, 0.0f, static_cast<float>(unit)});
        b = Project({20.0f, 0.0f, static_cast<float>(unit)});
        if (a && b)
            dc.DrawLine(*a, *b);
    }
    const auto origin = Project({0, 0, 0});
    for (const auto& [axis, colour] :
         std::array<std::pair<Vec3, wxColour>, 3>{{{{2, 0, 0}, wxColour(218, 97, 99)},
                                                   {{0, 2, 0}, wxColour(105, 190, 132)},
                                                   {{0, 0, 2}, wxColour(102, 151, 232)}}}) {
        const auto tip = Project(axis);
        if (origin && tip) {
            dc.SetPen(wxPen(colour, 3));
            dc.DrawLine(*origin, *tip);
        }
    }

    const Entity selected = document_.GetSelected();
    for (Entity entity : document_.GetScene().GetEntities()) {
        if (!entity.HasComponent<TransformComponent>())
            continue;
        const auto& transform = entity.GetComponent<TransformComponent>();
        if (!Finite(transform.position) || !Finite(transform.scale))
            continue;
        const bool sprite = entity.HasComponent<SpriteRendererComponent>();
        const bool mesh = entity.HasComponent<MeshRendererComponent>();
        const bool active = selected && selected.GetId() == entity.GetId();
        const wxColour colour = active   ? Highlight
                                : mesh   ? wxColour(117, 183, 223)
                                : sprite ? wxColour(130, 205, 158)
                                         : wxColour(176, 187, 201);
        const Vec3 half{std::max(0.05f, std::abs(transform.scale.x) * 0.5f),
                        std::max(0.05f, std::abs(transform.scale.y) * 0.5f),
                        sprite ? 0.02f : std::max(0.05f, std::abs(transform.scale.z) * 0.5f)};
        std::array<std::optional<wxPoint>, 8> corners;
        for (size_t i = 0; i < corners.size(); ++i)
            corners[i] = Project(
                Add(transform.position, {(i & 1) ? half.x : -half.x, (i & 2) ? half.y : -half.y,
                                         (i & 4) ? half.z : -half.z}));
        dc.SetPen(wxPen(colour, active ? 3 : 2));
        for (const auto [a, b] : std::array<std::pair<int, int>, 12>{{{0, 1},
                                                                      {0, 2},
                                                                      {1, 3},
                                                                      {2, 3},
                                                                      {4, 5},
                                                                      {4, 6},
                                                                      {5, 7},
                                                                      {6, 7},
                                                                      {0, 4},
                                                                      {1, 5},
                                                                      {2, 6},
                                                                      {3, 7}}})
            if (corners[a] && corners[b])
                dc.DrawLine(*corners[a], *corners[b]);
        const auto center = Project(transform.position);
        if (center) {
            const wxString label =
                entity.HasComponent<TagComponent>()
                    ? wxString::FromUTF8(entity.GetComponent<TagComponent>().tag)
                    : wxString::Format("#%u", static_cast<unsigned>(entity.GetId()));
            dc.SetTextForeground(colour);
            dc.DrawText(label, center->x + 9, center->y - 18);
        }
    }
    dc.SetTextForeground(wxColour(227, 235, 243));
    dc.DrawText(wxString::FromUTF8("3D 배치 미리보기  |  우클릭 회전 · 휠 확대 · F 선택 포커스"),
                14, 12);
    dc.SetTextForeground(wxColour(153, 169, 185));
    dc.DrawText(wxString::FromUTF8("현재는 엔티티 경계 프록시를 표시합니다."), 14, 34);
}

void SceneCanvas3D::OnLeftDown(wxMouseEvent& event) {
    SetFocus();
    std::optional<uint32> nearest;
    long long best = 22 * 22;
    for (Entity entity : document_.GetScene().GetEntities()) {
        if (!entity.HasComponent<TransformComponent>())
            continue;
        const auto point = Project(entity.GetComponent<TransformComponent>().position);
        if (!point)
            continue;
        const long long dx = static_cast<long long>(point->x) - event.GetX();
        const long long dy = static_cast<long long>(point->y) - event.GetY();
        const long long distance = dx * dx + dy * dy;
        if (distance < best) {
            best = distance;
            nearest = entity.GetId();
        }
    }
    onSelect_(nearest);
}

void SceneCanvas3D::OnMouseMove(wxMouseEvent& event) {
    const wxPoint current = event.GetPosition();
    const wxPoint delta = current - lastMouse_;
    if (orbiting_) {
        yaw_ += delta.x * 0.008;
        pitch_ = std::clamp(pitch_ + delta.y * 0.008, -1.45, 1.45);
        RefreshScene();
    } else if (panning_) {
        const double scale = distance_ / std::max(1, GetClientSize().GetHeight());
        focus_.x -= static_cast<float>(delta.x * scale * 1.6);
        focus_.y += static_cast<float>(delta.y * scale * 1.6);
        RefreshScene();
    }
    lastMouse_ = current;
}

void SceneCanvas3D::OnMouseWheel(wxMouseEvent& event) {
    distance_ =
        std::clamp(distance_ * std::pow(0.9, event.GetWheelRotation() /
                                                 static_cast<double>(event.GetWheelDelta())),
                   1.0, 1000.0);
    RefreshScene();
}

void SceneCanvas3D::OnCaptureLost(wxMouseCaptureLostEvent&) {
    orbiting_ = false;
    panning_ = false;
}
} // namespace LuxEngine
