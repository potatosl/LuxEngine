add_library(AsterEngine STATIC
    src/Application.cpp
    src/GpuRenderer.cpp
)
add_library(Aster::Engine ALIAS AsterEngine)
target_include_directories(AsterEngine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(AsterEngine PUBLIC SDL3::SDL3)
target_compile_features(AsterEngine PUBLIC cxx_std_20)
