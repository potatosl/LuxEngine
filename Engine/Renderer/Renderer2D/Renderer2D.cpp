#include "Engine/Renderer/Renderer2D/Renderer2D.hpp"
#include "Engine/Core/Log.hpp"

namespace LuxEngine {

    struct Renderer2DStorage {
        GpuDevice* device = nullptr;
        Mat4 viewProjection = Mat4::Identity();
        std::vector<Vertex2D> quadVertices;
        Renderer2D::Statistics stats;
        bool isSceneActive = false;
    };

    static Renderer2DStorage s_Storage;

    OrthographicCamera::OrthographicCamera(float left, float right, float bottom, float top) {
        SetProjection(left, right, bottom, top);
    }

    void OrthographicCamera::SetProjection(float left, float right, float bottom, float top) {
        projectionMatrix_ = Mat4::Orthographic(left, right, bottom, top, -1.0f, 1.0f);
        RecalculateMatrix();
    }

    void OrthographicCamera::RecalculateMatrix() {
        Mat4 transform = Mat4::Translate(position_) * Mat4::RotateZ(rotation_);
        viewMatrix_ = Mat4::Translate(Vec3(-position_.x, -position_.y, -position_.z)) * Mat4::RotateZ(-rotation_);
        viewProjectionMatrix_ = projectionMatrix_ * viewMatrix_;
    }

    void Renderer2D::Init(GpuDevice* device) {
        s_Storage.device = device;
        s_Storage.quadVertices.reserve(1000 * 4);
        ResetStats();
        Log::Info("Renderer2D initialized with batch storage.");
    }

    void Renderer2D::Shutdown() {
        s_Storage.quadVertices.clear();
        s_Storage.device = nullptr;
        s_Storage.isSceneActive = false;
        Log::Info("Renderer2D shutdown.");
    }

    void Renderer2D::BeginScene(const OrthographicCamera& camera) {
        s_Storage.viewProjection = camera.GetViewProjectionMatrix();
        s_Storage.quadVertices.clear();
        s_Storage.isSceneActive = true;
    }

    void Renderer2D::EndScene() {
        if (!s_Storage.isSceneActive) return;
        s_Storage.isSceneActive = false;
        if (!s_Storage.quadVertices.empty()) {
            s_Storage.stats.drawCalls++;
        }
    }

    void Renderer2D::DrawQuad(const Vec2& position, const Vec2& size, const Vec4& color) {
        DrawQuad(Vec3(position.x, position.y, 0.0f), size, color);
    }

    void Renderer2D::DrawQuad(const Vec3& position, const Vec2& size, const Vec4& color) {
        const float halfW = size.x * 0.5f;
        const float halfH = size.y * 0.5f;

        Vertex2D v0{ Vec3(position.x - halfW, position.y - halfH, position.z), color, Vec2(0.0f, 0.0f) };
        Vertex2D v1{ Vec3(position.x + halfW, position.y - halfH, position.z), color, Vec2(1.0f, 0.0f) };
        Vertex2D v2{ Vec3(position.x + halfW, position.y + halfH, position.z), color, Vec2(1.0f, 1.0f) };
        Vertex2D v3{ Vec3(position.x - halfW, position.y + halfH, position.z), color, Vec2(0.0f, 1.0f) };

        s_Storage.quadVertices.push_back(v0);
        s_Storage.quadVertices.push_back(v1);
        s_Storage.quadVertices.push_back(v2);
        s_Storage.quadVertices.push_back(v3);

        s_Storage.stats.quadCount++;
    }

    void Renderer2D::DrawRotatedQuad(const Vec2& position, const Vec2& size, float rotationRadians, const Vec4& color) {
        const float halfW = size.x * 0.5f;
        const float halfH = size.y * 0.5f;

        const float cosA = std::cos(rotationRadians);
        const float sinA = std::sin(rotationRadians);

        auto rotate = [&](float x, float y) -> Vec3 {
            return Vec3(
                position.x + (x * cosA - y * sinA),
                position.y + (x * sinA + y * cosA),
                0.0f
            );
        };

        Vertex2D v0{ rotate(-halfW, -halfH), color, Vec2(0.0f, 0.0f) };
        Vertex2D v1{ rotate( halfW, -halfH), color, Vec2(1.0f, 0.0f) };
        Vertex2D v2{ rotate( halfW,  halfH), color, Vec2(1.0f, 1.0f) };
        Vertex2D v3{ rotate(-halfW,  halfH), color, Vec2(0.0f, 1.0f) };

        s_Storage.quadVertices.push_back(v0);
        s_Storage.quadVertices.push_back(v1);
        s_Storage.quadVertices.push_back(v2);
        s_Storage.quadVertices.push_back(v3);

        s_Storage.stats.quadCount++;
    }

    void Renderer2D::DrawSprite(const Vec2& position, const Vec2& size, GpuTexture* /*texture*/, const Vec4& tint) {
        DrawQuad(position, size, tint);
    }

    void Renderer2D::ResetStats() {
        s_Storage.stats = Statistics{};
    }

    Renderer2D::Statistics Renderer2D::GetStats() {
        return s_Storage.stats;
    }

} // namespace LuxEngine
