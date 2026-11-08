add_executable(LuxEngineTests MainTest.cpp)
target_link_libraries(LuxEngineTests PRIVATE LuxEngine::Engine)
target_include_directories(LuxEngineTests SYSTEM PRIVATE ${stb_SOURCE_DIR})
lux_set_target_warnings(LuxEngineTests)
lux_copy_runtime(LuxEngineTests)

# Headless suites need no display or GPU.
foreach(suite core physics2d scene assets audio window)
    add_test(NAME Engine_${suite} COMMAND LuxEngineTests ${suite})
    set_tests_properties(Engine_${suite} PROPERTIES TIMEOUT 30 LABELS headless
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:LuxEngineTests>")
endforeach()
set_tests_properties(Engine_audio PROPERTIES ENVIRONMENT "SDL_AUDIO_DRIVER=dummy")
set_tests_properties(Engine_window PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy")

# GPU suites need a display and a Vulkan driver; they exit with 77 (skipped) when either is missing.
set(LUX_GPU_TEST_ENVIRONMENT "SDL_AUDIO_DRIVER=dummy;LUX_VULKAN_VALIDATION=sync")
foreach(suite gpu vulkan failures stress lifecycle)
    add_test(NAME Engine_${suite} COMMAND LuxEngineTests ${suite})
    set_tests_properties(Engine_${suite} PROPERTIES TIMEOUT 120 LABELS gpu SKIP_RETURN_CODE 77
        ENVIRONMENT "${LUX_GPU_TEST_ENVIRONMENT}"
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:LuxEngineTests>")
endforeach()
add_test(NAME Engine_longrun COMMAND LuxEngineTests longrun)
set_tests_properties(Engine_longrun PROPERTIES TIMEOUT 600 LABELS "gpu;long" SKIP_RETURN_CODE 77
    ENVIRONMENT "SDL_AUDIO_DRIVER=dummy;LUX_VULKAN_VALIDATION=off"
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:LuxEngineTests>")

add_test(NAME Engine_runtime COMMAND LuxEngineDemo --smoke-test)
set_tests_properties(Engine_runtime PROPERTIES TIMEOUT 60 LABELS gpu
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:LuxEngineDemo>"
    ENVIRONMENT "${LUX_GPU_TEST_ENVIRONMENT}")

# Runtime: LuxPlayer argument parsing and view fitting, then a real LuxPlayer run.
add_executable(LuxRuntimeTests RuntimeTest.cpp)
target_link_libraries(LuxRuntimeTests PRIVATE LuxEngine::Runtime)
lux_set_target_warnings(LuxRuntimeTests)
if(MSVC)
    target_compile_options(LuxRuntimeTests PRIVATE /utf-8)
endif()
lux_copy_runtime(LuxRuntimeTests)
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
    add_executable(LuxFuzzSceneEngine FuzzScene.cpp)
    target_link_libraries(LuxFuzzSceneEngine PRIVATE LuxEngine::Engine)
    target_compile_definitions(LuxFuzzSceneEngine PRIVATE LUX_FUZZING_ENGINE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(LuxFuzzSceneEngine PRIVATE -fsanitize=fuzzer,address,undefined)
        target_link_options(LuxFuzzSceneEngine PRIVATE -fsanitize=fuzzer,address,undefined)
    endif()
    lux_copy_runtime(LuxFuzzSceneEngine)
endif()

