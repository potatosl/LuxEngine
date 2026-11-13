# The Forge render backend (Windows, Direct3D 12 inside The Forge).
#
# The Forge is not downloaded by CMake. Check it out at the pinned revision into
# LUX_FORGE_ROOT (default: external/TheForge) and configure with -DLUX_ENABLE_FORGE=ON.
# It is built into LuxForge.dll, which LuxEngine loads at run time, so The Forge's compile
# settings and globals never mix with the engine or the games that link it.

option(LUX_ENABLE_FORGE "Build the The Forge render backend (LuxForge.dll)" OFF)
set(LUX_FORGE_ROOT "${CMAKE_SOURCE_DIR}/external/TheForge" CACHE PATH "The Forge checkout")
set(LUX_FORGE_REVISION "cd5046893faba2dc7869243873bf01f02a6f0df9")

if(NOT LUX_ENABLE_FORGE)
    return()
endif()
if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "LUX_ENABLE_FORGE currently supports Windows with MSVC only.")
endif()
set(TF "${LUX_FORGE_ROOT}/Common_3")
if(NOT EXISTS "${TF}/Graphics/Interfaces/IGraphics.h")
    message(FATAL_ERROR
        "The Forge was not found at LUX_FORGE_ROOT=${LUX_FORGE_ROOT}.\n"
        "Check out https://github.com/ConfettiFX/The-Forge at ${LUX_FORGE_REVISION} there "
        "(Common_3 and Tools/python-3.6.0-embed-amd64 are required).")
endif()

# The Forge reads its feature switches from Application/Config.h. Generate a copy with the
# application-framework features (UI, scripting, profiler, shader reload, ...) turned off;
# LuxEngine only uses the renderer and resource loader.
file(READ "${TF}/Application/Config.h" LUX_FORGE_CONFIG)
string(REPLACE "#if defined(EXTERNAL_CONFIG_FILEPATH)" "#if 0 // LuxEngine generated copy"
       LUX_FORGE_CONFIG "${LUX_FORGE_CONFIG}")
foreach(feature ENABLE_FORGE_SCRIPTING ENABLE_FORGE_RELOAD_SHADER ENABLE_FORGE_UI ENABLE_FORGE_FONTS
                ENABLE_FORGE_INPUT ENABLE_FORGE_ANIMATION_DEBUG ENABLE_FORGE_MATERIALS
                ENABLE_ZIP_FILESYSTEM ENABLE_SCREENSHOT ENABLE_PROFILER ENABLE_MESHOPTIMIZER
                ENABLE_THREAD_PERFORMANCE_STATS)
    string(REGEX REPLACE "#define ${feature}([\r\n])" "// #define ${feature} (off in LuxEngine)\\1"
           LUX_FORGE_CONFIG "${LUX_FORGE_CONFIG}")
endforeach()
# The Forge whitelists VS 2019 (_MSC_VER 1929) only; newer MSVC builds it fine.
string(REPLACE "#if (_MSC_VER == 1929)" "#if (_MSC_VER >= 1929)" LUX_FORGE_CONFIG "${LUX_FORGE_CONFIG}")
set(LUX_FORGE_CONFIG_DIR "${CMAKE_BINARY_DIR}/forge")
file(WRITE "${LUX_FORGE_CONFIG_DIR}/LuxForgeConfig.h.tmp" "${LUX_FORGE_CONFIG}")
configure_file("${LUX_FORGE_CONFIG_DIR}/LuxForgeConfig.h.tmp"
               "${LUX_FORGE_CONFIG_DIR}/LuxForgeConfig.h" COPYONLY)

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
    ${TF}/OS/ThirdParty/OpenSource/cpu_features/src/impl_x86_windows.c
    ${TF}/OS/Windows/WindowsFileSystem.cpp
    ${TF}/OS/Windows/WindowsLog.c
    ${TF}/OS/Windows/WindowsStackTraceDump.cpp
    ${TF}/OS/Windows/WindowsThread.c
    ${TF}/OS/Windows/WindowsTime.c
    # Renderer (Direct3D 12) and resource loader
    ${TF}/Graphics/Direct3D12/Direct3D12.c
    ${TF}/Graphics/Direct3D12/Direct3D12_cxx.cpp
    ${TF}/Graphics/Direct3D12/Direct3D12Hooks.c
    ${TF}/Graphics/Direct3D12/Direct3D12Raytracing.c
    ${TF}/Graphics/GraphicsConfig.cpp
    ${TF}/Resources/ResourceLoader/ResourceLoader.cpp
)

add_library(TheForge STATIC ${LUX_FORGE_SOURCES})
set_target_properties(TheForge PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
                                          POSITION_INDEPENDENT_CODE ON FOLDER ThirdParty)
target_include_directories(TheForge PUBLIC "${LUX_FORGE_CONFIG_DIR}" "${TF}")
target_compile_definitions(TheForge PUBLIC
    EXTERNAL_CONFIG_FILEPATH="LuxForgeConfig.h"
    _WINDOWS UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE
    # Without it a GPU missing from gpu.data (any newer card) is rejected as "office preset";
    # with it The Forge falls back to the default preset (low). Public so struct layouts match.
    ENABLE_GRAPHICS_RUNTIME_CHECK)
# Third-party code: keep its own warning level and never fail the build on its warnings.
target_compile_options(TheForge PRIVATE /W1 /WX- /MP /bigobj)
target_link_libraries(TheForge PUBLIC
    "${TF_THIRD}/ags/ags_lib/lib/amd_ags_x64.lib"
    "${TF_THIRD}/nvapi/amd64/nvapi64.lib"
    "${TF_THIRD}/DirectXShaderCompiler/lib/x64/dxcompiler.lib"
    "${TF_THIRD}/winpixeventruntime/bin/WinPixEventRuntime.lib"
    d3d12 dxgi dxguid winmm wbemuuid shlwapi DbgHelp ws2_32)

add_library(LuxForge SHARED
    ${CMAKE_SOURCE_DIR}/Engine/Renderer/Private/Forge/LuxForgeApi.h
    ${CMAKE_SOURCE_DIR}/Engine/Renderer/Private/Forge/LuxForge.cpp)
set_target_properties(LuxForge PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON DEBUG_POSTFIX "")
target_compile_definitions(LuxForge PRIVATE LUX_FORGE_BUILD)
target_compile_options(LuxForge PRIVATE /W3 /utf-8)
target_link_libraries(LuxForge PRIVATE TheForge)
# dxguid.lib and The Forge both define some D3D12 IIDs; the duplicates are identical.
target_link_options(LuxForge PRIVATE /FORCE:MULTIPLE)

# Small program that loads LuxForge.dll and checks that Direct3D 12 comes up.
add_executable(LuxForgeProbe Engine/Renderer/Private/Forge/LuxForgeProbe.cpp)
target_include_directories(LuxForgeProbe PRIVATE "${CMAKE_SOURCE_DIR}/Engine/Renderer/Private/Forge")
add_dependencies(LuxForgeProbe LuxForge)
target_link_libraries(LuxForgeProbe PRIVATE psapi)

# GpuDevice on the Forge backend (engine side), run as a GPU test.
add_executable(LuxForgeBackendTests Engine/Tests/ForgeBackendTest.cpp)
target_link_libraries(LuxForgeBackendTests PRIVATE LuxEngine::Engine)
target_include_directories(LuxForgeBackendTests PRIVATE "${CMAKE_SOURCE_DIR}/Engine/Renderer/Private")
lux_copy_runtime(LuxForgeBackendTests)
add_dependencies(LuxForgeBackendTests LuxForge)
if(LUX_BUILD_TESTS)
add_test(NAME Forge_backend_basic COMMAND LuxForgeBackendTests
         WORKING_DIRECTORY "$<TARGET_FILE_DIR:LuxForgeBackendTests>")
set_tests_properties(Forge_backend_basic PROPERTIES LABELS "gpu;forge" TIMEOUT 300 SKIP_RETURN_CODE 77)
endif()

# Runtime DLLs The Forge needs next to LuxForge.dll.
set(LUX_FORGE_RUNTIME_DLLS
    "${TF_THIRD}/ags/ags_lib/lib/amd_ags_x64.dll"
    "${TF_THIRD}/DirectXShaderCompiler/bin/x64/dxcompiler.dll"
    "${TF_THIRD}/winpixeventruntime/bin/WinPixEventRuntime.dll")
if(EXISTS "${TF_THIRD}/DirectXShaderCompiler/bin/x64/dxil.dll")
    list(APPEND LUX_FORGE_RUNTIME_DLLS "${TF_THIRD}/DirectXShaderCompiler/bin/x64/dxil.dll")
endif()
foreach(dll ${LUX_FORGE_RUNTIME_DLLS})
    add_custom_command(TARGET LuxForge POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${dll}" "$<TARGET_FILE_DIR:LuxForge>")
endforeach()
# Shaders: FSL -> DXIL with The Forge's shader compiler driver, written next to LuxForge.dll
# (<dir>/DIRECT3D12/*.vert|frag), where the resource loader looks for them.
set(LUX_FORGE_SHADER_DIR "${CMAKE_SOURCE_DIR}/Engine/Renderer/Private/Forge/Shaders")
file(GLOB LUX_FORGE_SHADER_SOURCES CONFIGURE_DEPENDS "${LUX_FORGE_SHADER_DIR}/*.fsl" "${LUX_FORGE_SHADER_DIR}/*.h"
     "${LUX_FORGE_SHADER_DIR}/*.list")
set(LUX_FORGE_PYTHON "${LUX_FORGE_ROOT}/Tools/python-3.6.0-embed-amd64/python.exe")
set(LUX_FORGE_SHADER_STAMP "${CMAKE_BINARY_DIR}/forge/shaders.stamp")
add_custom_command(
    OUTPUT "${LUX_FORGE_SHADER_STAMP}"
    COMMAND "${LUX_FORGE_PYTHON}" "${TF}/Tools/ForgeShadingLanguage/fsl.py"
            "${LUX_FORGE_SHADER_DIR}/LuxForgeShaders.list"
            -l DIRECT3D12 --compile
            -d "${CMAKE_BINARY_DIR}/forge/shaders/generated"
            -b "${CMAKE_BINARY_DIR}/forge/shaders/bin"
            -i "${CMAKE_BINARY_DIR}/forge/shaders/intermediate"
            -I "${TF}/Graphics/FSL"
    COMMAND ${CMAKE_COMMAND} -E touch "${LUX_FORGE_SHADER_STAMP}"
    DEPENDS ${LUX_FORGE_SHADER_SOURCES}
    COMMENT "Compiling LuxForge shaders (FSL -> DXIL)"
    VERBATIM)
add_custom_target(LuxForgeShaders DEPENDS "${LUX_FORGE_SHADER_STAMP}")
add_dependencies(LuxForge LuxForgeShaders)
add_custom_command(TARGET LuxForge POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/forge/shaders/bin/DIRECT3D12"
            "$<TARGET_FILE_DIR:LuxForge>/DIRECT3D12")
install(DIRECTORY "${CMAKE_BINARY_DIR}/forge/shaders/bin/DIRECT3D12" DESTINATION bin)

# GPU preset table The Forge reads at start-up (resource directory = folder of LuxForge.dll).
add_custom_command(TARGET LuxForge POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${TF}/OS/Windows/pc_gpu.data"
            "$<TARGET_FILE_DIR:LuxForge>/gpu.data")
install(FILES "${TF}/OS/Windows/pc_gpu.data" DESTINATION bin RENAME gpu.data)
install(TARGETS LuxForge RUNTIME DESTINATION bin)
install(FILES ${LUX_FORGE_RUNTIME_DLLS} DESTINATION bin)
