target_sources(LuxEditorUi PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/LuxEditor/Viewport/EngineSceneRenderer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/LuxEditor/Viewport/GameViewport.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/LuxEditor/Viewport/ModelViewportGeometry.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/LuxEditor/Viewport/ModelingViewport.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/LuxEditor/Viewport/CameraGizmos.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/LuxEditor/Viewport/SceneViewport2D.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/LuxEditor/Viewport/SceneViewport3D.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/LuxEditor/Viewport/ViewportCamera3D.cpp"
)
