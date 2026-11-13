#include "RHI/RenderBackend.hpp"

#include "Forge/ForgeBackend.hpp"
#include "Vulkan/VulkanBackend.hpp"

#include <cstdlib>
#include <string_view>

namespace LuxEngine {

// LUX_RENDER_BACKEND=forge selects The Forge (Direct3D 12, Windows); anything else, or
// nothing, selects the Vulkan backend.
Ref<Rhi::RenderBackend> Rhi::CreateRenderBackend(const GpuDeviceConfig&) {
#if defined(_WIN32)
    char* value = nullptr;
    size_t length = 0;
    bool forge = false;
    if (_dupenv_s(&value, &length, "LUX_RENDER_BACKEND") == 0 && value) {
        forge = std::string_view(value) == "forge";
        std::free(value);
    }
    if (forge)
        return CreateRef<Forge::ForgeBackend>();
#endif
    return CreateRef<Vk::VulkanBackend>();
}

} // namespace LuxEngine
