# LuxEngine Runtime Library

set(LUXRUNTIME_SOURCES
    Runtime.hpp
    Runtime.cpp
    ScenePlayer.hpp
    ScenePlayer.cpp
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${LUXRUNTIME_SOURCES})

add_library(LuxRuntime STATIC ${LUXRUNTIME_SOURCES})
add_library(LuxEngine::Runtime ALIAS LuxRuntime)
set_target_properties(LuxRuntime PROPERTIES EXPORT_NAME Runtime)

target_include_directories(LuxRuntime PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(LuxRuntime PUBLIC LuxEngine::Engine)

lux_set_target_warnings(LuxRuntime)

# Standalone scene player used by the editor's "run in window" command.
add_executable(LuxPlayer Player/Main.cpp)
target_link_libraries(LuxPlayer PRIVATE LuxEngine::Runtime)
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
