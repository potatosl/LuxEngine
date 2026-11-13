lux_engine_module(Scene
    PUBLIC_DEPENDENCIES LuxModuleCore LuxModuleECS LuxModuleAnimation LuxModulePhysics
    PRIVATE_DEPENDENCIES LuxModuleRenderer box2d::box2d Threads::Threads nlohmann_json::nlohmann_json
    HEADERS
        Private/PhysicsWorld2D.hpp
        Public/LuxEngine/Scene/Entity.hpp
        Public/LuxEngine/Scene/Scene.hpp
        Public/LuxEngine/Scene/SceneManager.hpp
    SOURCES
        Private/Entity.cpp
        Private/PhysicsWorld2D.cpp
        Private/SceneJson.cpp
        Private/Scene.cpp
        Private/SceneHierarchy.cpp
        Private/SceneManager.cpp
)
