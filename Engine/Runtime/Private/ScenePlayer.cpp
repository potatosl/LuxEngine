#include <LuxEngine/Core/Log.hpp>
#include <LuxEngine/ECS/ECS.hpp>
#include <LuxEngine/Physics/PhysicsEngine.hpp>
#include <LuxEngine/Platform/Input.hpp>
#include <LuxEngine/Platform/Window.hpp>
#include <LuxEngine/Runtime/ScenePlayer.hpp>
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
bool Finite(Vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
Vec3 Unit(Vec3 value, Vec3 fallback) {
    const double length = std::hypot(static_cast<double>(value.x), static_cast<double>(value.y),
                                     static_cast<double>(value.z));
    if (!std::isfinite(length) || length < 1e-12)
        return fallback;
    return {static_cast<float>(value.x / length), static_cast<float>(value.y / length),
            static_cast<float>(value.z / length)};
}
Vec3 CameraUp(Vec3 forward, Vec3 preferred) {
    preferred = Unit(preferred, {0, 1, 0});
    auto right = forward.Cross(preferred);
    if (right.Dot(right) < 1e-10f) {
        preferred = std::abs(forward.y) < .95f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
        right = forward.Cross(preferred);
    }
    return Unit(Unit(right, {1, 0, 0}).Cross(forward), {0, 1, 0});
}
} // namespace

ScenePlayerCameraView ResolveScenePlayerCamera(Scene& scene, const ScenePlayerView& fallback) {
    ScenePlayerCameraView result;
    result.position = {fallback.centerX, fallback.centerY + 3,
                       std::max(5.0f, fallback.halfHeight * 2)};
    result.target = {fallback.centerX, fallback.centerY, 0};
    if (!Finite(result.position) || !Finite(result.target))
        result = {};
    for (const auto id : scene.GetComponentEntityIds<CameraComponent>()) {
        const auto& camera = scene.GetComponent<CameraComponent>(id);
        if (!camera.isPrimary || !camera.isPerspective || !std::isfinite(camera.fov) ||
            camera.fov <= 0 || camera.fov >= 180 || !scene.HasComponent<TransformComponent>(id))
            continue;
        const float tangent = std::tan(ToRadians(camera.fov) * .5f);
        if (!std::isfinite(tangent) || tangent <= 0 || !std::isfinite(1.0f / tangent))
            continue;
        const auto matrix = scene.GetWorldMatrix(id);
        const auto* m = matrix.elements;
        const Vec3 position{m[12], m[13], m[14]};
        if (!Finite(position))
            continue;
        const Vec3 forward = Unit({-m[8], -m[9], -m[10]}, {0, 0, -1});
        const Vec3 up = CameraUp(forward, {m[4], m[5], m[6]});
        // At very large coordinates, adding a unit direction can round back to the eye itself.
        const float distance =
            std::max({1.0f, std::abs(position.x) * .00001f, std::abs(position.y) * .00001f,
                      std::abs(position.z) * .00001f});
        const Vec3 target = position + forward * distance;
        const float separation = (target - position).Dot(target - position);
        if (!Finite(target) || !std::isfinite(separation) || separation <= 0)
            continue;
        result = {position, target, up, camera.fov};
        break;
    }
    return result;
}

std::optional<ScenePlayerOptions> ParseScenePlayerArguments(const std::vector<std::string>& args,
                                                            std::string& error) {
    error.clear();
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
        if (arg == "--title") {
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
        const auto matrix = scene.GetWorldMatrix(entity.GetId());
        float localX0 = -.5f, localX1 = .5f, localY0 = -.5f, localY1 = .5f;
        if (entity.HasComponent<TilemapComponent>()) {
            const auto& map = entity.GetComponent<TilemapComponent>();
            localX0 = localY0 = 0;
            localX1 = map.width * map.tileSize;
            localY1 = map.height * map.tileSize;
        }
        float x0 = std::numeric_limits<float>::max(), y0 = x0;
        float x1 = std::numeric_limits<float>::lowest(), y1 = x1;
        for (const float x : {localX0, localX1})
            for (const float y : {localY0, localY1})
                for (const float z : {-.5f, .5f}) {
                    const auto* m = matrix.elements;
                    const float worldX = m[0] * x + m[4] * y + m[8] * z + m[12];
                    const float worldY = m[1] * x + m[5] * y + m[9] * z + m[13];
                    x0 = std::min(x0, worldX);
                    x1 = std::max(x1, worldX);
                    y0 = std::min(y0, worldY);
                    y1 = std::max(y1, worldY);
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
    auto loaded = sceneManager_.Load(std::filesystem::path(std::u8string(
        reinterpret_cast<const char8_t*>(options_.scenePath.data()), options_.scenePath.size())));
    if (!loaded) {
        Log::Error("씬을 불러올 수 없습니다: " + loaded.error);
        return false;
    }
    scene_ = std::move(loaded.scene);
    streamingAccumulator_ = 0;
    lastStreamingError_.clear();
    FindPlayer();
    return true;
}

void ScenePlayer::FindPlayer() {
    player_ = {};
    for (Entity entity : scene_->GetEntities())
        if (entity.HasComponent<TagComponent>() &&
            IsPlayerTag(entity.GetComponent<TagComponent>().tag)) {
            player_ = entity;
            break;
        }
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
    view_ = options_.view ? *options_.view : FitSceneView(*scene_);
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
}

void ScenePlayer::UpdateCamera() {
    uint32 width = 0, height = 0;
    float aspect = 16.0f / 9.0f;
    if (GetWindow().GetFramebufferSize(width, height) && width > 0 && height > 0)
        aspect = static_cast<float>(width) / static_cast<float>(height);
    const float halfWidth = view_.halfHeight * aspect;
    camera_.SetProjection(view_.centerX - halfWidth, view_.centerX + halfWidth,
                          view_.centerY - view_.halfHeight, view_.centerY + view_.halfHeight);

    const auto primary = ResolveScenePlayerCamera(*scene_, view_);
    camera3D_.SetPerspective(ToRadians(primary.fovDegrees), aspect, 0.1f, 1000.0f);
    camera3D_.SetPosition(primary.position);
    camera3D_.SetTarget(primary.target);
    camera3D_.SetUp(primary.up);
}

void ScenePlayer::OnUpdate(double dt) {
    if (Input::WasActionPressed("Quit")) {
        Close();
        return;
    }
    if (Input::WasActionPressed("Reload") && LoadScene())
        view_ = options_.view ? *options_.view : FitSceneView(*scene_);
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
            auto transform = scene_->GetWorldTransform(player_.GetId());
            const float length = std::sqrt(horizontal * horizontal + vertical * vertical);
            if (length > 0.0f) {
                transform.position.x += horizontal / length * 4.0f * static_cast<float>(dt);
                transform.position.y += vertical / length * 4.0f * static_cast<float>(dt);
                scene_->SetWorldTransform(player_.GetId(), transform);
            }
        }
    }
    streamingAccumulator_ += dt;
    if (streamingAccumulator_ >= .1) {
        streamingAccumulator_ = 0;
        const auto observer = player_ && player_.HasComponent<TransformComponent>()
                                  ? scene_->GetWorldTransform(player_.GetId()).position
                                  : Vec3{view_.centerX, view_.centerY, 0};
        std::string error;
        bool changed = false;
        if (!sceneManager_.UpdateStreaming(*scene_, {observer.x, observer.y}, error, &changed)) {
            if (error != lastStreamingError_)
                Log::Error(error);
            lastStreamingError_ = std::move(error);
        } else if (changed)
            lastStreamingError_.clear();
        if (changed)
            FindPlayer();
    }
    scene_->OnUpdate(dt);
    if (options_.maxFrames != 0 && ++frames_ >= options_.maxFrames)
        Close();
}

void ScenePlayer::OnRender(float) {
    UpdateCamera();
    const auto clear = scene_->GetSettings().clearColor;
    GetGpuDevice().Clear(clear.x, clear.y, clear.z, clear.w);
    bool has3DMeshes = false;
    for (Entity entity : scene_->GetEntities()) {
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
        auto renderSettings = scene_->GetSettings().render3D;
        if (!renderSettings.environmentPath.empty())
            renderSettings.environmentPath =
                scene_->ResolveAssetPath(renderSettings.environmentPath);
        if (scene_->GetSettings().render3D.mode == Render3DMode::Pbr)
            Renderer3D::BeginScene(camera3D_, renderSettings, scene_->GetSettings().exposure);
        else
            Renderer3D::BeginScene(camera3D_, sun);
        scene_->OnRender3D();
        Renderer3D::EndScene();
    }
    Renderer2D::BeginScene(camera_);
    scene_->OnRender();
    Renderer2D::EndScene();
}

void ScenePlayer::OnShutdown() {
    Input::ClearBindings();
}

} // namespace LuxEngine
