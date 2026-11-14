#include <LuxEditor/Viewport/EngineSceneRenderer.hpp>
#include <LuxEditor/Viewport/GameViewport.hpp>
#include <LuxEditor/Viewport/ViewportCamera3D.hpp>
#include <LuxEngine/Platform/Input.hpp>
#include <LuxEngine/Scene/Entity.hpp>
#include <LuxEngine/Scene/Scene.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace LuxEditor {
namespace {
LuxUI::Color Color(LuxEngine::Vec4 color) {
    return LuxUI::Color::fromRgbF(std::clamp(color.x, 0.f, 1.f), std::clamp(color.y, 0.f, 1.f),
                                  std::clamp(color.z, 0.f, 1.f), std::clamp(color.w, 0.f, 1.f));
}

LuxEngine::Vec3 Transform(const LuxEngine::Mat4& matrix, LuxEngine::Vec3 point) {
    const auto* m = matrix.elements;
    return {m[0] * point.x + m[4] * point.y + m[8] * point.z + m[12],
            m[1] * point.x + m[5] * point.y + m[9] * point.z + m[13],
            m[2] * point.x + m[6] * point.y + m[10] * point.z + m[14]};
}

LuxUI::PointF Project2D(const LuxEngine::Mat4& matrix, LuxUI::SizeF size, LuxEngine::Vec3 point) {
    const auto clip = Transform(matrix, point);
    return {(clip.x + 1) * size.width() * .5, (1 - clip.y) * size.height() * .5};
}

std::optional<LuxEngine::LuxMouseButton> MouseButton(LuxUI::Flags::MouseButton button) {
    switch (button) {
    case LuxUI::Flags::LeftButton:
        return LuxEngine::LuxMouseButton::Left;
    case LuxUI::Flags::MiddleButton:
        return LuxEngine::LuxMouseButton::Middle;
    case LuxUI::Flags::RightButton:
        return LuxEngine::LuxMouseButton::Right;
    case LuxUI::Flags::XButton1:
        return LuxEngine::LuxMouseButton::X1;
    case LuxUI::Flags::XButton2:
        return LuxEngine::LuxMouseButton::X2;
    default:
        return std::nullopt;
    }
}

void ClearInput() {
    LuxEngine::Input::ClearExternalKeys();
    LuxEngine::Input::ClearExternalMouse();
}
} // namespace

GameViewport::GameViewport(LuxUI::Widget* parent)
    : LuxUI::Widget(parent), renderer_(std::make_shared<EngineSceneRenderer>()) {
    setObjectName(LUX_UI_TEXT("gameViewport"));
    setFocusPolicy(LuxUI::Flags::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(240, 160);
    setAutoFillBackground(false);
}

GameViewport::~GameViewport() {
    ClearInput();
}

void GameViewport::SetScene(LuxEngine::Scene* scene) {
    ClearInput();
    scene_ = scene;
    paused_ = false;
    frame_ = {};
    update();
}

LuxEngine::Scene* GameViewport::GetScene() const noexcept {
    return scene_;
}

void GameViewport::SetPaused(bool paused) {
    paused_ = paused;
    ClearInput();
    update();
}

bool GameViewport::IsPaused() const noexcept {
    return paused_;
}

void GameViewport::RefreshScene() {
    update();
}

std::shared_ptr<EngineSceneRenderer> GameViewport::GetRenderer() const {
    return renderer_;
}

bool GameViewport::event(LuxUI::Event* event) {
    if (event->type() == LuxUI::Event::FocusOut || event->type() == LuxUI::Event::Hide ||
        event->type() == LuxUI::Event::WindowDeactivate)
        ClearInput();
    return LuxUI::Widget::event(event);
}

void GameViewport::mousePressEvent(LuxUI::MouseEvent* event) {
    setFocus(LuxUI::Flags::MouseFocusReason);
    if (scene_ && !paused_) {
        LuxEngine::Input::SetExternalMousePosition(static_cast<float>(event->position().x()),
                                                   static_cast<float>(event->position().y()));
        if (const auto button = MouseButton(event->button()))
            LuxEngine::Input::SetExternalMouseButton(*button, true);
    }
    event->accept();
}

void GameViewport::mouseReleaseEvent(LuxUI::MouseEvent* event) {
    if (scene_ && !paused_ && hasFocus()) {
        if (const auto button = MouseButton(event->button()))
            LuxEngine::Input::SetExternalMouseButton(*button, false);
    }
    event->accept();
}

void GameViewport::mouseMoveEvent(LuxUI::MouseEvent* event) {
    if (scene_ && !paused_ && hasFocus())
        LuxEngine::Input::SetExternalMousePosition(static_cast<float>(event->position().x()),
                                                   static_cast<float>(event->position().y()));
    event->accept();
}

void GameViewport::paintEvent(LuxUI::PaintEvent*) {
    // SDL/GPU initialization can dispatch native window messages; finish it before QPainter.
    const double scale = devicePixelRatioF();
    const int pixelWidth = std::clamp(static_cast<int>(std::lround(width() * scale)), 1, 16384);
    const int pixelHeight = std::clamp(static_cast<int>(std::lround(height() * scale)), 1, 16384);
    const bool rendered = scene_ && renderer_->RenderGame(*scene_, pixelWidth, pixelHeight, frame_);
    LuxUI::Painter painter(this);
    if (!painter.isActive())
        return;
    painter.fillRect(rect(), LuxUI::Color(22, 24, 28));
    if (!scene_) {
        painter.setPen(LuxUI::Color(165, 170, 180));
        painter.drawText(rect(), LuxUI::Flags::AlignCenter,
                         LUX_UI_TEXT("게임이 실행되고 있지 않습니다."));
        return;
    }
    painter.fillRect(rect(), Color(scene_->GetSettings().clearColor));
    if (rendered) {
        frame_.setDevicePixelRatio(scale);
        painter.drawImage(LuxUI::RectF(rect()), frame_);
    } else {
        PaintSoftware(painter);
    }
}

void GameViewport::PaintSoftware(LuxUI::Painter& painter) {
    const auto camera = ResolveGameViewCamera(*scene_, width(), height());
    painter.setRenderHint(LuxUI::Painter::Antialiasing);
    painter.setPen(LuxUI::Flags::NoPen);
    if (camera.perspective) {
        struct Face {
            LuxUI::PolygonF points;
            LuxUI::Color color;
            float depth;
        };

        std::vector<Face> faces;
        constexpr std::array<LuxEngine::Vec3, 8> corners{{{-.5f, -.5f, -.5f},
                                                          {.5f, -.5f, -.5f},
                                                          {.5f, .5f, -.5f},
                                                          {-.5f, .5f, -.5f},
                                                          {-.5f, -.5f, .5f},
                                                          {.5f, -.5f, .5f},
                                                          {.5f, .5f, .5f},
                                                          {-.5f, .5f, .5f}}};
        constexpr std::array<std::array<int, 4>, 6> indices{{{{0, 3, 2, 1}},
                                                             {{4, 5, 6, 7}},
                                                             {{0, 1, 5, 4}},
                                                             {{3, 7, 6, 2}},
                                                             {{0, 4, 7, 3}},
                                                             {{1, 2, 6, 5}}}};
        for (auto entity : scene_->GetEntities()) {
            const auto* mesh = entity.TryGetComponent<LuxEngine::MeshRendererComponent>();
            if (!mesh || !entity.HasComponent<LuxEngine::TransformComponent>())
                continue;
            const auto world = scene_->GetWorldMatrix(entity.GetId());
            for (size_t side = 0; side < indices.size(); ++side) {
                Face face;
                face.depth = 0;
                face.color =
                    Color(mesh->material.albedoColor).darker(100 + static_cast<int>(side) * 8);
                for (const int index : indices[side]) {
                    const auto point = Transform(world, corners[index]);
                    const auto projected = ProjectViewportPoint(camera.camera3D, size(), point);
                    if (!projected) {
                        face.points.clear();
                        break;
                    }
                    face.points << *projected;
                    const auto delta = point - camera.camera3D.GetPosition();
                    face.depth += delta.Dot(delta);
                }
                if (face.points.size() == 4)
                    faces.push_back(std::move(face));
            }
        }
        std::stable_sort(faces.begin(), faces.end(), [](const auto& left, const auto& right) {
            return left.depth > right.depth;
        });
        for (const auto& face : faces) {
            painter.setBrush(face.color);
            painter.drawPolygon(face.points);
        }
    }
    const auto drawQuad = [&](const LuxEngine::Mat4& world, LuxEngine::Vec4 color) {
        LuxUI::PolygonF points;
        for (const auto point :
             {LuxEngine::Vec3{-.5f, -.5f, 0}, {.5f, -.5f, 0}, {.5f, .5f, 0}, {-.5f, .5f, 0}})
            points << Project2D(camera.camera2D.GetViewProjectionMatrix(), size(),
                                Transform(world, point));
        painter.setBrush(Color(color));
        painter.drawPolygon(points);
    };
    const std::array<LuxEngine::Vec4, 6> tileColors{{{},
                                                     {.36f, .65f, .27f, 1},
                                                     {.43f, .31f, .21f, 1},
                                                     {.48f, .51f, .54f, 1},
                                                     {.22f, .46f, .74f, .82f},
                                                     {.76f, .66f, .42f, 1}}};
    for (auto entity : scene_->GetEntitiesIn2DLayerOrder()) {
        if (!entity.HasComponent<LuxEngine::TransformComponent>())
            continue;
        const auto world = scene_->GetWorldMatrix(entity.GetId());
        if (const auto* sprite = entity.TryGetComponent<LuxEngine::SpriteRendererComponent>())
            drawQuad(world, sprite->color);
        if (const auto* map = entity.TryGetComponent<LuxEngine::TilemapComponent>())
            for (LuxEngine::uint32 y = 0; y < map->height; ++y)
                for (LuxEngine::uint32 x = 0; x < map->width; ++x) {
                    const auto tile = map->Get(x, y);
                    if (tile == 0 || tile >= tileColors.size())
                        continue;
                    drawQuad(world *
                                 LuxEngine::Mat4::Translate(
                                     {(x + .5f) * map->tileSize, (y + .5f) * map->tileSize, 0}) *
                                 LuxEngine::Mat4::Scale({map->tileSize, map->tileSize, 1}),
                             tileColors[tile]);
                }
    }
}
} // namespace LuxEditor
