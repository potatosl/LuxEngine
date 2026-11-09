cmake_minimum_required(VERSION 3.24)
project(MinimalGame LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find installed LuxEngine SDK
find_package(LuxEngine CONFIG REQUIRED)

add_executable(MinimalGame main.cpp)
target_link_libraries(MinimalGame PRIVATE LuxEngine::Engine LuxEngine::Runtime)

# Copies shaders and runtime DLLs (e.g. SDL3.dll) next to the executable
if(COMMAND lux_copy_runtime)
    lux_copy_runtime(MinimalGame)
elseif(COMMAND lux_copy_shaders)
    lux_copy_shaders(MinimalGame)
endif()
