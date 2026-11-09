# tnzcanvas: the path-level canvas IR (doc/nexttoonz_canvas_ir.md).
#
# The one library of the tree that has to build for iPadOS today, so it may
# depend on the C++ standard library only: no Qt, no OpenGL, no tnzcore. It
# links nothing, and tests/canvas checks that its sources include nothing
# from those either.

set(HEADERS
    ../include/canvas/tcanvastypes.h
    ../include/canvas/tcanvas.h
    ../include/canvas/tcanvasrecorder.h
)

set(SOURCES
    tcanvas.cpp
    tcanvasrecorder.cpp
)

add_library(tnzcanvas SHARED ${HEADERS} ${SOURCES})

target_compile_definitions(tnzcanvas PRIVATE TNZCANVAS_EXPORTS)

target_include_directories(tnzcanvas PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

if(BUILD_ENV_APPLE)
    # Like the other libraries of the tree: found next to the executable.
    set_target_properties(tnzcanvas PROPERTIES
        INSTALL_NAME_DIR "@executable_path"
        BUILD_WITH_INSTALL_NAME_DIR ON
    )
endif()
