#include "RHI/RenderBackend.hpp"
#include "Forge/ForgeBackend.hpp"

namespace LuxEngine {
Ref<Rhi::RenderBackend> Rhi::CreateRenderBackend(const GpuDeviceConfig&) {
    return std::make_shared<Forge::ForgeBackend>();
}
} // namespace LuxEngine

namespace LuxEngine::Rhi {
Scene3DRenderer::~Scene3DRenderer() = default;
RenderBackend::~RenderBackend() = default;
} // namespace LuxEngine::Rhi
