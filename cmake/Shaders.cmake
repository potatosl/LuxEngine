# HLSL is compiled to SPIR-V for the native Vulkan renderer. The Windows SDK dxc
# lacks SPIR-V code generation, so only the Vulkan SDK or portable DXC builds are searched first.
find_program(LUX_DXC NAMES dxc
    HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin" "${CMAKE_SOURCE_DIR}/Tools/dxc/bin/x64"
    NO_DEFAULT_PATH)
if(NOT LUX_DXC)
    find_program(LUX_GLSLC NAMES glslc HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
endif()
if(NOT LUX_DXC AND NOT LUX_GLSLC AND NOT WIN32)
    find_program(LUX_DXC NAMES dxc)
endif()
if(NOT LUX_DXC AND NOT LUX_GLSLC)
    message(FATAL_ERROR "No SPIR-V capable shader compiler found. Install the Vulkan SDK (dxc or glslc) or run Tools/SetupShaderCompiler.ps1.")
endif()
find_program(LUX_SPIRV_VAL NAMES spirv-val HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")

foreach(shader Basic.vert Basic.frag Modern.frag Tone.vert Tone.frag Sky.frag)
    get_filename_component(stage "${shader}" LAST_EXT)
    string(SUBSTRING "${stage}" 1 -1 stage)
    if(stage STREQUAL "vert")
        set(profile vs_6_0)
    else()
        set(profile ps_6_0)
    endif()
    set(source "${CMAKE_SOURCE_DIR}/Shaders/${shader}.hlsl")
    set(output "${CMAKE_BINARY_DIR}/shaders/${shader}.spv")
    if(LUX_DXC)
        set(compile_command "${LUX_DXC}" -spirv -fspv-target-env=vulkan1.1 -T ${profile} -E main
            -Fo "${output}" "${source}")
    else()
        set(compile_command "${LUX_GLSLC}" -fshader-stage=${stage} -fentry-point=main -x hlsl
            --target-env=vulkan1.1 "${source}" -o "${output}")
    endif()
    set(validate_command)
    if(LUX_SPIRV_VAL)
        set(validate_command COMMAND "${LUX_SPIRV_VAL}" --target-env vulkan1.1 "${output}")
    endif()
    add_custom_command(OUTPUT "${output}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/shaders"
        COMMAND ${compile_command}
        ${validate_command}
        DEPENDS "${source}" "${CMAKE_SOURCE_DIR}/Shaders/Basic.frag.hlsl" "${CMAKE_SOURCE_DIR}/Shaders/Scene3D.hlsli"
        VERBATIM)
    list(APPEND LUX_SHADER_OUTPUTS "${output}")
endforeach()
add_custom_target(LuxShaders ALL DEPENDS ${LUX_SHADER_OUTPUTS})

# Every executable is built into the same bin folder. Copying shaders and SDL3.dll from each
# target's POST_BUILD made parallel builds write the same files at once and fail with
# "Permission denied", so one target copies them and the executables depend on it.
if(CMAKE_CONFIGURATION_TYPES)
    set(LUX_RUNTIME_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
else()
    set(LUX_RUNTIME_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin")
endif()
set(_lux_runtime_copy
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/shaders" "${LUX_RUNTIME_OUTPUT_DIR}/shaders")
if(WIN32 AND TARGET SDL3::SDL3-shared)
    list(APPEND _lux_runtime_copy
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL3::SDL3-shared> "${LUX_RUNTIME_OUTPUT_DIR}")
endif()
add_custom_target(LuxRuntimeFiles ALL ${_lux_runtime_copy} VERBATIM)
add_dependencies(LuxRuntimeFiles LuxShaders)

function(lux_copy_runtime target)
    add_dependencies(${target} LuxRuntimeFiles)
endfunction()
