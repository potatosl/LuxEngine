cmake_minimum_required(VERSION 3.24)

project(LuxEngine VERSION 0.1.0 LANGUAGES CXX C)

# -------------------------------------------------------------
# C++20 Standard Configuration
# -------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# -------------------------------------------------------------
# Global Binary Output Directories & Cross-Platform RPATH
# -------------------------------------------------------------
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# Ensure Linux and macOS find shared libraries in the same directory
set(CMAKE_BUILD_RPATH_USE_ORIGIN TRUE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
if(APPLE)
    set(CMAKE_INSTALL_RPATH
        "@executable_path;@executable_path/../lib;@executable_path/../../..;@executable_path/../../../lib")
elseif(UNIX)
    set(CMAKE_INSTALL_RPATH "$ORIGIN;$ORIGIN/../lib")
endif()

foreach(OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG_UPPER)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG_UPPER} "${CMAKE_BINARY_DIR}/bin/${OUTPUTCONFIG}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${OUTPUTCONFIG_UPPER} "${CMAKE_BINARY_DIR}/bin/${OUTPUTCONFIG}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${OUTPUTCONFIG_UPPER} "${CMAKE_BINARY_DIR}/lib/${OUTPUTCONFIG}")
endforeach()

# Debug and Release engine libraries are installed side by side into one SDK; without a suffix the
# second install overwrites the first and games fail to link (MSVC runtime mismatch).
if(NOT DEFINED CMAKE_DEBUG_POSTFIX)
    set(CMAKE_DEBUG_POSTFIX d)
endif()

# Fallback mapping for imported targets (e.g. Box2D) that only provide Debug and/or Release builds,
# preventing Visual Studio multi-config generators from failing on RelWithDebInfo or MinSizeRel.
set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO "RelWithDebInfo;Release;MinSizeRel;Debug;NOCONFIG;")
set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL "MinSizeRel;Release;RelWithDebInfo;Debug;NOCONFIG;")
set(CMAKE_MAP_IMPORTED_CONFIG_RELEASE "Release;RelWithDebInfo;MinSizeRel;Debug;NOCONFIG;")
set(CMAKE_MAP_IMPORTED_CONFIG_DEBUG "Debug;Release;RelWithDebInfo;MinSizeRel;NOCONFIG;")

# -------------------------------------------------------------
# Include Custom CMake Modules
# -------------------------------------------------------------
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Dependencies)
include(Shaders)
include(Branding)
option(LUX_BUILD_TESTS "Build engine tests" ON)
option(LUX_BUILD_TOOLS "Build development tools" ON)
option(LUX_BUILD_EDITOR "Build the separate Qt 6 editor" OFF)
if(LUX_BUILD_TESTS)
    enable_testing()
endif()

# -------------------------------------------------------------
# Subdirectories
# -------------------------------------------------------------
add_subdirectory(Engine)
add_subdirectory(samples)
if(LUX_BUILD_TOOLS)
    add_subdirectory(tools)
endif()
if(LUX_BUILD_EDITOR)
    add_subdirectory(Editor)
endif()

if(LUX_BUILD_TESTS)
    add_subdirectory(Engine/Tests)
endif()

# Optional The Forge backend (LuxForge.dll); off unless -DLUX_ENABLE_FORGE=ON.
include(TheForge)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/LuxEngineConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/LuxEngineConfig.cmake"
    INSTALL_DESTINATION lib/cmake/LuxEngine
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/LuxEngineConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(TARGETS LuxEngine LuxRuntime EXPORT LuxEngineTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)
if(WIN32 AND TARGET SDL3::SDL3-shared)
    install(FILES $<TARGET_FILE:SDL3::SDL3-shared> DESTINATION bin)
endif()
install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/shaders/" DESTINATION share/LuxEngine/shaders)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/docs/licenses/" DESTINATION share/LuxEngine/licenses)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/docs/licenses/" DESTINATION licenses)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/samples/MinimalGame/" DESTINATION share/LuxEngine/examples/MinimalGame)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/samples/MinimalGame/" DESTINATION examples/MinimalGame)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/README.md" DESTINATION .)
install(EXPORT LuxEngineTargets NAMESPACE LuxEngine:: DESTINATION lib/cmake/LuxEngine)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/LuxEngineConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/LuxEngineConfigVersion.cmake"
    DESTINATION lib/cmake/LuxEngine
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/LuxEngineConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/LuxEngineConfigVersion.cmake"
    DESTINATION cmake/LuxEngine
)
