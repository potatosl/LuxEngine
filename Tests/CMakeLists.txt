# LuxEngine Unit Tests

add_executable(LuxEngineTests
    MainTest.cpp
)

target_link_libraries(LuxEngineTests PRIVATE
    LuxEngine::Engine
)

lux_set_target_warnings(LuxEngineTests)

set_target_properties(LuxEngineTests PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:LuxEngineTests>"
)

add_test(NAME EngineSanityTest COMMAND LuxEngineTests)
