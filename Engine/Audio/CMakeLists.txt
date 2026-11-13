lux_engine_module(Audio
    PUBLIC_DEPENDENCIES LuxModuleCore
    PRIVATE_DEPENDENCIES SDL3::SDL3
    HEADERS
        Public/LuxEngine/Audio/AudioEngine.hpp
    SOURCES
        Private/AudioEngine.cpp
)
