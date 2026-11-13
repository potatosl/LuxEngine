lux_engine_module(Application
    PUBLIC_DEPENDENCIES LuxModuleCore LuxModulePlatform LuxModuleRenderer LuxModuleAssets LuxModuleAudio LuxModulePhysics LuxModuleScene
    HEADERS
        Public/LuxEngine/Application.hpp
        Public/LuxEngine/EntryPoint.hpp
        Public/LuxEngine/LuxEngine.hpp
    SOURCES
        Private/Application.cpp
)
