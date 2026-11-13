lux_engine_module(Core
    PRIVATE_DEPENDENCIES SDL3::SDL3
    HEADERS
        Public/LuxEngine/Core/Core.hpp
        Public/LuxEngine/Core/Event.hpp
        Public/LuxEngine/Core/Log.hpp
        Public/LuxEngine/Core/Math.hpp
        Public/LuxEngine/Core/Random.hpp
        Public/LuxEngine/Core/Timer.hpp
    SOURCES
        Private/Random.cpp
        Private/Event.cpp
        Private/Math.cpp
        Private/Log.cpp
        Private/Timer.cpp
)
