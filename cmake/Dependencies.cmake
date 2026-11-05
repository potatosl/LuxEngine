# LuxEngine Dependency Management
include(FetchContent)

# -------------------------------------------------------------
# Options for external dependencies
# Initial build: SDL3 is required; others are prepared for extension
# -------------------------------------------------------------
option(LUX_ENABLE_SDL3      "Enable SDL3 for Windowing, Input, and GPU" ON)
option(LUX_ENABLE_ENTT      "Enable EnTT ECS Library" OFF)
option(LUX_ENABLE_GLM       "Enable GLM Mathematics Library" OFF)
option(LUX_ENABLE_BOX2D     "Enable Box2D 2D Physics Library" OFF)
option(LUX_ENABLE_JOLT      "Enable Jolt 3D Physics Library" OFF)
option(LUX_ENABLE_MINIAUDIO "Enable miniaudio Audio Library" OFF)
option(LUX_ENABLE_SPDLOG    "Enable spdlog Logging Library" OFF)
option(LUX_ENABLE_STB_IMAGE "Enable stb_image Image Loader" OFF)
option(LUX_ENABLE_CGLTF     "Enable cgltf glTF Model Loader" OFF)
option(LUX_ENABLE_TESTS     "Enable Test Suite (Catch2)" OFF)

# -------------------------------------------------------------
# SDL3 (Required)
# -------------------------------------------------------------
if(LUX_ENABLE_SDL3)
    message(STATUS "[LuxEngine] Configuring SDL3 dependency...")
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.4.18
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(SDL3)
endif()

# -------------------------------------------------------------
# EnTT (ECS)
# -------------------------------------------------------------
if(LUX_ENABLE_ENTT)
    message(STATUS "[LuxEngine] Configuring EnTT dependency...")
    FetchContent_Declare(EnTT
        GIT_REPOSITORY https://github.com/skypjack/entt.git
        GIT_TAG v3.14.0
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(EnTT)
endif()

# -------------------------------------------------------------
# GLM (Math)
# -------------------------------------------------------------
if(LUX_ENABLE_GLM)
    message(STATUS "[LuxEngine] Configuring GLM dependency...")
    FetchContent_Declare(glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG 1.0.1
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(glm)
endif()

# -------------------------------------------------------------
# Box2D (2D Physics)
# -------------------------------------------------------------
if(LUX_ENABLE_BOX2D)
    message(STATUS "[LuxEngine] Configuring Box2D dependency...")
    set(BOX2D_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
    set(BOX2D_BUILD_TESTBED OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(box2d
        GIT_REPOSITORY https://github.com/erincatto/box2d.git
        GIT_TAG v3.0.0
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(box2d)
endif()

# -------------------------------------------------------------
# Jolt Physics (3D Physics)
# -------------------------------------------------------------
if(LUX_ENABLE_JOLT)
    message(STATUS "[LuxEngine] Configuring Jolt Physics dependency...")
    set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
    set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(JoltPhysics
        GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
        GIT_TAG v5.2.0
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(JoltPhysics)
endif()

# -------------------------------------------------------------
# spdlog (Logging)
# -------------------------------------------------------------
if(LUX_ENABLE_SPDLOG)
    message(STATUS "[LuxEngine] Configuring spdlog dependency...")
    set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
    set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.15.0
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(spdlog)
endif()

# -------------------------------------------------------------
# Catch2 (Testing)
# -------------------------------------------------------------
if(LUX_ENABLE_TESTS)
    message(STATUS "[LuxEngine] Configuring Catch2 dependency...")
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.7.1
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(Catch2)
endif()
