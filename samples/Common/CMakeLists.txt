add_library(LuxSampleSupport STATIC SampleApplication.cpp Artwork.cpp)
target_include_directories(LuxSampleSupport PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(LuxSampleSupport PUBLIC LuxEngine::Engine)
lux_set_target_warnings(LuxSampleSupport)

find_path(LUX_SAMPLE_STB_INCLUDE_DIR stb_truetype.h HINTS "${stb_SOURCE_DIR}" REQUIRED)
target_include_directories(LuxSampleSupport SYSTEM PRIVATE "${LUX_SAMPLE_STB_INCLUDE_DIR}")
