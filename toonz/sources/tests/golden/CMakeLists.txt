# Golden-render tests.
#
# One ctest per scene found in fixtures/reference_project/scenes/*.tnz.
# Each test renders the scene with the freshly built tcomposer and compares
# the frames with tests/golden/expected/<scene>/ (see run_golden.py).
#
# Vector levels and GL effects still need an OpenGL context (Phase 1 removes
# that). On Linux the tests therefore run under xvfb-run with Mesa's
# software rasteriser when NEXTTOONZ_GOLDEN_XVFB is ON (the default on
# Linux); macOS runs tcomposer directly on the default cocoa platform
# (its offscreen plugin has no OpenGL).

find_package(Python3 COMPONENTS Interpreter)

set(_fixture_root ${CMAKE_CURRENT_SOURCE_DIR}/../fixtures/reference_project)
file(GLOB _scenes RELATIVE ${_fixture_root} ${_fixture_root}/scenes/*.tnz)

if(NOT Python3_Interpreter_FOUND)
    message(STATUS "Golden tests: Python3 not found, skipping")
    return()
endif()
if(NOT _scenes)
    message(STATUS "Golden tests: no fixture scenes under ${_fixture_root}/scenes, skipping")
    return()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_xvfb_default ON)
else()
    set(_xvfb_default OFF)
endif()
option(NEXTTOONZ_GOLDEN_XVFB "Run golden renders under xvfb-run (Linux)" ${_xvfb_default})

set(_xvfb_arg)
if(NEXTTOONZ_GOLDEN_XVFB)
    find_program(XVFB_RUN xvfb-run)
    if(XVFB_RUN)
        set(_xvfb_arg --xvfb)
    else()
        message(WARNING "Golden tests: xvfb-run not found; GL-dependent scenes will fail")
    endif()
endif()

set(_stuff_dir ${CMAKE_SOURCE_DIR}/../../stuff)
set(_golden_work ${CMAKE_BINARY_DIR}/golden)

# On macOS the runnable tcomposer is the copy inside the app bundle, where
# the OpenToonz target also copies the shared libraries and lzocompress
# (the dylibs use @executable_path install names, see tests/CMakeLists.txt).
if(APPLE)
    set(_tcomposer "$<TARGET_FILE_DIR:OpenToonz>/tcomposer")
else()
    set(_tcomposer "$<TARGET_FILE:tcomposer>")
endif()

foreach(_scene ${_scenes})
    get_filename_component(_name ${_scene} NAME_WE)
    add_test(
        NAME golden_${_name}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run_golden.py
            --tcomposer ${_tcomposer}
            --stuff ${_stuff_dir}
            --project ${_fixture_root}
            --scene ${_scene}
            --expected ${CMAKE_CURRENT_SOURCE_DIR}/expected/${_name}
            --work ${_golden_work}/${_name}
            ${_xvfb_arg}
    )
    set_tests_properties(golden_${_name} PROPERTIES
        LABELS "golden"
        TIMEOUT 900
    )
endforeach()

message(STATUS "Golden tests: registered ${_scenes}")
