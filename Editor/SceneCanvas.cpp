#include "Editor/SceneCanvas.hpp"

#include "Editor/EditorDocument.hpp"
#include "Engine/ECS/ECS.hpp"

#include <wx/dcbuffer.h>
#include <wx/event.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace LuxEngine {
namespace {

const wxColour kBackground(30, 34, 42);
const wxColour kSelection(255, 205, 92);

struct SpriteShape {
    uint32 id;
    TransformComponent transform;
    Vec4 color;
    wxString name;
};

bool ValidTransform(const TransformComponent& transform) {
    const auto& p = transform.position;
    const auto& s = transform.scale;
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
           std::isfinite(s.x) && std::isfinite(s.y) && std::isfinite(transform.rotation.z);
}

wxString EntityName(const Entity& entity) {
    if (entity.HasComponent<TagComponent>()) {
        const std::string& tag = entity.GetComponent<TagComponent>().tag;
        if (!tag.empty()) {
            const wxString name = wxString::FromUTF8(tag);
            if (!name.empty()) return name;
        }
    }
    return wxString::Format("Entity #%u", static_cast<unsigned>(entity.GetId()));
}

std::vector<SpriteShape> CollectSprites(EditorDocument& document) {
    std::vector<SpriteShape> result;
    for (const Entity& entity : document.GetScene().GetEntities()) {
        if (!entity.HasComponent<TransformComponent>() || !entity.HasComponent<SpriteRendererComponent>()) continue;
        const auto& transform = entity.GetComponent<TransformComponent>();
        if (!ValidTransform(transform)) continue;
        result.push_back({entity.GetId(), transform, entity.GetComponent<SpriteRendererComponent>().color,
                          EntityName(entity)});
    }
    std::sort(result.begin(), result.end(), [](const SpriteShape& a, const SpriteShape& b) {
        if (a.transform.position.z != b.transform.position.z)
            return a.transform.position.z < b.transform.position.z;
        return a.id < b.id;
    });
    return result;
}

unsigned char BlendedChannel(float component, double alpha, unsigned char background) {
    const double value = std::isfinite(component) ? std::clamp(static_cast<double>(component), 0.0, 1.0) : 1.0;
    return static_cast<unsigned char>(std::lround(background * (1.0 - alpha) + value * 255.0 * alpha));
}

wxColour SpriteColor(const Vec4& color) {
    const double alpha = std::isfinite(color.w) ? std::clamp(static_cast<double>(color.w), 0.0, 1.0) : 1.0;
    return {BlendedChannel(color.x, alpha, kBackground.Red()),
            BlendedChannel(color.y, alpha, kBackground.Green()),
            BlendedChannel(color.z, alpha, kBackground.Blue())};
}

} // namespace

SceneCanvas::SceneCanvas(wxWindow* parent, EditorDocument& document,
                         std::function<void(std::optional<uint32>)> onSelect,
                         std::function<void(uint32, double, double)> onMove)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
              wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE),
      document_(document), onSelect_(std::move(onSelect)), onMove_(std::move(onMove)) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Bind(wxEVT_PAINT, &SceneCanvas::OnPaint, this);
    Bind(wxEVT_LEFT_DOWN, &SceneCanvas::OnLeftDown, this);
    Bind(wxEVT_LEFT_UP, &SceneCanvas::OnLeftUp, this);
    Bind(wxEVT_MOUSEWHEEL, &SceneCanvas::OnMouseWheel, this);
    Bind(wxEVT_MIDDLE_DOWN, &SceneCanvas::OnMiddleDown, this);
    Bind(wxEVT_MIDDLE_UP, &SceneCanvas::OnMiddleUp, this);
    Bind(wxEVT_MOTION, &SceneCanvas::OnMouseMove, this);
    Bind(wxEVT_MOUSE_CAPTURE_LOST, &SceneCanvas::OnCaptureLost, this);
}

void SceneCanvas::RefreshScene() { Refresh(false); }

void SceneCanvas::SetSnapToGrid(bool enabled) {
    snapToGrid_ = enabled;
    RefreshScene();
}

wxPoint SceneCanvas::WorldToScreen(double x, double y) const {
    const wxSize size = GetClientSize();
    const double screenX = size.GetWidth() * 0.5 + panX_ + x * pixelsPerUnit_;
    const double screenY = size.GetHeight() * 0.5 + panY_ - y * pixelsPerUnit_;
    constexpr double limit = 100000000.0;
    return {static_cast<int>(std::lround(std::clamp(screenX, -limit, limit))),
            static_cast<int>(std::lround(std::clamp(screenY, -limit, limit)))};
}

std::pair<double, double> SceneCanvas::ScreenToWorld(const wxPoint& point) const {
    const wxSize size = GetClientSize();
    const double originX = size.GetWidth() * 0.5 + panX_;
    const double originY = size.GetHeight() * 0.5 + panY_;
    return {(point.x - originX) / pixelsPerUnit_, (originY - point.y) / pixelsPerUnit_};
}

void SceneCanvas::OnPaint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(kBackground));
    dc.Clear();

    const wxSize size = GetClientSize();
    const int width = size.GetWidth(), height = size.GetHeight();
    if (width <= 0 || height <= 0) return;

    const double originX = width * 0.5 + panX_;
    const double originY = height * 0.5 + panY_;
    const auto verticalStart = static_cast<long long>(std::floor(-originX / pixelsPerUnit_));
    const auto verticalEnd = static_cast<long long>(std::ceil((width - originX) / pixelsPerUnit_));
    const auto horizontalStart = static_cast<long long>(std::floor((originY - height) / pixelsPerUnit_));
    const auto horizontalEnd = static_cast<long long>(std::ceil(originY / pixelsPerUnit_));
    for (long long unit = verticalStart; unit <= verticalEnd; ++unit) {
        const int x = static_cast<int>(std::lround(originX + unit * pixelsPerUnit_));
        dc.SetPen(wxPen(unit == 0 ? wxColour(170, 84, 91)
                       : unit % 5 == 0 ? wxColour(67, 74, 86) : wxColour(48, 54, 64), unit == 0 ? 2 : 1));
        dc.DrawLine(x, 0, x, height);
    }
    for (long long unit = horizontalStart; unit <= horizontalEnd; ++unit) {
        const int y = static_cast<int>(std::lround(originY - unit * pixelsPerUnit_));
        dc.SetPen(wxPen(unit == 0 ? wxColour(89, 157, 113)
                       : unit % 5 == 0 ? wxColour(67, 74, 86) : wxColour(48, 54, 64), unit == 0 ? 2 : 1));
        dc.DrawLine(0, y, width, y);
    }

    const Entity selected = document_.GetSelected();
    const std::optional<uint32> selectedId = selected ? std::optional<uint32>(selected.GetId()) : std::nullopt;
    const auto sprites = CollectSprites(document_);
    for (const auto& sprite : sprites) {
        const auto& transform = sprite.transform;
        const double cosine = std::cos(transform.rotation.z);
        const double sine = std::sin(transform.rotation.z);
        const double halfX = transform.scale.x * 0.5;
        const double halfY = transform.scale.y * 0.5;
        const std::array<std::pair<double, double>, 4> corners{{
            {-halfX, -halfY}, {halfX, -halfY}, {halfX, halfY}, {-halfX, halfY}
        }};
        std::array<wxPoint, 4> points;
        for (size_t index = 0; index < corners.size(); ++index) {
            const auto [localX, localY] = corners[index];
            points[index] = WorldToScreen(transform.position.x + localX * cosine - localY * sine,
                                          transform.position.y + localX * sine + localY * cosine);
        }

        const bool isSelected = selectedId == sprite.id;
        dc.SetBrush(wxBrush(SpriteColor(sprite.color)));
        dc.SetPen(wxPen(isSelected ? kSelection : wxColour(178, 189, 204), isSelected ? 3 : 1));
        dc.DrawPolygon(static_cast<int>(points.size()), points.data());
        if (isSelected) {
            dc.SetBrush(wxBrush(kSelection));
            dc.SetPen(*wxTRANSPARENT_PEN);
            for (const wxPoint& point : points) dc.DrawRectangle(point.x - 3, point.y - 3, 7, 7);
        }

        const int top = std::min_element(points.begin(), points.end(),
                                         [](const wxPoint& a, const wxPoint& b) { return a.y < b.y; })->y;
        const wxPoint center = WorldToScreen(transform.position.x, transform.position.y);
        const wxSize textSize = dc.GetTextExtent(sprite.name);
        dc.SetTextForeground(isSelected ? kSelection : wxColour(215, 222, 231));
        dc.DrawText(sprite.name, center.x - textSize.GetWidth() / 2, top - textSize.GetHeight() - 5);
    }

    if (selected && selected.HasComponent<TransformComponent>() &&
        !selected.HasComponent<SpriteRendererComponent>()) {
        const auto& transform = selected.GetComponent<TransformComponent>();
        if (ValidTransform(transform)) {
            const wxPoint center = WorldToScreen(transform.position.x, transform.position.y);
            dc.SetPen(wxPen(kSelection, 2));
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.DrawCircle(center, 8);
            dc.DrawLine(center.x - 12, center.y, center.x + 12, center.y);
            dc.DrawLine(center.x, center.y - 12, center.x, center.y + 12);
            dc.SetTextForeground(kSelection);
            dc.DrawText(EntityName(selected), center.x + 14, center.y - 10);
        }
    }

    dc.SetTextForeground(wxColour(219, 226, 235));
    dc.DrawText(wxString::Format(wxString::FromUTF8("2D 장면  |  %.0f px/단위"), pixelsPerUnit_), 12, 10);
    dc.SetTextForeground(wxColour(166, 178, 194));
    wxString help = wxString::FromUTF8("왼쪽 드래그: 엔티티 이동  |  휠: 확대/축소  |  가운데 드래그: 화면 이동");
    help += snapToGrid_ ? wxString::FromUTF8("  |  1칸 스냅 켜짐")
                        : wxString::FromUTF8("  |  스냅 꺼짐");
    dc.DrawText(help, 12, height - 26);
}

void SceneCanvas::OnLeftDown(wxMouseEvent& event) {
    if (panning_) return;
    const auto [worldX, worldY] = ScreenToWorld(event.GetPosition());
    std::optional<uint32> hit;
    const auto sprites = CollectSprites(document_);
    for (auto it = sprites.rbegin(); it != sprites.rend(); ++it) {
        const auto& transform = it->transform;
        const double offsetX = worldX - transform.position.x;
        const double offsetY = worldY - transform.position.y;
        const double cosine = std::cos(transform.rotation.z);
        const double sine = std::sin(transform.rotation.z);
        const double localX = offsetX * cosine + offsetY * sine;
        const double localY = -offsetX * sine + offsetY * cosine;
        if (std::abs(localX) <= std::abs(transform.scale.x) * 0.5 &&
            std::abs(localY) <= std::abs(transform.scale.y) * 0.5) {
            hit = it->id;
            break;
        }
    }
    if (onSelect_) onSelect_(hit);
    if (hit && onMove_) {
        const Entity selected = document_.GetSelected();
        if (selected && selected.GetId() == *hit &&
            selected.HasComponent<TransformComponent>() && selected.HasComponent<SpriteRendererComponent>()) {
            const auto& position = selected.GetComponent<TransformComponent>().position;
            dragEntityId_ = *hit;
            dragOffsetX_ = static_cast<double>(position.x) - worldX;
            dragOffsetY_ = static_cast<double>(position.y) - worldY;
            draggingSprite_ = true;
            if (!HasCapture()) CaptureMouse();
        }
    }
    RefreshScene();
}

void SceneCanvas::OnLeftUp(wxMouseEvent&) { StopSpriteDrag(); }

void SceneCanvas::StopSpriteDrag() {
    if (!draggingSprite_) return;
    draggingSprite_ = false;
    dragEntityId_ = 0;
    if (HasCapture()) ReleaseMouse();
}

void SceneCanvas::OnMouseWheel(wxMouseEvent& event) {
    if (event.GetWheelAxis() != wxMOUSE_WHEEL_VERTICAL || event.GetWheelDelta() == 0) {
        event.Skip();
        return;
    }
    const wxPoint cursor = event.GetPosition();
    const auto [worldX, worldY] = ScreenToWorld(cursor);
    const double steps = static_cast<double>(event.GetWheelRotation()) / event.GetWheelDelta();
    const double nextScale = std::clamp(pixelsPerUnit_ * std::pow(1.2, steps), 16.0, 320.0);
    const wxSize size = GetClientSize();
    panX_ = cursor.x - size.GetWidth() * 0.5 - worldX * nextScale;
    panY_ = cursor.y - size.GetHeight() * 0.5 + worldY * nextScale;
    pixelsPerUnit_ = nextScale;
    RefreshScene();
}

void SceneCanvas::OnMiddleDown(wxMouseEvent& event) {
    if (draggingSprite_) return;
    if (!HasCapture()) CaptureMouse();
    panning_ = true;
    lastPanPoint_ = event.GetPosition();
}

void SceneCanvas::OnMiddleUp(wxMouseEvent&) {
    if (!panning_) return;
    panning_ = false;
    if (HasCapture()) ReleaseMouse();
}

void SceneCanvas::OnMouseMove(wxMouseEvent& event) {
    if (panning_) {
        if (!event.MiddleIsDown()) {
            panning_ = false;
            if (HasCapture()) ReleaseMouse();
            return;
        }
        const wxPoint current = event.GetPosition();
        panX_ += current.x - lastPanPoint_.x;
        panY_ += current.y - lastPanPoint_.y;
        lastPanPoint_ = current;
        RefreshScene();
        return;
    }
    if (!draggingSprite_) return;
    if (!event.LeftIsDown()) {
        StopSpriteDrag();
        return;
    }
    const Entity selected = document_.GetSelected();
    if (!selected || selected.GetId() != dragEntityId_ ||
        !selected.HasComponent<TransformComponent>() || !selected.HasComponent<SpriteRendererComponent>()) {
        StopSpriteDrag();
        return;
    }
    const auto [worldX, worldY] = ScreenToWorld(event.GetPosition());
    double x = worldX + dragOffsetX_;
    double y = worldY + dragOffsetY_;
    if (snapToGrid_) {
        x = std::round(x);
        y = std::round(y);
    }
    const auto& position = selected.GetComponent<TransformComponent>().position;
    if (std::isfinite(x) && std::isfinite(y) &&
        (position.x != x || position.y != y) && onMove_) onMove_(dragEntityId_, x, y);
    RefreshScene();
}

void SceneCanvas::OnCaptureLost(wxMouseCaptureLostEvent&) {
    panning_ = false;
    draggingSprite_ = false;
    dragEntityId_ = 0;
}

} // namespace LuxEngine
