option(LUX_ENABLE_SANITIZERS "Instrument LuxEngine targets with AddressSanitizer and UBSan" OFF)
if(LUX_ENABLE_SANITIZERS AND
   (NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang)$"))
    message(FATAL_ERROR "LUX_ENABLE_SANITIZERS requires Linux with GCC or Clang.")
endif()

function(lux_set_target_warnings target_name)
    target_compile_features(${target_name} PUBLIC cxx_std_20)

    if(LUX_ENABLE_SANITIZERS)
        target_compile_options(${target_name} PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target_name} PUBLIC -fsanitize=address,undefined)
        # Modules enter the engine via TARGET_OBJECTS, which does not forward link usage
        # requirements. The final engine must carry the sanitizer runtime to its consumers.
        get_target_property(_lux_target_type ${target_name} TYPE)
        if(_lux_target_type STREQUAL "OBJECT_LIBRARY" AND TARGET LuxEngine)
            target_link_options(LuxEngine PUBLIC -fsanitize=address,undefined)
        endif()
    endif()

    if(MSVC)
        target_compile_options(${target_name} PRIVATE
            /W4
            /FS
            /MP
            /permissive-
            /utf-8
            /Zc:__cplusplus
            /Zc:preprocessor
        )
        target_compile_definitions(${target_name} PRIVATE
            $<$<CONFIG:Debug>:LUX_DEBUG>
            $<$<CONFIG:Release>:LUX_RELEASE>
            _CRT_SECURE_NO_WARNINGS
        )
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(${target_name} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
        target_compile_definitions(${target_name} PRIVATE
            $<$<CONFIG:Debug>:LUX_DEBUG>
            $<$<CONFIG:Release>:LUX_RELEASE>
        )
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${target_name} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
        target_compile_definitions(${target_name} PRIVATE
            $<$<CONFIG:Debug>:LUX_DEBUG>
            $<$<CONFIG:Release>:LUX_RELEASE>
        )
    endif()
endfunction()
