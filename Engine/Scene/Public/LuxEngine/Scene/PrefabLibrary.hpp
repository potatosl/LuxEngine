#pragma once

#include <LuxEngine/Scene/Scene.hpp>
#include <filesystem>

namespace LuxEngine {

class LUX_API PrefabLibrary final {
public:
    static bool IsPrefabPath(const std::filesystem::path& path);
    static bool Save(Scene& source, uint32 root, const std::filesystem::path& path,
                     std::string& error);
    static uint32 Instantiate(Scene& target, const std::filesystem::path& path, std::string& error,
                              uint32 parent = 0);
    static uint32 FindInstanceRoot(const Scene& scene, uint32 entity);
    static bool Refresh(Scene& target, uint32 root, std::string& error);
    static bool Revert(Scene& target, uint32 root, std::string& error);
    static bool Unpack(Scene& target, uint32 root, std::string& error);
};

} // namespace LuxEngine
