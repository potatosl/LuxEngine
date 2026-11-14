lux_engine_module(Assets
    PUBLIC_DEPENDENCIES LuxModuleCore LuxModuleRHI
    PRIVATE_DEPENDENCIES SDL3::SDL3
    HEADERS
        Public/LuxEngine/Assets/AssetManager.hpp
        Public/LuxEngine/Assets/BasicModelLibrary.hpp
        Public/LuxEngine/Assets/ModelAsset.hpp
    SOURCES
        Private/AssetManager.cpp
        Private/BasicModelLibrary.cpp
        Private/FbxImporter.cpp
        Private/ModelAsset.cpp
        Private/FbxModelImporter.cpp
)
target_include_directories(LuxModuleAssets SYSTEM PRIVATE "${stb_SOURCE_DIR}")
target_compile_definitions(LuxModuleAssets PRIVATE
    LUX_BASIC_MODEL_SOURCE_DIR="${LUX_BASIC_MODEL_SOURCE_DIR}")

# Keep the pinned parser private and include its object in the final static/shared engine.
# SDK consumers do not need ufbx headers, packages, or a second linker dependency.
add_library(LuxUfbx OBJECT "${CMAKE_SOURCE_DIR}/Engine/ThirdParty/ufbx/ufbx.c")
set_target_properties(LuxUfbx PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden FOLDER ThirdParty)
target_include_directories(LuxModuleAssets SYSTEM PRIVATE
    "${CMAKE_SOURCE_DIR}/Engine/ThirdParty/ufbx")
if(MSVC)
    target_compile_options(LuxUfbx PRIVATE /W1 /utf-8)
endif()
if(LUX_ENABLE_SANITIZERS)
    target_compile_options(LuxUfbx PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
endif()
target_sources(LuxEngine PRIVATE $<TARGET_OBJECTS:LuxUfbx>)
