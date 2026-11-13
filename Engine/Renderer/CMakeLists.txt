lux_engine_module(Renderer
    PUBLIC_DEPENDENCIES LuxModuleCore LuxModuleRHI
    PRIVATE_DEPENDENCIES LuxModulePlatform LuxModuleEnvironment LuxModuleAssets
    HEADERS
        Private/Forge/ForgeBackend.hpp
        Private/Forge/LuxForgeApi.h
        Private/RHI/RenderBackend.hpp
        Public/LuxEngine/Renderer/Renderer2D/Renderer2D.hpp
        Public/LuxEngine/Renderer/Renderer3D/Render3DSettings.hpp
        Public/LuxEngine/Renderer/Renderer3D/Renderer3D.hpp
        Public/LuxEngine/Testing/GpuFault.hpp
    SOURCES
        Private/Forge/ForgeBackend.cpp
        Private/RHI/RenderBackend.cpp
        Private/RHI/Buffer.cpp
        Private/RHI/GpuDevice.cpp
        Private/RHI/Texture.cpp
        Private/Renderer2D/Renderer2D.cpp
        Private/Renderer3D/Render3DSettings.cpp
        Private/Renderer3D/Renderer3D.cpp
)
