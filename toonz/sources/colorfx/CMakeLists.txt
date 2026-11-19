set(HEADERS
    ../include/colorfx.h
    colorfxutils.h
    rasterstyles.h
    regionstyles.h
    strokestyles.h
    flowlinestrokestyle.h
)

set(SOURCES
    colorfx.cpp
    colorfxutils.cpp
    rasterstyles.cpp
    regionstyles.cpp
    strokestyles.cpp
    flowlinestrokestyle.cpp
)

add_library(colorfx SHARED ${HEADERS} ${SOURCES})

if(WITH_TRANSLATION)
    add_translation(colorfx ${HEADERS} ${SOURCES})
    add_compile_translations_command(colorfx colorfx)
endif()

add_definitions(
    -D_USRDLL
    -DCOLORFX_EXPORTS
    -DUSE_MESA
)

if(BUILD_ENV_APPLE)
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-install_name,@executable_path/libcolorfx.dylib")
    if(WITH_QT)
        add_dependencies(colorfx tnzcore tnzbase)
    endif()
endif()

if(NOT WITH_QT)
    # Without Qt (doc/nexttoonz_core_noqt.md, 8d): the styles need tnzcore
    # and the canvas only (they use nothing of tnzbase, which the build
    # without Qt does not have).
    target_link_libraries(colorfx tnzcore tnzcanvas)
    return()
endif()

message("subdir: colorfx")
message("type:" ${CMAKE_SHARED_LIBRARY_SUFFIX})
message("Bin: " ${CMAKE_CURRENT_BINARY_DIR})

_find_toonz_library(EXTRA_LIBS "tnzcore;tnzbase")

message("FIND_FILE:" ${EXTRA_LIBS})

# No OpenGL since step 8c (doc/nexttoonz_core_noqt.md): the OpenGL drawers
# of these styles (colorfx_gl.cpp) are built into tnzglview.
target_link_libraries(colorfx Qt5::Core Qt5::Gui ${EXTRA_LIBS})

# The styles of this library draw on the canvas interface.
target_link_libraries(colorfx tnzcanvas)
