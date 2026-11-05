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
    Renderer/RHI/GpuDevice.cpp
    Renderer/RHI/Buffer.cpp
    Renderer/RHI/Texture.cpp
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

if(LUX_USE_OGRE)
    list(REMOVE_ITEM LUXENGINE_SOURCES Renderer/RHI/GpuDevice.cpp)
    list(APPEND LUXENGINE_SOURCES Renderer/RHI/GpuDeviceOgre.cpp)
    add_library(LuxOgreBridge OBJECT Renderer/Ogre/OgreBridge.cpp Renderer/Ogre/OgreBridge.h)
    set_target_properties(LuxOgreBridge PROPERTIES
        CXX_STANDARD 14
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
        POSITION_INDEPENDENT_CODE ON)
    target_include_directories(LuxOgreBridge PRIVATE "${CMAKE_SOURCE_DIR}")
    target_link_libraries(LuxOgreBridge PRIVATE SDL3::SDL3 LuxOgre::Main)
endif()

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
if(LUX_USE_OGRE)
    target_sources(LuxEngine PRIVATE $<TARGET_OBJECTS:LuxOgreBridge>)
    target_link_libraries(LuxEngine PUBLIC LuxOgre::Main)
    target_compile_definitions(LuxEngine PUBLIC LUX_USE_OGRE=1
        PRIVATE "LUX_OGRE_GL_PLUGIN_PATH=\"${LUX_OGRE_GL_PLUGIN_PATH}\"")
else()
    target_compile_definitions(LuxEngine PUBLIC LUX_USE_OGRE=0)
endif()

target_include_directories(LuxEngine PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(LuxEngine PUBLIC
    SDL3::SDL3
    box2d::box2d
)

target_compile_features(LuxEngine PUBLIC cxx_std_20)
target_include_directories(LuxEngine SYSTEM PRIVATE ${stb_SOURCE_DIR})

lux_set_target_warnings(LuxEngine)

if(MSVC)
    target_compile_options(LuxEngine PRIVATE /utf-8)
endif()
