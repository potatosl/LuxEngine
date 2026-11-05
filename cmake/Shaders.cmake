if(NOT APPLE)
    file(GLOB LUX_DXC_HINTS "C:/Program Files (x86)/Windows Kits/10/bin/*/x64")
    list(SORT LUX_DXC_HINTS ORDER DESCENDING)
    find_program(LUX_DXC NAMES dxc HINTS "${CMAKE_SOURCE_DIR}/Tools/dxc/bin/x64" ${LUX_DXC_HINTS} REQUIRED)
    foreach(stage vert frag)
        if(stage STREQUAL "vert")
            set(profile vs_6_0)
        else()
            set(profile ps_6_0)
        endif()
        foreach(format dxil spv)
            set(extra)
            if(format STREQUAL "spv")
                set(extra -spirv -fspv-target-env=vulkan1.0)
            endif()
            set(output "${CMAKE_BINARY_DIR}/shaders/Basic.${stage}.${format}")
            add_custom_command(OUTPUT "${output}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/shaders"
                COMMAND "${LUX_DXC}" -T ${profile} -E main ${extra} -Fo "${output}" "${CMAKE_SOURCE_DIR}/Shaders/Basic.${stage}.hlsl"
                DEPENDS "${CMAKE_SOURCE_DIR}/Shaders/Basic.${stage}.hlsl"
                VERBATIM)
            list(APPEND LUX_SHADER_OUTPUTS "${output}")
        endforeach()
    endforeach()
    add_custom_target(LuxShaders ALL DEPENDS ${LUX_SHADER_OUTPUTS})
endif()
function(lux_copy_runtime target)
    if(TARGET LuxShaders)
        add_dependencies(${target} LuxShaders)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/shaders" "$<TARGET_FILE_DIR:${target}>/shaders")
    endif()
    if(APPLE)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/shaders"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/Shaders/Basic.metal" "$<TARGET_FILE_DIR:${target}>/shaders/Basic.metal")
    endif()
    if(WIN32)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL3::SDL3-shared> $<TARGET_FILE_DIR:${target}>)
    endif()
endfunction()
