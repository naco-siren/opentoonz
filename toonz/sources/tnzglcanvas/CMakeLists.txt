# tnzglcanvas: the canvas backend that draws with fixed-function OpenGL, the
# way the old vector renderer did (doc/nexttoonz_canvas_ir.md, 7.2).
#
# It keeps OpenGL targets working while drawing code moves to the canvas
# interface. It links OpenGL and tnzcanvas and nothing else of the tree, so
# that it could stay behind with the viewers when the core libraries gave up
# OpenGL: since step 8c of doc/nexttoonz_core_noqt.md tnzglview links it,
# for the styles the old renderer reaches through tglDraw(), and tnzcore
# does not.

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
        ${GL_LIB} ${GLU_LIB}
)
