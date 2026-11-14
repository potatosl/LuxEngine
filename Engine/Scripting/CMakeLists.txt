lux_engine_module(Scripting
    PUBLIC_DEPENDENCIES LuxModuleCore LuxModuleECS
    PRIVATE_DEPENDENCIES LuxModuleScene LuxModulePlatform SDL3::SDL3
    HEADERS
        Public/LuxEngine/Scripting/ScriptRuntime.hpp
        Private/ScriptAbi.hpp
    SOURCES
        Private/ScriptRuntime.cpp
)
target_include_directories(LuxModuleScripting PRIVATE "${LUX_DOTNET_HOST_INCLUDE_DIR}")
