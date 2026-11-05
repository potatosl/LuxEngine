function(lux_set_target_warnings target_name)
    target_compile_features(${target_name} PUBLIC cxx_std_20)

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
