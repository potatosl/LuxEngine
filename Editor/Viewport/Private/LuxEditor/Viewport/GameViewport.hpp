#pragma once

#include <LuxUI/Widgets.hpp>
#include <memory>

namespace LuxEngine {
class Scene;
}

namespace LuxEditor {
class EngineSceneRenderer;

// Runtime-only surface: no editor selection, navigation camera, grid or transform handles.
class GameViewport final : public LuxUI::Widget {
public:
    explicit GameViewport(LuxUI::Widget* parent = nullptr);
    ~GameViewport() override;

    void SetScene(LuxEngine::Scene* scene);
    LuxEngine::Scene* GetScene() const noexcept;
    void SetPaused(bool paused);
    bool IsPaused() const noexcept;
    void RefreshScene();
    std::shared_ptr<EngineSceneRenderer> GetRenderer() const;

private:
    bool event(LuxUI::Event*) override;
    void paintEvent(LuxUI::PaintEvent*) override;
    void mousePressEvent(LuxUI::MouseEvent*) override;
    void mouseReleaseEvent(LuxUI::MouseEvent*) override;
    void mouseMoveEvent(LuxUI::MouseEvent*) override;
    void PaintSoftware(LuxUI::Painter& painter);

    LuxEngine::Scene* scene_ = nullptr;
    std::shared_ptr<EngineSceneRenderer> renderer_;
    LuxUI::Image frame_;
    bool paused_ = false;
};
} // namespace LuxEditor
