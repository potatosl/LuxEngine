# LuxEngine Runtime Library

set(LUXRUNTIME_SOURCES
    Runtime.hpp
    Runtime.cpp
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${LUXRUNTIME_SOURCES})

add_library(LuxRuntime STATIC ${LUXRUNTIME_SOURCES})
add_library(LuxEngine::Runtime ALIAS LuxRuntime)

target_include_directories(LuxRuntime PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
)

target_link_libraries(LuxRuntime PUBLIC LuxEngine::Engine)

lux_set_target_warnings(LuxRuntime)
