#pragma once
#include "Engine/Application.hpp"
#include "Game/SurvivalGame.hpp"
#include "Presentation/ForestRenderer.hpp"
#include <array>
#include <cstdint>
#include <string>

namespace ForestGame {
struct RunOptions {
    bool smokeTest = false;
    bool autoplay = false;
    bool pauseOnFocusLoss = true;
    bool stopAtUpgrade = false;
    std::uint32_t maxFrames = 0;
    std::uint32_t seed = 0x51a7e123u;
    float showcaseSeconds = 0;
    std::string capture;
    std::string assets;
};

class ForestSurvivorApp final : public LuxEngine::Application {
public:
    explicit ForestSurvivorApp(RunOptions options);
    const SurvivalGame& GetGame() const {
        return game_;
    }
    UiAction ActionAt(float normalizedX, float normalizedY) const {
        return renderer_.HitAction(normalizedX, normalizedY);
    }
    bool UiTextFits() const {
        return renderer_.TextLayoutFits();
    }
    void OnEvent(LuxEngine::Event& event) override;

protected:
    void OnInit() override;
    void OnUpdate(double dt) override;
    void OnRender(float elapsedSeconds) override;
    void OnShutdown() override;

private:
    void ProcessControls();
    void ExecuteButton(UiAction action);
    LuxEngine::Vec2 Movement() const;
    LuxEngine::Vec2 AutomaticMovement() const;
    void AdvanceAutoplay(double dt);
    void SaveCapture();
    void UpdateSound(double dt);
    RunOptions options_;
    SurvivalGame game_;
    ForestRenderer renderer_;
    std::uint32_t frames_ = 0;
    int selectedChoice_ = 0;
    bool captureRequested_ = false;
    float pointerX_ = 0, pointerY_ = 0;
    bool pointerDown_ = false;
    UiAction downAction_ = UiAction::None;
    UiAction queuedAction_ = UiAction::None;
    bool audioReady_ = false;
    std::array<std::string, 4> sounds_{};
    Phase lastPhase_ = Phase::Title;
    float lastHealth_ = 100, soundCooldown_ = 0;
    int lastXp_ = 0;
};
} // namespace ForestGame
