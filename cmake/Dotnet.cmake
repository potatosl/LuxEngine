# C# build tooling is supplied by the developer. No NuGet packages are downloaded.
find_program(LUX_DOTNET_EXECUTABLE NAMES dotnet HINTS "$ENV{DOTNET_ROOT}" REQUIRED)
execute_process(COMMAND "${LUX_DOTNET_EXECUTABLE}" --version
    OUTPUT_VARIABLE _lux_dotnet_version OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE _lux_dotnet_result)
if(NOT _lux_dotnet_result EQUAL 0 OR _lux_dotnet_version VERSION_LESS 10.0)
    message(FATAL_ERROR "C# scripting requires .NET SDK 10 or later. Set LUX_DOTNET_EXECUTABLE or DOTNET_ROOT.")
endif()
get_filename_component(_lux_dotnet_binary "${LUX_DOTNET_EXECUTABLE}" REALPATH)
get_filename_component(LUX_DOTNET_ROOT "${_lux_dotnet_binary}" DIRECTORY)
if(WIN32)
    set(_lux_dotnet_os win)
    set(LUX_NETHOST_NAME nethost.dll)
elseif(APPLE)
    set(_lux_dotnet_os osx)
    set(LUX_NETHOST_NAME libnethost.dylib)
else()
    set(_lux_dotnet_os linux)
    set(LUX_NETHOST_NAME libnethost.so)
endif()
if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
    set(_lux_dotnet_arch arm64)
else()
    set(_lux_dotnet_arch x64)
endif()
set(LUX_DOTNET_RID "${_lux_dotnet_os}-${_lux_dotnet_arch}" CACHE STRING ".NET native host runtime identifier")
set(_lux_host_rids "${LUX_DOTNET_RID}")
if(NOT CMAKE_CROSSCOMPILING)
    execute_process(COMMAND ${CMAKE_COMMAND} -E env DOTNET_CLI_UI_LANGUAGE=en-US
        "${LUX_DOTNET_EXECUTABLE}" --info
        OUTPUT_VARIABLE _lux_dotnet_info RESULT_VARIABLE _lux_dotnet_info_result)
    if(_lux_dotnet_info_result EQUAL 0 AND
       _lux_dotnet_info MATCHES "RID:[ \t]+([A-Za-z0-9._-]+)")
        set(_lux_sdk_rid "${CMAKE_MATCH_1}")
        if(_lux_sdk_rid MATCHES "-${_lux_dotnet_arch}$")
            list(APPEND _lux_host_rids "${_lux_sdk_rid}")
        endif()
    endif()
endif()
include(DotnetHost)
lux_dotnet_host_candidates(_lux_host_packs "${LUX_DOTNET_ROOT}" ${_lux_host_rids})
find_path(LUX_DOTNET_HOST_INCLUDE_DIR nethost.h HINTS ${_lux_host_packs} NO_DEFAULT_PATH)
if(NOT LUX_DOTNET_HOST_INCLUDE_DIR OR
   NOT EXISTS "${LUX_DOTNET_HOST_INCLUDE_DIR}/hostfxr.h" OR
   NOT EXISTS "${LUX_DOTNET_HOST_INCLUDE_DIR}/coreclr_delegates.h")
    message(FATAL_ERROR "The .NET 10 native host headers were not found under ${LUX_DOTNET_ROOT}. "
        "Searched host RIDs: ${_lux_host_rids}. Install the .NET SDK native host pack, "
        "or set LUX_DOTNET_HOST_INCLUDE_DIR to its native directory.")
endif()
find_file(LUX_NETHOST_LIBRARY ${LUX_NETHOST_NAME}
    HINTS "${LUX_DOTNET_HOST_INCLUDE_DIR}" REQUIRED NO_DEFAULT_PATH)
message(STATUS "C# native host: ${LUX_DOTNET_HOST_INCLUDE_DIR}")
set(LUX_MANAGED_OUTPUT "${CMAKE_BINARY_DIR}/managed")
set(LUX_SCRIPT_CORE_PROJECT "${CMAKE_SOURCE_DIR}/Engine/Scripting/Managed/LuxEngine.ScriptCore/LuxEngine.ScriptCore.csproj")
file(GLOB LUX_SCRIPT_CORE_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/Engine/Scripting/Managed/LuxEngine.ScriptCore/*.cs")
add_custom_command(OUTPUT "${LUX_MANAGED_OUTPUT}/LuxEngine.ScriptCore.dll"
    COMMAND ${CMAKE_COMMAND} -E env "DOTNET_CLI_TELEMETRY_OPTOUT=1" "DOTNET_NOLOGO=1"
        "${LUX_DOTNET_EXECUTABLE}" build "${LUX_SCRIPT_CORE_PROJECT}" --configuration Release
        --output "${LUX_MANAGED_OUTPUT}" --artifacts-path "${CMAKE_BINARY_DIR}/dotnet-artifacts"
        -p:RestoreIgnoreFailedSources=true -p:NuGetAudit=false
        -p:RestoreConfigFile="${CMAKE_SOURCE_DIR}/Engine/Scripting/Managed/NuGet.Config"
    DEPENDS "${LUX_SCRIPT_CORE_PROJECT}" ${LUX_SCRIPT_CORE_SOURCES} VERBATIM)
add_custom_target(LuxScriptCore DEPENDS "${LUX_MANAGED_OUTPUT}/LuxEngine.ScriptCore.dll")
if(CMAKE_CONFIGURATION_TYPES)
    set(_lux_managed_bin "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
else()
    set(_lux_managed_bin "${CMAKE_BINARY_DIR}/bin")
endif()
add_custom_target(LuxManagedRuntimeFiles
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${LUX_MANAGED_OUTPUT}" "${_lux_managed_bin}/managed"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LUX_NETHOST_LIBRARY}" "${_lux_managed_bin}/${LUX_NETHOST_NAME}"
    DEPENDS LuxScriptCore VERBATIM)
add_dependencies(LuxRuntimeFiles LuxManagedRuntimeFiles)
install(DIRECTORY "${LUX_MANAGED_OUTPUT}/" DESTINATION share/LuxEngine/managed)
install(DIRECTORY "${LUX_MANAGED_OUTPUT}/" DESTINATION bin/managed)
install(FILES "${LUX_NETHOST_LIBRARY}" DESTINATION bin)
