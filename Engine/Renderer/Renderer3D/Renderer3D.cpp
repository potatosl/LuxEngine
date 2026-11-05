#include "Engine/Renderer/Renderer3D/Renderer3D.hpp"
#include "Engine/Core/Log.hpp"

namespace LuxEngine {

    struct Renderer3DStorage {
        GpuDevice* device = nullptr;
        Mat4 viewProjection = Mat4::Identity();
        DirectionalLight dirLight;
        Renderer3D::Statistics stats;
        bool isSceneActive = false;
    };

    static Renderer3DStorage s_3DStorage;

    PerspectiveCamera::PerspectiveCamera(float fovRadians, float aspect, float nearClip, float farClip) {
        SetPerspective(fovRadians, aspect, nearClip, farClip);
    }

    void PerspectiveCamera::SetPerspective(float fovRadians, float aspect, float nearClip, float farClip) {
        projectionMatrix_ = Mat4::Perspective(fovRadians, aspect, nearClip, farClip);
        RecalculateMatrix();
    }

    void PerspectiveCamera::RecalculateMatrix() {
        viewMatrix_ = Mat4::LookAt(position_, target_, up_);
        viewProjectionMatrix_ = projectionMatrix_ * viewMatrix_;
    }

    void Renderer3D::Init(GpuDevice* device) {
        s_3DStorage.device = device;
        ResetStats();
        Log::Info("Renderer3D initialized.");
    }

    void Renderer3D::Shutdown() {
        s_3DStorage.device = nullptr;
        s_3DStorage.isSceneActive = false;
        Log::Info("Renderer3D shutdown.");
    }

    void Renderer3D::BeginScene(const PerspectiveCamera& camera, const DirectionalLight& light) {
        s_3DStorage.viewProjection = camera.GetViewProjectionMatrix();
        s_3DStorage.dirLight = light;
        s_3DStorage.isSceneActive = true;
    }

    void Renderer3D::EndScene() {
        if (!s_3DStorage.isSceneActive) return;
        s_3DStorage.isSceneActive = false;
    }

    void Renderer3D::DrawMesh(const Mat4& /*transform*/, const Material3D& /*material*/) {
        if (!s_3DStorage.isSceneActive) return;
        s_3DStorage.stats.drawCalls++;
        s_3DStorage.stats.meshCount++;
    }

    void Renderer3D::DrawCube(const Vec3& position, const Vec3& scale, const Material3D& material) {
        Mat4 transform = Mat4::Translate(position) * Mat4::Scale(scale);
        DrawMesh(transform, material);
    }

    void Renderer3D::ResetStats() {
        s_3DStorage.stats = Statistics{};
    }

    Renderer3D::Statistics Renderer3D::GetStats() {
        return s_3DStorage.stats;
    }

} // namespace LuxEngine
