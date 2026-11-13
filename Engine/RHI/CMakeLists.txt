# GPU interface headers are independent of their Vulkan implementation.
lux_engine_module(RHI
    HEADER_ONLY
    PUBLIC_DEPENDENCIES LuxModuleCore
    HEADERS
        Public/LuxEngine/Renderer/RHI/Buffer.hpp
        Public/LuxEngine/Renderer/RHI/GpuDevice.hpp
        Public/LuxEngine/Renderer/RHI/Texture.hpp
)
