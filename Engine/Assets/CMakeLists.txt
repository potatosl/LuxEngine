lux_engine_module(Assets
    PUBLIC_DEPENDENCIES LuxModuleCore LuxModuleRHI
    PRIVATE_DEPENDENCIES SDL3::SDL3
    HEADERS
        Public/LuxEngine/Assets/AssetManager.hpp
    SOURCES
        Private/AssetManager.cpp
)
target_include_directories(LuxModuleAssets SYSTEM PRIVATE "${stb_SOURCE_DIR}")
