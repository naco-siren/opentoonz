# Fails if a source of tnzcanvas includes anything but the C++ standard
# library and its own headers. Run by ctest (see CMakeLists.txt).

file(GLOB _files
    "${CANVAS_SOURCE_DIR}/*.cpp" "${CANVAS_SOURCE_DIR}/*.h"
    "${CANVAS_INCLUDE_DIR}/*.h"
)
if(NOT _files)
    message(FATAL_ERROR "no tnzcanvas sources found")
endif()

set(_bad "")
foreach(_file ${_files})
    file(STRINGS "${_file}" _includes REGEX "^[ \t]*#[ \t]*include")
    foreach(_line ${_includes})
        if(_line MATCHES "#[ \t]*include[ \t]*\"canvas/[a-z]+\\.h\"")
            # the library's own headers
        elseif(_line MATCHES "#[ \t]*include[ \t]*<[a-z_]+>")
            # a standard header: lower case, no directory, no extension
        else()
            string(APPEND _bad "\n  ${_file}: ${_line}")
        endif()
    endforeach()
endforeach()

if(_bad)
    message(FATAL_ERROR
        "tnzcanvas may include only the C++ standard library and its own "
        "headers:${_bad}")
endif()
