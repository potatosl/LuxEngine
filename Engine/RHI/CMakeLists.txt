# GPU interface headers are independent of their Vulkan implementation.
lux_engine_module(RHI
    PUBLIC_DEPENDENCIES LuxModuleCore
    SOURCES
        Private/Buffer.cpp
        Private/Texture.cpp
        Private/TextureMipChain.cpp
    HEADERS
        Public/LuxEngine/Renderer/RHI/Buffer.hpp
        Public/LuxEngine/Renderer/RHI/GpuDevice.hpp
        Public/LuxEngine/Renderer/RHI/Texture.hpp
)
