target_sources(LuxEditorUi PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/LuxEditor/App/EditorWindowPhysics3D.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/LuxEditor/App/EditorWindowDevelopment.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/LuxEditor/App/EditorWindowModels.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/LuxEditor/App/EditorWindowPrefabs.cpp"
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
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/LuxEditor/App/EditorWindowScripts.cpp"
)
if(LUX_BUILD_TESTS)
    add_executable(LuxWorkspaceLayoutTests "${CMAKE_CURRENT_SOURCE_DIR}/../Tests/WorkspaceLayoutTest.cpp")
    target_link_libraries(LuxWorkspaceLayoutTests PRIVATE LuxEditorUi)
    target_include_directories(LuxWorkspaceLayoutTests PRIVATE ${LUX_EDITOR_PRIVATE_INCLUDE_DIRS})
    add_test(NAME Editor_workspace_layout COMMAND LuxWorkspaceLayoutTests)
    set_tests_properties(Editor_workspace_layout PROPERTIES LABELS "headless;editor" TIMEOUT 90
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LUX_EDITOR_SOFTWARE_VIEWPORT=1")
endif()

if(LUX_BUILD_TESTS)
    add_executable(LuxScriptFieldsUiTests "${CMAKE_CURRENT_SOURCE_DIR}/../Tests/ScriptFieldsUiTest.cpp")
    target_link_libraries(LuxScriptFieldsUiTests PRIVATE LuxEditorUi)
    target_include_directories(LuxScriptFieldsUiTests PRIVATE ${LUX_EDITOR_PRIVATE_INCLUDE_DIRS})
    add_test(NAME Editor_script_fields COMMAND LuxScriptFieldsUiTests)
    set_tests_properties(Editor_script_fields PROPERTIES LABELS "headless;editor;scripting" TIMEOUT 90
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LUX_EDITOR_SOFTWARE_VIEWPORT=1")
endif()

if(LUX_BUILD_TESTS)
    add_executable(LuxPrefabUiTests "${CMAKE_CURRENT_SOURCE_DIR}/../Tests/PrefabUiTest.cpp")
    target_link_libraries(LuxPrefabUiTests PRIVATE LuxEditorUi)
    target_include_directories(LuxPrefabUiTests PRIVATE ${LUX_EDITOR_PRIVATE_INCLUDE_DIRS})
    add_test(NAME Editor_prefab_ui COMMAND LuxPrefabUiTests)
    set_tests_properties(Editor_prefab_ui PROPERTIES LABELS "headless;editor;prefab" TIMEOUT 90
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LUX_EDITOR_SOFTWARE_VIEWPORT=1")
endif()
