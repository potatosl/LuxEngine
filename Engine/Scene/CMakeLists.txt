lux_engine_module(Scene
    PUBLIC_DEPENDENCIES LuxModuleCore LuxModuleECS LuxModuleAnimation LuxModulePhysics
    PRIVATE_DEPENDENCIES LuxModuleRenderer box2d::box2d Threads::Threads
    HEADERS
        Private/PhysicsWorld2D.hpp
        Public/LuxEngine/Scene/Entity.hpp
        Public/LuxEngine/Scene/Scene.hpp
        Public/LuxEngine/Scene/SceneManager.hpp
    SOURCES
        Private/PhysicsWorld2D.cpp
        Private/Scene.cpp
        Private/SceneHierarchy.cpp
        Private/SceneManager.cpp
)
