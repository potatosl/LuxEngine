target_sources(LuxEditorUi PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/LuxEditor/App/EditorWindowModels.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/LuxEditor/App/EditorWindow.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/LuxEditor/App/EditorWindowInteraction.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/LuxEditor/App/EditorWindowLayout.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/LuxEditor/App/EditorWindowProjects.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/LuxEditor/App/EditorWindowScenes.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/LuxEditor/App/EditorWindowPlay.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/LuxEditor/App/EditorWindowBuild.cpp"
)
target_sources(LuxEditorUi PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/LuxEditor/App/EditorWindowWorkspace.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/LuxEditor/App/EditorWindowTheme.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/LuxEditor/App/EditorWindowCamera.cpp"
)
if(LUX_BUILD_TESTS)
    add_executable(LuxWorkspaceLayoutTests "${CMAKE_CURRENT_SOURCE_DIR}/../Tests/WorkspaceLayoutTest.cpp")
    target_link_libraries(LuxWorkspaceLayoutTests PRIVATE LuxEditorUi)
    target_include_directories(LuxWorkspaceLayoutTests PRIVATE ${LUX_EDITOR_PRIVATE_INCLUDE_DIRS})
    add_test(NAME Editor_workspace_layout COMMAND LuxWorkspaceLayoutTests)
    set_tests_properties(Editor_workspace_layout PROPERTIES LABELS "headless;editor" TIMEOUT 90
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LUX_EDITOR_SOFTWARE_VIEWPORT=1")
endif()
