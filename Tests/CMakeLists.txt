add_executable(LuxEngineTests MainTest.cpp)
add_executable(LuxSceneCoreTests SceneCoreTest.cpp)
target_link_libraries(LuxSceneCoreTests PRIVATE LuxEngine::Engine)
lux_set_target_warnings(LuxSceneCoreTests)
lux_copy_runtime(LuxSceneCoreTests)
add_test(NAME Engine_scene_management COMMAND LuxSceneCoreTests)
set_tests_properties(Engine_scene_management PROPERTIES LABELS "headless;scene" TIMEOUT 60)
add_executable(LuxSceneManagerTests SceneManagerTest.cpp)
target_link_libraries(LuxSceneManagerTests PRIVATE LuxEngine::Engine)
lux_set_target_warnings(LuxSceneManagerTests)
lux_copy_runtime(LuxSceneManagerTests)
add_test(NAME Engine_scene_loading COMMAND LuxSceneManagerTests)
set_tests_properties(Engine_scene_loading PROPERTIES LABELS "headless;scene" TIMEOUT 90)
target_link_libraries(LuxEngineTests PRIVATE LuxEngine::Engine)
target_include_directories(LuxEngineTests SYSTEM PRIVATE ${stb_SOURCE_DIR})
lux_set_target_warnings(LuxEngineTests)
lux_copy_runtime(LuxEngineTests)

# Headless suites need no display or GPU.
foreach(suite core physics2d scene assets audio window security)
    add_test(NAME Engine_${suite} COMMAND LuxEngineTests ${suite})
    set_tests_properties(Engine_${suite} PROPERTIES TIMEOUT 30 LABELS headless
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:LuxEngineTests>")
endforeach()
set_tests_properties(Engine_audio PROPERTIES TIMEOUT 180 ENVIRONMENT "SDL_AUDIO_DRIVER=dummy")
set_tests_properties(Engine_window PROPERTIES TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=dummy")

# GPU suites need a display and a Vulkan driver; they exit with 77 (skipped) when either is missing.
set(LUX_GPU_TEST_ENVIRONMENT "SDL_AUDIO_DRIVER=dummy;LUX_VULKAN_VALIDATION=sync")
add_test(NAME Engine_scene_hierarchy_gpu COMMAND LuxSceneCoreTests --gpu)
set_tests_properties(Engine_scene_hierarchy_gpu PROPERTIES LABELS "gpu;scene" TIMEOUT 60 SKIP_RETURN_CODE 77
    ENVIRONMENT "${LUX_GPU_TEST_ENVIRONMENT}" WORKING_DIRECTORY "$<TARGET_FILE_DIR:LuxSceneCoreTests>")
foreach(suite gpu vulkan failures stress lifecycle multidevice texture_filter present_completion pbr hdr shadows render_lifetime render_failures)
    add_test(NAME Engine_${suite} COMMAND LuxEngineTests ${suite})
    set_tests_properties(Engine_${suite} PROPERTIES TIMEOUT 120 LABELS gpu SKIP_RETURN_CODE 77
        ENVIRONMENT "${LUX_GPU_TEST_ENVIRONMENT}"
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:LuxEngineTests>")
endforeach()
# Mixed validation settings must be taken from each device config in this regression.
set_tests_properties(Engine_multidevice PROPERTIES
    ENVIRONMENT "SDL_AUDIO_DRIVER=dummy;LUX_VULKAN_VALIDATION=")
add_test(NAME Engine_longrun COMMAND LuxEngineTests longrun)
set_tests_properties(Engine_longrun PROPERTIES TIMEOUT 600 LABELS "gpu;long" SKIP_RETURN_CODE 77
    ENVIRONMENT "SDL_AUDIO_DRIVER=dummy;LUX_VULKAN_VALIDATION=off"
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:LuxEngineTests>")

# Runtime: LuxPlayer argument parsing and view fitting, then a real LuxPlayer run.
add_executable(LuxRuntimeTests RuntimeTest.cpp)
target_link_libraries(LuxRuntimeTests PRIVATE LuxEngine::Runtime)
lux_set_target_warnings(LuxRuntimeTests)
if(MSVC)
    target_compile_options(LuxRuntimeTests PRIVATE /utf-8)
endif()
lux_copy_runtime(LuxRuntimeTests)
add_test(NAME Runtime_scene_management_gpu COMMAND LuxRuntimeTests scene-management-gpu)
set_tests_properties(Runtime_scene_management_gpu PROPERTIES LABELS "gpu;scene" TIMEOUT 90 SKIP_RETURN_CODE 77
    ENVIRONMENT "${LUX_GPU_TEST_ENVIRONMENT}" WORKING_DIRECTORY "$<TARGET_FILE_DIR:LuxRuntimeTests>")
foreach(suite parse fit)
    add_test(NAME Runtime_${suite} COMMAND LuxRuntimeTests ${suite})
    set_tests_properties(Runtime_${suite} PROPERTIES TIMEOUT 30 LABELS headless)
endforeach()
add_test(NAME Runtime_player_scene
    COMMAND LuxRuntimeTests write-scene "${CMAKE_CURRENT_BINARY_DIR}/PlayerSmoke.lux")
set_tests_properties(Runtime_player_scene PROPERTIES FIXTURES_SETUP lux_player_scene LABELS gpu)
add_test(NAME Runtime_player
    COMMAND LuxPlayer "${CMAKE_CURRENT_BINARY_DIR}/PlayerSmoke.lux" --frames 30)
set_tests_properties(Runtime_player PROPERTIES TIMEOUT 60 LABELS gpu
    FIXTURES_REQUIRED lux_player_scene
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:LuxPlayer>"
    ENVIRONMENT "${LUX_GPU_TEST_ENVIRONMENT}")

# Dedicated fuzz target for coverage-guided fuzzing and seed corpus verification
add_executable(LuxFuzzScene FuzzScene.cpp)
target_link_libraries(LuxFuzzScene PRIVATE LuxEngine::Engine)
lux_set_target_warnings(LuxFuzzScene)
lux_copy_runtime(LuxFuzzScene)

add_test(NAME Engine_fuzz_seeds COMMAND LuxFuzzScene)
set_tests_properties(Engine_fuzz_seeds PROPERTIES TIMEOUT 30 LABELS headless
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:LuxFuzzScene>")

option(LUX_BUILD_FUZZERS "Build coverage-guided fuzzers using libFuzzer and sanitizers" OFF)
if(LUX_BUILD_FUZZERS)
    add_executable(LuxFuzzSceneEngine FuzzScene.cpp FuzzStubs.cpp)
    target_link_libraries(LuxFuzzSceneEngine PRIVATE LuxEngine::Engine)
    target_compile_definitions(LuxFuzzSceneEngine PRIVATE LUX_FUZZING_ENGINE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(LuxFuzzSceneEngine PRIVATE -fsanitize=fuzzer)
        target_link_options(LuxFuzzSceneEngine PRIVATE -fsanitize=fuzzer)
    endif()
    lux_copy_runtime(LuxFuzzSceneEngine)
endif()

