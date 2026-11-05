add_library(LuxEngine STATIC
    src/Application.cpp
    src/GpuRenderer.cpp
)
add_library(LuxEngine::Engine ALIAS LuxEngine)
target_include_directories(LuxEngine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(LuxEngine PUBLIC SDL3::SDL3)
target_compile_features(LuxEngine PUBLIC cxx_std_20)
