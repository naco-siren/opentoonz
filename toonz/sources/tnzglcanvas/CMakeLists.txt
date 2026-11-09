# tnzglcanvas: the canvas backend that draws with fixed-function OpenGL, the
# way the old vector renderer did (doc/nexttoonz_canvas_ir.md, 7.2).
#
# It keeps OpenGL targets working while drawing code moves to the canvas
# interface, and it is the one canvas library that links OpenGL and tnzcore.
# The libraries that are to lose their OpenGL dependency must not link it.

set(HEADERS
    ../include/canvas/tglcanvas.h
)

set(SOURCES
    tglcanvas.cpp
)

add_library(tnzglcanvas SHARED ${HEADERS} ${SOURCES})

target_compile_definitions(tnzglcanvas PRIVATE TNZGLCANVAS_EXPORTS)

if(BUILD_ENV_APPLE)
    set_target_properties(tnzglcanvas PROPERTIES
        INSTALL_NAME_DIR "@executable_path"
        BUILD_WITH_INSTALL_NAME_DIR ON
    )
endif()

target_link_libraries(tnzglcanvas
    PUBLIC
        tnzcanvas
    PRIVATE
        tnzcore
        Qt5::Core Qt5::Gui Qt5::OpenGL
        ${GLUT_LIB} ${GL_LIB} ${GLU_LIB}
)
