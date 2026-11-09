# Pixel tests of the Skia canvas backend (tnzskia): what each call of the
# canvas interface puts into the pixels. They link tnzskia and tnzcanvas and
# nothing else of the tree, and need no display.

file(GLOB NEXTTOONZ_CANVAS_SKIA_TEST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp
)

add_executable(nexttoonz_canvas_skia_tests
    ../canvas/canvas_pixel_test_util.h
    ${NEXTTOONZ_CANVAS_SKIA_TEST_SOURCES}
)

target_include_directories(nexttoonz_canvas_skia_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../canvas
)

target_link_libraries(nexttoonz_canvas_skia_tests
    PRIVATE
        tnzskia
        tnzcanvas
        GTest::gtest
        GTest::gtest_main
)

nexttoonz_stage_runtime(nexttoonz_canvas_skia_tests tnzskia tnzcanvas)

gtest_discover_tests(nexttoonz_canvas_skia_tests
    TEST_PREFIX "canvas_skia."
    PROPERTIES LABELS canvas_skia
    DISCOVERY_TIMEOUT 60
)

# Like tnzcanvas, tnzskia must stay buildable without Qt, OpenGL and tnzcore.
add_test(NAME canvas_skia.sources_are_self_contained
    COMMAND ${CMAKE_COMMAND}
        -DLIBRARY=tnzskia
        -DCANVAS_SOURCE_DIR=${CMAKE_SOURCE_DIR}/tnzskia
        -DALLOW_SKIA=ON
        -P ${CMAKE_CURRENT_SOURCE_DIR}/../canvas/check_includes.cmake
)
set_tests_properties(canvas_skia.sources_are_self_contained
    PROPERTIES LABELS canvas_skia)
