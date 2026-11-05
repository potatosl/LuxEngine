#include "Engine/LuxEngine.hpp"
#include "Runtime/Runtime.hpp"
#include <SDL3/SDL_main.h>
#include <stdexcept>
#include <string_view>

class SandboxApp : public LuxEngine::Application {
public:
    explicit SandboxApp(bool smokeTest = false)
        : LuxEngine::Application(LuxEngine::ApplicationSpecification{
              .name = "LuxEngine", .width = 1280, .height = 720}),
          smokeTest_(smokeTest), camera2D_(-6.4f, 6.4f, -3.6f, 3.6f),
          camera3D_(LuxEngine::ToRadians(60.0f), 1280.0f / 720.0f, 0.1f, 100.0f) {}

protected:
    void OnInit() override {
        LuxEngine::Runtime::PrintEngineInfo();

        LuxEngine::Renderer2D::Init(&GetGpuDevice());
        LuxEngine::Renderer3D::Init(&GetGpuDevice());
        LuxEngine::PhysicsEngine::Init();
        if (!LuxEngine::AudioEngine::Init())
            throw std::runtime_error("Audio initialization failed");
        LuxEngine::AnimationEngine::Init();
        LuxEngine::AssetManager::Init();

        // Scene entities
        playerEntity_ = scene_.CreateEntity("Player");
        playerEntity_.AddComponent<LuxEngine::SpriteRendererComponent>(
            LuxEngine::Vec4(0.2f, 0.8f, 0.4f, 1.0f));

        floorEntity_ = scene_.CreateEntity("PhysicsFloor");
        auto& floorTransform = floorEntity_.GetComponent<LuxEngine::TransformComponent>();
        floorTransform.position = {0.0f, -2.6f, 0.0f};
        floorTransform.scale = {10.0f, 0.4f, 1.0f};
        floorEntity_.AddComponent<LuxEngine::BoxCollider2DComponent>();
        floorEntity_.AddComponent<LuxEngine::SpriteRendererComponent>(
            LuxEngine::Vec4(0.32f, 0.36f, 0.44f, 1.0f));

        crateEntity_ = scene_.CreateEntity("PhysicsCrate");
        auto& crateTransform = crateEntity_.GetComponent<LuxEngine::TransformComponent>();
        crateTransform.position = {-3.5f, 2.4f, 0.0f};
        crateTransform.scale = {0.8f, 0.8f, 1.0f};
        crateEntity_.AddComponent<LuxEngine::Rigidbody2DComponent>();
        crateEntity_.AddComponent<LuxEngine::BoxCollider2DComponent>();
        crateEntity_.AddComponent<LuxEngine::SpriteRendererComponent>(
            LuxEngine::Vec4(0.95f, 0.62f, 0.2f, 1.0f));

        cubeEntity_ = scene_.CreateEntity("3D_Cube");
        cubeEntity_.AddComponent<LuxEngine::MeshRendererComponent>();

        // Animation clip
        animClip_.name = "PlayerBobbing";
        animClip_.duration = 2.0f;
        animClip_.loop = true;
        animClip_.AddKeyframe({0.0f, LuxEngine::Vec3(0.0f, 0.0f, 0.0f),
                               LuxEngine::Vec3(0.0f, 0.0f, 0.0f),
                               LuxEngine::Vec3(1.0f, 1.0f, 1.0f)});
        animClip_.AddKeyframe({1.0f, LuxEngine::Vec3(0.0f, 1.0f, 0.0f),
                               LuxEngine::Vec3(0.0f, 0.0f, 3.14f),
                               LuxEngine::Vec3(1.2f, 1.2f, 1.0f)});
        animClip_.AddKeyframe({2.0f, LuxEngine::Vec3(0.0f, 0.0f, 0.0f),
                               LuxEngine::Vec3(0.0f, 0.0f, 6.28f),
                               LuxEngine::Vec3(1.0f, 1.0f, 1.0f)});

        LuxEngine::Log::Info("Sandbox systems and runtime scene initialized successfully.");
    }

    void OnUpdate(double dt) override {
        // Animation update
        auto& playerTransform = playerEntity_.GetComponent<LuxEngine::TransformComponent>();
        LuxEngine::AnimationEngine::UpdateClip(animClip_, animTime_, static_cast<float>(dt),
                                               playerTransform);

        // Give the crate another jump after it has settled on the floor.
        crateJumpTimer_ += static_cast<float>(dt);
        auto& crateBody = crateEntity_.GetComponent<LuxEngine::Rigidbody2DComponent>();
        if (crateBody.isGrounded && crateJumpTimer_ >= 2.5f) {
            crateBody.velocity.y = 6.0f;
            crateJumpTimer_ = 0.0f;
        }
        // 3D rotation
        cubeRotation_ += static_cast<float>(dt) * 45.0f;

        scene_.OnUpdate(dt);
        if (smokeTest_ && ++frames_ >= 10)
            Close();
    }

    void OnRender(float elapsedSeconds) override {
        // 2D rendering
        LuxEngine::Renderer2D::BeginScene(camera2D_);
        const auto drawEntity = [](const LuxEngine::Entity& entity) {
            const auto& transform = entity.GetComponent<LuxEngine::TransformComponent>();
            const auto& sprite = entity.GetComponent<LuxEngine::SpriteRendererComponent>();
            LuxEngine::Renderer2D::DrawRotatedQuad({transform.position.x, transform.position.y},
                                                   {transform.scale.x, transform.scale.y},
                                                   transform.rotation.z, sprite.color);
        };
        drawEntity(floorEntity_);
        drawEntity(crateEntity_);
        drawEntity(playerEntity_);
        LuxEngine::Renderer2D::EndScene();

        // 2. 3D Rendering
        LuxEngine::DirectionalLight sunLight{.direction = LuxEngine::Vec3(-0.5f, -1.0f, -0.3f),
                                             .color = LuxEngine::Vec3(1.0f, 0.95f, 0.8f),
                                             .intensity = 1.0f};
        LuxEngine::Renderer3D::BeginScene(camera3D_, sunLight);
        LuxEngine::Material3D cubeMaterial{.albedoColor = LuxEngine::Vec4(0.8f, 0.3f, 0.2f, 1.0f),
                                           .metallic = 0.2f,
                                           .roughness = 0.4f};
        LuxEngine::Renderer3D::DrawMesh(
            LuxEngine::Mat4::Translate({2.0f, 0.0f, 0.0f}) *
                LuxEngine::Mat4::RotateY(LuxEngine::ToRadians(cubeRotation_)) *
                LuxEngine::Mat4::Scale({1.5f, 1.5f, 1.5f}),
            cubeMaterial);

        LuxEngine::Renderer3D::EndScene();

        (void)elapsedSeconds;
    }

    void OnShutdown() override {
        LuxEngine::AssetManager::Shutdown();
        LuxEngine::AnimationEngine::Shutdown();
        LuxEngine::AudioEngine::Shutdown();
        LuxEngine::PhysicsEngine::Shutdown();
        LuxEngine::Renderer3D::Shutdown();
        LuxEngine::Renderer2D::Shutdown();
        LuxEngine::Log::Info("Sandbox application shutdown complete.");
    }

private:
    bool smokeTest_ = false;
    int frames_ = 0;
    LuxEngine::Scene scene_;
    LuxEngine::Entity playerEntity_;
    LuxEngine::Entity floorEntity_;
    LuxEngine::Entity crateEntity_;
    LuxEngine::Entity cubeEntity_;
    LuxEngine::OrthographicCamera camera2D_;
    LuxEngine::PerspectiveCamera camera3D_;
    LuxEngine::AnimationClip animClip_;
    float animTime_ = 0.0f;
    float crateJumpTimer_ = 0.0f;
    float cubeRotation_ = 0.0f;
};

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    SandboxApp app(argc > 1 && std::string_view(argv[1]) == "--smoke-test");
    if (!app.Initialize()) {
        return 1;
    }

    const int result = app.Run();
    app.Shutdown();
    return result;
}
