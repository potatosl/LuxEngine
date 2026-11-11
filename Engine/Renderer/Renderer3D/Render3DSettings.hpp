#pragma once

#include "Engine/Core/Math.hpp"
#include <string>

namespace LuxEngine {

enum class Render3DMode { Legacy, Pbr };

struct DirectionalLight {
    Vec3 direction{0.0f, -1.0f, 0.0f};
    Vec3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
};

struct Render3DSettings {
    Render3DMode mode = Render3DMode::Legacy;
    DirectionalLight sun{{.5f, -1.0f, .5f}, {1.0f, .98f, .95f}, 1.0f};
    Vec3 environmentColor{.15f, .15f, .15f};
    float environmentIntensity = 1.0f;
    float environmentRotation = 0.0f;
    std::string environmentPath;
    bool imageBasedLighting = true;
    bool showSkybox = false;
    bool toneMapping = true;
    bool shadows = true;
    uint32 shadowResolution = 2048;
    float shadowRange = 50.0f;
    float shadowDepthBias = .0005f;
    float shadowNormalBias = .02f;
};

LUX_API bool ValidateRender3DSettings(const Render3DSettings& settings);
LUX_API bool IsDefaultRender3DSettings(const Render3DSettings& settings);

}
