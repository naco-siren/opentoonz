# Fails if a source of a library that has to build without Qt, OpenGL and
# tnzcore includes anything but the C++ standard library and the headers of
# the canvas libraries. Run by ctest (see CMakeLists.txt here and in
# ../canvas_skia).
#
#   -DLIBRARY=<name>          for the message
#   -DCANVAS_SOURCE_DIR=<dir> the library's sources
#   -DCANVAS_INCLUDE_DIR=<dir> include/canvas (optional)
#   -DALLOW_SKIA=ON           also accept Skia's headers ("include/...")

file(GLOB _files
    "${CANVAS_SOURCE_DIR}/*.cpp" "${CANVAS_SOURCE_DIR}/*.h"
)
if(NOT _files)
    message(FATAL_ERROR "no ${LIBRARY} sources found")
endif()
if(CANVAS_INCLUDE_DIR)
    file(GLOB _headers "${CANVAS_INCLUDE_DIR}/*.h")
    list(APPEND _files ${_headers})
endif()

set(_bad "")
foreach(_file ${_files})
    file(STRINGS "${_file}" _includes REGEX "^[ \t]*#[ \t]*include")
    foreach(_line ${_includes})
        if(_line MATCHES "#[ \t]*include[ \t]*\"canvas/[a-z]+\\.h\"")
            # the canvas libraries' own headers
        elseif(_line MATCHES "#[ \t]*include[ \t]*<[a-z_]+>")
            # a standard header: lower case, no directory, no extension
        elseif(ALLOW_SKIA AND
               _line MATCHES "#[ \t]*include[ \t]*\"include/[a-z]+/Sk[A-Za-z0-9]+\\.h\"")
            # a public header of Skia
        else()
            string(APPEND _bad "\n  ${_file}: ${_line}")
        endif()
    endforeach()
endforeach()

if(_bad)
    message(FATAL_ERROR
        "${LIBRARY} may include only the C++ standard library and the "
        "headers of the canvas libraries:${_bad}")
endif()
