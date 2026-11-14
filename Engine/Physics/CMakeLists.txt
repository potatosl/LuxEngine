lux_engine_module(Physics
    PUBLIC_DEPENDENCIES LuxModuleCore
    PRIVATE_DEPENDENCIES Jolt::Jolt
    HEADERS
        Public/LuxEngine/Physics/PhysicsEngine.hpp
        Public/LuxEngine/Physics/PhysicsWorld.hpp
    SOURCES
        Private/PhysicsEngine.cpp
        Private/PhysicsWorld.cpp
)
