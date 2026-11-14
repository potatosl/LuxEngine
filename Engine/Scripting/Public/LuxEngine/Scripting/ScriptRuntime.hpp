#pragma once

#include <LuxEngine/Core/Core.hpp>
#include <LuxEngine/ECS/ScriptFields.hpp>
#include <filesystem>
#include <string>
#include <vector>

namespace LuxEngine {
class Scene;

struct ScriptRuntimeConfig {
    std::filesystem::path assemblyPath;
    std::filesystem::path coreDirectory;
    std::filesystem::path hostDirectory;
    std::filesystem::path dotnetRoot;
};

// One script world per scene. Calls and scene access belong to its owning scene thread.
class LUX_API ScriptRuntime final {
public:
    ScriptRuntime();
    ~ScriptRuntime();
    ScriptRuntime(const ScriptRuntime&) = delete;
    ScriptRuntime& operator=(const ScriptRuntime&) = delete;

    bool Initialize(Scene& scene, const ScriptRuntimeConfig& config);
    bool Update(double deltaTime);
    bool DescribeFields(const std::string& typeName, std::vector<ScriptFieldDescription>& fields);
    void Shutdown();
    bool IsRunning() const;
    const std::string& GetLastError() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace LuxEngine
