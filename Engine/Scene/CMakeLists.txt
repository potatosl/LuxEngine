lux_engine_module(Scene
    PUBLIC_DEPENDENCIES LuxModuleCore LuxModuleECS LuxModuleAnimation LuxModulePhysics
    PRIVATE_DEPENDENCIES LuxModuleRenderer LuxModuleAssets Threads::Threads nlohmann_json::nlohmann_json
    HEADERS
        Private/ScenePhysicsWorld.hpp
        Public/LuxEngine/Scene/Entity.hpp
        Public/LuxEngine/Scene/Scene.hpp
        Public/LuxEngine/Scene/SceneManager.hpp
        Public/LuxEngine/Scene/PrefabLibrary.hpp
    SOURCES
        Private/Entity.cpp
        Private/ScenePhysicsWorld.cpp
        Private/SceneJson.cpp
        Private/Scene.cpp
        Private/SceneHierarchy.cpp
        Private/SceneManager.cpp
        Private/PrefabLibrary.cpp
)
