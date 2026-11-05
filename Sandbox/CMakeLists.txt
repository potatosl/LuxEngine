add_executable(LuxEngineDemo Main.cpp)
target_link_libraries(LuxEngineDemo PRIVATE LuxEngine::Runtime)
lux_set_target_warnings(LuxEngineDemo)
set_target_properties(LuxEngineDemo PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:LuxEngineDemo>")
set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT LuxEngineDemo)
lux_copy_runtime(LuxEngineDemo)
if(WIN32 AND LUX_BUILD_SHARED)
    add_custom_command(TARGET LuxEngineDemo POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:LuxEngine>
            $<TARGET_FILE_DIR:LuxEngineDemo>
        COMMENT "Copying LuxEngine.dll to demo output directory")
endif()
