target_sources(LuxEditorUi PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/LuxEditor/Project/ErrorEncoding.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/LuxEditor/Project/ProjectFileOperations.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/LuxEditor/Project/PathEncoding.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/LuxEditor/Project/ProjectJson.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/LuxEditor/Project/EditorProject.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/LuxEditor/Project/ProjectArchive.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/LuxEditor/Project/ProjectDialog.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/LuxEditor/Project/ProjectSettings.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/LuxEditor/Project/ProjectTemplates.cpp"
)
