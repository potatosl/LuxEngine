#include "Runtime/ScenePlayer.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/ECS/ECS.hpp"
#include "Engine/Physics/PhysicsEngine.hpp"
#include "Engine/Platform/Input.hpp"
#include "Engine/Platform/Window.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>

namespace LuxEngine {
namespace {
bool ParseNumber(const std::string& text, float& value) {
    const auto* end = text.data() + text.size();
    const auto result = std::from_chars(text.data(), end, value);
    return result.ec == std::errc{} && result.ptr == end && std::isfinite(value);
}
bool ParseNumber(const std::string& text, uint32& value) {
    const auto* end = text.data() + text.size();
    const auto result = std::from_chars(text.data(), end, value);
    return result.ec == std::errc{} && result.ptr == end;
}
bool IsPlayerTag(const std::string& tag) {
    return tag == "플레이어" || tag == "주인공" || tag == "Player";
}
} // namespace

std::optional<ScenePlayerOptions> ParseScenePlayerArguments(const std::vector<std::string>& args,
                                                            std::string& error) {
    ScenePlayerOptions options;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        auto need = [&](size_t count) {
            if (i + count >= args.size()) {
                error = arg + " needs " + std::to_string(count) + " value(s)";
                return false;
            }
            return true;
        };
        if (arg == "--asset-root") {
            if (!need(1))
                return std::nullopt;
            options.assetRoot = args[++i];
            if (options.assetRoot.empty()) {
                error = "--asset-root expects a non-empty directory";
                return std::nullopt;
            }
        } else if (arg == "--title") {
            if (!need(1))
                return std::nullopt;
            options.title = args[++i];
        } else if (arg == "--size") {
            if (!need(2) || !ParseNumber(args[i + 1], options.width) ||
                !ParseNumber(args[i + 2], options.height) || options.width < 64 ||
                options.height < 64 || options.width > 16384 || options.height > 16384) {
                if (error.empty())
                    error = "--size expects two integers between 64 and 16384";
                return std::nullopt;
            }
            i += 2;
        } else if (arg == "--fullscreen") {
            options.fullscreen = true;
        } else if (arg == "--view") {
            ScenePlayerView view;
            if (!need(3) || !ParseNumber(args[i + 1], view.centerX) ||
                !ParseNumber(args[i + 2], view.centerY) ||
                !ParseNumber(args[i + 3], view.halfHeight) || view.halfHeight <= 0.0f) {
                if (error.empty())
                    error = "--view expects X Y HALF_HEIGHT with HALF_HEIGHT > 0";
                return std::nullopt;
            }
            options.view = view;
            i += 3;
        } else if (arg == "--frames") {
            if (!need(1) || !ParseNumber(args[i + 1], options.maxFrames)) {
                if (error.empty())
                    error = "--frames expects a non-negative integer";
                return std::nullopt;
            }
            ++i;
        } else if (!arg.empty() && arg[0] == '-') {
            error = "unknown option: " + arg;
            return std::nullopt;
        } else if (options.scenePath.empty()) {
            options.scenePath = arg;
        } else {
            error = "only one scene file can be given";
            return std::nullopt;
        }
    }
    if (options.scenePath.empty()) {
        error = "no scene file given";
        return std::nullopt;
    }
    return options;
}

ScenePlayerView FitSceneView(Scene& scene) {
    float minX = std::numeric_limits<float>::max(), minY = minX;
    float maxX = std::numeric_limits<float>::lowest(), maxY = maxX;
    for (Entity entity : scene.GetEntities()) {
        if (!entity.HasComponent<TransformComponent>())
            continue;
        const auto& t = entity.GetComponent<TransformComponent>();
        float x0 = t.position.x - std::abs(t.scale.x) * 0.5f;
        float x1 = t.position.x + std::abs(t.scale.x) * 0.5f;
        float y0 = t.position.y - std::abs(t.scale.y) * 0.5f;
        float y1 = t.position.y + std::abs(t.scale.y) * 0.5f;
        if (entity.HasComponent<TilemapComponent>()) {
            const auto& map = entity.GetComponent<TilemapComponent>();
            x0 = t.position.x;
            y0 = t.position.y;
            x1 = t.position.x + map.width * map.tileSize;
            y1 = t.position.y + map.height * map.tileSize;
        }
        if (!std::isfinite(x0) || !std::isfinite(x1) || !std::isfinite(y0) || !std::isfinite(y1))
            continue;
        minX = std::min(minX, x0);
        maxX = std::max(maxX, x1);
        minY = std::min(minY, y0);
        maxY = std::max(maxY, y1);
    }
    ScenePlayerView view;
    if (minX > maxX)
        return view;
    view.centerX = (minX + maxX) * 0.5f;
    view.centerY = (minY + maxY) * 0.5f;
    // Fit both axes for a 16:9 window with a small margin.
    view.halfHeight =
        std::max({(maxY - minY) * 0.5f, (maxX - minX) * 0.5f * 9.0f / 16.0f, 1.0f}) * 1.1f;
    return view;
}

ScenePlayer::ScenePlayer(ScenePlayerOptions options)
    : Application({.name = options.title, .width = options.width, .height = options.height}),
      options_(std::move(options)) {}

bool ScenePlayer::LoadScene() {
    // Deserialize only replaces the scene when the whole file parses, so a bad reload keeps
    // the running scene. It also invalidates old entity handles.
    if (!scene_.Deserialize(options_.scenePath)) {
        Log::Error("Cannot load scene: " + options_.scenePath);
        return false;
    }
    if (!options_.assetRoot.empty())
        scene_.SetAssetRoot(options_.assetRoot);
    player_ = {};
    for (Entity entity : scene_.GetEntities())
        if (entity.HasComponent<TagComponent>() &&
            IsPlayerTag(entity.GetComponent<TagComponent>().tag)) {
            player_ = entity;
            break;
        }
    return true;
}

void ScenePlayer::OnInit() {
    Renderer2D::Init(&GetGpuDevice());
    Renderer3D::Init(&GetGpuDevice());
    PhysicsEngine::Init();
    if (!LoadScene()) {
        Close();
        return;
    }
    loaded_ = true;
    view_ = options_.view ? *options_.view : FitSceneView(scene_);
    Input::BindAction("Left", LuxKey::A);
    Input::BindAction("Left", LuxKey::Left);
    Input::BindAction("Right", LuxKey::D);
    Input::BindAction("Right", LuxKey::Right);
    Input::BindAction("Up", LuxKey::W);
    Input::BindAction("Up", LuxKey::Up);
    Input::BindAction("Down", LuxKey::S);
    Input::BindAction("Down", LuxKey::Down);
    Input::BindAction("Jump", LuxKey::Space);
    Input::BindAction("Reload", LuxKey::R);
    Input::BindAction("Quit", LuxKey::Escape);
    if (options_.fullscreen)
        GetWindow().SetFullscreen(true);
    UpdateCamera();
    Log::Info("Playing scene " + options_.scenePath);
}

void ScenePlayer::UpdateCamera() {
    uint32 width = 0, height = 0;
    float aspect = 16.0f / 9.0f;
    if (GetWindow().GetFramebufferSize(width, height) && width > 0 && height > 0)
        aspect = static_cast<float>(width) / static_cast<float>(height);
    const float halfWidth = view_.halfHeight * aspect;
    camera_.SetProjection(view_.centerX - halfWidth, view_.centerX + halfWidth,
                          view_.centerY - view_.halfHeight, view_.centerY + view_.halfHeight);

    float fovDegrees = 60.0f;
    Vec3 camPos{view_.centerX, view_.centerY + 3.0f, std::max(5.0f, view_.halfHeight * 2.0f)};
    Vec3 camTarget{view_.centerX, view_.centerY, 0.0f};
    for (Entity entity : scene_.GetEntities()) {
        if (entity.HasComponent<CameraComponent>() && entity.HasComponent<TransformComponent>()) {
            const auto& cam = entity.GetComponent<CameraComponent>();
            const auto& tr = entity.GetComponent<TransformComponent>();
            if (cam.isPrimary && cam.isPerspective && std::isfinite(cam.fov) && cam.fov > 0.0f &&
                cam.fov < 180.0f) {
                fovDegrees = cam.fov;
                camPos = tr.position;
                break;
            }
        }
    }
    camera3D_.SetPerspective(ToRadians(fovDegrees), aspect, 0.1f, 1000.0f);
    camera3D_.SetPosition(camPos);
    camera3D_.SetTarget(camTarget);
    camera3D_.SetUp({0.0f, 1.0f, 0.0f});
}

void ScenePlayer::OnUpdate(double dt) {
    if (Input::WasActionPressed("Quit")) {
        Close();
        return;
    }
    if (Input::WasActionPressed("Reload") && LoadScene())
        Log::Info("Scene reloaded");
    if (player_ && player_.HasComponent<TransformComponent>()) {
        const float horizontal = (Input::IsActionDown("Right") ? 1.0f : 0.0f) -
                                 (Input::IsActionDown("Left") ? 1.0f : 0.0f);
        const float vertical =
            (Input::IsActionDown("Up") ? 1.0f : 0.0f) - (Input::IsActionDown("Down") ? 1.0f : 0.0f);
        if (player_.HasComponent<Rigidbody2DComponent>()) {
            auto& body = player_.GetComponent<Rigidbody2DComponent>();
            body.velocity.x = horizontal * 4.0f;
            if (body.isGrounded && Input::WasActionPressed("Jump"))
                body.velocity.y = 7.0f;
        } else {
            auto& transform = player_.GetComponent<TransformComponent>();
            const float length = std::sqrt(horizontal * horizontal + vertical * vertical);
            if (length > 0.0f) {
                transform.position.x += horizontal / length * 4.0f * static_cast<float>(dt);
                transform.position.y += vertical / length * 4.0f * static_cast<float>(dt);
            }
        }
    }
    scene_.OnUpdate(dt);
    if (options_.maxFrames != 0 && ++frames_ >= options_.maxFrames)
        Close();
}

void ScenePlayer::OnRender(float) {
    UpdateCamera();
    bool has3DMeshes = false;
    for (Entity entity : scene_.GetEntities()) {
        if (entity.HasComponent<MeshRendererComponent>()) {
            has3DMeshes = true;
            break;
        }
    }
    if (has3DMeshes) {
        DirectionalLight sun;
        sun.direction = Vec3(0.5f, -1.0f, 0.5f).Normalized();
        sun.color = {1.0f, 0.98f, 0.95f};
        sun.intensity = 1.0f;
        Renderer3D::BeginScene(camera3D_, sun);
        scene_.OnRender3D();
        Renderer3D::EndScene();
    }
    Renderer2D::BeginScene(camera_);
    scene_.OnRender(&GetGpuDevice());
    Renderer2D::EndScene();
}

void ScenePlayer::OnShutdown() {
    Input::ClearBindings();
    PhysicsEngine::Shutdown();
    Renderer3D::Shutdown();
    Renderer2D::Shutdown();
}

} // namespace LuxEngine
