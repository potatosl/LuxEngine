#include "Engine/Renderer/Renderer3D/Renderer3D.hpp"
#include "Engine/Core/Log.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
namespace LuxEngine {
namespace {
constexpr size_t MaxSubmitVertices = 3 * 16384;
constexpr size_t MaxPointLights = 16;
GpuDevice* device = nullptr;
Mat4 viewProjection;
DirectionalLight light;
std::vector<PointLight> pointLights;
std::vector<RenderVertex> vertices;
Renderer3D::Statistics stats;
bool active = false;
Vec3 TransformPoint(const Mat4& m, const Vec3& v) {
    const auto& e = m.elements;
    return {e[0] * v.x + e[4] * v.y + e[8] * v.z + e[12],
            e[1] * v.x + e[5] * v.y + e[9] * v.z + e[13],
            e[2] * v.x + e[6] * v.y + e[10] * v.z + e[14]};
}
} // namespace
PerspectiveCamera::PerspectiveCamera(float fovRadians, float aspect, float nearClip,
                                     float farClip) {
    SetPerspective(fovRadians, aspect, nearClip, farClip);
}

void PerspectiveCamera::SetPerspective(float fovRadians, float aspect, float nearClip,
                                       float farClip) {
    projectionMatrix_ = Mat4::Perspective(fovRadians, aspect, nearClip, farClip);
    RecalculateMatrix();
}

void PerspectiveCamera::RecalculateMatrix() {
    viewMatrix_ = Mat4::LookAt(position_, target_, up_);
    viewProjectionMatrix_ = projectionMatrix_ * viewMatrix_;
}

void Renderer3D::Init(GpuDevice* gpu) {
    Shutdown();
    device = gpu;
    vertices.reserve(3600);
}
void Renderer3D::Shutdown() {
    vertices.clear();
    pointLights.clear();
    device = nullptr;
    active = false;
    stats = {};
}
void Renderer3D::BeginScene(const PerspectiveCamera& camera, const DirectionalLight& sun) {
    BeginScene(camera, sun, {});
}
void Renderer3D::BeginScene(const PerspectiveCamera& camera, const DirectionalLight& sun,
                            std::span<const PointLight> points) {
    if (active)
        throw std::logic_error("3D scene already active");
    viewProjection = camera.GetViewProjectionMatrix();
    light = sun;
    if (points.size() > MaxPointLights)
        Log::Warn("Renderer3D uses the first " + std::to_string(MaxPointLights) + " of " +
                  std::to_string(points.size()) + " point lights");
    pointLights.assign(points.begin(), points.begin() + std::min(points.size(), MaxPointLights));
    vertices.clear();
    stats = {};
    active = true;
}
void Renderer3D::EndScene() {
    if (!active)
        return;
    active = false;
    if (device) {
        const std::span<const RenderVertex> all(vertices);
        for (size_t first = 0; first < all.size(); first += MaxSubmitVertices) {
            const auto chunk = all.subspan(first, std::min(MaxSubmitVertices, all.size() - first));
            if (!device->DrawTriangles(chunk, viewProjection, nullptr, true)) {
                Log::Warn("Renderer3D dropped " + std::to_string((all.size() - first) / 3) +
                          " of " + std::to_string(all.size() / 3) + " triangles this frame");
                break;
            }
            ++stats.drawCalls;
        }
    }
    vertices.clear();
}
void Renderer3D::DrawMesh(std::span<const Vec3> positions, std::span<const uint32> indices,
                          const Mat4& transform, const Material3D& material) {
    if (!active || positions.empty() || indices.empty() || indices.size() % 3)
        return;
    for (auto index : indices)
        if (index >= positions.size())
            throw std::out_of_range("Mesh index out of range");
    for (size_t i = 0; i < indices.size(); i += 3) {
        std::array<Vec3, 3> triangle;
        for (size_t j = 0; j < 3; ++j)
            triangle[j] = TransformPoint(transform, positions[indices[i + j]]);
        const auto normal =
            (triangle[1] - triangle[0]).Cross(triangle[2] - triangle[0]).Normalized();
        const float diffuse = std::max(0.0f, normal.Dot(light.direction.Normalized() * -1.0f)) *
                              std::max(0.0f, light.intensity);
        Vec3 lit = light.color * diffuse;
        const Vec3 center = (triangle[0] + triangle[1] + triangle[2]) * (1.0f / 3.0f);
        for (const auto& point : pointLights) {
            const Vec3 toLight = point.position - center;
            const float distance = std::sqrt(toLight.Dot(toLight));
            if (point.radius <= 0.0f || distance >= point.radius)
                continue;
            const float facing =
                distance > 0.00001f ? std::max(0.0f, normal.Dot(toLight * (1.0f / distance))) : 1.0f;
            const float falloff = 1.0f - distance / point.radius;
            lit += point.color * (facing * falloff * falloff * std::max(0.0f, point.intensity));
        }
        const auto& c = material.albedoColor;
        const Vec4 color{c.x * (0.15f + lit.x), c.y * (0.15f + lit.y), c.z * (0.15f + lit.z), c.w};
        for (const auto& p : triangle)
            vertices.push_back({p, color, {0, 0}});
    }
    ++stats.meshCount;
}
void Renderer3D::DrawMesh(const Mat4& transform, const Material3D& material) {
    static const std::array<Vec3, 8> cube{{{-0.5f, -0.5f, -0.5f},
                                           {0.5f, -0.5f, -0.5f},
                                           {0.5f, 0.5f, -0.5f},
                                           {-0.5f, 0.5f, -0.5f},
                                           {-0.5f, -0.5f, 0.5f},
                                           {0.5f, -0.5f, 0.5f},
                                           {0.5f, 0.5f, 0.5f},
                                           {-0.5f, 0.5f, 0.5f}}};
    static const std::array<uint32, 36> indices{{0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7,
                                                 0, 4, 7, 0, 7, 3, 1, 2, 6, 1, 6, 5,
                                                 0, 1, 5, 0, 5, 4, 3, 7, 6, 3, 6, 2}};
    DrawMesh(cube, indices, transform, material);
}
void Renderer3D::DrawCube(const Vec3& p, const Vec3& scale, const Material3D& material) {
    DrawMesh(Mat4::Translate(p) * Mat4::Scale(scale), material);
}
void Renderer3D::ResetStats() {
    stats = {};
}
Renderer3D::Statistics Renderer3D::GetStats() {
    return stats;
}
} // namespace LuxEngine
