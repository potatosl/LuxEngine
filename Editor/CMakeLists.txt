find_package(wxWidgets REQUIRED COMPONENTS core base)
include(${wxWidgets_USE_FILE})

add_executable(LuxEditor WIN32 MACOSX_BUNDLE Main.cpp)
target_link_libraries(LuxEditor PRIVATE LuxEngine::Engine ${wxWidgets_LIBRARIES})
lux_set_target_warnings(LuxEditor)
lux_copy_runtime(LuxEditor)

if(WIN32 AND LUX_BUILD_SHARED)
    add_custom_command(TARGET LuxEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:LuxEngine>
            $<TARGET_FILE_DIR:LuxEditor>
        COMMENT "Copying LuxEngine.dll to editor output directory"
    )
endif()
