# Build dependencies are installed by the developer, never downloaded by CMake.
find_package(SDL3 3.2 CONFIG REQUIRED)
find_package(Jolt CONFIG REQUIRED HINTS "$ENV{LUX_JOLT_ROOT}" "$ENV{HOME}/.local")
# Older configurations cached an empty value before downloading stb.
# An empty cache entry prevents find_path from searching installed headers.
if(DEFINED STB_IMAGE_INCLUDE_DIR AND STB_IMAGE_INCLUDE_DIR STREQUAL "")
    unset(STB_IMAGE_INCLUDE_DIR CACHE)
endif()
find_path(STB_IMAGE_INCLUDE_DIR NAMES stb_image.h PATH_SUFFIXES stb)
if(NOT STB_IMAGE_INCLUDE_DIR OR NOT EXISTS "${STB_IMAGE_INCLUDE_DIR}/stb_image.h")
    message(FATAL_ERROR "stb_image.h not found. Install stb-devel or set STB_IMAGE_INCLUDE_DIR to its containing directory.")
endif()
set(stb_SOURCE_DIR "${STB_IMAGE_INCLUDE_DIR}")

# Private parser dependency; JSON types are not part of the engine SDK API.
find_package(nlohmann_json 3.11 CONFIG REQUIRED)
