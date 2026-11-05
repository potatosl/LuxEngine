add_executable(AsterSandbox src/Main.cpp)
target_link_libraries(AsterSandbox PRIVATE Aster::Engine)
set_target_properties(AsterSandbox PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:AsterSandbox>")
