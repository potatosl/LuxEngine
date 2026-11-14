add_executable(LuxEngineTests MainTest.cpp)
add_executable(LuxFbxImportTests FbxImportTest.cpp)
target_link_libraries(LuxFbxImportTests PRIVATE LuxEngine::Engine)
lux_set_target_warnings(LuxFbxImportTests)
lux_copy_runtime(LuxFbxImportTests)
add_test(NAME Engine_fbx_import COMMAND LuxFbxImportTests "${LUX_BASIC_MODEL_SOURCE_DIR}")
set_tests_properties(Engine_fbx_import PROPERTIES LABELS "headless;assets;fbx" TIMEOUT 90)
add_executable(LuxBasicModelLibraryTests BasicModelLibraryTest.cpp)
target_link_libraries(LuxBasicModelLibraryTests PRIVATE LuxEngine::Engine)
lux_set_target_warnings(LuxBasicModelLibraryTests)
lux_copy_runtime(LuxBasicModelLibraryTests)
add_test(NAME Engine_basic_model_library COMMAND LuxBasicModelLibraryTests)
set_tests_properties(Engine_basic_model_library PROPERTIES LABELS "headless;assets;fbx" TIMEOUT 60)
add_executable(LuxFbxRenderTests FbxRenderTest.cpp)
target_link_libraries(LuxFbxRenderTests PRIVATE LuxEngine::Engine)
target_include_directories(LuxFbxRenderTests SYSTEM PRIVATE ${stb_SOURCE_DIR})
lux_set_target_warnings(LuxFbxRenderTests)
lux_copy_runtime(LuxFbxRenderTests)
add_test(NAME Engine_fbx_render COMMAND LuxFbxRenderTests)
set_tests_properties(Engine_fbx_render PROPERTIES LABELS "gpu;assets;fbx" TIMEOUT 90
    SKIP_RETURN_CODE 77 ENVIRONMENT "SDL_AUDIO_DRIVER=dummy")
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
    target_include_directories(LuxFuzzSceneEngine PRIVATE "${PROJECT_SOURCE_DIR}/Engine/Scene/Private")
    target_link_libraries(LuxFuzzSceneEngine PRIVATE LuxEngine::Engine)
    target_compile_definitions(LuxFuzzSceneEngine PRIVATE LUX_FUZZING_ENGINE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(LuxFuzzSceneEngine PRIVATE -fsanitize=fuzzer)
        target_link_options(LuxFuzzSceneEngine PRIVATE -fsanitize=fuzzer)
    endif()
    lux_copy_runtime(LuxFuzzSceneEngine)
endif()


add_executable(LuxSceneJsonTests SceneJsonTest.cpp)
target_link_libraries(LuxSceneJsonTests PRIVATE LuxEngine::Engine nlohmann_json::nlohmann_json)
lux_set_target_warnings(LuxSceneJsonTests)
lux_copy_runtime(LuxSceneJsonTests)
add_test(NAME Engine_scene_json COMMAND LuxSceneJsonTests)
set_tests_properties(Engine_scene_json PROPERTIES LABELS "headless;scene" TIMEOUT 60)

# Compile every public header on its own with only its module's declared public dependencies.
foreach(module Core Platform Environment RHI Assets Renderer ECS Physics Animation Audio Scene Scripting Application)
    file(GLOB_RECURSE headers CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/Engine/${module}/Public/*.hpp")
    set(sources)
    foreach(header IN LISTS headers)
        file(RELATIVE_PATH include_path "${PROJECT_SOURCE_DIR}/Engine/${module}/Public" "${header}")
        string(MAKE_C_IDENTIFIER "${include_path}" unit)
        set(source "${CMAKE_CURRENT_BINARY_DIR}/headers/${module}/${unit}.cpp")
        file(GENERATE OUTPUT "${source}" CONTENT "#include <${include_path}>\n")
        list(APPEND sources "${source}")
    endforeach()
    add_library(LuxHeaderCheck${module} OBJECT ${sources})
    target_link_libraries(LuxHeaderCheck${module} PRIVATE LuxModule${module})
endforeach()

add_executable(LuxScriptRuntimeTests ScriptRuntimeTest.cpp)
target_link_libraries(LuxScriptRuntimeTests PRIVATE LuxEngine::Engine)
lux_set_target_warnings(LuxScriptRuntimeTests)
lux_copy_runtime(LuxScriptRuntimeTests)
set(_lux_test_scripts "${CMAKE_BINARY_DIR}/test-scripts")
add_custom_command(OUTPUT "${_lux_test_scripts}/Scripts.dll"
    COMMAND "${LUX_DOTNET_EXECUTABLE}" build "${CMAKE_CURRENT_SOURCE_DIR}/Scripting/Scripts.csproj"
        --configuration Release --output "${_lux_test_scripts}"
        --artifacts-path "${CMAKE_BINARY_DIR}/script-test-artifacts"
        "-p:LuxEngineScriptCore=${LUX_MANAGED_OUTPUT}/LuxEngine.ScriptCore.dll"
        "-p:RestoreConfigFile=${CMAKE_SOURCE_DIR}/Engine/Scripting/Managed/NuGet.Config"
        -p:NuGetAudit=false
    DEPENDS LuxScriptCore Scripting/Scripts.csproj Scripting/Scripts.cs VERBATIM)
add_custom_target(LuxTestScripts DEPENDS "${_lux_test_scripts}/Scripts.dll")
add_dependencies(LuxScriptRuntimeTests LuxTestScripts)
add_test(NAME Engine_csharp COMMAND LuxScriptRuntimeTests "${_lux_test_scripts}/Scripts.dll")
set_tests_properties(Engine_csharp PROPERTIES LABELS "headless;scripting" TIMEOUT 60)

add_executable(LuxForgeBackendTests ForgeBackendTest.cpp)
target_link_libraries(LuxForgeBackendTests PRIVATE LuxEngine::Engine)
target_include_directories(LuxForgeBackendTests PRIVATE "${PROJECT_SOURCE_DIR}/Engine/Renderer/Private")
lux_set_target_warnings(LuxForgeBackendTests)
lux_copy_runtime(LuxForgeBackendTests)
foreach(suite basics misuse ownership frames drawing meshes scene3d materials material_textures)
    add_test(NAME Forge_${suite} COMMAND LuxForgeBackendTests ${suite})
    set_tests_properties(Forge_${suite} PROPERTIES LABELS "gpu;forge" TIMEOUT 180 SKIP_RETURN_CODE 77
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:LuxForgeBackendTests>" ENVIRONMENT "SDL_AUDIO_DRIVER=dummy")
endforeach()

if(WIN32)
    add_executable(LuxForgeInitializationTests ForgeInitializationTest.cpp)
    target_include_directories(LuxForgeInitializationTests PRIVATE "${PROJECT_SOURCE_DIR}/Engine/Renderer/Private")
    target_link_libraries(LuxForgeInitializationTests PRIVATE user32)
    lux_set_target_warnings(LuxForgeInitializationTests)
    lux_copy_runtime(LuxForgeInitializationTests)
    add_test(NAME Forge_initialization_failure COMMAND LuxForgeInitializationTests)
    set_tests_properties(Forge_initialization_failure PROPERTIES LABELS "gpu;forge" TIMEOUT 30
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:LuxForgeInitializationTests>")
endif()
add_executable(LuxTextureSamplingTests TextureSamplingTest.cpp)
target_link_libraries(LuxTextureSamplingTests PRIVATE LuxEngine::Engine)
lux_set_target_warnings(LuxTextureSamplingTests)
lux_copy_runtime(LuxTextureSamplingTests)
add_test(NAME Engine_texture_mips COMMAND LuxTextureSamplingTests)
set_tests_properties(Engine_texture_mips PROPERTIES LABELS "headless;assets;texture" TIMEOUT 30)

add_executable(LuxPrefabLibraryTests PrefabLibraryTest.cpp)
target_link_libraries(LuxPrefabLibraryTests PRIVATE LuxEngine::Engine)
lux_set_target_warnings(LuxPrefabLibraryTests)
lux_copy_runtime(LuxPrefabLibraryTests)
add_test(NAME Engine_prefab_library COMMAND LuxPrefabLibraryTests)
set_tests_properties(Engine_prefab_library PROPERTIES LABELS "headless;scene;prefab" TIMEOUT 90)

add_executable(LuxJoltPhysicsTests JoltPhysicsTest.cpp)
target_link_libraries(LuxJoltPhysicsTests PRIVATE LuxEngine::Engine)
lux_set_target_warnings(LuxJoltPhysicsTests)
lux_copy_runtime(LuxJoltPhysicsTests)
add_test(NAME Engine_jolt_physics COMMAND LuxJoltPhysicsTests)
set_tests_properties(Engine_jolt_physics PROPERTIES LABELS "headless;physics;jolt" TIMEOUT 60)

add_executable(LuxEmbeddedInputTests EmbeddedInputTest.cpp)
target_link_libraries(LuxEmbeddedInputTests PRIVATE LuxEngine::Engine Threads::Threads)
lux_set_target_warnings(LuxEmbeddedInputTests)
lux_copy_runtime(LuxEmbeddedInputTests)
add_test(NAME Engine_embedded_input COMMAND LuxEmbeddedInputTests)
set_tests_properties(Engine_embedded_input PROPERTIES LABELS "headless;input;editor" TIMEOUT 30)

add_executable(LuxFbxModelTests FbxModelTest.cpp)
target_link_libraries(LuxFbxModelTests PRIVATE LuxEngine::Engine)
lux_set_target_warnings(LuxFbxModelTests)
lux_copy_runtime(LuxFbxModelTests)
add_test(NAME Engine_fbx_model COMMAND LuxFbxModelTests "${LUX_BASIC_MODEL_SOURCE_DIR}")
set_tests_properties(Engine_fbx_model PROPERTIES LABELS "headless;assets;fbx" TIMEOUT 90)

add_executable(LuxFbxModelSceneTests FbxModelSceneTest.cpp)
target_link_libraries(LuxFbxModelSceneTests PRIVATE LuxEngine::Engine)
lux_set_target_warnings(LuxFbxModelSceneTests)
lux_copy_runtime(LuxFbxModelSceneTests)
add_test(NAME Engine_fbx_model_scene COMMAND LuxFbxModelSceneTests
    "${CMAKE_CURRENT_SOURCE_DIR}/Fixtures/AnimatedTriangle.fbx")
set_tests_properties(Engine_fbx_model_scene PROPERTIES LABELS "headless;assets;fbx;scene" TIMEOUT 90)
target_compile_definitions(LuxFbxRenderTests PRIVATE
    LUX_FBX_ANIMATED_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/Fixtures/AnimatedTriangle.fbx")

add_executable(LuxMaterialSidedTests MaterialSidedTest.cpp)
target_link_libraries(LuxMaterialSidedTests PRIVATE LuxEngine::Engine)
lux_set_target_warnings(LuxMaterialSidedTests)
lux_copy_runtime(LuxMaterialSidedTests)
add_test(NAME Engine_material_sided COMMAND LuxMaterialSidedTests)
set_tests_properties(Engine_material_sided PROPERTIES LABELS "gpu;materials;fbx" TIMEOUT 90
    SKIP_RETURN_CODE 77 ENVIRONMENT "SDL_AUDIO_DRIVER=dummy;LUX_VULKAN_VALIDATION=sync")
