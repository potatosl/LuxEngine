# Sandbox Application - LuxEngineDemo

add_executable(LuxEngineDemo
    Main.cpp
)

target_link_libraries(LuxEngineDemo PRIVATE
    LuxEngine::Engine
    LuxEngine::Runtime
    SDL3::SDL3
)

lux_set_target_warnings(LuxEngineDemo)

# Set debugger working directory for Visual Studio
set_target_properties(LuxEngineDemo PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:LuxEngineDemo>"
)

# Set LuxEngineDemo as default startup project in Visual Studio
set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT LuxEngineDemo)

# Copy SDL3 runtime DLL next to executable on Windows if SDL3 is shared
if(WIN32 AND TARGET SDL3::SDL3-shared)
    add_custom_command(TARGET LuxEngineDemo POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3::SDL3-shared>
            $<TARGET_FILE_DIR:LuxEngineDemo>
        COMMENT "Copying SDL3.dll to demo output directory"
    )
elseif(WIN32 AND TARGET SDL3::SDL3)
    add_custom_command(TARGET LuxEngineDemo POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3::SDL3>
            $<TARGET_FILE_DIR:LuxEngineDemo>
        COMMENT "Copying SDL3 binary to demo output directory"
    )
endif()
