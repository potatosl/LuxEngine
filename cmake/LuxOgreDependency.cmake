include_guard(GLOBAL)

# Ogre 1.9 on Fedora does not install a CMake config package. Search the
# developer's local installation without downloading or vendoring anything.
find_path(LUX_OGRE_INCLUDE_DIR NAMES OgreRoot.h PATH_SUFFIXES OGRE)
find_library(LUX_OGRE_MAIN_LIBRARY NAMES OgreMain OgreMain_d Ogre)

if(NOT LUX_OGRE_INCLUDE_DIR OR NOT LUX_OGRE_MAIN_LIBRARY)
    message(FATAL_ERROR
        "Ogre headers or library not found. Install Ogre development files or set "
        "LUX_OGRE_INCLUDE_DIR and LUX_OGRE_MAIN_LIBRARY.")
endif()

file(STRINGS "${LUX_OGRE_INCLUDE_DIR}/OgrePrerequisites.h" _lux_ogre_major_line
    REGEX "^[ \t]*#define[ \t]+OGRE_VERSION_MAJOR[ \t]+[0-9]+")
file(STRINGS "${LUX_OGRE_INCLUDE_DIR}/OgrePrerequisites.h" _lux_ogre_minor_line
    REGEX "^[ \t]*#define[ \t]+OGRE_VERSION_MINOR[ \t]+[0-9]+")
if(NOT _lux_ogre_major_line OR NOT _lux_ogre_minor_line)
    message(FATAL_ERROR "Could not determine the installed Ogre version.")
endif()
string(REGEX MATCH "[0-9]+$" _lux_ogre_major "${_lux_ogre_major_line}")
string(REGEX MATCH "[0-9]+$" _lux_ogre_minor "${_lux_ogre_minor_line}")
if(NOT _lux_ogre_major EQUAL 1 OR _lux_ogre_minor LESS 9)
    message(FATAL_ERROR
        "This Ogre bridge supports Ogre 1.9 or newer 1.x only; found "
        "${_lux_ogre_major}.${_lux_ogre_minor}. Set LUX_USE_OGRE=OFF to use SDL GPU.")
endif()

get_filename_component(_lux_ogre_library_dir "${LUX_OGRE_MAIN_LIBRARY}" DIRECTORY)
find_file(LUX_OGRE_GL_PLUGIN_PATH
    NAMES RenderSystem_GL.so RenderSystem_GL.dylib RenderSystem_GL.dll RenderSystem_GL_d.dll
    HINTS "${_lux_ogre_library_dir}/OGRE" "${_lux_ogre_library_dir}/../bin"
          "${_lux_ogre_library_dir}/../bin/OGRE"
          "${_lux_ogre_library_dir}/../bin/Release"
          "${_lux_ogre_library_dir}/../bin/Debug"
          "${_lux_ogre_library_dir}/../Plugins"
    PATH_SUFFIXES OGRE bin bin/OGRE bin/Release bin/Debug
                  Plugins Plugins/Release Plugins/Debug Release Debug)
if(NOT LUX_OGRE_GL_PLUGIN_PATH)
    message(FATAL_ERROR
        "Ogre OpenGL render plugin not found. Install it or set LUX_OGRE_GL_PLUGIN_PATH.")
endif()

if(WIN32)
    find_file(LUX_OGRE_MAIN_DLL
        NAMES OgreMain.dll OgreMain_d.dll Ogre.dll
        HINTS "${_lux_ogre_library_dir}" "${_lux_ogre_library_dir}/../bin"
              "${_lux_ogre_library_dir}/../bin/Release"
              "${_lux_ogre_library_dir}/../bin/Debug"
        PATH_SUFFIXES bin Release Debug)
    if(NOT LUX_OGRE_MAIN_DLL)
        message(FATAL_ERROR
            "OgreMain runtime DLL not found. Set LUX_OGRE_MAIN_DLL to its installed path.")
    endif()
endif()

if(NOT TARGET LuxOgre::Main)
    add_library(LuxOgre::Main UNKNOWN IMPORTED)
    set_target_properties(LuxOgre::Main PROPERTIES
        IMPORTED_LOCATION "${LUX_OGRE_MAIN_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${LUX_OGRE_INCLUDE_DIR}")
endif()
