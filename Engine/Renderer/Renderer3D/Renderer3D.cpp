#include "Engine/Renderer/Renderer3D/Renderer3D.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/Renderer/Vulkan/VulkanDevice.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>

namespace LuxEngine {
namespace {
constexpr size_t MaxSubmitVertices = 3 * 16384;
constexpr size_t MaxPointLights = 16;

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

Vec3 TransformPoint(const Mat4& m, const Vec3& v) {
    const auto& e = m.elements;
    return {e[0] * v.x + e[4] * v.y + e[8] * v.z + e[12],
            e[1] * v.x + e[5] * v.y + e[9] * v.z + e[13],
            e[2] * v.x + e[6] * v.y + e[10] * v.z + e[14]};
}

bool SameOwner(const std::weak_ptr<Vk::DeviceState>& a, const std::weak_ptr<Vk::DeviceState>& b) {
    return !a.owner_before(b) && !b.owner_before(a);
}

thread_local Renderer3D::Context* t_ActiveContext3D = nullptr;
std::recursive_mutex s_DefaultContext3DMutex;

Renderer3D::Context& DefaultContext3D() {
    static Renderer3D::Context s_Default;
    return s_Default;
}
} // namespace

struct Renderer3D::Context::Impl {
    GpuDevice* device = nullptr;
    std::weak_ptr<Vk::DeviceState> deviceOwner;
    Mat4 viewProjection;
    DirectionalLight light;
    std::vector<PointLight> pointLights;
    std::vector<RenderVertex> vertices;
    Renderer3D::Statistics stats;
    bool active = false;

    std::shared_ptr<Vk::DeviceState> LockValidState() {
        if (!device)
            return nullptr;
        auto locked = deviceOwner.lock();
        if (!locked || !locked->Usable()) {
            device = nullptr;
            deviceOwner.reset();
            return nullptr;
        }
        return locked;
    }
};

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

Renderer3D::Context::Context() : impl_(std::make_unique<Impl>()) {}

Renderer3D::Context::Context(GpuDevice* gpu) : impl_(std::make_unique<Impl>()) {
    Init(gpu);
}

Renderer3D::Context::~Context() {
    Shutdown();
}

Renderer3D::Context::Context(Context&& other) noexcept = default;
Renderer3D::Context& Renderer3D::Context::operator=(Context&& other) noexcept = default;

void Renderer3D::Context::Init(GpuDevice* gpu) {
    if (!impl_)
        impl_ = std::make_unique<Impl>();
    const std::weak_ptr<Vk::DeviceState> nextOwner =
        gpu ? std::weak_ptr<Vk::DeviceState>(gpu->state_) : std::weak_ptr<Vk::DeviceState>{};
    if (impl_->device == gpu && SameOwner(impl_->deviceOwner, nextOwner) && !impl_->active) {
        impl_->vertices.clear();
        impl_->pointLights.clear();
        impl_->stats = {};
        return;
    }
    Shutdown();
    impl_->device = gpu;
    impl_->deviceOwner = nextOwner;
    impl_->vertices.reserve(3600);
}

void Renderer3D::Context::Shutdown() {
    if (!impl_)
        return;
    impl_->vertices.clear();
    impl_->pointLights.clear();
    impl_->device = nullptr;
    impl_->deviceOwner.reset();
    impl_->active = false;
    impl_->stats = {};
}

void Renderer3D::Context::DetachDevice(const GpuDevice* gpu) {
    if (!impl_ || !gpu)
        return;
    if (impl_->device == gpu) {
        AbortScene();
        impl_->device = nullptr;
        impl_->deviceOwner.reset();
    }
}

bool Renderer3D::Context::IsDeviceValid() const {
    if (!impl_ || !impl_->device)
        return false;
    auto locked = impl_->deviceOwner.lock();
    return locked && locked->Usable();
}

void Renderer3D::Context::BeginScene(const PerspectiveCamera& camera, const DirectionalLight& sun) {
    BeginScene(camera, sun, {});
}

void Renderer3D::Context::BeginScene(const PerspectiveCamera& camera, const DirectionalLight& sun,
                                     std::span<const PointLight> points) {
    if (!impl_)
        impl_ = std::make_unique<Impl>();
    if (impl_->active)
        throw std::logic_error("3D scene already active");
    impl_->viewProjection = camera.GetViewProjectionMatrix();
    const float sunIntensity =
        std::isfinite(sun.intensity) ? std::max(0.0f, sun.intensity) : 0.0f;
    const Vec3 sunDir = (std::isfinite(sun.direction.x) && std::isfinite(sun.direction.y) &&
                         std::isfinite(sun.direction.z))
                            ? sun.direction
                            : Vec3{0.0f, -1.0f, 0.0f};
    const Vec3 sunColor =
        (std::isfinite(sun.color.x) && std::isfinite(sun.color.y) && std::isfinite(sun.color.z))
            ? sun.color
            : Vec3{1.0f, 1.0f, 1.0f};
    impl_->light = {sunDir, sunColor, sunIntensity};
    if (points.size() > MaxPointLights)
        Log::Warn("Renderer3D uses the first " + std::to_string(MaxPointLights) + " of " +
                  std::to_string(points.size()) + " point lights");
    impl_->pointLights.assign(points.begin(),
                              points.begin() + std::min(points.size(), MaxPointLights));
    if (auto lockedState = impl_->LockValidState()) {
        std::array<GpuPointLight, MaxPointLights> gpuPoints{};
        for (size_t index = 0; index < impl_->pointLights.size(); ++index) {
            const auto& pt = impl_->pointLights[index];
            const float r = std::isfinite(pt.radius) ? std::max(0.0f, pt.radius) : 0.0f;
            const float inten =
                std::isfinite(pt.intensity) ? std::max(0.0f, pt.intensity) : 0.0f;
            const Vec3 pos = (std::isfinite(pt.position.x) && std::isfinite(pt.position.y) &&
                              std::isfinite(pt.position.z))
                                 ? pt.position
                                 : Vec3{0.0f, 0.0f, 0.0f};
            const Vec3 col =
                (std::isfinite(pt.color.x) && std::isfinite(pt.color.y) && std::isfinite(pt.color.z))
                    ? pt.color
                    : Vec3{1.0f, 1.0f, 1.0f};
            gpuPoints[index] = {pos, r, col, inten};
        }
        impl_->device->SetLighting({impl_->light.direction, impl_->light.color,
                                    impl_->light.intensity,
                                    std::span(gpuPoints.data(), impl_->pointLights.size())});
    }
    impl_->vertices.clear();
    impl_->stats = {};
    impl_->active = true;
}

void Renderer3D::Context::EndScene() {
    if (!impl_ || !impl_->active)
        return;
    impl_->active = false;
    if (auto lockedState = impl_->LockValidState()) {
        const std::span<const RenderVertex> all(impl_->vertices);
        for (size_t first = 0; first < all.size(); first += MaxSubmitVertices) {
            const auto chunk = all.subspan(first, std::min(MaxSubmitVertices, all.size() - first));
            if (!impl_->device->DrawTriangles(chunk, impl_->viewProjection, nullptr, true)) {
                Log::Warn("Renderer3D dropped " + std::to_string((all.size() - first) / 3) +
                          " of " + std::to_string(all.size() / 3) + " triangles this frame");
                break;
            }
            ++impl_->stats.drawCalls;
        }
    }
    impl_->vertices.clear();
}

void Renderer3D::Context::AbortScene() {
    if (!impl_)
        return;
    impl_->active = false;
    impl_->vertices.clear();
    impl_->pointLights.clear();
}

void Renderer3D::Context::DrawMesh(std::span<const Vec3> positions,
                                   std::span<const uint32> indices, const Mat4& transform,
                                   const Material3D& material) {
    if (!impl_ || !impl_->active || positions.empty() || indices.empty() || indices.size() % 3)
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
        const float diffuse =
            std::max(0.0f, normal.Dot(impl_->light.direction.Normalized() * -1.0f)) *
            std::max(0.0f, impl_->light.intensity);
        Vec3 lit = impl_->light.color * diffuse;
        const Vec3 center = (triangle[0] + triangle[1] + triangle[2]) * (1.0f / 3.0f);
        for (const auto& point : impl_->pointLights) {
            const Vec3 toLight = point.position - center;
            const float distanceSq = toLight.Dot(toLight);
            if (!std::isfinite(distanceSq) || point.radius <= 0.0f ||
                distanceSq >= point.radius * point.radius)
                continue;
            const float distance = std::sqrt(distanceSq);
            const float facing = distance > 0.00001f
                                     ? std::max(0.0f, normal.Dot(toLight * (1.0f / distance)))
                                     : 1.0f;
            const float falloff = std::max(0.0f, 1.0f - distance / point.radius);
            lit += point.color * (facing * falloff * falloff * std::max(0.0f, point.intensity));
        }
        const auto& c = material.albedoColor;
        const Vec4 color{c.x * (0.15f + lit.x), c.y * (0.15f + lit.y), c.z * (0.15f + lit.z), c.w};
        for (const auto& p : triangle)
            impl_->vertices.push_back({p, color, {0, 0}});
    }
    ++impl_->stats.meshCount;
}

void Renderer3D::Context::DrawMesh(const Mat4& transform, const Material3D& material) {
    DrawMesh(CubePositions, CubeIndices, transform, material);
}

void Renderer3D::Context::DrawMesh(const Mesh3D& mesh, const Mat4& transform,
                                   const Material3D& material) {
    if (!impl_ || !impl_->active || !mesh.IsValid())
        return;
    auto lockedState = impl_->LockValidState();
    if (!lockedState)
        return;
    if (impl_->device->DrawIndexed(mesh.vertices_, mesh.indices_, mesh.indexCount_,
                                   impl_->viewProjection, transform, material.albedoColor))
        ++impl_->stats.drawCalls;
    ++impl_->stats.meshCount;
}

void Renderer3D::Context::DrawCube(const Vec3& p, const Vec3& scale, const Material3D& material) {
    DrawMesh(Mat4::Translate(p) * Mat4::Scale(scale), material);
}

void Renderer3D::Context::ResetStats() {
    if (impl_)
        impl_->stats = {};
}

Renderer3D::Statistics Renderer3D::Context::GetStats() const {
    return impl_ ? impl_->stats : Statistics{};
}

Renderer3D::ScopedContext::ScopedContext(Context& context)
    : previous_(t_ActiveContext3D), current_(&context) {
    t_ActiveContext3D = current_;
}

Renderer3D::ScopedContext::~ScopedContext() {
    if (current_)
        current_->AbortScene();
    t_ActiveContext3D = previous_;
}

void Renderer3D::Init(GpuDevice* gpu) {
    if (t_ActiveContext3D) {
        t_ActiveContext3D->Init(gpu);
        return;
    }
    std::lock_guard<std::recursive_mutex> lock(s_DefaultContext3DMutex);
    DefaultContext3D().Init(gpu);
}

void Renderer3D::Shutdown() {
    if (t_ActiveContext3D) {
        t_ActiveContext3D->Shutdown();
        return;
    }
    std::lock_guard<std::recursive_mutex> lock(s_DefaultContext3DMutex);
    DefaultContext3D().Shutdown();
}

void Renderer3D::DetachDevice(const GpuDevice* gpu) {
    if (t_ActiveContext3D)
        t_ActiveContext3D->DetachDevice(gpu);
    std::lock_guard<std::recursive_mutex> lock(s_DefaultContext3DMutex);
    DefaultContext3D().DetachDevice(gpu);
}

void Renderer3D::BeginScene(const PerspectiveCamera& camera, const DirectionalLight& sun) {
    BeginScene(camera, sun, {});
}

void Renderer3D::BeginScene(const PerspectiveCamera& camera, const DirectionalLight& sun,
                            std::span<const PointLight> points) {
    if (t_ActiveContext3D) {
        t_ActiveContext3D->BeginScene(camera, sun, points);
        return;
    }
    std::lock_guard<std::recursive_mutex> lock(s_DefaultContext3DMutex);
    DefaultContext3D().BeginScene(camera, sun, points);
}

void Renderer3D::EndScene() {
    if (t_ActiveContext3D) {
        t_ActiveContext3D->EndScene();
        return;
    }
    std::lock_guard<std::recursive_mutex> lock(s_DefaultContext3DMutex);
    DefaultContext3D().EndScene();
}

void Renderer3D::DrawMesh(std::span<const Vec3> positions, std::span<const uint32> indices,
                          const Mat4& transform, const Material3D& material) {
    if (t_ActiveContext3D) {
        t_ActiveContext3D->DrawMesh(positions, indices, transform, material);
        return;
    }
    std::lock_guard<std::recursive_mutex> lock(s_DefaultContext3DMutex);
    DefaultContext3D().DrawMesh(positions, indices, transform, material);
}

void Renderer3D::DrawMesh(const Mat4& transform, const Material3D& material) {
    if (t_ActiveContext3D) {
        t_ActiveContext3D->DrawMesh(transform, material);
        return;
    }
    std::lock_guard<std::recursive_mutex> lock(s_DefaultContext3DMutex);
    DefaultContext3D().DrawMesh(transform, material);
}

void Renderer3D::DrawMesh(const Mesh3D& mesh, const Mat4& transform, const Material3D& material) {
    if (t_ActiveContext3D) {
        t_ActiveContext3D->DrawMesh(mesh, transform, material);
        return;
    }
    std::lock_guard<std::recursive_mutex> lock(s_DefaultContext3DMutex);
    DefaultContext3D().DrawMesh(mesh, transform, material);
}

void Renderer3D::DrawCube(const Vec3& p, const Vec3& scale, const Material3D& material) {
    if (t_ActiveContext3D) {
        t_ActiveContext3D->DrawCube(p, scale, material);
        return;
    }
    std::lock_guard<std::recursive_mutex> lock(s_DefaultContext3DMutex);
    DefaultContext3D().DrawCube(p, scale, material);
}

void Renderer3D::ResetStats() {
    if (t_ActiveContext3D) {
        t_ActiveContext3D->ResetStats();
        return;
    }
    std::lock_guard<std::recursive_mutex> lock(s_DefaultContext3DMutex);
    DefaultContext3D().ResetStats();
}

Renderer3D::Statistics Renderer3D::GetStats() {
    if (t_ActiveContext3D)
        return t_ActiveContext3D->GetStats();
    std::lock_guard<std::recursive_mutex> lock(s_DefaultContext3DMutex);
    return DefaultContext3D().GetStats();
}
} // namespace LuxEngine
