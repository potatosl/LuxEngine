add_executable(LuxEngineDemo src/Main.cpp)
target_link_libraries(LuxEngineDemo PRIVATE LuxEngine::Engine)
set_target_properties(LuxEngineDemo PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:LuxEngineDemo>")
