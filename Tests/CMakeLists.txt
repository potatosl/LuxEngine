# LuxEngine Unit Tests

add_executable(LuxEngineTests
    MainTest.cpp
)

target_link_libraries(LuxEngineTests PRIVATE
    LuxEngine::Engine
)

lux_set_target_warnings(LuxEngineTests)

add_test(NAME EngineSanityTest COMMAND LuxEngineTests)
