lux_engine_module(Platform
    PUBLIC_DEPENDENCIES LuxModuleCore SDL3::SDL3
    HEADERS
        Public/LuxEngine/Platform/Branding.hpp
        Public/LuxEngine/Platform/Input.hpp
        Public/LuxEngine/Platform/Window.hpp
    SOURCES
        Private/Branding.cpp
        Private/Input.cpp
        Private/Window.cpp
)
target_include_directories(LuxModulePlatform SYSTEM PRIVATE "${stb_SOURCE_DIR}")
target_sources(LuxModulePlatform PRIVATE "${CMAKE_BINARY_DIR}/generated/Branding.cpp")
