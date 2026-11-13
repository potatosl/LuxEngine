# Renderer resources are built/staged by TheForge.cmake, not by a native Vulkan path.
add_custom_target(LuxRuntimeFiles ALL)
function(lux_copy_runtime target)
    add_dependencies(${target} LuxRuntimeFiles)
endfunction()
