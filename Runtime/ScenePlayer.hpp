#pragma once

#include "Engine/Application.hpp"
#include "Engine/Renderer/Renderer2D/Renderer2D.hpp"
#include "Engine/Renderer/Renderer3D/Renderer3D.hpp"
#include "Engine/Scene/Entity.hpp"
#include "Engine/Scene/Scene.hpp"
#include <optional>
#include <string>
#include <vector>

namespace LuxEngine {

// View rectangle given by the editor: centre and half height in world units.
struct ScenePlayerView {
    float centerX = 0.0f;
    float centerY = 0.0f;
    float halfHeight = 4.5f;
};

struct ScenePlayerOptions {
    std::string scenePath;
    std::string title = "LuxEngine Player";
    uint32 width = 1280;
    uint32 height = 720;
    bool fullscreen = false;
    std::optional<ScenePlayerView> view;
    // Stops after this many frames when non-zero (smoke tests).
    uint32 maxFrames = 0;
    std::string assetRoot;
};

// Parses: <scene> [--title T] [--size W H] [--fullscreen] [--view X Y HALF_HEIGHT] [--frames N]
// Returns std::nullopt and sets error on invalid arguments.
std::optional<ScenePlayerOptions> ParseScenePlayerArguments(const std::vector<std::string>& args,
                                                            std::string& error);

// Runs a saved scene in its own window: physics, animation, Renderer3D and Renderer2D drawing.
// An entity tagged "플레이어", "주인공" or "Player" is driven by WASD/arrows (Space jumps for
// bodies with Rigidbody2D). Escape closes the window; R reloads the scene file.
class ScenePlayer final : public Application {
public:
    explicit ScenePlayer(ScenePlayerOptions options);

    bool LoadedScene() const {
        return loaded_;
    }

protected:
    void OnInit() override;
    void OnUpdate(double dt) override;
    void OnRender(float elapsedSeconds) override;
    void OnShutdown() override;

private:
    bool LoadScene();
    void UpdateCamera();

    ScenePlayerOptions options_;
    Scene scene_;
    Entity player_;
    OrthographicCamera camera_;
    PerspectiveCamera camera3D_;
    ScenePlayerView view_;
    bool loaded_ = false;
    uint32 frames_ = 0;
};

// Computes a view that frames every entity with a transform; used when no view is given.
ScenePlayerView FitSceneView(Scene& scene);

} // namespace LuxEngine
