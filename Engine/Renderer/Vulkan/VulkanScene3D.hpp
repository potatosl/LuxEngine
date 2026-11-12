#pragma once

#include "Engine/Renderer/Renderer3D/Renderer3D.hpp"
#include "VulkanDevice.hpp"
#include <memory>

namespace LuxEngine::Vk {
struct SceneDraw {
    VkBuffer vertices = VK_NULL_HANDLE;
    VkBuffer indices = VK_NULL_HANDLE;
    VkDescriptorSet texture = VK_NULL_HANDLE;
    uint32 indexCount = 0;
    Mat4 model;
    Vec4 albedo;
    float metallic = 0, roughness = .5f;
};

class Scene3DRenderer {
public:
    explicit Scene3DRenderer(const std::shared_ptr<DeviceState>& state);
    ~Scene3DRenderer();
    bool Render(const PerspectiveCamera& camera, const Render3DSettings& settings, float exposure,
                std::span<const SceneDraw> draws, std::string& error);
    bool EnvironmentReady() const;
    double EnvironmentPreparationMs() const;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
}
