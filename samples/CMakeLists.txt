# Independent executables demonstrating the engine as a framework.
if(CMAKE_CONFIGURATION_TYPES)
    set(_lux_sample_assets_dir "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/assets")
else()
    set(_lux_sample_assets_dir "${CMAKE_BINARY_DIR}/bin/assets")
endif()
add_custom_target(LuxSampleAssets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/Assets" "${_lux_sample_assets_dir}"
    VERBATIM)
function(lux_add_sample target)
    add_executable(${target} ${ARGN})
    target_link_libraries(${target} PRIVATE LuxSampleSupport)
    lux_set_target_warnings(${target})
    lux_copy_runtime(${target})
    add_dependencies(${target} LuxSampleAssets)
    set_target_properties(${target} PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:${target}>")
    if(LUX_BUILD_TESTS)
        add_test(NAME ${target}_smoke COMMAND ${target} --smoke-test)
        set_tests_properties(${target}_smoke PROPERTIES TIMEOUT 60 LABELS "gpu;sample"
            WORKING_DIRECTORY "$<TARGET_FILE_DIR:${target}>"
            ENVIRONMENT "SDL_AUDIO_DRIVER=dummy;LUX_VULKAN_VALIDATION=sync")
        add_test(NAME ${target}_help COMMAND ${target} --help)
        set_tests_properties(${target}_help PROPERTIES TIMEOUT 10 LABELS "headless;sample")
    endif()
endfunction()

add_subdirectory(Common)
add_subdirectory(Basic2D)
add_subdirectory(Physics2D)
add_subdirectory(InputBindings)
# The complete game sample is integrated alongside the focused framework examples.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ForestSurvivor/CMakeLists.txt")
    add_subdirectory(ForestSurvivor)
    add_dependencies(SampleForestSurvivor LuxSampleAssets)
endif()
set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT SampleBasic2D)

if(LUX_BUILD_TESTS)
    add_test(NAME Samples_cc0_assets COMMAND ${CMAKE_COMMAND}
        "-DASSET_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/Assets" -P "${CMAKE_CURRENT_SOURCE_DIR}/Tests/VerifyAssets.cmake")
    set_tests_properties(Samples_cc0_assets PROPERTIES LABELS "headless;sample" TIMEOUT 30)
    add_executable(LuxEntryPointTests Tests/EntryPointTest.cpp)
    target_link_libraries(LuxEntryPointTests PRIVATE LuxEngine::Engine)
    lux_set_target_warnings(LuxEntryPointTests)
    lux_copy_runtime(LuxEntryPointTests)
    add_test(NAME Core_entry_point COMMAND LuxEntryPointTests)
    set_tests_properties(Core_entry_point PROPERTIES LABELS "headless;sample" TIMEOUT 10)
endif()
