# LuxEngine Runtime Library

add_library(LuxRuntime STATIC
    Runtime.cpp
)
add_library(LuxEngine::Runtime ALIAS LuxRuntime)

target_include_directories(LuxRuntime PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
)

target_link_libraries(LuxRuntime PUBLIC LuxEngine::Engine)

lux_set_target_warnings(LuxRuntime)
