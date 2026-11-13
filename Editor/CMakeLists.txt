if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.24)
    project(LuxEditor LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    option(LUX_BUILD_TESTS "Build editor document tests" OFF)
    if(LUX_BUILD_TESTS)
        enable_testing()
    endif()
    find_package(LuxEngine CONFIG REQUIRED)
    # Same layout as the engine build: every executable and the files it needs in bin[/<Config>].
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    foreach(_config ${CMAKE_CONFIGURATION_TYPES})
        string(TOUPPER ${_config} _config_upper)
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_config_upper} "${CMAKE_BINARY_DIR}/bin/${_config}")
    endforeach()
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets)
find_package(libzip CONFIG REQUIRED)
find_package(nlohmann_json 3.11 CONFIG REQUIRED)

if(NOT COMMAND lux_copy_runtime)
    # Standalone build against the installed SDK: copy the engine's shaders, SDL3.dll and the
    # SDK's LuxPlayer (used by "run in window") once into the shared bin folder.
    if(CMAKE_CONFIGURATION_TYPES)
        set(_lux_editor_runtime_dir "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
    else()
        set(_lux_editor_runtime_dir "${CMAKE_BINARY_DIR}/bin")
    endif()
    set(_lux_editor_runtime_copy
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${LuxEngine_SHADER_DIR}" "${_lux_editor_runtime_dir}/shaders")
    if(WIN32 AND TARGET SDL3::SDL3-shared)
        list(APPEND _lux_editor_runtime_copy
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL3::SDL3-shared> "${_lux_editor_runtime_dir}")
    endif()
    find_program(LUX_PLAYER_EXECUTABLE LuxPlayer
        HINTS "${LuxEngine_DIR}/../../../bin" NO_DEFAULT_PATH)
    if(LUX_PLAYER_EXECUTABLE)
        list(APPEND _lux_editor_runtime_copy
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LUX_PLAYER_EXECUTABLE}" "${_lux_editor_runtime_dir}")
    else()
        message(WARNING "LuxPlayer was not found in the engine SDK; \"run in window\" will be unavailable.")
    endif()
    add_custom_target(LuxEditorRuntimeFiles ALL ${_lux_editor_runtime_copy} VERBATIM)
endif()

function(lux_editor_copy_runtime target)
    if(COMMAND lux_copy_runtime)
        lux_copy_runtime(${target})
    else()
        add_dependencies(${target} LuxEditorRuntimeFiles)
    endif()
endfunction()

# Qt needs its platform plugin (platforms/qwindows.dll) next to the executable; vcpkg does not copy it.
# One target copies it into the shared bin folder so parallel builds do not write it at once.
if(WIN32 AND TARGET Qt6::QWindowsIntegrationPlugin)
    if(CMAKE_CONFIGURATION_TYPES)
        set(_lux_editor_bin "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
    else()
        set(_lux_editor_bin "${CMAKE_BINARY_DIR}/bin")
    endif()
    set(_lux_qt_plugins $<TARGET_FILE:Qt6::QWindowsIntegrationPlugin>)
    if(TARGET Qt6::QOffscreenIntegrationPlugin)
        list(APPEND _lux_qt_plugins $<TARGET_FILE:Qt6::QOffscreenIntegrationPlugin>)
    endif()
    add_custom_target(LuxEditorQtPlugins ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_lux_editor_bin}/platforms"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_lux_qt_plugins} "${_lux_editor_bin}/platforms"
        VERBATIM)
endif()
function(lux_editor_copy_qt_plugins target)
    if(TARGET LuxEditorQtPlugins)
        add_dependencies(${target} LuxEditorQtPlugins)
    endif()
endfunction()

set(LUX_EDITOR_PRIVATE_INCLUDE_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/Animation/Private"
    "${CMAKE_CURRENT_SOURCE_DIR}/App/Private"
    "${CMAKE_CURRENT_SOURCE_DIR}/Build/Private"
    "${CMAKE_CURRENT_SOURCE_DIR}/Debug/Private"
    "${CMAKE_CURRENT_SOURCE_DIR}/Project/Private"
    "${CMAKE_CURRENT_SOURCE_DIR}/Scene/Private"
    "${CMAKE_CURRENT_SOURCE_DIR}/Ui/Private"
    "${CMAKE_CURRENT_SOURCE_DIR}/Viewport/Private"
)

add_library(LuxEditorUi STATIC)
add_subdirectory(Build)
add_subdirectory(App)
add_subdirectory(Animation)
add_subdirectory(Debug)
add_subdirectory(Project)
add_subdirectory(Scene)
add_subdirectory(Ui)
add_subdirectory(Viewport)

target_include_directories(LuxEditorUi PRIVATE ${LUX_EDITOR_PRIVATE_INCLUDE_DIRS})
target_compile_definitions(LuxEditorUi PRIVATE LUX_EDITOR_ENGINE_VERSION="${LuxEngine_VERSION}")
target_link_libraries(LuxEditorUi PUBLIC LuxEngine::Engine Qt6::Widgets libzip::zip
    PRIVATE nlohmann_json::nlohmann_json)
if(COMMAND lux_set_target_warnings)
    lux_set_target_warnings(LuxEditorUi)
endif()
add_executable(LuxEditor WIN32 MACOSX_BUNDLE App/Private/LuxEditor/App/Main.cpp)
target_link_libraries(LuxEditor PRIVATE LuxEditorUi)
target_include_directories(LuxEditor PRIVATE ${LUX_EDITOR_PRIVATE_INCLUDE_DIRS})
target_compile_definitions(LuxEditor PRIVATE LUX_EDITOR_ENGINE_VERSION="${LuxEngine_VERSION}")
lux_editor_copy_runtime(LuxEditor)
lux_editor_copy_qt_plugins(LuxEditor)
# With the editor enabled, F5 in Visual Studio should open the editor rather than the demo.
set_target_properties(LuxEditor PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:LuxEditor>")
set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT LuxEditor)
if(TARGET LuxPlayer)
    # The editor launches LuxPlayer from its own folder for "run in window".
    add_dependencies(LuxEditor LuxPlayer)
endif()
install(TARGETS LuxEditor RUNTIME DESTINATION bin BUNDLE DESTINATION .)
if(WIN32)
    if(TARGET Qt6::QWindowsIntegrationPlugin)
        install(FILES $<TARGET_FILE:Qt6::QWindowsIntegrationPlugin> DESTINATION bin/platforms)
    endif()
    if(TARGET Qt6::Core AND TARGET Qt6::Gui AND TARGET Qt6::Widgets)
        install(FILES $<TARGET_FILE:Qt6::Core> $<TARGET_FILE:Qt6::Gui> $<TARGET_FILE:Qt6::Widgets> DESTINATION bin)
    endif()
    if(TARGET libzip::zip)
        install(FILES $<TARGET_FILE:libzip::zip> DESTINATION bin)
    endif()
    if(TARGET SDL3::SDL3-shared)
        install(FILES $<TARGET_FILE:SDL3::SDL3-shared> DESTINATION bin)
    endif()
    install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/../forge/shaders/bin/" DESTINATION bin)
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../docs/licenses/" DESTINATION bin/licenses)
endif()

if(MSVC)
    target_compile_options(LuxEditor PRIVATE /utf-8)
endif()

if(LUX_BUILD_TESTS)
    add_executable(LuxBasicModelUiTests Tests/BasicModelUiTest.cpp)
    target_link_libraries(LuxBasicModelUiTests PRIVATE LuxEditorUi)
    target_include_directories(LuxBasicModelUiTests PRIVATE ${LUX_EDITOR_PRIVATE_INCLUDE_DIRS})
    lux_editor_copy_runtime(LuxBasicModelUiTests)
    lux_editor_copy_qt_plugins(LuxBasicModelUiTests)
    add_test(NAME Editor_basic_model_ui COMMAND LuxBasicModelUiTests)
    set_tests_properties(Editor_basic_model_ui PROPERTIES LABELS "headless;editor;assets;fbx"
        TIMEOUT 90 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LUX_EDITOR_SOFTWARE_VIEWPORT=1")
    foreach(pair
        "LuxSelectionCoreTests|SelectionCoreTest.cpp|Editor_selection_core"
        "LuxInteraction2DTests|Interaction2DTest.cpp|Editor_interaction_2d"
        "LuxInteraction3DTests|Interaction3DTest.cpp|Editor_interaction_3d"
        "LuxEditorInteractionTests|EditorInteractionTest.cpp|Editor_interaction_integration"
    )
        string(REPLACE "|" ";" fields "${pair}")
        list(GET fields 0 target)
        list(GET fields 1 source)
        list(GET fields 2 test)
        add_executable(${target} "Tests/${source}")
        target_link_libraries(${target} PRIVATE LuxEditorUi)
        target_include_directories(${target} PRIVATE ${LUX_EDITOR_PRIVATE_INCLUDE_DIRS})
        lux_editor_copy_runtime(${target})
        add_test(NAME ${test} COMMAND ${target})
        set_tests_properties(${test} PROPERTIES LABELS "headless;editor;interaction" TIMEOUT 90
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LUX_EDITOR_SOFTWARE_VIEWPORT=1")
    endforeach()
    add_executable(LuxViewportFoundationTests Tests/ViewportFoundationTest.cpp)
    target_link_libraries(LuxViewportFoundationTests PRIVATE LuxEditorUi)
    target_include_directories(LuxViewportFoundationTests PRIVATE ${LUX_EDITOR_PRIVATE_INCLUDE_DIRS})
    lux_editor_copy_runtime(LuxViewportFoundationTests)
    lux_editor_copy_qt_plugins(LuxViewportFoundationTests)
    add_test(NAME Editor_viewport_foundation COMMAND LuxViewportFoundationTests)
    set_tests_properties(Editor_viewport_foundation PROPERTIES LABELS "headless;editor;viewport"
        TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LUX_EDITOR_SOFTWARE_VIEWPORT=1")
    add_executable(LuxEditorBuildOutputTests Tests/BuildOutputTest.cpp)
    target_link_libraries(LuxEditorBuildOutputTests PRIVATE LuxEditorUi)
    target_include_directories(LuxEditorBuildOutputTests PRIVATE ${LUX_EDITOR_PRIVATE_INCLUDE_DIRS})
    add_test(NAME Editor_build_output COMMAND LuxEditorBuildOutputTests)
    set_tests_properties(Editor_build_output PROPERTIES LABELS "headless;editor" TIMEOUT 30)
    add_executable(LuxProjectFormatTests Tests/ProjectFormatTest.cpp)
    target_include_directories(LuxProjectFormatTests PRIVATE ${LUX_EDITOR_PRIVATE_INCLUDE_DIRS})
    target_link_libraries(LuxProjectFormatTests PRIVATE LuxEditorUi LuxEngine::Runtime nlohmann_json::nlohmann_json)
    if(COMMAND lux_set_target_warnings)
        lux_set_target_warnings(LuxProjectFormatTests)
    endif()
    lux_editor_copy_runtime(LuxProjectFormatTests)
    add_test(NAME Editor_project_json COMMAND LuxProjectFormatTests)
    set_tests_properties(Editor_project_json PROPERTIES LABELS "headless;editor;project" TIMEOUT 60)
    add_executable(LuxEditorSceneDependencyTests Tests/EditorSceneDependencyTest.cpp)
    target_link_libraries(LuxEditorSceneDependencyTests PRIVATE LuxEditorUi)
    target_include_directories(LuxEditorSceneDependencyTests PRIVATE ${LUX_EDITOR_PRIVATE_INCLUDE_DIRS})
    if(COMMAND lux_set_target_warnings)
        lux_set_target_warnings(LuxEditorSceneDependencyTests)
    endif()
    lux_editor_copy_runtime(LuxEditorSceneDependencyTests)
    lux_editor_copy_qt_plugins(LuxEditorSceneDependencyTests)
    add_test(NAME Editor_scene_dependencies COMMAND LuxEditorSceneDependencyTests)
    set_tests_properties(Editor_scene_dependencies PROPERTIES LABELS "headless;editor;scene" TIMEOUT 90)
    add_executable(LuxEditorSceneUiTests Tests/EditorSceneUiTest.cpp)
    target_link_libraries(LuxEditorSceneUiTests PRIVATE LuxEditorUi)
    target_include_directories(LuxEditorSceneUiTests PRIVATE ${LUX_EDITOR_PRIVATE_INCLUDE_DIRS})
    if(COMMAND lux_set_target_warnings)
        lux_set_target_warnings(LuxEditorSceneUiTests)
    endif()
    lux_editor_copy_runtime(LuxEditorSceneUiTests)
    lux_editor_copy_qt_plugins(LuxEditorSceneUiTests)
    add_test(NAME Editor_scene_ui COMMAND LuxEditorSceneUiTests)
    set_tests_properties(Editor_scene_ui PROPERTIES LABELS "headless;editor;scene" TIMEOUT 120
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LUX_EDITOR_SOFTWARE_VIEWPORT=1")
    add_executable(LuxEditorSceneDocumentTests
        Tests/EditorSceneDocumentTest.cpp
        Scene/Private/LuxEditor/Scene/EditorDocument.cpp
        Scene/Private/LuxEditor/Scene/SceneAuthoringIO.cpp
        Project/Private/LuxEditor/Project/ProjectJson.cpp
        Project/Private/LuxEditor/Project/EditorProject.cpp
        Project/Private/LuxEditor/Project/ProjectSettings.cpp
        Project/Private/LuxEditor/Project/ProjectTemplates.cpp)
    target_sources(LuxEditorSceneDocumentTests PRIVATE
        Project/Private/LuxEditor/Project/PathEncoding.cpp
        Project/Private/LuxEditor/Project/ErrorEncoding.cpp
        Project/Private/LuxEditor/Project/ProjectFileOperations.cpp)
    target_include_directories(LuxEditorSceneDocumentTests PRIVATE ${LUX_EDITOR_PRIVATE_INCLUDE_DIRS})
    target_compile_definitions(LuxEditorSceneDocumentTests PRIVATE LUX_EDITOR_ENGINE_VERSION="${LuxEngine_VERSION}")
    target_link_libraries(LuxEditorSceneDocumentTests PRIVATE LuxEngine::Engine Qt6::Gui nlohmann_json::nlohmann_json)
    if(COMMAND lux_set_target_warnings)
        lux_set_target_warnings(LuxEditorSceneDocumentTests)
    endif()
    lux_editor_copy_runtime(LuxEditorSceneDocumentTests)
    add_test(NAME Editor_scene_documents COMMAND LuxEditorSceneDocumentTests)
    set_tests_properties(Editor_scene_documents PROPERTIES LABELS "headless;editor;scene" TIMEOUT 90)
    add_executable(LuxEditorDocumentTests
        Scene/Private/LuxEditor/Scene/EditorDocument.cpp
        Scene/Private/LuxEditor/Scene/SceneAuthoringIO.cpp
        Project/Private/LuxEditor/Project/ProjectJson.cpp
        Project/Private/LuxEditor/Project/EditorProject.cpp
        Project/Private/LuxEditor/Project/ProjectArchive.cpp
        Project/Private/LuxEditor/Project/ProjectSettings.cpp
        Project/Private/LuxEditor/Project/ProjectTemplates.cpp
        Tests/EditorDocumentTest.cpp
    )
    target_sources(LuxEditorDocumentTests PRIVATE
        Project/Private/LuxEditor/Project/PathEncoding.cpp
        Project/Private/LuxEditor/Project/ErrorEncoding.cpp
        Project/Private/LuxEditor/Project/ProjectFileOperations.cpp)
    target_include_directories(LuxEditorDocumentTests PRIVATE ${LUX_EDITOR_PRIVATE_INCLUDE_DIRS})
    target_compile_definitions(LuxEditorDocumentTests PRIVATE LUX_EDITOR_ENGINE_VERSION="${LuxEngine_VERSION}")
    target_link_libraries(LuxEditorDocumentTests PRIVATE LuxEngine::Engine Qt6::Gui libzip::zip nlohmann_json::nlohmann_json)
    if(COMMAND lux_set_target_warnings)
        lux_set_target_warnings(LuxEditorDocumentTests)
    endif()
    lux_editor_copy_runtime(LuxEditorDocumentTests)
    add_test(NAME Editor_document COMMAND LuxEditorDocumentTests)
    add_test(NAME Editor_archive_limits COMMAND LuxEditorDocumentTests archive-limits)
    set_tests_properties(Editor_archive_limits PROPERTIES TIMEOUT 180 LABELS "headless;editor")
    add_executable(LuxEditorBuildTests
        Build/Private/LuxEditor/Build/ProjectBuild.cpp
        Project/Private/LuxEditor/Project/PathEncoding.cpp
        Project/Private/LuxEditor/Project/ErrorEncoding.cpp
        Tests/EditorBuildTest.cpp
    )
    target_include_directories(LuxEditorBuildTests PRIVATE ${LUX_EDITOR_PRIVATE_INCLUDE_DIRS})
    target_link_libraries(LuxEditorBuildTests PRIVATE libzip::zip)
    if(MSVC)
        target_compile_options(LuxEditorBuildTests PRIVATE /utf-8)
    endif()
    if(COMMAND lux_set_target_warnings)
        lux_set_target_warnings(LuxEditorBuildTests)
    endif()
    add_test(NAME Editor_build COMMAND LuxEditorBuildTests)
    add_executable(LuxEditorUiTests Tests/EditorUiTest.cpp)
    target_link_libraries(LuxEditorUiTests PRIVATE LuxEditorUi)
    target_include_directories(LuxEditorUiTests PRIVATE ${LUX_EDITOR_PRIVATE_INCLUDE_DIRS})
    if(MSVC)
        target_compile_options(LuxEditorUiTests PRIVATE /utf-8)
    endif()
    if(COMMAND lux_set_target_warnings)
        lux_set_target_warnings(LuxEditorUiTests)
    endif()
    lux_editor_copy_runtime(LuxEditorUiTests)
    lux_editor_copy_qt_plugins(LuxEditorUiTests)
    add_test(NAME Editor_ui COMMAND LuxEditorUiTests ui)
    add_executable(LuxEditorStateTests Tests/EditorStateTest.cpp)
    target_link_libraries(LuxEditorStateTests PRIVATE LuxEditorUi)
    target_include_directories(LuxEditorStateTests PRIVATE ${LUX_EDITOR_PRIVATE_INCLUDE_DIRS})
    if(MSVC)
        target_compile_options(LuxEditorStateTests PRIVATE /utf-8)
    endif()
    if(COMMAND lux_set_target_warnings)
        lux_set_target_warnings(LuxEditorStateTests)
    endif()
    lux_editor_copy_runtime(LuxEditorStateTests)
    lux_editor_copy_qt_plugins(LuxEditorStateTests)
    add_test(NAME Editor_scene_context COMMAND LuxEditorStateTests scene-context)
    add_test(NAME Editor_recovery_history COMMAND LuxEditorStateTests recovery-history)
    add_test(NAME Editor_broken_scene_recovery COMMAND LuxEditorStateTests broken-scene-recovery)
    add_test(NAME Editor_session_scene COMMAND LuxEditorStateTests session-scene)
    add_test(NAME Editor_project_reopen COMMAND LuxEditorStateTests project-reopen)
    add_test(NAME Editor_project_restore COMMAND LuxEditorStateTests project-restore)
    set_tests_properties(Editor_scene_context Editor_recovery_history Editor_broken_scene_recovery Editor_session_scene Editor_project_reopen Editor_project_restore PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LUX_EDITOR_SOFTWARE_VIEWPORT=1"
        TIMEOUT 60 LABELS "headless;editor")
    add_executable(LuxEditorRegressionTests Tests/EditorRegressionTest.cpp)
    target_link_libraries(LuxEditorRegressionTests PRIVATE LuxEditorUi)
    target_include_directories(LuxEditorRegressionTests PRIVATE ${LUX_EDITOR_PRIVATE_INCLUDE_DIRS})
    if(MSVC)
        target_compile_options(LuxEditorRegressionTests PRIVATE /utf-8)
    endif()
    if(COMMAND lux_set_target_warnings)
        lux_set_target_warnings(LuxEditorRegressionTests)
    endif()
    lux_editor_copy_runtime(LuxEditorRegressionTests)
    lux_editor_copy_qt_plugins(LuxEditorRegressionTests)
    add_test(NAME Editor_timeline COMMAND LuxEditorRegressionTests timeline)
    add_test(NAME Editor_sparse_copy COMMAND LuxEditorRegressionTests sparse-copy)
    add_test(NAME Editor_names COMMAND LuxEditorRegressionTests names)
    add_test(NAME Editor_sparse_tilemap COMMAND LuxEditorRegressionTests sparse-tilemap)
    add_test(NAME Editor_picking2d COMMAND LuxEditorRegressionTests picking2d)
    add_test(NAME Editor_picking2d_gpu COMMAND LuxEditorRegressionTests picking2d-gpu)
    add_test(NAME Editor_picking3d COMMAND LuxEditorRegressionTests picking3d)
    add_test(NAME Editor_picking3d_gpu COMMAND LuxEditorRegressionTests picking3d-gpu)
    add_test(NAME Editor_picking3d_hidpi COMMAND LuxEditorRegressionTests picking3d-gpu)
    set_tests_properties(Editor_picking2d_gpu Editor_picking3d_gpu Editor_picking3d_hidpi PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120
        LABELS "gpu;editor" ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LUX_VULKAN_VALIDATION=sync")
    set_property(TEST Editor_picking3d_hidpi APPEND PROPERTY ENVIRONMENT "QT_SCALE_FACTOR=2")
    set_tests_properties(Editor_timeline Editor_sparse_copy Editor_names Editor_sparse_tilemap Editor_picking2d Editor_picking3d PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 60 LABELS "headless;editor")
    set_tests_properties(Editor_ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 300 LABELS "headless;editor")
    add_test(NAME Editor_ui_1000 COMMAND LuxEditorUiTests ui --iterations=1000)
    set_tests_properties(Editor_ui_1000 PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 600 LABELS "long;headless;editor")
    add_test(NAME Editor_gpu_lifecycle COMMAND LuxEditorUiTests gpu)
    set_tests_properties(Editor_gpu_lifecycle PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 900 LABELS "gpu;editor")
    add_test(NAME Editor_gpu_lifecycle_1000 COMMAND LuxEditorUiTests gpu --iterations=1000)
    set_tests_properties(Editor_gpu_lifecycle_1000 PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 3600 LABELS "long;gpu;editor")
    set_tests_properties(Editor_document Editor_build PROPERTIES TIMEOUT 120 LABELS "headless;editor")
    # Builds a template game against the installed SDK (LUX_ENGINE_DIR), runs it and zips it.
    add_executable(LuxEditorInspectorTests Tests/EditorInspectorTest.cpp)
    target_link_libraries(LuxEditorInspectorTests PRIVATE LuxEditorUi)
    target_include_directories(LuxEditorInspectorTests PRIVATE ${LUX_EDITOR_PRIVATE_INCLUDE_DIRS})
    if(MSVC)
        target_compile_options(LuxEditorInspectorTests PRIVATE /utf-8)
    endif()
    if(COMMAND lux_set_target_warnings)
        lux_set_target_warnings(LuxEditorInspectorTests)
    endif()
    lux_editor_copy_runtime(LuxEditorInspectorTests)
    lux_editor_copy_qt_plugins(LuxEditorInspectorTests)
    add_test(NAME Editor_inspector_fields COMMAND LuxEditorInspectorTests)
    set_tests_properties(Editor_inspector_fields PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LUX_EDITOR_SOFTWARE_VIEWPORT=1"
        TIMEOUT 60 LABELS "headless;editor")
    add_executable(LuxEditorGameBuildTests Tests/EditorGameBuildTest.cpp)
    target_link_libraries(LuxEditorGameBuildTests PRIVATE LuxEditorUi libzip::zip nlohmann_json::nlohmann_json)
    target_include_directories(LuxEditorGameBuildTests PRIVATE ${LUX_EDITOR_PRIVATE_INCLUDE_DIRS})
    if(MSVC)
        target_compile_options(LuxEditorGameBuildTests PRIVATE /utf-8)
    endif()
    lux_editor_copy_runtime(LuxEditorGameBuildTests)
    add_test(NAME Editor_game_build COMMAND LuxEditorGameBuildTests)
    set_tests_properties(Editor_game_build PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 1800 LABELS slow)
endif()

get_target_property(_lux_engine_type LuxEngine::Engine TYPE)
if(WIN32 AND _lux_engine_type STREQUAL "SHARED_LIBRARY")
    add_custom_command(TARGET LuxEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:LuxEngine::Engine>
            $<TARGET_FILE_DIR:LuxEditor>
        COMMENT "Copying LuxEngine.dll to editor output directory"
    )
    if(LUX_BUILD_TESTS)
        add_custom_command(TARGET LuxEditorDocumentTests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:LuxEngine::Engine>
                $<TARGET_FILE_DIR:LuxEditorDocumentTests>
        )
    endif()
endif()
