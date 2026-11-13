#pragma once

#include <LuxEngine/Application.hpp>
#include <LuxEngine/Renderer/Renderer2D/Renderer2D.hpp>
#include <LuxEngine/Renderer/Renderer3D/Renderer3D.hpp>
#include <LuxEngine/Scene/Entity.hpp>
#include <LuxEngine/Scene/Scene.hpp>
#include <LuxEngine/Scene/SceneManager.hpp>
#include <LuxEngine/Scripting/ScriptRuntime.hpp>
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
    std::string scriptAssemblyPath;
    std::string title = "LuxEngine Player";
    uint32 width = 1280;
    uint32 height = 720;
    bool fullscreen = false;
    std::optional<ScenePlayerView> view;
    // Stops after this many frames when non-zero (smoke tests).
    uint32 maxFrames = 0;
};

struct ScenePlayerCameraView {
    Vec3 position{0, 3, 9};
    Vec3 target{};
    Vec3 up{0, 1, 0};
    float fovDegrees = 60;
};

// Primary perspective cameras follow their complete world transform: local -Z is forward and
// +Y is up. Scale is normalized away; collapsed/parallel axes use a finite orthonormal fallback.
ScenePlayerCameraView ResolveScenePlayerCamera(Scene& scene, const ScenePlayerView& fallback);

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

    bool LoadedScene() const;

protected:
    void OnInit() override;
    void OnUpdate(double dt) override;
    void OnRender(float elapsedSeconds) override;
    void OnShutdown() override;

private:
    bool LoadScene();
    void FindPlayer();
    void UpdateCamera();

    ScenePlayerOptions options_;
    SceneManager sceneManager_;
    ScriptRuntime scripts_;
    std::shared_ptr<Scene> scene_ = std::make_shared<Scene>();
    Entity player_;
    OrthographicCamera camera_;
    PerspectiveCamera camera3D_;
    ScenePlayerView view_;
    bool loaded_ = false;
    uint32 frames_ = 0;
    double streamingAccumulator_ = 0;
    std::string lastStreamingError_;
};

// Computes a view that frames every entity with a transform; used when no view is given.
ScenePlayerView FitSceneView(Scene& scene);

} // namespace LuxEngine
