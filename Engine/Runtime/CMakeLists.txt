# LuxEngine Runtime Library

set(LUXRUNTIME_SOURCES
    Public/LuxEngine/Runtime/Runtime.hpp
    Private/ProjectManifest.cpp
    Public/LuxEngine/Runtime/ProjectManifest.hpp
    Private/Runtime.cpp
    Public/LuxEngine/Runtime/ScenePlayer.hpp
    Private/ScenePlayer.cpp
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${LUXRUNTIME_SOURCES})

add_library(LuxRuntime STATIC ${LUXRUNTIME_SOURCES})
add_library(LuxEngine::Runtime ALIAS LuxRuntime)
set_target_properties(LuxRuntime PROPERTIES EXPORT_NAME Runtime)

target_include_directories(LuxRuntime PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Public>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(LuxRuntime PUBLIC LuxEngine::Engine
    PRIVATE $<BUILD_INTERFACE:nlohmann_json::nlohmann_json>)

lux_set_target_warnings(LuxRuntime)

# Standalone scene player used by the editor's "run in window" command.
add_executable(LuxPlayer Private/Player/Main.cpp)
target_link_libraries(LuxPlayer PRIVATE LuxEngine::Runtime)
target_compile_definitions(LuxPlayer PRIVATE LUX_RUNTIME_ENGINE_VERSION="${LuxEngine_VERSION}")
lux_set_target_warnings(LuxPlayer)
if(MSVC)
    target_compile_options(LuxPlayer PRIVATE /utf-8)
endif()
lux_copy_runtime(LuxPlayer)
if(WIN32 AND LUX_BUILD_SHARED)
    add_custom_command(TARGET LuxPlayer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:LuxEngine>
            $<TARGET_FILE_DIR:LuxPlayer>)
endif()
install(TARGETS LuxPlayer RUNTIME DESTINATION bin)

install(DIRECTORY Public/ DESTINATION include FILES_MATCHING PATTERN "*.hpp")
