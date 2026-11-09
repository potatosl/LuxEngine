set(LUXENGINE_HEADERS
    Core/Core.hpp
    Core/Log.hpp
    Core/Timer.hpp
    Core/Math.hpp
    Core/Event.hpp
    Platform/Branding.hpp
    Platform/Window.hpp
    Platform/Input.hpp
    Renderer/RHI/GpuDevice.hpp
    Renderer/RHI/Buffer.hpp
    Renderer/RHI/Texture.hpp
    Renderer/Renderer2D/Renderer2D.hpp
    Renderer/Renderer3D/Renderer3D.hpp
    Scene/Scene.hpp
    Scene/Entity.hpp
    ECS/ECS.hpp
    Physics/PhysicsEngine.hpp
    Audio/AudioEngine.hpp
    Animation/AnimationEngine.hpp
    Assets/AssetManager.hpp
    Application.hpp
    LuxEngine.hpp
)

set(LUXENGINE_SOURCES
    Platform/Branding.cpp
    Core/Log.cpp
    Core/Timer.cpp
    Platform/Window.cpp
    Platform/Input.cpp
    Platform/VulkanPlatform.hpp
    Platform/VulkanPlatform.cpp
    Renderer/RHI/GpuDevice.cpp
    Renderer/RHI/Buffer.cpp
    Renderer/RHI/Texture.cpp
    Renderer/Vulkan/VulkanFault.hpp
    Renderer/Vulkan/VulkanDevice.hpp
    Renderer/Vulkan/VulkanAllocator.cpp
    Renderer/Vulkan/VulkanContext.cpp
    Renderer/Vulkan/VulkanSwapchain.cpp
    Renderer/Vulkan/VulkanPipeline.cpp
    Renderer/Vulkan/VulkanResources.cpp
    Renderer/Renderer2D/Renderer2D.cpp
    Renderer/Renderer3D/Renderer3D.cpp
    Scene/Scene.cpp
    Physics/PhysicsEngine.cpp
    Physics/PhysicsWorld2D.h
    Physics/PhysicsWorld2D.cpp
    Audio/AudioEngine.cpp
    Animation/AnimationEngine.cpp
    Assets/AssetManager.cpp
    Application.cpp
)

# Organize source files into Visual Studio folders matching the directory structure
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${LUXENGINE_SOURCES} ${LUXENGINE_HEADERS})

option(LUX_BUILD_SHARED "Build LuxEngine as a shared library" OFF)
if(LUX_BUILD_SHARED)
    add_library(LuxEngine SHARED ${LUXENGINE_SOURCES} ${LUXENGINE_HEADERS})
    target_compile_definitions(LuxEngine PUBLIC LUX_ENGINE_SHARED PRIVATE LUX_ENGINE_BUILD)
else()
    add_library(LuxEngine STATIC ${LUXENGINE_SOURCES} ${LUXENGINE_HEADERS})
endif()
add_library(LuxEngine::Engine ALIAS LuxEngine)
set_target_properties(LuxEngine PROPERTIES EXPORT_NAME Engine)
target_sources(LuxEngine PRIVATE "${CMAKE_BINARY_DIR}/generated/Branding.cpp")
option(LUX_GPU_FAULT_INJECTION "Compile Vulkan failure-injection hooks used by the tests" ${LUX_BUILD_TESTS})
if(LUX_GPU_FAULT_INJECTION)
    target_compile_definitions(LuxEngine PUBLIC LUX_GPU_FAULT_INJECTION=1)
endif()

target_include_directories(LuxEngine PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(LuxEngine PUBLIC
    SDL3::SDL3
    box2d::box2d
    PRIVATE
    Vulkan::Vulkan
)
target_include_directories(LuxEngine PRIVATE
    $<TARGET_PROPERTY:GPUOpen::VulkanMemoryAllocator,INTERFACE_INCLUDE_DIRECTORIES>
)

target_compile_features(LuxEngine PUBLIC cxx_std_20)
target_include_directories(LuxEngine SYSTEM PRIVATE ${stb_SOURCE_DIR})

lux_set_target_warnings(LuxEngine)

if(MSVC)
    # PUBLIC: games and tools built on the engine (including from the installed SDK) also read
    # their sources as UTF-8. Without it Korean Windows reads them as code page 949 and Korean
    # string literals fail to compile (C2001).
    target_compile_options(LuxEngine PUBLIC $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>)
endif()
