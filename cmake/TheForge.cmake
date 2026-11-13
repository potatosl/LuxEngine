# The Forge source is installed separately; configuring this project never downloads it.
set(LUX_ENABLE_FORGE ON)
set(LUX_FORGE_ROOT "${CMAKE_SOURCE_DIR}/Engine/ThirdParty/TheForge" CACHE PATH "The Forge source checkout")
set(LUX_FORGE_REVISION "cd5046893faba2dc7869243873bf01f02a6f0df9")
set(TF "${LUX_FORGE_ROOT}/Common_3")
if(NOT EXISTS "${TF}/Graphics/Interfaces/IGraphics.h")
    message(FATAL_ERROR "The Forge source is required. Set LUX_FORGE_ROOT to revision ${LUX_FORGE_REVISION}; see docs/TheForge.md.")
endif()
find_package(Threads REQUIRED)
find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)
file(READ "${TF}/Application/Config.h" LUX_FORGE_CONFIG)
string(REPLACE "#if defined(EXTERNAL_CONFIG_FILEPATH)" "#if 0 // LuxEngine generated copy"
    LUX_FORGE_CONFIG "${LUX_FORGE_CONFIG}")
foreach(feature ENABLE_FORGE_SCRIPTING ENABLE_FORGE_RELOAD_SHADER ENABLE_FORGE_UI ENABLE_FORGE_FONTS
        ENABLE_FORGE_INPUT ENABLE_FORGE_ANIMATION_DEBUG ENABLE_FORGE_MATERIALS ENABLE_ZIP_FILESYSTEM
        ENABLE_SCREENSHOT ENABLE_PROFILER ENABLE_MESHOPTIMIZER ENABLE_THREAD_PERFORMANCE_STATS)
    string(REGEX REPLACE "#define ${feature}([\r\n])" "// #define ${feature} (off in LuxEngine)\\1"
        LUX_FORGE_CONFIG "${LUX_FORGE_CONFIG}")
endforeach()
string(REPLACE "#if (_MSC_VER == 1929)" "#if (_MSC_VER >= 1929)" LUX_FORGE_CONFIG "${LUX_FORGE_CONFIG}")
set(LUX_FORGE_CONFIG_DIR "${CMAKE_BINARY_DIR}/forge")
file(MAKE_DIRECTORY "${LUX_FORGE_CONFIG_DIR}")
file(WRITE "${LUX_FORGE_CONFIG_DIR}/LuxForgeConfig.h.tmp" "${LUX_FORGE_CONFIG}")
configure_file("${LUX_FORGE_CONFIG_DIR}/LuxForgeConfig.h.tmp" "${LUX_FORGE_CONFIG_DIR}/LuxForgeConfig.h" COPYONLY)
set(TF_THIRD "${TF}/Graphics/ThirdParty/OpenSource")
set(LUX_FORGE_SOURCES
    # Utilities and OS layer (memory, files, logging, threads, timers)
    ${TF}/Utilities/FileSystem/FileSystem.c
    ${TF}/Utilities/Log/Log.c
    ${TF}/Utilities/Math/Algorithms.c
    ${TF}/Utilities/Math/StbDs.c
    ${TF}/Utilities/MemoryTracking/MemoryTracking.c
    ${TF}/Utilities/ThirdParty/OpenSource/bstrlib/bstrlib.c
    ${TF}/Utilities/ThirdParty/OpenSource/lz4/lz4.c
    ${TF}/Utilities/ThirdParty/OpenSource/zstd/common/debug.c
    ${TF}/Utilities/ThirdParty/OpenSource/zstd/common/entropy_common.c
    ${TF}/Utilities/ThirdParty/OpenSource/zstd/common/error_private.c
    ${TF}/Utilities/ThirdParty/OpenSource/zstd/common/fse_decompress.c
    ${TF}/Utilities/ThirdParty/OpenSource/zstd/common/pool.c
    ${TF}/Utilities/ThirdParty/OpenSource/zstd/common/threading.c
    ${TF}/Utilities/ThirdParty/OpenSource/zstd/common/xxhash.c
    ${TF}/Utilities/ThirdParty/OpenSource/zstd/common/zstd_common.c
    ${TF}/Utilities/ThirdParty/OpenSource/zstd/decompress/huf_decompress.c
    ${TF}/Utilities/ThirdParty/OpenSource/zstd/decompress/zstd_ddict.c
    ${TF}/Utilities/ThirdParty/OpenSource/zstd/decompress/zstd_decompress.c
    ${TF}/Utilities/ThirdParty/OpenSource/zstd/decompress/zstd_decompress_block.c
    ${TF}/Utilities/Threading/ThreadSystem.c
    ${TF}/Utilities/Timer.c
    ${TF}/OS/CPUConfig.cpp
    # Renderer (Direct3D 12) and resource loader
    ${TF}/Graphics/GraphicsConfig.cpp
    ${TF}/Resources/ResourceLoader/ResourceLoader.cpp
)

if(WIN32)
    if(NOT MSVC)
        message(FATAL_ERROR "The Forge D3D12 renderer requires MSVC on Windows.")
    endif()
    list(APPEND LUX_FORGE_SOURCES
        ${TF}/OS/ThirdParty/OpenSource/cpu_features/src/impl_x86_windows.c
        ${TF}/OS/Windows/WindowsFileSystem.cpp ${TF}/OS/Windows/WindowsLog.c
        ${TF}/OS/Windows/WindowsStackTraceDump.cpp ${TF}/OS/Windows/WindowsThread.c
        ${TF}/OS/Windows/WindowsTime.c
        ${TF}/Graphics/Direct3D12/Direct3D12.c ${TF}/Graphics/Direct3D12/Direct3D12_cxx.cpp
        ${TF}/Graphics/Direct3D12/Direct3D12Hooks.c ${TF}/Graphics/Direct3D12/Direct3D12Raytracing.c)
    set(LUX_FORGE_SHADER_TARGET DIRECT3D12)
    set(LUX_FORGE_GPU_DATA "${TF}/OS/Windows/pc_gpu.data")
elseif(APPLE)
    list(APPEND LUX_FORGE_SOURCES ${TF}/Utilities/FileSystem/UnixFileSystem.c)
    enable_language(OBJC OBJCXX)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
        set(_forge_cpu impl_aarch64_iOS.c)
    else()
        set(_forge_cpu impl_x86_macos.c)
    endif()
    list(APPEND LUX_FORGE_SOURCES
        ${TF}/OS/ThirdParty/OpenSource/cpu_features/src/${_forge_cpu}
        ${TF}/OS/Darwin/CocoaFileSystem.mm ${TF}/OS/Darwin/DarwinLog.c
        ${TF}/OS/Darwin/DarwinThread.c ${TF}/OS/Linux/LinuxTime.c
        ${TF}/Graphics/Metal/MetalRenderer.mm ${TF}/Graphics/Metal/MetalRaytracing.mm)
    set(LUX_FORGE_SHADER_TARGET MACOS)
    set(LUX_FORGE_GPU_DATA "${TF}/OS/Darwin/apple_gpu.data")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND LUX_FORGE_SOURCES ${TF}/Utilities/FileSystem/UnixFileSystem.c)
    find_package(X11 REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(ForgeWayland REQUIRED IMPORTED_TARGET wayland-client)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        set(_forge_cpu impl_aarch64_linux_or_android.c)
    else()
        set(_forge_cpu impl_x86_linux_or_android.c)
    endif()
    list(APPEND LUX_FORGE_SOURCES
        ${TF}/OS/ThirdParty/OpenSource/cpu_features/src/${_forge_cpu}
        ${TF}/OS/ThirdParty/OpenSource/cpu_features/src/hwcaps.c
        ${TF}/OS/Linux/LinuxFileSystem.c ${TF}/OS/Linux/LinuxLog.c
        ${TF}/OS/Linux/LinuxThread.c ${TF}/OS/Linux/LinuxTime.c
        ${TF}/Graphics/Vulkan/Vulkan.c ${TF}/Graphics/Vulkan/Vulkan_cxx.cpp
        ${TF}/Graphics/Vulkan/VulkanRaytracing.c)
    set(LUX_FORGE_SHADER_TARGET VULKAN)
    set(LUX_FORGE_GPU_DATA "${TF}/OS/Windows/pc_gpu.data")
else()
    message(FATAL_ERROR "The Forge renderer supports Windows, Linux and macOS.")
endif()
# The pinned resource loader transitions raw textures on its copy queue. New Vulkan
# validation rejects shader access barriers on a transfer-only queue. Use a graphics-capable
# upload queue for this renderer; keep the external checkout untouched.
file(READ "${TF}/Resources/ResourceLoader/ResourceLoader.cpp" _forge_loader)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    string(REPLACE "desc.mQueueType = QUEUE_TYPE_TRANSFER;" "desc.mQueueType = QUEUE_TYPE_GRAPHICS;"
        _forge_loader "${_forge_loader}")
endif()
# Shader hot reload is deliberately disabled in this integration. Its startup notice is
# informational; retain all other upstream warnings and errors at their original level.
string(REPLACE [[LOGF(eWARNING, "ReloadServer is DISABLED.]] [[LOGF(eINFO, "ReloadServer is DISABLED.]]
    _forge_loader "${_forge_loader}")
file(WRITE "${LUX_FORGE_CONFIG_DIR}/ResourceLoader.cpp.tmp" "${_forge_loader}")
configure_file("${LUX_FORGE_CONFIG_DIR}/ResourceLoader.cpp.tmp"
    "${LUX_FORGE_CONFIG_DIR}/ResourceLoader.cpp" COPYONLY)
list(REMOVE_ITEM LUX_FORGE_SOURCES "${TF}/Resources/ResourceLoader/ResourceLoader.cpp")
list(APPEND LUX_FORGE_SOURCES "${LUX_FORGE_CONFIG_DIR}/ResourceLoader.cpp")
# Swapchain images are always 2D, including one-pixel-high windows. The pinned Vulkan
# renderer infers 1D views from height alone; preserve the requested texture dimension.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    file(READ "${TF}/Graphics/Vulkan/Vulkan.c" _forge_vulkan)
    string(REPLACE "descColor.mDepth = 1;"
        "descColor.mDepth = 1; descColor.mFlags = TEXTURE_CREATION_FLAG_FORCE_2D;"
        _forge_vulkan "${_forge_vulkan}")
    string(REPLACE [[if (pDesc->mHeight > 1)
    {
        viewType]] [[if (pDesc->mHeight > 1 || (pDesc->mFlags & TEXTURE_CREATION_FLAG_FORCE_2D))
    {
        viewType]] _forge_vulkan "${_forge_vulkan}")
    file(WRITE "${LUX_FORGE_CONFIG_DIR}/Vulkan.c.tmp" "${_forge_vulkan}")
    configure_file("${LUX_FORGE_CONFIG_DIR}/Vulkan.c.tmp"
        "${LUX_FORGE_CONFIG_DIR}/Vulkan.c" COPYONLY)
    list(REMOVE_ITEM LUX_FORGE_SOURCES "${TF}/Graphics/Vulkan/Vulkan.c")
    list(APPEND LUX_FORGE_SOURCES "${LUX_FORGE_CONFIG_DIR}/Vulkan.c")
endif()
if(WIN32)
    include(TheForgeD3D12)
endif()
add_library(TheForge STATIC ${LUX_FORGE_SOURCES})
set_target_properties(TheForge PROPERTIES CXX_STANDARD 17 C_STANDARD 11
    CXX_STANDARD_REQUIRED ON POSITION_INDEPENDENT_CODE ON FOLDER ThirdParty)
target_include_directories(TheForge PUBLIC "${LUX_FORGE_CONFIG_DIR}" "${TF}"
    PRIVATE "${TF}/Resources/ResourceLoader" "${TF}/Graphics/Vulkan" "${TF}/Graphics/Direct3D12")
target_compile_definitions(TheForge PUBLIC EXTERNAL_CONFIG_FILEPATH="LuxForgeConfig.h"
    ZSTD_DISABLE_ASM ENABLE_GRAPHICS_RUNTIME_CHECK)
target_link_libraries(TheForge PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
if(WIN32)
    target_compile_definitions(TheForge PUBLIC _WINDOWS UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
    target_compile_options(TheForge PRIVATE /W1 /WX- /MP /bigobj)
    target_link_libraries(TheForge PUBLIC
        "${TF_THIRD}/ags/ags_lib/lib/amd_ags_x64.lib"
        "${TF_THIRD}/nvapi/amd64/nvapi64.lib"
        "${TF_THIRD}/DirectXShaderCompiler/lib/x64/dxcompiler.lib"
        "${TF_THIRD}/winpixeventruntime/bin/WinPixEventRuntime.lib"
        d3d12 dxgi dxguid winmm wbemuuid shlwapi DbgHelp ws2_32)
elseif(APPLE)
    target_compile_options(TheForge PRIVATE -Wno-deprecated-declarations $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    target_link_libraries(TheForge PUBLIC "-framework Metal" "-framework MetalKit"
        "-framework Cocoa" "-framework QuartzCore" "-framework IOKit" "-framework CoreVideo")
else()
    target_link_libraries(TheForge PUBLIC X11::X11 PkgConfig::ForgeWayland)
    target_compile_options(TheForge PRIVATE -Wno-unused-result -Wno-deprecated-declarations)
endif()
add_library(LuxForge SHARED Engine/Renderer/Private/Forge/LuxForge.cpp)
set_target_properties(LuxForge PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON DEBUG_POSTFIX "")
target_compile_definitions(LuxForge PRIVATE LUX_FORGE_BUILD)
target_link_libraries(LuxForge PRIVATE TheForge)
if(MSVC)
    target_compile_options(LuxForge PRIVATE /W3 /utf-8)
    target_link_options(LuxForge PRIVATE /FORCE:MULTIPLE)
elseif(UNIX AND NOT APPLE)
    target_link_options(LuxForge PRIVATE -Wl,--no-undefined)
elseif(APPLE)
    set_source_files_properties(Engine/Renderer/Private/Forge/LuxForge.cpp PROPERTIES LANGUAGE OBJCXX)
    target_compile_options(LuxForge PRIVATE -fobjc-arc)
endif()
# D3D12 vendor and compiler runtime DLLs travel with the renderer.
if(WIN32)
    set(LUX_FORGE_RUNTIME_DLLS
        "${TF_THIRD}/ags/ags_lib/lib/amd_ags_x64.dll"
        "${TF_THIRD}/DirectXShaderCompiler/bin/x64/dxcompiler.dll"
        "${TF_THIRD}/winpixeventruntime/bin/WinPixEventRuntime.dll")
    if(EXISTS "${TF_THIRD}/DirectXShaderCompiler/bin/x64/dxil.dll")
        list(APPEND LUX_FORGE_RUNTIME_DLLS "${TF_THIRD}/DirectXShaderCompiler/bin/x64/dxil.dll")
    endif()
    foreach(dll IN LISTS LUX_FORGE_RUNTIME_DLLS)
        add_custom_command(TARGET LuxForge POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${dll}" "$<TARGET_FILE_DIR:LuxForge>")
    endforeach()
    install(FILES ${LUX_FORGE_RUNTIME_DLLS} DESTINATION bin)
endif()
# FSL compiles to the native shader language for the selected OS.
set(LUX_FORGE_SHADER_DIR "${CMAKE_SOURCE_DIR}/Engine/Renderer/Private/Forge/Shaders")
file(GLOB LUX_FORGE_SHADER_SOURCES CONFIGURE_DEPENDS "${LUX_FORGE_SHADER_DIR}/*.fsl"
    "${LUX_FORGE_SHADER_DIR}/*.h" "${LUX_FORGE_SHADER_DIR}/*.list")
set(_forge_shader_env)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_program(LUX_FORGE_GLSLANG glslangValidator REQUIRED)
    get_filename_component(_forge_compiler_dir "${LUX_FORGE_GLSLANG}" DIRECTORY)
    list(APPEND _forge_shader_env "FSL_COMPILER_VK=${_forge_compiler_dir}" "FSL_COMPILER_LINUX_VK=${_forge_compiler_dir}")
elseif(WIN32)
    list(APPEND _forge_shader_env "FSL_COMPILER_DXC=${TF_THIRD}/DirectXShaderCompiler/bin/x64")
endif()
set(LUX_FORGE_SHADER_STAMP "${CMAKE_BINARY_DIR}/forge/shaders.stamp")
add_custom_command(OUTPUT "${LUX_FORGE_SHADER_STAMP}"
    COMMAND ${CMAKE_COMMAND} -E env ${_forge_shader_env} "${Python3_EXECUTABLE}"
        "${TF}/Tools/ForgeShadingLanguage/fsl.py" "${LUX_FORGE_SHADER_DIR}/LuxForgeShaders.list"
        -l ${LUX_FORGE_SHADER_TARGET} --compile
        -d "${CMAKE_BINARY_DIR}/forge/shaders/generated" -b "${CMAKE_BINARY_DIR}/forge/shaders/bin"
        -i "${CMAKE_BINARY_DIR}/forge/shaders/intermediate" -I "${TF}/Graphics/FSL"
    COMMAND ${CMAKE_COMMAND} -E touch "${LUX_FORGE_SHADER_STAMP}"
    DEPENDS ${LUX_FORGE_SHADER_SOURCES} VERBATIM)
add_custom_target(LuxForgeShaders DEPENDS "${LUX_FORGE_SHADER_STAMP}")
add_dependencies(LuxForge LuxForgeShaders)
add_custom_command(TARGET LuxForge POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/forge/shaders/bin/${LUX_FORGE_SHADER_TARGET}"
        "$<TARGET_FILE_DIR:LuxForge>/${LUX_FORGE_SHADER_TARGET}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LUX_FORGE_GPU_DATA}" "$<TARGET_FILE_DIR:LuxForge>/gpu.data"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/Engine/Renderer/Private/Forge/gpu.cfg" "$<TARGET_FILE_DIR:LuxForge>/gpu.cfg")
install(TARGETS LuxForge RUNTIME DESTINATION bin LIBRARY DESTINATION bin)
install(DIRECTORY "${CMAKE_BINARY_DIR}/forge/shaders/bin/${LUX_FORGE_SHADER_TARGET}" DESTINATION bin)
install(FILES "${LUX_FORGE_GPU_DATA}" DESTINATION bin RENAME gpu.data)
# All executables share bin, so one dependency stages the renderer for them.
add_dependencies(LuxRuntimeFiles LuxForge)

install(FILES "${CMAKE_SOURCE_DIR}/Engine/Renderer/Private/Forge/gpu.cfg" DESTINATION bin)

install(DIRECTORY "${CMAKE_BINARY_DIR}/forge/shaders/bin/" DESTINATION share/LuxEngine/shaders)
install(FILES "${LUX_FORGE_GPU_DATA}" DESTINATION share/LuxEngine/renderer RENAME gpu.data)
install(FILES "${CMAKE_SOURCE_DIR}/Engine/Renderer/Private/Forge/gpu.cfg" DESTINATION share/LuxEngine/renderer)
