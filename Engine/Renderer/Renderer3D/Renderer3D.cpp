#include "Engine/Renderer/Renderer3D/Renderer3D.hpp"
#include "Engine/Core/Log.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
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
constexpr std::array<Vec3, 8> CubePositions{{{-0.5f, -0.5f, -0.5f},
                                             {0.5f, -0.5f, -0.5f},
                                             {0.5f, 0.5f, -0.5f},
                                             {-0.5f, 0.5f, -0.5f},
                                             {-0.5f, -0.5f, 0.5f},
                                             {0.5f, -0.5f, 0.5f},
                                             {0.5f, 0.5f, 0.5f},
                                             {-0.5f, 0.5f, 0.5f}}};
constexpr std::array<uint32, 36> CubeIndices{{0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 4, 7, 0, 7, 3,
                                              1, 2, 6, 1, 6, 5, 0, 1, 5, 0, 5, 4, 3, 7, 6, 3, 6, 2}};
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

bool Mesh3D::Upload(GpuDevice* gpu, std::span<const Vec3> positions,
                    std::span<const uint32> indices) {
    Release();
    if (!gpu || positions.empty() || indices.empty() || indices.size() % 3 != 0 ||
        positions.size() > std::numeric_limits<uint32>::max() / sizeof(RenderVertex) ||
        indices.size() > std::numeric_limits<uint32>::max() / sizeof(uint32))
        return false;
    for (uint32 index : indices)
        if (index >= positions.size())
            return false;
    std::vector<RenderVertex> data;
    data.reserve(positions.size());
    for (const Vec3& position : positions) {
        if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
            return false;
        data.push_back({position, {1, 1, 1, 1}, {0, 0}});
    }
    const auto vertexBytes = static_cast<uint32>(data.size() * sizeof(RenderVertex));
    const auto indexBytes = static_cast<uint32>(indices.size_bytes());
    if (!vertices_.Initialize(gpu, BufferType::Vertex, vertexBytes) ||
        !vertices_.SetData(data.data(), vertexBytes) ||
        !indices_.Initialize(gpu, BufferType::Index, indexBytes) ||
        !indices_.SetData(indices.data(), indexBytes)) {
        Release();
        return false;
    }
    indexCount_ = static_cast<uint32>(indices.size());
    return true;
}
void Mesh3D::Release() {
    vertices_.Release();
    indices_.Release();
    indexCount_ = 0;
}
bool Mesh3D::IsValid() const {
    return indexCount_ != 0 && vertices_.IsValid() && indices_.IsValid();
}

void Renderer3D::Init(GpuDevice* gpu) {
    if (device == gpu && !active) {
        vertices.clear();
        pointLights.clear();
        stats = {};
        return;
    }
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
    const float sunIntensity = std::isfinite(sun.intensity) ? std::max(0.0f, sun.intensity) : 0.0f;
    const Vec3 sunDir = (std::isfinite(sun.direction.x) && std::isfinite(sun.direction.y) && std::isfinite(sun.direction.z))
                            ? sun.direction : Vec3{0.0f, -1.0f, 0.0f};
    const Vec3 sunColor = (std::isfinite(sun.color.x) && std::isfinite(sun.color.y) && std::isfinite(sun.color.z))
                              ? sun.color : Vec3{1.0f, 1.0f, 1.0f};
    light = {sunDir, sunColor, sunIntensity};
    if (points.size() > MaxPointLights)
        Log::Warn("Renderer3D uses the first " + std::to_string(MaxPointLights) + " of " +
                  std::to_string(points.size()) + " point lights");
    pointLights.assign(points.begin(), points.begin() + std::min(points.size(), MaxPointLights));
    if (device) {
        std::array<GpuPointLight, MaxPointLights> gpuPoints{};
        for (size_t index = 0; index < pointLights.size(); ++index) {
            const auto& pt = pointLights[index];
            const float r = std::isfinite(pt.radius) ? std::max(0.0f, pt.radius) : 0.0f;
            const float inten = std::isfinite(pt.intensity) ? std::max(0.0f, pt.intensity) : 0.0f;
            const Vec3 pos = (std::isfinite(pt.position.x) && std::isfinite(pt.position.y) && std::isfinite(pt.position.z))
                                 ? pt.position : Vec3{0.0f, 0.0f, 0.0f};
            const Vec3 col = (std::isfinite(pt.color.x) && std::isfinite(pt.color.y) && std::isfinite(pt.color.z))
                                 ? pt.color : Vec3{1.0f, 1.0f, 1.0f};
            gpuPoints[index] = {pos, r, col, inten};
        }
        device->SetLighting({light.direction, light.color, light.intensity,
                             std::span(gpuPoints.data(), pointLights.size())});
    }
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
            return;
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
            const float distanceSq = toLight.Dot(toLight);
            if (!std::isfinite(distanceSq) || point.radius <= 0.0f || distanceSq >= point.radius * point.radius)
                continue;
            const float distance = std::sqrt(distanceSq);
            const float facing =
                distance > 0.00001f ? std::max(0.0f, normal.Dot(toLight * (1.0f / distance))) : 1.0f;
            const float falloff = std::max(0.0f, 1.0f - distance / point.radius);
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
    DrawMesh(CubePositions, CubeIndices, transform, material);
}
void Renderer3D::DrawMesh(const Mesh3D& mesh, const Mat4& transform, const Material3D& material) {
    if (!active || !device || !mesh.IsValid())
        return;
    if (device->DrawIndexed(mesh.vertices_, mesh.indices_, mesh.indexCount_, viewProjection,
                            transform, material.albedoColor))
        ++stats.drawCalls;
    ++stats.meshCount;
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
