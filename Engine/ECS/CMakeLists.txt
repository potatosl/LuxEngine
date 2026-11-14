lux_engine_module(ECS
    PUBLIC_DEPENDENCIES LuxModuleCore LuxModuleRenderer
    PRIVATE_DEPENDENCIES nlohmann_json::nlohmann_json
    SOURCES
        Private/ECS.cpp
        Private/ScriptFields.cpp
    HEADERS
        Public/LuxEngine/ECS/ECS.hpp
        Public/LuxEngine/ECS/ScriptFields.hpp
)
