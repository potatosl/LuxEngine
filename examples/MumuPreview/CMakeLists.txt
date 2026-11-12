cmake_minimum_required(VERSION 3.24)
project(MumuPreview LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(MUMU_REPOSITORY "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(MUMU_ASSET_DIRECTORY "${MUMU_REPOSITORY}/Assets/Characters/Mumu/luxengine" CACHE PATH "Mumu OBJ asset directory")
find_package(LuxEngine CONFIG REQUIRED PATHS "${MUMU_REPOSITORY}/dist/LuxEngine-SDK-0.1.0-x64")

# dist/ is not tracked, so an SDK installed before the current engine sources can linger there.
# Fail at configure time instead of with a compile error when it predates Mesh3D vertex normals.
get_target_property(MUMU_SDK_INCLUDES LuxEngine::Engine INTERFACE_INCLUDE_DIRECTORIES)
set(MUMU_SDK_COMPATIBLE OFF)
foreach(directory IN LISTS MUMU_SDK_INCLUDES)
    set(header "${directory}/Engine/Renderer/Renderer3D/Renderer3D.hpp")
    if(EXISTS "${header}")
        file(READ "${header}" MUMU_RENDERER3D_HEADER)
        if(MUMU_RENDERER3D_HEADER MATCHES "std::span<const Vertex3D>")
            set(MUMU_SDK_COMPATIBLE ON)
        endif()
    endif()
endforeach()
if(NOT MUMU_SDK_COMPATIBLE)
    message(FATAL_ERROR
        "LuxEngine SDK at ${LuxEngine_DIR} is older than the engine sources and lacks "
        "Mesh3D::Upload(std::span<const Vertex3D>). Reinstall it from a current build, e.g.\n"
        "  cmake --install build/main-audit --config Debug --prefix dist/LuxEngine-SDK-0.1.0-x64\n"
        "  cmake --install build/main-audit --config Release --prefix dist/LuxEngine-SDK-0.1.0-x64\n"
        "or pass -DLuxEngine_DIR=<SDK>/lib/cmake/LuxEngine.")
endif()

add_executable(MumuPreview main.cpp)
target_link_libraries(MumuPreview PRIVATE LuxEngine::Engine)
if(MSVC)
    target_compile_options(MumuPreview PRIVATE /utf-8 /W4)
endif()
lux_copy_runtime(MumuPreview)
add_custom_target(MumuAssets
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${MUMU_ASSET_DIRECTORY}" "$<TARGET_FILE_DIR:MumuPreview>/assets/mumu"
    VERBATIM)
add_dependencies(MumuPreview MumuAssets)

enable_testing()
add_test(NAME Mumu_assets COMMAND MumuPreview --validate-assets)
add_test(NAME Mumu_gpu_smoke COMMAND MumuPreview --smoke-test)
set_tests_properties(Mumu_gpu_smoke PROPERTIES LABELS gpu TIMEOUT 120)
