#pragma once

#include <LuxEngine/Renderer/Renderer3D/Renderer3D.hpp>
#include <RainyCreek/Catalog.hpp>
#include <RainyCreek/CreekState.hpp>
#include <array>
#include <map>
#include <memory>

namespace RainyCreek {

class CreekRenderer {
public:
    CreekRenderer();
    ~CreekRenderer();
    void Initialize(LuxEngine::GpuDevice& device, Catalog catalog);
    void Shutdown();
    void Draw(const CreekState& state, const LuxEngine::PerspectiveCamera& camera);
    LuxEngine::Renderer3D::Statistics Statistics() const;
    bool EnvironmentReady() const;

private:
    struct Part {
        LuxEngine::Mesh3D mesh;
        std::shared_ptr<LuxEngine::GpuTexture> texture;
        LuxEngine::Material3D material;
    };

    struct GpuModel {
        std::vector<Part> near;
        std::vector<Part> high;
        std::vector<Part> far;
    };

    Part LoadPart(LuxEngine::GpuDevice& device, const ModelPart& part);
    std::shared_ptr<LuxEngine::GpuTexture> LoadTexture(LuxEngine::GpuDevice& device,
                                                       const std::filesystem::path& path);
    Catalog catalog_;
    LuxEngine::Renderer3D::Context context_;
    std::map<std::string, GpuModel> models_;
    std::map<std::filesystem::path, std::shared_ptr<LuxEngine::GpuTexture>> textures_;
    LuxEngine::Mesh3D terrain_;
    LuxEngine::Mesh3D lampPost_;
    LuxEngine::Mesh3D lampGlass_;
    std::array<LuxEngine::Mesh3D, 12> water_;
    std::array<LuxEngine::Mesh3D, 24> rain_;
    std::shared_ptr<LuxEngine::GpuTexture> ground_;
};

} // namespace RainyCreek
