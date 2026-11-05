set(LUXENGINE_SOURCES
    Core/Log.cpp
    Core/Timer.cpp
    Platform/Window.cpp
    Renderer/RHI/GpuDevice.cpp
    Renderer/Renderer2D/Renderer2D.cpp
    Renderer/Renderer3D/Renderer3D.cpp
    Scene/Scene.cpp
    Physics/PhysicsEngine.cpp
    Audio/AudioEngine.cpp
    Animation/AnimationEngine.cpp
    Assets/AssetManager.cpp
    Application.cpp
)

add_library(LuxEngine STATIC ${LUXENGINE_SOURCES})
add_library(LuxEngine::Engine ALIAS LuxEngine)

target_include_directories(LuxEngine PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(LuxEngine PUBLIC
    SDL3::SDL3
)

target_compile_features(LuxEngine PUBLIC cxx_std_20)

lux_set_target_warnings(LuxEngine)

if(MSVC)
    target_compile_options(LuxEngine PRIVATE /utf-8)
endif()
