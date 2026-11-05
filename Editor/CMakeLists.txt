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

find_package(wxWidgets REQUIRED COMPONENTS core base)
include(${wxWidgets_USE_FILE})

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
        if(WIN32 AND LuxEngine_USE_OGRE)
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${LUX_OGRE_MAIN_DLL}" "${LUX_OGRE_GL_PLUGIN_PATH}"
                    "$<TARGET_FILE_DIR:${target}>"
                VERBATIM)
        endif()
    endif()
endfunction()

add_executable(LuxEditor WIN32 MACOSX_BUNDLE
    App/Main.cpp
    App/EditorFrame.cpp
    App/EditorFrameProjects.cpp
    Project/EditorProject.cpp
    Project/ProjectArchive.cpp
    Project/ProjectDialog.cpp
    Project/ProjectSettings.cpp
    Project/ProjectTemplates.cpp
    Scene/EditorDocument.cpp
    Viewport/SceneCanvas.cpp
)
target_include_directories(LuxEditor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(LuxEditor PRIVATE LUX_EDITOR_ENGINE_VERSION="${LuxEngine_VERSION}")
target_link_libraries(LuxEditor PRIVATE LuxEngine::Engine ${wxWidgets_LIBRARIES})
if(COMMAND lux_set_target_warnings)
    lux_set_target_warnings(LuxEditor)
endif()
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
    target_link_libraries(LuxEditorDocumentTests PRIVATE LuxEngine::Engine ${wxWidgets_LIBRARIES})
    if(COMMAND lux_set_target_warnings)
        lux_set_target_warnings(LuxEditorDocumentTests)
    endif()
    lux_editor_copy_runtime(LuxEditorDocumentTests)
    add_test(NAME Editor_document COMMAND LuxEditorDocumentTests)
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
