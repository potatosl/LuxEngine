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
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets)
find_package(libzip CONFIG REQUIRED)

function(lux_editor_copy_runtime target)
    if(COMMAND lux_copy_runtime)
        lux_copy_runtime(${target})
    elseif(COMMAND lux_copy_shaders)
        lux_copy_shaders(${target})
        if(WIN32 AND TARGET SDL3::SDL3-shared)
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:SDL3::SDL3-shared>
                    "$<TARGET_FILE_DIR:${target}>"
                VERBATIM)
        endif()
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

add_library(LuxEditorUi STATIC
    Build/ProjectBuild.cpp
    App/EditorWindow.cpp
    App/EditorWindowLayout.cpp
    App/EditorWindowProjects.cpp
    App/EditorWindowPlay.cpp
    App/EditorWindowBuild.cpp
    Debug/DebugPanel.cpp
    Project/EditorProject.cpp
    Project/ProjectArchive.cpp
    Project/ProjectDialog.cpp
    Project/ProjectSettings.cpp
    Project/ProjectTemplates.cpp
    Scene/EditorDocument.cpp
    Ui/IconFactory.cpp
    Viewport/EngineSceneRenderer.cpp
    Viewport/SceneViewport2D.cpp
    Viewport/SceneViewport3D.cpp
)
target_include_directories(LuxEditorUi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(LuxEditorUi PRIVATE LUX_EDITOR_ENGINE_VERSION="${LuxEngine_VERSION}")
target_link_libraries(LuxEditorUi PUBLIC LuxEngine::Engine Qt6::Widgets libzip::zip)
if(COMMAND lux_set_target_warnings)
    lux_set_target_warnings(LuxEditorUi)
endif()
add_executable(LuxEditor WIN32 MACOSX_BUNDLE App/Main.cpp)
target_link_libraries(LuxEditor PRIVATE LuxEditorUi)
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
if(WIN32 AND TARGET Qt6::QWindowsIntegrationPlugin)
    install(FILES $<TARGET_FILE:Qt6::QWindowsIntegrationPlugin> DESTINATION bin/platforms)
endif()

if(MSVC)
    target_compile_options(LuxEditor PRIVATE /utf-8)
endif()

if(LUX_BUILD_TESTS)
    add_executable(LuxEditorDocumentTests
        Scene/EditorDocument.cpp
        Project/EditorProject.cpp
        Project/ProjectArchive.cpp
        Project/ProjectSettings.cpp
        Project/ProjectTemplates.cpp
        Tests/EditorDocumentTest.cpp
    )
    target_include_directories(LuxEditorDocumentTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(LuxEditorDocumentTests PRIVATE LUX_EDITOR_ENGINE_VERSION="${LuxEngine_VERSION}")
    target_link_libraries(LuxEditorDocumentTests PRIVATE LuxEngine::Engine Qt6::Gui libzip::zip)
    if(COMMAND lux_set_target_warnings)
        lux_set_target_warnings(LuxEditorDocumentTests)
    endif()
    lux_editor_copy_runtime(LuxEditorDocumentTests)
    add_test(NAME Editor_document COMMAND LuxEditorDocumentTests)
    add_executable(LuxEditorBuildTests
        Build/ProjectBuild.cpp
        Tests/EditorBuildTest.cpp
    )
    target_include_directories(LuxEditorBuildTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
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
    lux_editor_copy_runtime(LuxEditorUiTests)
    lux_editor_copy_qt_plugins(LuxEditorUiTests)
    add_test(NAME Editor_ui COMMAND LuxEditorUiTests)
    set_tests_properties(Editor_ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)
    set_tests_properties(Editor_document Editor_build PROPERTIES TIMEOUT 120)
    # Builds a template game against the installed SDK (LUX_ENGINE_DIR), runs it and zips it.
    add_executable(LuxEditorGameBuildTests Tests/EditorGameBuildTest.cpp)
    target_link_libraries(LuxEditorGameBuildTests PRIVATE LuxEditorUi)
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
