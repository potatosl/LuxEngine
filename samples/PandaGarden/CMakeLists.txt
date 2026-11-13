add_executable(SamplePandaGarden Main.cpp)
target_link_libraries(SamplePandaGarden PRIVATE LuxEngine::Engine)
lux_set_target_warnings(SamplePandaGarden)
lux_copy_runtime(SamplePandaGarden)
add_custom_command(TARGET SamplePandaGarden POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:SamplePandaGarden>/panda-assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/Assets/panda.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/Assets/bamboo.png"
        "$<TARGET_FILE_DIR:SamplePandaGarden>/panda-assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/Assets/BackgroundTinyTown"
        "$<TARGET_FILE_DIR:SamplePandaGarden>/panda-assets/BackgroundTinyTown"
    VERBATIM)
set_target_properties(SamplePandaGarden PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:SamplePandaGarden>")
