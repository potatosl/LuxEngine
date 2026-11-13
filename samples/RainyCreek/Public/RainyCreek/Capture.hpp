#pragma once

#include <LuxEngine/Renderer/RHI/GpuDevice.hpp>
#include <filesystem>

namespace RainyCreek {
void WriteCapture(LuxEngine::GpuDevice& device, const std::filesystem::path& path);
} // namespace RainyCreek
