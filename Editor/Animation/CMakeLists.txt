target_sources(LuxEditorUi PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/LuxEditor/Animation/AnimationTimeline.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/LuxEditor/Animation/AnimationWorkspace.cpp"
)

if(LUX_BUILD_TESTS)
    add_executable(LuxAnimationWorkspaceTests "${CMAKE_CURRENT_SOURCE_DIR}/../Tests/AnimationWorkspaceTest.cpp")
    target_link_libraries(LuxAnimationWorkspaceTests PRIVATE LuxEditorUi)
    target_include_directories(LuxAnimationWorkspaceTests PRIVATE ${LUX_EDITOR_PRIVATE_INCLUDE_DIRS})
    add_test(NAME Editor_animation_workspace COMMAND LuxAnimationWorkspaceTests)
    set_tests_properties(Editor_animation_workspace PROPERTIES
        LABELS "headless;editor;animation" TIMEOUT 90
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LUX_EDITOR_SOFTWARE_VIEWPORT=1")
endif()

if(LUX_BUILD_TESTS)
    add_executable(LuxAnimationTimelineTests
        "${CMAKE_CURRENT_SOURCE_DIR}/../Tests/AnimationTimelineTest.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Private/LuxEditor/Animation/AnimationTimeline.cpp")
    target_link_libraries(LuxAnimationTimelineTests PRIVATE LuxEngine::Engine Qt6::Widgets)
    target_include_directories(LuxAnimationTimelineTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private")
    add_test(NAME Editor_animation_timeline COMMAND LuxAnimationTimelineTests)
    set_tests_properties(Editor_animation_timeline PROPERTIES
        LABELS "headless;editor;animation" TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
