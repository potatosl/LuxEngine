# Renderer resources are built/staged by TheForge.cmake, not by a native Vulkan path.
add_custom_target(LuxRuntimeFiles ALL)
get_target_property(_lux_jolt_library_type Jolt::Jolt TYPE)
if(_lux_jolt_library_type STREQUAL "SHARED_LIBRARY")
    if(CMAKE_CONFIGURATION_TYPES)
        set(_lux_jolt_runtime_directory "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
    else()
        set(_lux_jolt_runtime_directory "${CMAKE_BINARY_DIR}/bin")
    endif()
    add_custom_target(LuxJoltRuntimeFiles
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_lux_jolt_runtime_directory}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Jolt::Jolt>" "${_lux_jolt_runtime_directory}"
        VERBATIM)
    add_dependencies(LuxRuntimeFiles LuxJoltRuntimeFiles)
endif()
function(lux_copy_runtime target)
    add_dependencies(${target} LuxRuntimeFiles)
endfunction()
