#pragma once

#include <string>

namespace LuxEngine {
struct RawMeshData;

namespace Detail {
// Imports static polygon geometry into one world-baked mesh. Materials and node animation
// remain outside RawMeshData; deforming geometry is rejected instead of importing a wrong pose.
bool ReadFbxMesh(const std::string& filepath, RawMeshData& outMesh, std::string& error);
} // namespace Detail
} // namespace LuxEngine
