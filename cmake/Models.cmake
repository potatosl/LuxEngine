# Basic models are versioned resources, not configure-time downloads.
set(LUX_BASIC_MODEL_SOURCE_DIR
    "${CMAKE_SOURCE_DIR}/Engine/Resources/Models/Primitives/KenneyPrototype")
if(CMAKE_CONFIGURATION_TYPES)
    set(_lux_model_runtime_dir "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/Resources/Models")
else()
    set(_lux_model_runtime_dir "${CMAKE_BINARY_DIR}/bin/Resources/Models")
endif()
add_custom_target(LuxModelRuntimeFiles ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/Engine/Resources/Models" "${_lux_model_runtime_dir}"
    VERBATIM)
add_dependencies(LuxRuntimeFiles LuxModelRuntimeFiles)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/Engine/Resources/Models/"
    DESTINATION share/LuxEngine/models)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/Engine/Resources/Models/"
    DESTINATION bin/Resources/Models)
