find_package(wxWidgets REQUIRED COMPONENTS core base)
include(${wxWidgets_USE_FILE})

add_executable(LuxEditor WIN32 MACOSX_BUNDLE
    Main.cpp
    EditorDocument.cpp
    EditorFrame.cpp
    SceneCanvas.cpp
)
target_link_libraries(LuxEditor PRIVATE LuxEngine::Engine ${wxWidgets_LIBRARIES})
lux_set_target_warnings(LuxEditor)
lux_copy_runtime(LuxEditor)
install(TARGETS LuxEditor RUNTIME DESTINATION bin BUNDLE DESTINATION .)

if(MSVC)
    target_compile_options(LuxEditor PRIVATE /utf-8)
endif()

if(LUX_BUILD_TESTS)
    add_executable(LuxEditorDocumentTests EditorDocument.cpp EditorDocumentTest.cpp)
    target_link_libraries(LuxEditorDocumentTests PRIVATE LuxEngine::Engine)
    lux_set_target_warnings(LuxEditorDocumentTests)
    lux_copy_runtime(LuxEditorDocumentTests)
    add_test(NAME Editor_document COMMAND LuxEditorDocumentTests)
endif()

if(WIN32 AND LUX_BUILD_SHARED)
    add_custom_command(TARGET LuxEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:LuxEngine>
            $<TARGET_FILE_DIR:LuxEditor>
        COMMENT "Copying LuxEngine.dll to editor output directory"
    )
    if(LUX_BUILD_TESTS)
        add_custom_command(TARGET LuxEditorDocumentTests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:LuxEngine>
                $<TARGET_FILE_DIR:LuxEditorDocumentTests>
        )
    endif()
endif()
