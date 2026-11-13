lux_engine_module(Renderer
    PUBLIC_DEPENDENCIES LuxModuleCore LuxModuleRHI
    PRIVATE_DEPENDENCIES LuxModulePlatform LuxModuleEnvironment LuxModuleAssets Vulkan::Vulkan
    HEADERS
        Private/Forge/ForgeBackend.hpp
        Private/Forge/LuxForgeApi.h
        Private/RHI/RenderBackend.hpp
        Private/Vulkan/VulkanBackend.hpp
        Private/Vulkan/VulkanDevice.hpp
        Private/Vulkan/VulkanFault.hpp
        Private/Vulkan/VulkanPlatform.hpp
        Private/Vulkan/VulkanScene3D.hpp
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
        Private/Vulkan/VulkanAllocator.cpp
        Private/Vulkan/VulkanBackend.cpp
        Private/Vulkan/VulkanContext.cpp
        Private/Vulkan/VulkanPipeline.cpp
        Private/Vulkan/VulkanPlatform.cpp
        Private/Vulkan/VulkanResources.cpp
        Private/Vulkan/VulkanScene3D.cpp
        Private/Vulkan/VulkanSwapchain.cpp
)
target_include_directories(LuxModuleRenderer PRIVATE
    $<TARGET_PROPERTY:GPUOpen::VulkanMemoryAllocator,INTERFACE_INCLUDE_DIRECTORIES>)
