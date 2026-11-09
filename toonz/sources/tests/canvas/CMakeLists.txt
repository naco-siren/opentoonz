# Tests of the canvas IR library (tnzcanvas). They need no rasteriser, no
# display and none of the other libraries: what is checked is which calls
# reach a canvas and how they are printed.

file(GLOB NEXTTOONZ_CANVAS_TEST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp
)

add_executable(nexttoonz_canvas_tests ${NEXTTOONZ_CANVAS_TEST_SOURCES})

target_link_libraries(nexttoonz_canvas_tests
    PRIVATE
        tnzcanvas
        GTest::gtest
        GTest::gtest_main
)

nexttoonz_stage_runtime(nexttoonz_canvas_tests tnzcanvas)

gtest_discover_tests(nexttoonz_canvas_tests
    TEST_PREFIX "canvas."
    PROPERTIES LABELS canvas
    DISCOVERY_TIMEOUT 60
)

# tnzcanvas must stay buildable without Qt, OpenGL and tnzcore. On a desktop
# build their headers are on the include path, so a stray #include would
# compile; this test reads the sources instead.
add_test(NAME canvas.sources_are_self_contained
    COMMAND ${CMAKE_COMMAND}
        -DCANVAS_SOURCE_DIR=${CMAKE_SOURCE_DIR}/tnzcanvas
        -DCANVAS_INCLUDE_DIR=${CMAKE_SOURCE_DIR}/include/canvas
        -P ${CMAKE_CURRENT_SOURCE_DIR}/check_includes.cmake
)
set_tests_properties(canvas.sources_are_self_contained PROPERTIES LABELS canvas)
