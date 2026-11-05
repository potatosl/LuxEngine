set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/Assets/Branding/dark.png"
    "${CMAKE_SOURCE_DIR}/Assets/Branding/light.png")
foreach(theme dark light)
    file(READ "${CMAKE_SOURCE_DIR}/Assets/Branding/${theme}.png" icon HEX)
    string(REGEX REPLACE "(..)" "0x\\1," icon "${icon}")
    set(LUX_ICON_${theme} "${icon}")
endforeach()
configure_file("${CMAKE_SOURCE_DIR}/cmake/Branding.cpp.in" "${CMAKE_BINARY_DIR}/generated/Branding.cpp" @ONLY)
