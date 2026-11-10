#pragma once

#include "Engine/Renderer/RHI/GpuDevice.hpp"
#include <filesystem>
#include <memory>

namespace ForestGame {
class SurvivalGame;

enum class UiAction { None, Start, Resume, Restart, Quit, Choice0, Choice1, Choice2, Pause };

class ForestRenderer final {
public:
    ForestRenderer();
    ~ForestRenderer();
    ForestRenderer(const ForestRenderer&) = delete;
    ForestRenderer& operator=(const ForestRenderer&) = delete;
    void Initialize(LuxEngine::GpuDevice& device, const std::filesystem::path& assetRoot);
    void Shutdown();
    bool TextLayoutFits() const;
    void Render(const SurvivalGame& game, float time, float aspect);
    // Pointer coordinates cover the entire window; Y points down.
    UiAction HitAction(float normalizedX, float normalizedY) const;
    int HitChoice(float normalizedX, float normalizedY) const;
    void SetPointer(float normalizedX, float normalizedY, bool pressed = false);
    void SetSelectedChoice(int index);

private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace ForestGame
