#include "RHI/RenderBackend.hpp"
#include <LuxEngine/Assets/AssetManager.hpp>
#include <LuxEngine/Assets/ModelAsset.hpp>
#include <LuxEngine/Core/Log.hpp>
#include <LuxEngine/Renderer/Renderer3D/Renderer3D.hpp>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace LuxEngine {
namespace {
constexpr size_t MaxSubmitVertices = 3 * 16384;
constexpr size_t MaxPointLights = 16;

// Entry cap of the mesh and texture caches. The 256 MiB byte budgets do the real limiting; a
// small cap (it was 32) made scenes with more unique assets evict and re-upload every frame (#35).
constexpr size_t MaxCachedAssets = 4096;
constexpr std::array<Vec3, 8> CubePositions{{{-0.5f, -0.5f, -0.5f},
                                             {0.5f, -0.5f, -0.5f},
                                             {0.5f, 0.5f, -0.5f},
                                             {-0.5f, 0.5f, -0.5f},
                                             {-0.5f, -0.5f, 0.5f},
                                             {0.5f, -0.5f, 0.5f},
                                             {0.5f, 0.5f, 0.5f},
                                             {-0.5f, 0.5f, 0.5f}}};
constexpr std::array<uint32, 36> CubeIndices{{0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7,
                                              0, 4, 7, 0, 7, 3, 1, 2, 6, 1, 6, 5,
                                              0, 1, 5, 0, 5, 4, 3, 7, 6, 3, 6, 2}};

Vec3 TransformPoint(const Mat4& m, const Vec3& v) {
    const auto& e = m.elements;
    return {e[0] * v.x + e[4] * v.y + e[8] * v.z + e[12],
            e[1] * v.x + e[5] * v.y + e[9] * v.z + e[13],
            e[2] * v.x + e[6] * v.y + e[10] * v.z + e[14]};
}

bool SameOwner(const std::weak_ptr<Rhi::RenderBackend>& a,
               const std::weak_ptr<Rhi::RenderBackend>& b) {
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
    std::weak_ptr<Rhi::RenderBackend> deviceOwner;
    Mat4 viewProjection;
    DirectionalLight light;
    std::vector<PointLight> pointLights;
    std::vector<RenderVertex> vertices;
    Renderer3D::Statistics stats;
    bool active = false;
    bool pbr = false;
    Vec3 camera;
    Render3DSettings settings;
    float exposure = 1;
    PerspectiveCamera sceneCamera;
    std::unique_ptr<Rhi::Scene3DRenderer> modern;

    struct CachedMesh {
        Ref<Mesh3D> mesh;
        Ref<ModelAsset> model;
        int32 clip = -1;
        double time = std::numeric_limits<double>::quiet_NaN();
        uint64 used = 0;
        size_t bytes = 0;
        uint64 attempted = std::numeric_limits<uint64>::max();
        std::string error;
        Vec3 minimum{-.5f, -.5f, -.5f}, maximum{.5f, .5f, .5f};
        bool hasUvs = false, uvBlocked = false;
    };

    struct CachedTexture {
        Ref<GpuTexture> texture;
        uint64 attempted = std::numeric_limits<uint64>::max();
        uint64 used = 0;
        size_t bytes = 0;
        std::string error;
    };

    std::unordered_map<std::string, CachedTexture> textures;
    // Scene draws retain GPU IDs until Render() records them. Cache eviction must not retire
    // buffers or textures that a previously submitted draw in this scene still references.
    std::vector<Ref<Mesh3D>> frameMeshes;
    std::vector<Ref<GpuTexture>> frameTextures;
    std::unordered_map<std::string, std::string> warnings;
    std::string lastError;
    size_t textureBytes = 0;

    void Report(const std::string& key, const std::string& message) {
        if (message.empty()) {
            warnings.erase(key);
            return;
        }
        if (lastError.empty())
            lastError = message;
        const auto previous = warnings.find(key);
        if (previous == warnings.end() || previous->second != message) {
            if (warnings.size() >= 128)
                warnings.erase(warnings.begin());
            warnings[key] = message;
            Log::Warn(message);
        }
    }

    std::unordered_map<std::string, CachedMesh> meshes;
    uint64 sequence = 0;
    size_t meshBytes = 0;

    std::shared_ptr<Rhi::RenderBackend> LockValidState() {
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

bool Renderer3D::Context::IsEnvironmentReady() const {
    return impl_ && impl_->modern && impl_->modern->EnvironmentReady();
}

double Renderer3D::Context::GetEnvironmentPreparationMs() const {
    return impl_ && impl_->modern ? impl_->modern->EnvironmentPreparationMs() : 0;
}

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
    if (!gpu || positions.empty() || indices.empty() || indices.size() % 3 != 0 ||
        positions.size() > std::numeric_limits<uint32>::max() / sizeof(RenderVertex) ||
        indices.size() > std::numeric_limits<uint32>::max() / sizeof(uint32)) {
        Release();
        return false;
    }
    std::vector<Vertex3D> vertices;
    vertices.reserve(positions.size());
    for (const auto& position : positions)
        vertices.push_back({position, {}, {}});
    return Upload(gpu, vertices, indices);
}

bool Mesh3D::Upload(GpuDevice* gpu, std::span<const Vertex3D> vertices,
                    std::span<const uint32> indices) {
    Release();
    if (!gpu || vertices.empty() || indices.empty() || indices.size() % 3 != 0 ||
        vertices.size() > std::numeric_limits<uint32>::max() / sizeof(RenderVertex) ||
        indices.size() > std::numeric_limits<uint32>::max() / sizeof(uint32))
        return false;
    for (uint32 index : indices)
        if (index >= vertices.size())
            return false;
    std::vector<RenderVertex> data;
    data.reserve(vertices.size());
    for (const auto& vertex : vertices) {
        const auto& position = vertex.position;
        const auto& normal = vertex.normal;
        if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
            !std::isfinite(position.z) || !std::isfinite(normal.x) || !std::isfinite(normal.y) ||
            !std::isfinite(normal.z) || !std::isfinite(vertex.texCoords.x) ||
            !std::isfinite(vertex.texCoords.y))
            return false;
        data.push_back({position, {1, 1, 1, 1}, vertex.texCoords, normal});
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

bool Mesh3D::UpdateVertices(std::span<const Vertex3D> vertices) {
    if (!IsValid() ||
        vertices.size_bytes() / sizeof(Vertex3D) != vertices_.GetSize() / sizeof(RenderVertex))
        return false;
    std::vector<RenderVertex> data;
    data.reserve(vertices.size());
    for (const auto& vertex : vertices) {
        const auto& p = vertex.position;
        const auto& n = vertex.normal;
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) ||
            !std::isfinite(n.x) || !std::isfinite(n.y) || !std::isfinite(n.z) ||
            !std::isfinite(vertex.texCoords.x) || !std::isfinite(vertex.texCoords.y))
            return false;
        data.push_back({p, {1, 1, 1, 1}, vertex.texCoords, n});
    }
    return vertices_.SetData(data.data(), static_cast<uint32>(data.size() * sizeof(RenderVertex)));
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
    const std::weak_ptr<Rhi::RenderBackend> nextOwner =
        gpu ? std::weak_ptr<Rhi::RenderBackend>(gpu->backend_)
            : std::weak_ptr<Rhi::RenderBackend>{};
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
    if (impl_->modern)
        impl_->modern->ClearDraws();
    impl_->modern.reset();
    impl_->frameMeshes.clear();
    impl_->frameTextures.clear();
    impl_->meshes.clear();
    impl_->textures.clear();
    impl_->warnings.clear();
    impl_->pbr = false;
    impl_->settings = {};
    impl_->settings.mode = Render3DMode::Legacy;
    impl_->settings.shadows = false;
    impl_->settings.imageBasedLighting = false;
    impl_->settings.toneMapping = false;
    impl_->exposure = 1.0f;
    impl_->lastError.clear();
    impl_->textureBytes = 0;
    impl_->meshBytes = 0;
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
    impl_->pbr = false;
    impl_->settings = {};
    impl_->settings.mode = Render3DMode::Legacy;
    impl_->settings.shadows = false;
    impl_->settings.imageBasedLighting = false;
    impl_->settings.toneMapping = false;
    impl_->exposure = 1.0f;
    impl_->camera = camera.GetPosition();
    impl_->sceneCamera = camera;
    if (impl_->modern)
        impl_->modern->ClearDraws();
    impl_->lastError.clear();
    impl_->viewProjection = camera.GetViewProjectionMatrix();
    const float sunIntensity = std::isfinite(sun.intensity) ? std::max(0.0f, sun.intensity) : 0.0f;
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
            const float inten = std::isfinite(pt.intensity) ? std::max(0.0f, pt.intensity) : 0.0f;
            const Vec3 pos = (std::isfinite(pt.position.x) && std::isfinite(pt.position.y) &&
                              std::isfinite(pt.position.z))
                                 ? pt.position
                                 : Vec3{0.0f, 0.0f, 0.0f};
            const Vec3 col = (std::isfinite(pt.color.x) && std::isfinite(pt.color.y) &&
                              std::isfinite(pt.color.z))
                                 ? pt.color
                                 : Vec3{1.0f, 1.0f, 1.0f};
            gpuPoints[index] = {pos, r, col, inten};
        }
        impl_->device->SetLighting(
            {impl_->light.direction, impl_->light.color, impl_->light.intensity,
             std::span(gpuPoints.data(), impl_->pointLights.size()), camera.GetPosition(), false});
    }
    impl_->vertices.clear();
    impl_->stats = {};
    impl_->frameMeshes.clear();
    impl_->frameTextures.clear();
    impl_->active = true;
}

void Renderer3D::Context::BeginScene(const PerspectiveCamera& camera,
                                     const Render3DSettings& settings, float exposure,
                                     std::span<const PointLight> points) {
    if (!ValidateRender3DSettings(settings) || !std::isfinite(exposure) || exposure < 0 ||
        exposure > 16)
        throw std::invalid_argument("Invalid 3D rendering settings");
    BeginScene(camera, settings.sun, points);
    impl_->settings = settings;
    impl_->exposure = exposure;
    impl_->pbr = settings.mode == Render3DMode::Pbr;
    if (impl_->pbr && impl_->LockValidState()) {
        std::array<GpuPointLight, MaxPointLights> gpuPoints{};
        for (size_t i = 0; i < impl_->pointLights.size(); ++i) {
            const auto& p = impl_->pointLights[i];
            const auto finite = [](Vec3 v) {
                return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
            };
            gpuPoints[i] = {finite(p.position) ? p.position : Vec3{},
                            std::isfinite(p.radius) ? std::max(0.f, p.radius) : 0.f,
                            finite(p.color) ? p.color : Vec3{},
                            std::isfinite(p.intensity) ? std::max(0.f, p.intensity) : 0.f};
        }
        impl_->device->SetLighting(
            {impl_->light.direction, impl_->light.color, impl_->light.intensity,
             std::span(gpuPoints.data(), impl_->pointLights.size()), camera.GetPosition(), true});
    }
}

void Renderer3D::Context::EndScene() {
    if (!impl_ || !impl_->active)
        return;
    impl_->active = false;
    if (impl_->pbr || (impl_->modern && impl_->modern->DrawCount())) {
        if (auto state = impl_->LockValidState()) {
            if (!impl_->modern)
                impl_->modern = state->CreateScene3DRenderer();
            if (impl_->modern) {
                std::string error;
                impl_->modern->Render(impl_->sceneCamera, impl_->settings, impl_->exposure, error);
                impl_->Report("render3D", error);
            }
        }
        if (impl_->modern)
            impl_->modern->ClearDraws();
    }
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
    impl_->frameMeshes.clear();
    impl_->frameTextures.clear();
    impl_->vertices.clear();
    impl_->pointLights.clear();
}

void Renderer3D::Context::DrawMesh(std::span<const Vec3> positions, std::span<const uint32> indices,
                                   const Mat4& transform, const Material3D& material) {
    if (!impl_ || !impl_->active || positions.empty() || indices.empty() || indices.size() % 3)
        return;
    for (auto index : indices)
        if (index >= positions.size())
            return;
    Mesh3D mesh;
    if (mesh.Upload(impl_->device, positions, indices))
        DrawMesh(mesh, transform, material);
}

void Renderer3D::Context::DrawMesh(const Mat4& transform, const Material3D& material) {
    DrawMeshAsset({}, transform, material);
}

void Renderer3D::Context::DrawMesh(const Mesh3D& mesh, const Mat4& transform,
                                   const Material3D& material) {
    auto texture = ResolveBaseColorTexture(material.baseColorTexture, material.textureSampling);
    DrawMesh(mesh, transform, material, texture.get());
}

void Renderer3D::Context::DrawMesh(const Mesh3D& mesh, const Mat4& transform,
                                   const Material3D& material, GpuTexture* texture) {
    if (!impl_ || !impl_->active || !mesh.IsValid())
        return;
    auto lockedState = impl_->LockValidState();
    if (!lockedState)
        return;
    if (!mesh.vertices_.BelongsTo(*impl_->device) || !mesh.indices_.BelongsTo(*impl_->device))
        return;
    if (texture && !texture->BelongsTo(*impl_->device))
        return;
    if (!impl_->modern)
        impl_->modern = lockedState->CreateScene3DRenderer();
    auto normalSampling = material.textureSampling;
    normalSampling.normalMap = true;
    std::array<Ref<GpuTexture>, 4> maps;
    const auto map = [this, &maps](size_t index, const std::string& path,
                                   TextureSampling sampling) {
        maps[index] = ResolveBaseColorTexture(path, sampling, TextureColorSpace::Linear);
        return maps[index] ? maps[index]->handle_ : uint64{0};
    };
    const uint64 normal = map(0, material.normalTexture, normalSampling);
    const uint64 metallic = map(1, material.metallicTexture, material.textureSampling);
    const uint64 roughness = map(2, material.roughnessTexture, material.textureSampling);
    const uint64 occlusion = map(3, material.occlusionTexture, material.textureSampling);
    const uint32 sampling =
        (material.textureSampling.addressMode == TextureAddressMode::Repeat ? 1u : 0u) |
        (material.textureSampling.anisotropic ? 2u : 0u);
    if (!impl_->modern || impl_->modern->DrawCount() >= 65536 ||
        !impl_->modern->AddDraw(
            {mesh.vertices_.handle_, mesh.indices_.handle_, texture ? texture->handle_ : 0,
             mesh.indexCount_, transform, material.albedoColor, material.metallic,
             material.roughness, static_cast<uint32>(material.alphaMode), material.alphaCutoff,
             material.doubleSided, !impl_->pbr, normal, metallic, roughness, occlusion,
             material.normalScale, material.occlusionStrength, sampling}))
        return;
    ++impl_->stats.meshCount;
    ++impl_->stats.drawCalls;
}

Ref<GpuTexture> Renderer3D::Context::ResolveBaseColorTexture(const std::string& path,
                                                             TextureSampling sampling,
                                                             TextureColorSpace colorSpace) {
    if (path.empty() || !impl_ || !impl_->LockValidState())
        return nullptr;
    const std::string key =
        path + "\n" + std::to_string(static_cast<int>(colorSpace)) +
        std::to_string(sampling.mipmaps) + std::to_string(static_cast<int>(sampling.addressMode)) +
        std::to_string(sampling.anisotropic) + std::to_string(sampling.normalMap);
    auto found = impl_->textures.find(key);
    if (found == impl_->textures.end()) {
        if (impl_->textures.size() >= MaxCachedAssets) {
            const auto oldest = std::min_element(
                impl_->textures.begin(), impl_->textures.end(),
                [](const auto& a, const auto& b) { return a.second.used < b.second.used; });
            impl_->textureBytes -= oldest->second.bytes;
            impl_->textures.erase(oldest);
        }
        found = impl_->textures.emplace(key, Impl::CachedTexture{}).first;
    }
    auto& cached = found->second;
    cached.used = ++impl_->sequence;
    const auto revision = AssetManager::GetTextureRevision(path);
    if (cached.attempted != revision) {
        cached.attempted = revision;
        try {
            int width = 0, height = 0;
            std::vector<uint8> pixels;
            const bool decoded = AssetManager::IsBaseColorTextureFile(path) &&
                                 AssetManager::DecodeTextureCpu(path, width, height, pixels);
            size_t bytes = pixels.size();
            if (decoded && (!sampling.mipmaps || (width == 1 && height == 1)))
                bytes =
                    static_cast<size_t>(width == 1 ? 2 : width) * (height == 1 ? 2 : height) * 4;
            if (sampling.mipmaps)
                for (uint32 w = std::max(1, width), h = std::max(1, height); w > 1 || h > 1;) {
                    w = std::max(1u, w / 2);
                    h = std::max(1u, h / 2);
                    bytes += static_cast<size_t>(w) * h * 4;
                }
            constexpr size_t Budget = 256ull * 1024 * 1024;
            bool room = decoded && bytes <= Budget;
            if (room) {
                while (impl_->textureBytes - cached.bytes + bytes > Budget &&
                       impl_->textures.size() > 1) {
                    auto oldest = impl_->textures.end();
                    for (auto item = impl_->textures.begin(); item != impl_->textures.end(); ++item)
                        if (item != found && (oldest == impl_->textures.end() ||
                                              item->second.used < oldest->second.used))
                            oldest = item;
                    if (oldest == impl_->textures.end())
                        break;
                    impl_->textureBytes -= oldest->second.bytes;
                    impl_->textures.erase(oldest);
                }
                room = impl_->textureBytes - cached.bytes + bytes <= Budget;
            }
            auto next = CreateRef<GpuTexture>();
            if (room && next->Initialize(impl_->device, static_cast<uint32>(width),
                                         static_cast<uint32>(height), pixels.data(),
                                         TextureFilter::Linear, colorSpace, sampling)) {
                impl_->textureBytes = impl_->textureBytes - cached.bytes + bytes;
                cached.texture = std::move(next);
                cached.bytes = bytes;
                cached.error.clear();
            } else {
                cached.error =
                    !decoded ? "텍스처 파일을 디코딩하지 못했습니다: " + path
                    : !room
                        ? "텍스처 캐시 예산을 초과했습니다: " + path
                        : "GPU 텍스처 업로드 실패: " + impl_->device->GetLastError() + " / " + path;
            }
        } catch (const std::exception&) {
            cached.error = "기본 색상 텍스처 갱신에 실패했습니다: " + path;
        }
    }
    auto error = AssetManager::GetAssetError(path, true);
    if (error.empty())
        error = cached.error;
    if (!error.empty() && cached.texture)
        error += " (이전 정상 텍스처 유지)";
    impl_->Report("texture:" + path, error);
    if (impl_->active && cached.texture)
        impl_->frameTextures.push_back(cached.texture);
    return cached.texture;
}

bool Renderer3D::Context::DrawMeshAsset(const std::string& path, const Mat4& transform,
                                        const Material3D& material, int32 modelSubmesh, int32 clip,
                                        double time, uint64 instance) {
    if (!impl_ || !impl_->active || !impl_->LockValidState())
        return false;
    std::string key = path;
    if (modelSubmesh >= 0) {
        key.push_back('\0');
        key += std::to_string(modelSubmesh) + ":" + std::to_string(instance);
        // A host without an instance ID can still draw distinct poses in the same frame.
        if (instance == 0 && clip >= 0)
            key += ":" + std::to_string(clip) + ":" + std::to_string(std::bit_cast<uint64>(time));
    }
    auto found = impl_->meshes.find(key);
    if (found == impl_->meshes.end()) {
        if (impl_->meshes.size() >= MaxCachedAssets) {
            const auto oldest = std::min_element(
                impl_->meshes.begin(), impl_->meshes.end(),
                [](const auto& a, const auto& b) { return a.second.used < b.second.used; });
            impl_->meshBytes -= oldest->second.bytes;
            impl_->meshes.erase(oldest);
        }
        found = impl_->meshes.emplace(key, Impl::CachedMesh{}).first;
    }
    auto& cached = found->second;
    cached.used = ++impl_->sequence;
    const auto revision = path.empty() ? uint64{1} : AssetManager::GetMeshRevision(path);
    const bool requiresUv = !material.baseColorTexture.empty() || !material.normalTexture.empty() ||
                            !material.metallicTexture.empty() ||
                            !material.roughnessTexture.empty() ||
                            !material.occlusionTexture.empty();
    if (modelSubmesh >= 0) {
        std::string error;
        auto model = ModelAsset::LoadFbx(path, error);
        if (!std::isfinite(time) || clip < -1) {
            model.reset();
            error = "FBX 애니메이션 시간 또는 클립 번호가 올바르지 않습니다.";
        }
        if (model && static_cast<size_t>(modelSubmesh) < model->submeshes.size()) {
            const auto& part = model->submeshes[static_cast<size_t>(modelSubmesh)];
            if (requiresUv && !part.hasTexCoords) {
                error = "텍스처를 적용할 UV 좌표가 없는 모델입니다: " + path;
            } else if (cached.model != model || cached.clip != clip || cached.time != time) {
                ModelPose pose;
                const std::vector<ModelVertex>* source = &part.vertices;
                if (clip >= 0 || part.skinned || part.morphTargets) {
                    if (model->Evaluate(time, clip, pose, error))
                        source = &pose.submeshes[static_cast<size_t>(modelSubmesh)];
                }
                if (error.empty()) {
                    std::vector<Vertex3D> vertices;
                    vertices.reserve(source->size());
                    for (const auto& vertex : *source)
                        vertices.push_back({vertex.position, vertex.normal, vertex.texCoords});
                    const size_t bytes = vertices.size() * sizeof(RenderVertex) +
                                         part.indices.size() * sizeof(uint32);
                    constexpr size_t Budget = 256ull * 1024 * 1024;
                    bool room = bytes <= Budget;
                    while (room && impl_->meshBytes - cached.bytes + bytes > Budget &&
                           impl_->meshes.size() > 1) {
                        auto oldest = impl_->meshes.end();
                        for (auto item = impl_->meshes.begin(); item != impl_->meshes.end(); ++item)
                            if (item != found && (oldest == impl_->meshes.end() ||
                                                  item->second.used < oldest->second.used))
                                oldest = item;
                        if (oldest == impl_->meshes.end())
                            break;
                        impl_->meshBytes -= oldest->second.bytes;
                        impl_->meshes.erase(oldest);
                    }
                    room = room && impl_->meshBytes - cached.bytes + bytes <= Budget;
                    bool uploaded = room && cached.model == model && cached.mesh &&
                                    cached.mesh->UpdateVertices(vertices);
                    if (room && !uploaded) {
                        auto mesh = CreateRef<Mesh3D>();
                        uploaded = mesh->Upload(impl_->device, vertices, part.indices);
                        if (uploaded)
                            cached.mesh = std::move(mesh);
                    }
                    if (uploaded) {
                        impl_->meshBytes = impl_->meshBytes - cached.bytes + bytes;
                        cached.bytes = bytes;
                        cached.model = std::move(model);
                        cached.clip = clip;
                        cached.time = time;
                        cached.hasUvs = part.hasTexCoords;
                        cached.error.clear();
                    } else {
                        error = room ? "FBX 메시 업로드에 실패했습니다: " + path
                                     : "메시 캐시 예산을 초과했습니다: " + path;
                    }
                }
            } else {
                cached.error.clear();
            }
        } else if (model) {
            error = "FBX 하위 메시 번호가 올바르지 않습니다: " + path;
        }
        if (!error.empty())
            cached.error = std::move(error);
    } else if (cached.attempted != revision || (cached.uvBlocked && !requiresUv) ||
               (requiresUv && !cached.hasUvs && !cached.uvBlocked)) {
        cached.attempted = revision;
        cached.uvBlocked = false;
        try {
            RawMeshData raw;
            std::vector<Vertex3D> vertices;
            std::vector<uint32> indices;
            bool loaded = path.empty();
            if (path.empty()) {
                const std::array<Vec2, 6> uv{{{0, 1}, {1, 0}, {1, 1}, {0, 1}, {0, 0}, {1, 0}}};
                for (size_t face = 0; face < 6; ++face) {
                    const auto normal = (CubePositions[CubeIndices[face * 6 + 1]] -
                                         CubePositions[CubeIndices[face * 6]])
                                            .Cross(CubePositions[CubeIndices[face * 6 + 2]] -
                                                   CubePositions[CubeIndices[face * 6]])
                                            .Normalized();
                    for (size_t corner = 0; corner < 6; ++corner) {
                        indices.push_back(static_cast<uint32>(vertices.size()));
                        vertices.push_back(
                            {CubePositions[CubeIndices[face * 6 + corner]], normal, uv[corner]});
                    }
                }
            } else if ((loaded = AssetManager::LoadMesh(path, raw))) {
                cached.attempted = raw.revision;
                vertices.reserve(raw.vertices.size() / 3);
                for (size_t i = 0; i < raw.vertices.size() / 3; ++i)
                    vertices.push_back(
                        {{raw.vertices[i * 3], raw.vertices[i * 3 + 1], raw.vertices[i * 3 + 2]},
                         {raw.normals[i * 3], raw.normals[i * 3 + 1], raw.normals[i * 3 + 2]},
                         {raw.texCoords[i * 2], raw.texCoords[i * 2 + 1]}});
                indices = std::move(raw.indices);
            }
            const bool missingUv = loaded && requiresUv && !path.empty() && !raw.hasTexCoords;
            if (missingUv) {
                loaded = false;
                cached.uvBlocked = true;
            }
            const size_t bytes =
                vertices.size() * sizeof(RenderVertex) + indices.size() * sizeof(uint32);
            constexpr size_t Budget = 256ull * 1024 * 1024;
            bool room = loaded && bytes <= Budget;
            if (room) {
                while (impl_->meshBytes - cached.bytes + bytes > Budget &&
                       impl_->meshes.size() > 1) {
                    auto oldest = impl_->meshes.end();
                    for (auto item = impl_->meshes.begin(); item != impl_->meshes.end(); ++item)
                        if (item != found && (oldest == impl_->meshes.end() ||
                                              item->second.used < oldest->second.used))
                            oldest = item;
                    if (oldest == impl_->meshes.end())
                        break;
                    impl_->meshBytes -= oldest->second.bytes;
                    impl_->meshes.erase(oldest);
                }
                room = impl_->meshBytes - cached.bytes + bytes <= Budget;
            }
            auto next = CreateRef<Mesh3D>();
            if (room && next->Upload(impl_->device, vertices, indices)) {
                impl_->meshBytes = impl_->meshBytes - cached.bytes + bytes;
                cached.mesh = std::move(next);
                cached.bytes = bytes;
                cached.error.clear();
                cached.hasUvs = path.empty() || raw.hasTexCoords;
                cached.minimum = path.empty() ? Vec3{-.5f, -.5f, -.5f} : raw.boundsMin;
                cached.maximum = path.empty() ? Vec3{.5f, .5f, .5f} : raw.boundsMax;
            } else {
                const auto asset = path.empty() ? std::string("기본 큐브") : path;
                cached.error = missingUv ? "텍스처를 적용할 UV 좌표가 없는 모델입니다: " + asset
                               : !loaded ? "모델 메시를 읽지 못했습니다: " + asset
                               : !room   ? "메시 캐시 예산을 초과했습니다: " + asset
                                       : "GPU 메시 업로드 실패: " + impl_->device->GetLastError() +
                                             " / " + asset;
            }
        } catch (const std::exception&) {
            cached.error = "3D 메시 갱신에 실패했습니다: " + path;
        }
    }
    auto error =
        path.empty() || modelSubmesh >= 0 ? std::string{} : AssetManager::GetAssetError(path);
    if (error.empty())
        error = cached.error;
    if (!error.empty() && cached.mesh)
        error += " (이전 정상 메시 유지)";
    impl_->Report("mesh:" + key, error);
    if (!cached.mesh) {
        Material3D placeholder;
        placeholder.albedoColor = {.85f, .15f, .25f, 1};
        DrawMesh(CubePositions, CubeIndices, transform, placeholder);
        return false;
    }
    const auto previous = impl_->stats.drawCalls;
    auto drawable = material;
    if (!cached.hasUvs) {
        drawable.baseColorTexture.clear();
        drawable.normalTexture.clear();
        drawable.metallicTexture.clear();
        drawable.roughnessTexture.clear();
        drawable.occlusionTexture.clear();
    }
    impl_->frameMeshes.push_back(cached.mesh);
    DrawMesh(*cached.mesh, transform, drawable);
    return impl_->stats.drawCalls > previous;
}

const std::string& Renderer3D::Context::GetLastAssetError() const {
    static const std::string empty;
    return impl_ ? impl_->lastError : empty;
}

bool Renderer3D::Context::GetMeshBounds(const std::string& path, Vec3& minimum,
                                        Vec3& maximum) const {
    if (!impl_)
        return false;
    const auto found = impl_->meshes.find(path);
    if (found == impl_->meshes.end())
        return false;
    minimum = found->second.minimum;
    maximum = found->second.maximum;
    return true;
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

void Renderer3D::BeginScene(const PerspectiveCamera& camera, const Render3DSettings& settings,
                            float exposure, std::span<const PointLight> points) {
    if (t_ActiveContext3D) {
        t_ActiveContext3D->BeginScene(camera, settings, exposure, points);
        return;
    }
    std::lock_guard<std::recursive_mutex> lock(s_DefaultContext3DMutex);
    DefaultContext3D().BeginScene(camera, settings, exposure, points);
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

void Renderer3D::DrawMesh(const Mesh3D& mesh, const Mat4& transform, const Material3D& material,
                          GpuTexture* texture) {
    if (t_ActiveContext3D) {
        t_ActiveContext3D->DrawMesh(mesh, transform, material, texture);
        return;
    }
    std::lock_guard<std::recursive_mutex> lock(s_DefaultContext3DMutex);
    DefaultContext3D().DrawMesh(mesh, transform, material, texture);
}

bool Renderer3D::DrawMeshAsset(const std::string& path, const Mat4& transform,
                               const Material3D& material, int32 modelSubmesh, int32 clip,
                               double time, uint64 instance) {
    if (t_ActiveContext3D)
        return t_ActiveContext3D->DrawMeshAsset(path, transform, material, modelSubmesh, clip, time,
                                                instance);
    std::lock_guard<std::recursive_mutex> lock(s_DefaultContext3DMutex);
    return DefaultContext3D().DrawMeshAsset(path, transform, material, modelSubmesh, clip, time,
                                            instance);
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

const std::string& Renderer3D::GetLastAssetError() {
    if (t_ActiveContext3D)
        return t_ActiveContext3D->GetLastAssetError();
    std::lock_guard<std::recursive_mutex> lock(s_DefaultContext3DMutex);
    return DefaultContext3D().GetLastAssetError();
}

void PerspectiveCamera::SetPosition(const Vec3& position) {
    position_ = position;
    RecalculateMatrix();
}

void PerspectiveCamera::SetTarget(const Vec3& target) {
    target_ = target;
    RecalculateMatrix();
}

void PerspectiveCamera::SetUp(const Vec3& up) {
    up_ = up;
    RecalculateMatrix();
}

const Vec3& PerspectiveCamera::GetPosition() const {
    return position_;
}

const Vec3& PerspectiveCamera::GetTarget() const {
    return target_;
}

const Mat4& PerspectiveCamera::GetProjectionMatrix() const {
    return projectionMatrix_;
}

const Mat4& PerspectiveCamera::GetViewMatrix() const {
    return viewMatrix_;
}

const Mat4& PerspectiveCamera::GetViewProjectionMatrix() const {
    return viewProjectionMatrix_;
}

uint32 Mesh3D::GetIndexCount() const {
    return IsValid() ? indexCount_ : 0;
}

PerspectiveCamera::PerspectiveCamera() = default;
} // namespace LuxEngine
