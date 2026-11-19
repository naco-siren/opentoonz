# tnzglview: the desktop OpenGL drawing that left tnzcore and colorfx with
# step 8c of doc/nexttoonz_core_noqt.md. The tool layer (tgl, the stencil
# control, display lists, offline contexts), the old OpenGL renderer of
# vector images, the OpenGL side of the seams of tglentries.h and the
# drawers of the nine styles that set OpenGL state of their own.
#
# The sources and headers stay where they were (common/, colorfx/,
# include/); only the library that builds them changed. A program that can
# draw through OpenGL calls TGLView::install() (tglview.h) in main().

set(HEADERS
    ../include/tglview.h
    ../include/tgl.h
    ../include/tstencilcontrol.h
    ../include/tgldisplaylistsmanager.h
    ../include/tofflinegl.h
    ../include/qtofflinegl.h
    ../include/tvectorgl.h
    ../include/tglcanvasbridge.h
    ../colorfx/colorfx_gl.h
)

set(SOURCES
    ../common/tgl/tgl.cpp
    ../common/tgl/tstencilcontrol.cpp
    ../common/tgl/tgldisplaylistsmanager.cpp
    ../common/tvrender/tofflinegl.cpp
    ../common/tvrender/qtofflinegl.cpp
    ../common/tvrender/tglregions.cpp
    ../common/tvrender/tglcanvasbridge.cpp
    ../common/tvrender/tglentries_gl.cpp
    ../common/tvectorimage/drawutil_gl.cpp
    ../colorfx/colorfx_gl.cpp
)

add_library(tnzglview SHARED ${HEADERS} ${SOURCES})

# The headers keep the export macros of the libraries the code came from.
target_compile_definitions(tnzglview PRIVATE
    TNZGLVIEW_EXPORTS
    TGL_EXPORTS
    TVRENDER_EXPORTS
    TVECTORIMAGE_EXPORTS
)

if(BUILD_ENV_APPLE)
    set_target_properties(tnzglview PROPERTIES
        INSTALL_NAME_DIR "@executable_path"
        BUILD_WITH_INSTALL_NAME_DIR ON
    )
endif()

# All private: whoever links tnzglview links tnzcore itself, so that its
# link line gains tnzglview and nothing else.
target_link_libraries(tnzglview
    PRIVATE
        tnzcore
        colorfx
        tnzcanvas
        tnzglcanvas
        Qt5::Core Qt5::Gui Qt5::OpenGL
        ${GL_LIB} ${GLU_LIB} ${GLUT_LIB}
)
if(BUILD_ENV_MSVC)
    target_link_libraries(tnzglview PRIVATE opengl32.lib glu32.lib)
endif()
