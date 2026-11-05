#include "Engine/LuxEngine.hpp"
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>

int main() {
    std::cout << "[LuxEngineTests] Running Master Verification Suite (Phases 1-10)..." << std::endl;

    // 1. Core & Timer
    {
        LuxEngine::Timer timer;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        assert(timer.ElapsedSeconds() > 0.0);
        timer.Reset();
        assert(timer.ElapsedSeconds() < 1.0);

        auto scopeVal = LuxEngine::CreateScope<int>(100);
        assert(*scopeVal == 100);
        auto refVal = LuxEngine::CreateRef<std::string>("LuxEngine");
        assert(*refVal == "LuxEngine");
    }

    // 2. Event System
    {
        bool closeHandled = false;
        LuxEngine::WindowCloseEvent closeEv;
        LuxEngine::EventDispatcher disp(closeEv);
        disp.Dispatch<LuxEngine::WindowCloseEvent>([&](LuxEngine::WindowCloseEvent&) {
            closeHandled = true;
            return true;
        });
        assert(closeHandled);
        assert(closeEv.Handled);

        LuxEngine::WindowResizeEvent resizeEv(800, 600);
        assert(resizeEv.GetWidth() == 800 && resizeEv.GetHeight() == 600);
    }

    // 3. Math (Vec2, Vec3, Mat4)
    {
        LuxEngine::Vec3 a{ 1.0f, 0.0f, 0.0f };
        LuxEngine::Vec3 b{ 0.0f, 1.0f, 0.0f };
        LuxEngine::Vec3 c = a.Cross(b);
        assert(std::abs(c.z - 1.0f) < 0.0001f);
        assert(std::abs(a.Dot(b)) < 0.0001f);

        LuxEngine::Mat4 ortho = LuxEngine::Mat4::Orthographic(-1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f);
        assert(ortho.elements[0] > 0.0f);

        LuxEngine::Mat4 pers = LuxEngine::Mat4::Perspective(LuxEngine::ToRadians(60.0f), 1.777f, 0.1f, 100.0f);
        assert(pers.elements[5] > 0.0f);
    }

    // 4. Renderer2D
    {
        LuxEngine::Renderer2D::Init();
        LuxEngine::OrthographicCamera cam(-1.0f, 1.0f, -1.0f, 1.0f);
        LuxEngine::Renderer2D::BeginScene(cam);
        LuxEngine::Renderer2D::DrawQuad(LuxEngine::Vec2(0.0f, 0.0f), LuxEngine::Vec2(1.0f, 1.0f), LuxEngine::Vec4(1.0f, 1.0f, 1.0f, 1.0f));
        LuxEngine::Renderer2D::DrawRotatedQuad(LuxEngine::Vec2(0.5f, 0.5f), LuxEngine::Vec2(1.0f, 1.0f), 0.5f, LuxEngine::Vec4(1.0f, 0.0f, 0.0f, 1.0f));
        LuxEngine::Renderer2D::EndScene();

        auto stats = LuxEngine::Renderer2D::GetStats();
        assert(stats.quadCount == 2);
        assert(stats.drawCalls == 1);
        LuxEngine::Renderer2D::Shutdown();
    }

    // 5. Renderer3D
    {
        LuxEngine::Renderer3D::Init();
        LuxEngine::PerspectiveCamera cam(LuxEngine::ToRadians(45.0f), 1.0f, 0.1f, 50.0f);
        LuxEngine::DirectionalLight light;
        LuxEngine::Renderer3D::BeginScene(cam, light);
        LuxEngine::Renderer3D::DrawCube(LuxEngine::Vec3(0.0f, 0.0f, 0.0f), LuxEngine::Vec3(1.0f, 1.0f, 1.0f), LuxEngine::Material3D{});
        LuxEngine::Renderer3D::EndScene();

        auto stats = LuxEngine::Renderer3D::GetStats();
        assert(stats.meshCount == 1);
        LuxEngine::Renderer3D::Shutdown();
    }

    // 6. Scene & ECS
    {
        LuxEngine::Scene scene;
        auto e1 = scene.CreateEntity("Monster");
        assert(e1.IsValid());
        assert(e1.HasComponent<LuxEngine::TagComponent>());
        assert(e1.GetComponent<LuxEngine::TagComponent>().tag == "Monster");

        e1.AddComponent<LuxEngine::SpriteRendererComponent>(LuxEngine::Vec4(1.0f, 0.0f, 0.0f, 1.0f));
        assert(e1.HasComponent<LuxEngine::SpriteRendererComponent>());
        assert(e1.GetComponent<LuxEngine::SpriteRendererComponent>().color.x == 1.0f);

        scene.DestroyEntity(e1);
        assert(!e1.HasComponent<LuxEngine::SpriteRendererComponent>());
    }

    // 7. Physics (2D & 3D)
    {
        LuxEngine::PhysicsEngine::Init();
        LuxEngine::Vec3 pos{ 0.0f, 5.0f, 0.0f };
        LuxEngine::Rigidbody2DComponent rb2d;
        LuxEngine::BoxCollider2DComponent col2d;

        // Step simulation 10 times
        for (int i = 0; i < 10; ++i) {
            LuxEngine::PhysicsEngine::Step2D(pos, rb2d, col2d, 0.016);
        }
        assert(pos.y < 5.0f); // Falling due to gravity

        // Force ground hit
        for (int i = 0; i < 200; ++i) {
            LuxEngine::PhysicsEngine::Step2D(pos, rb2d, col2d, 0.016);
        }
        assert(pos.y >= 0.0f);
        assert(rb2d.isGrounded);
        LuxEngine::PhysicsEngine::Shutdown();
    }

    // 8. Audio
    {
        LuxEngine::AudioEngine::Init();
        LuxEngine::AudioEngine::SetMasterVolume(0.5f);
        assert(std::abs(LuxEngine::AudioEngine::GetMasterVolume() - 0.5f) < 0.01f);
        LuxEngine::AudioEngine::PlayMusic("Theme.mp3", true);
        assert(LuxEngine::AudioEngine::IsMusicPlaying());
        LuxEngine::AudioEngine::StopMusic();
        assert(!LuxEngine::AudioEngine::IsMusicPlaying());
        LuxEngine::AudioEngine::Shutdown();
    }

    // 9. Animation
    {
        LuxEngine::AnimationEngine::Init();
        LuxEngine::AnimationClip clip;
        clip.duration = 1.0f;
        clip.loop = true;
        clip.AddKeyframe({ 0.0f, LuxEngine::Vec3(0.0f, 0.0f, 0.0f), LuxEngine::Vec3(0.0f, 0.0f, 0.0f), LuxEngine::Vec3(1.0f, 1.0f, 1.0f) });
        clip.AddKeyframe({ 1.0f, LuxEngine::Vec3(10.0f, 0.0f, 0.0f), LuxEngine::Vec3(0.0f, 0.0f, 0.0f), LuxEngine::Vec3(1.0f, 1.0f, 1.0f) });

        LuxEngine::TransformComponent outTrans;
        float time = 0.5f;
        LuxEngine::AnimationEngine::UpdateClip(clip, time, 0.0f, outTrans);
        // At mid-point (0.5s), position.x should be ~5.0
        assert(std::abs(outTrans.position.x - 5.0f) < 0.1f);
        LuxEngine::AnimationEngine::Shutdown();
    }

    // 10. Asset Management
    {
        LuxEngine::AssetManager::Init();
        LuxEngine::RawMeshData mesh;
        bool loaded = LuxEngine::AssetManager::LoadMesh("test_mesh", mesh);
        assert(loaded);
        (void)loaded;
        assert(!mesh.vertices.empty());
        LuxEngine::AssetManager::Shutdown();
    }

    std::cout << "[LuxEngineTests] All Master Verification Suite tests passed successfully!" << std::endl;
    return 0;
}
