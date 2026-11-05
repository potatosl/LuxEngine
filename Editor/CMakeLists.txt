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

add_library(LuxEditorUi STATIC
    App/EditorWindow.cpp
    App/EditorWindowLayout.cpp
    App/EditorWindowProjects.cpp
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
install(TARGETS LuxEditor RUNTIME DESTINATION bin BUNDLE DESTINATION .)

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
    add_executable(LuxEditorUiTests Tests/EditorUiTest.cpp)
    target_link_libraries(LuxEditorUiTests PRIVATE LuxEditorUi)
    lux_editor_copy_runtime(LuxEditorUiTests)
    add_test(NAME Editor_ui COMMAND LuxEditorUiTests)
    set_tests_properties(Editor_ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
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
