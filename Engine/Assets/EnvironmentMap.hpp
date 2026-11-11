#pragma once

#include "Engine/Core/Math.hpp"
#include <memory>
#include <string>
#include <vector>

namespace LuxEngine {
struct EnvironmentImage {
    uint32 size = 1;
    uint32 levels = 1;
    uint32 layers = 6;
    std::vector<float> pixels;
};

struct LUX_API EnvironmentData {
    EnvironmentImage sky, diffuse, specular, brdf;
    double preparationMs = 0;
    std::string error;
    static std::shared_ptr<EnvironmentData> Prepare(const std::string& path);
    static std::shared_ptr<EnvironmentData> Neutral();
    static bool ValidateFile(const std::string& path, std::string& error);
    static uint64 Revision(const std::string& path);
    static bool Reload(const std::string& path, std::string& error);
};
}
