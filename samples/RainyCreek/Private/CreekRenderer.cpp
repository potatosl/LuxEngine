#include <LuxEngine/Assets/AssetManager.hpp>
#include <RainyCreek/CreekGeometry.hpp>
#include <RainyCreek/CreekRenderer.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace RainyCreek {
namespace {
namespace le = LuxEngine;
constexpr std::array<le::Vec2, 2> LampLocations{{{-2.4f, 1.3f}, {2.6f, -5.5f}}};

void Upload(le::Mesh3D& output, le::GpuDevice& device, const CpuMesh& input) {
    if (!output.Upload(&device, input.vertices, input.indices))
        throw std::runtime_error("Rainy Creek GPU 메시를 만들지 못했습니다.");
}

le::Mat4 ModelTransform(const Instance& instance, const Model& model, float wind) {
    const auto extent = model.maximum - model.minimum;
    const float dimension =
        instance.vegetation ? extent.y : std::max({extent.x, extent.y, extent.z});
    const float scale = instance.size / std::max(dimension, .001f);
    const le::Vec3 origin{(model.minimum.x + model.maximum.x) * .5f, model.minimum.y,
                          (model.minimum.z + model.maximum.z) * .5f};
    return le::Mat4::Translate(instance.position) * le::Mat4::RotateY(instance.yaw) *
           le::Mat4::RotateZ(wind) * le::Mat4::Scale({scale, scale, scale}) *
           le::Mat4::Translate(origin * -1.f);
}
} // namespace

CreekRenderer::CreekRenderer() = default;
CreekRenderer::~CreekRenderer() = default;

std::shared_ptr<le::GpuTexture> CreekRenderer::LoadTexture(le::GpuDevice& device,
                                                           const std::filesystem::path& path) {
    if (const auto found = textures_.find(path); found != textures_.end())
        return found->second;
    int width = 0, height = 0;
    std::vector<le::uint8> pixels;
    if (!le::AssetManager::DecodeTextureCpu(ToUtf8(path), width, height, pixels))
        throw std::runtime_error("Rainy Creek 텍스처를 읽지 못했습니다: " + ToUtf8(path));
    auto texture = std::make_shared<le::GpuTexture>();
    if (!texture->Initialize(&device, static_cast<le::uint32>(width),
                             static_cast<le::uint32>(height), pixels.data(),
                             le::TextureFilter::Linear, le::TextureColorSpace::Srgb))
        throw std::runtime_error("Rainy Creek 텍스처를 업로드하지 못했습니다.");
    textures_.emplace(path, texture);
    return texture;
}

CreekRenderer::Part CreekRenderer::LoadPart(le::GpuDevice& device, const ModelPart& part) {
    le::RawMeshData raw;
    if (!le::AssetManager::LoadMesh(ToUtf8(part.mesh), raw))
        throw std::runtime_error("Rainy Creek 메시를 읽지 못했습니다.");
    std::vector<le::Vertex3D> vertices;
    vertices.reserve(raw.vertices.size() / 3);
    for (size_t i = 0; i < raw.vertices.size() / 3; ++i)
        vertices.push_back({{raw.vertices[i * 3], raw.vertices[i * 3 + 1], raw.vertices[i * 3 + 2]},
                            {raw.normals[i * 3], raw.normals[i * 3 + 1], raw.normals[i * 3 + 2]},
                            {raw.texCoords[i * 2], raw.texCoords[i * 2 + 1]}});
    Part result;
    if (!result.mesh.Upload(&device, vertices, raw.indices))
        throw std::runtime_error("Rainy Creek 모델을 업로드하지 못했습니다.");
    result.texture = LoadTexture(device, part.albedo);
    result.material.roughness = part.roughness;
    // TODO(Material3D): alpha cutoff와 양면 재질 API가 생기면 이진 식생 마스크를 대체합니다.
    // TODO(Material3D): normal/roughness 텍스처 슬롯이 필요합니다. 현재는 albedo와 상수 거칠기만
    // 사용합니다.
    // TODO(Material3D): 원본 식생의 specular/IOR 속성과 Repeat 샘플러·밉맵 설정이 필요합니다.
    // 현재 Clamp 샘플러에서는 bark의 반복 UV가 늘어나며 먼 침엽의 축소 표시가 거칠 수 있습니다.
    return result;
}

void CreekRenderer::Initialize(le::GpuDevice& device, Catalog catalog) {
    catalog_ = std::move(catalog);
    context_.Init(&device);
    for (const auto& [id, model] : catalog_.models) {
        auto& output = models_[id];
        for (const auto& part : model.parts)
            output.near.push_back(LoadPart(device, part));
        for (const auto& part : model.highParts)
            output.high.push_back(LoadPart(device, part));
        for (const auto& part : model.farParts)
            output.far.push_back(LoadPart(device, part));
    }
    ground_ = LoadTexture(device, catalog_.ground);
    Upload(terrain_, device, BuildTerrain());
    Upload(lampPost_, device, BuildLampPost());
    Upload(lampGlass_, device, BuildLampGlass());
    // TODO(Mesh3D): 버퍼를 재생성하지 않는 정점 갱신 API가 필요합니다.
    // 지금은 작은 주기 메시를 한 번 업로드하고 고정 시간에 따라 선택합니다.
    for (unsigned i = 0; i < water_.size(); ++i)
        Upload(water_[i], device, BuildWater(i, static_cast<unsigned>(water_.size())));
    for (unsigned i = 0; i < rain_.size(); ++i)
        Upload(rain_[i], device, BuildRain(i, static_cast<unsigned>(rain_.size())));
    le::AssetManager::Clear();
}

void CreekRenderer::Draw(const CreekState& state, const le::PerspectiveCamera& camera) {
    const auto& options = state.GetSettings();
    le::Render3DSettings settings;
    settings.mode = le::Render3DMode::Pbr;
    settings.showSkybox = true;
    settings.environmentPath =
        ToUtf8(options.lighting == Lighting::Day ? catalog_.day : catalog_.dusk);
    settings.environmentIntensity = options.lighting == Lighting::Night ? .065f : .32f;
    settings.environmentRotation = .5f;
    settings.sun.direction = le::Vec3{-.6f, -1.f, -.35f}.Normalized();
    settings.sun.color = {.48f, .63f, .86f};
    settings.sun.intensity = .24f;
    if (options.lighting == Lighting::Dusk) {
        settings.sun.color = {.95f, .64f, .40f};
        settings.sun.intensity = .65f;
    } else if (options.lighting == Lighting::Day) {
        settings.sun.color = {1.f, .96f, .85f};
        settings.sun.intensity = 2.4f;
        settings.environmentIntensity = 1.4f;
    }
    settings.shadows = options.shadows;
    settings.shadowRange = 44;
    settings.shadowResolution = 2048;
    // TODO(Render3DSettings): 거리/높이 기반 안개, TAA 및 후처리 API가 필요합니다.
    // 미지원 효과를 흉내 내는 토글 대신 현재 지원되는 조명과 그림자만 노출합니다.
    std::array<le::PointLight, 2> lamps;
    for (size_t i = 0; i < lamps.size(); ++i) {
        const auto p = LampLocations[i];
        lamps[i] = {{p.x, TerrainHeight(p.x, p.y) + 2.2f, p.y},
                    {1.f, .48f, .16f},
                    options.lamps ? 8.f : 0.f,
                    9.f};
    }
    context_.BeginScene(camera, settings, options.exposure, lamps);
    le::Material3D ground;
    ground.roughness = .78f;
    context_.DrawMesh(terrain_, le::Mat4::Identity(), ground, ground_.get());
    const float time = static_cast<float>(std::fmod(state.Time(), 3600.0));
    for (const auto& instance : state.Instances()) {
        const auto& model = catalog_.models.at(instance.model);
        const auto& gpu = models_.at(instance.model);
        const auto delta = instance.position - camera.GetPosition();
        const float distanceSquared = delta.Dot(delta);
        const auto* parts = &gpu.near;
        if ((!options.lod || distanceSquared < 8 * 8) && !gpu.high.empty())
            parts = &gpu.high;
        else if (options.lod && distanceSquared > 14 * 14 && !gpu.far.empty())
            parts = &gpu.far;
        // TODO(Renderer3D): 정점 기반 식생 바람 API가 필요합니다. 현재는 뿌리를 고정한 작은 회전을
        // 사용합니다.
        const float wind = options.wind && instance.vegetation
                               ? .012f * std::sin(time * 1.3f + instance.position.x * .7f +
                                                  instance.position.z * .4f)
                               : 0;
        const auto transform = ModelTransform(instance, model, wind);
        for (const auto& part : *parts)
            context_.DrawMesh(part.mesh, transform, part.material, part.texture.get());
    }
    le::Material3D iron;
    iron.albedoColor = {.055f, .065f, .06f, 1};
    iron.metallic = .75f;
    iron.roughness = .32f;
    le::Material3D glass;
    glass.albedoColor = options.lamps ? le::Vec4{1.f, .72f, .3f, 1} : le::Vec4{.18f, .17f, .12f, 1};
    glass.roughness = .15f;
    // TODO(Material3D): emissive 및 bloom API가 필요합니다. 실제 조명은 위 PointLight가 담당합니다.
    for (const auto p : LampLocations) {
        const auto transform = le::Mat4::Translate({p.x, TerrainHeight(p.x, p.y), p.y});
        context_.DrawMesh(lampPost_, transform, iron);
        context_.DrawMesh(lampGlass_, transform, glass);
    }
    if (options.water) {
        le::Material3D water;
        water.albedoColor = {.035f, .095f, .115f, 1};
        water.metallic = .72f;
        water.roughness = .085f;
        // TODO(Renderer3D): 깊이를 공유하는 투명 패스와 굴절/반사 렌더 타깃이 필요합니다.
        // 현재는 IBL 하이라이트가 있는 불투명 저거칠기 수면입니다. SSR/굴절은 지원하지 않습니다.
        const auto phase =
            static_cast<size_t>(std::fmod(state.Time() * 12, static_cast<double>(water_.size())));
        context_.DrawMesh(water_[phase], le::Mat4::Identity(), water);
    }
    if (options.rain) {
        le::Material3D rain;
        rain.albedoColor = {.32f, .42f, .51f, 1};
        rain.roughness = .35f;
        // TODO(Renderer3D): depth test를 유지하는 alpha blend API가 필요합니다.
        // 화면 오버레이 대신 동일 PBR 깊이를 쓰는 얇은 불투명 빗줄기로 지형/식생 가림을 유지합니다.
        const auto phase =
            static_cast<size_t>(std::fmod(state.Time() * 24, static_cast<double>(rain_.size())));
        context_.DrawMesh(rain_[phase], le::Mat4::Identity(), rain);
    }
    context_.EndScene();
}

bool CreekRenderer::EnvironmentReady() const {
    return context_.IsEnvironmentReady();
}

le::Renderer3D::Statistics CreekRenderer::Statistics() const {
    return context_.GetStats();
}

void CreekRenderer::Shutdown() {
    context_.Shutdown();
    terrain_.Release();
    lampPost_.Release();
    lampGlass_.Release();
    for (auto& mesh : water_)
        mesh.Release();
    for (auto& mesh : rain_)
        mesh.Release();
    ground_.reset();
    models_.clear();
    textures_.clear();
}

} // namespace RainyCreek
