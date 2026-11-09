# Tests of the OpenGL canvas backend (tnzglcanvas), and of how far it and
# the Skia backend agree on the same calls.
#
# Off by default (WITH_CANVAS_GL_TESTS): they need an OpenGL context from the
# window system, like the golden tests, and what a driver makes of smooth
# lines differs from machine to machine. The comparison with Skia is built
# only with WITH_SKIA.

file(GLOB NEXTTOONZ_CANVAS_GL_TEST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp
)

add_executable(nexttoonz_canvas_gl_tests
    canvas_gl_test_main.cpp
    canvas_gl_test_util.h
    ../canvas/canvas_pixel_test_util.h
    ${NEXTTOONZ_CANVAS_GL_TEST_SOURCES}
)

target_include_directories(nexttoonz_canvas_gl_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../canvas
)

target_link_libraries(nexttoonz_canvas_gl_tests
    PRIVATE
        tnzglcanvas
        tnzcanvas
        tnzcore
        Qt5::Core
        Qt5::Gui
        ${GL_LIB}
        GTest::gtest
)

nexttoonz_stage_runtime(nexttoonz_canvas_gl_tests tnzglcanvas tnzcanvas tnzcore)

if(WITH_SKIA)
    target_compile_definitions(nexttoonz_canvas_gl_tests PRIVATE NEXTTOONZ_HAVE_SKIA)
    target_link_libraries(nexttoonz_canvas_gl_tests PRIVATE tnzskia)
    nexttoonz_stage_runtime(nexttoonz_canvas_gl_tests tnzskia)
endif()

gtest_discover_tests(nexttoonz_canvas_gl_tests
    TEST_PREFIX "canvas_gl."
    PROPERTIES LABELS canvas_gl
    DISCOVERY_TIMEOUT 60
)
