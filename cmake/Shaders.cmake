if(NOT LUX_USE_OGRE AND NOT APPLE)
    if(WIN32)
        file(GLOB LUX_DXC_HINTS "C:/Program Files (x86)/Windows Kits/10/bin/*/x64")
        list(SORT LUX_DXC_HINTS ORDER DESCENDING)
        find_program(LUX_DXC NAMES dxc HINTS "${CMAKE_SOURCE_DIR}/Tools/dxc/bin/x64" ${LUX_DXC_HINTS} REQUIRED)
        set(LUX_SHADER_FORMATS dxil spv)
    else()
        find_program(LUX_DXC NAMES dxc)
        if(NOT LUX_DXC)
            find_program(LUX_GLSLC NAMES glslc REQUIRED)
        endif()
        set(LUX_SHADER_FORMATS spv)
    endif()
    foreach(stage vert frag)
        if(stage STREQUAL "vert")
            set(profile vs_6_0)
        else()
            set(profile ps_6_0)
        endif()
        foreach(format ${LUX_SHADER_FORMATS})
            set(output "${CMAKE_BINARY_DIR}/shaders/Basic.${stage}.${format}")
            if(LUX_DXC)
                set(compile_command "${LUX_DXC}" -T ${profile} -E main -Fo "${output}")
                if(format STREQUAL "spv")
                    list(APPEND compile_command -spirv -fspv-target-env=vulkan1.0)
                endif()
                list(APPEND compile_command "${CMAKE_SOURCE_DIR}/Shaders/Basic.${stage}.hlsl")
            else()
                set(compile_command "${LUX_GLSLC}" -fshader-stage=${stage} -fentry-point=main -x hlsl
                    "${CMAKE_SOURCE_DIR}/Shaders/Basic.${stage}.hlsl" -o "${output}")
            endif()
            add_custom_command(OUTPUT "${output}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/shaders"
                COMMAND ${compile_command}
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
    if(APPLE AND NOT LUX_USE_OGRE)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/shaders"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/Shaders/Basic.metal" "$<TARGET_FILE_DIR:${target}>/shaders/Basic.metal")
    endif()
    if(WIN32 AND TARGET SDL3::SDL3-shared)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL3::SDL3-shared> $<TARGET_FILE_DIR:${target}>)
    endif()
    if(WIN32 AND LUX_USE_OGRE)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${LUX_OGRE_MAIN_DLL}" "${LUX_OGRE_GL_PLUGIN_PATH}"
                "$<TARGET_FILE_DIR:${target}>"
            VERBATIM)
    endif()
endfunction()
