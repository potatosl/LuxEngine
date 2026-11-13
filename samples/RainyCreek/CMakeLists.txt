add_library(RainyCreekScene STATIC
    Private/Catalog.cpp
    Private/CreekGeometry.cpp
    Private/CreekState.cpp
    Private/Options.cpp
    Private/OrbitController.cpp)
target_include_directories(RainyCreekScene PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Public")
target_link_libraries(RainyCreekScene PUBLIC LuxEngine::Engine PRIVATE nlohmann_json::nlohmann_json)
lux_set_target_warnings(RainyCreekScene)

add_executable(SampleRainyCreek
    Private/Main.cpp
    Private/CreekApplication.cpp
    Private/CreekRenderer.cpp
    Private/Capture.cpp)
target_link_libraries(SampleRainyCreek PRIVATE RainyCreekScene)
lux_set_target_warnings(SampleRainyCreek)
lux_copy_runtime(SampleRainyCreek)
target_include_directories(SampleRainyCreek SYSTEM PRIVATE "${stb_SOURCE_DIR}")
add_custom_target(RainyCreekAssets
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/Assets"
        "$<TARGET_FILE_DIR:SampleRainyCreek>/assets/rainy-creek"
    VERBATIM)
add_dependencies(SampleRainyCreek RainyCreekAssets)

if(LUX_BUILD_TESTS)
    find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)
    add_test(NAME SampleRainyCreek_smoke COMMAND SampleRainyCreek --smoke-test)
    set_tests_properties(SampleRainyCreek_smoke PROPERTIES TIMEOUT 120 LABELS "gpu;sample;rainy-creek"
        ENVIRONMENT "SDL_AUDIO_DRIVER=dummy;LUX_VULKAN_VALIDATION=sync")
    add_test(NAME SampleRainyCreek_help COMMAND SampleRainyCreek --help)
    set_tests_properties(SampleRainyCreek_help PROPERTIES TIMEOUT 10 LABELS "headless;sample;rainy-creek")
    foreach(suite OrbitController CreekGeometry CreekState Catalog)
        add_executable(RainyCreek${suite}Tests "Tests/${suite}Test.cpp")
        target_link_libraries(RainyCreek${suite}Tests PRIVATE RainyCreekScene)
        lux_set_target_warnings(RainyCreek${suite}Tests)
        lux_copy_runtime(RainyCreek${suite}Tests)
    endforeach()
    target_link_libraries(RainyCreekCatalogTests PRIVATE nlohmann_json::nlohmann_json)
    add_test(NAME RainyCreek_orbit COMMAND RainyCreekOrbitControllerTests)
    add_test(NAME RainyCreek_geometry COMMAND RainyCreekCreekGeometryTests)
    add_test(NAME RainyCreek_state COMMAND RainyCreekCreekStateTests)
    add_test(NAME RainyCreek_catalog COMMAND RainyCreekCatalogTests "${CMAKE_CURRENT_SOURCE_DIR}/Assets")
    add_test(NAME RainyCreek_assets COMMAND SampleRainyCreek --validate-assets
        --assets "${CMAKE_CURRENT_SOURCE_DIR}/Assets")
    set_tests_properties(RainyCreek_orbit RainyCreek_geometry RainyCreek_state RainyCreek_catalog
        RainyCreek_assets PROPERTIES LABELS "headless;sample;rainy-creek" TIMEOUT 90)
    add_test(NAME RainyCreek_asset_integrity COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_SOURCE_DIR}/tools/PrepareRainyCreekAssets.py" --verify)
    add_test(NAME RainyCreek_asset_tool COMMAND "${Python3_EXECUTABLE}" -m unittest discover
        -s "${CMAKE_SOURCE_DIR}/tools/Tests" -p test_rainy_creek_assets.py)
    set_tests_properties(RainyCreek_asset_integrity RainyCreek_asset_tool PROPERTIES
        LABELS "headless;sample;rainy-creek;assets" TIMEOUT 60)
endif()
