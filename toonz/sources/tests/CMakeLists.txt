# NextToonz test suite.
#
# Two kinds of tests live here:
#   core/    GoogleTest unit tests against the core libraries (tnzcore, ...).
#   golden/  tcomposer renders of the reference project compared against
#            checked-in golden images with a tolerance (see golden/README.md).
#
# GoogleTest is taken from the system (libgtest-dev, brew googletest, vcpkg)
# when available and fetched from GitHub otherwise.

find_package(GTest QUIET)
if(NOT GTest_FOUND)
    message(STATUS "GTest not found on the system, fetching googletest")
    include(FetchContent)
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.17.0
        GIT_SHALLOW    TRUE
    )
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()
include(GoogleTest)

# On macOS the toonz shared libraries carry @executable_path install names
# (see tnzcore/CMakeLists.txt), so a test executable can only load them when
# they sit next to it. The app bundle gets them copied in by the OpenToonz
# target; test executables stage their own copies here. Call this BEFORE
# gtest_discover_tests so the copy runs before test discovery.
function(nexttoonz_stage_runtime target)
    if(NOT APPLE)
        return()
    endif()
    # tnzcore loads the canvas libraries, so they go wherever it goes.
    set(_deps ${ARGN})
    if("tnzcore" IN_LIST _deps)
        list(APPEND _deps ${NEXTTOONZ_CANVAS_LIBS})
        list(REMOVE_DUPLICATES _deps)
    endif()
    foreach(_dep ${_deps})
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${_dep}> $<TARGET_FILE_DIR:${target}>
            COMMENT "Staging ${_dep} next to ${target}"
        )
    endforeach()
endfunction()

#-----------------------------------------------------------------------------
# Unit tests against tnzcore

# Every core/test_*.cpp is part of the core test binary; re-run cmake after
# adding a file (CONFIGURE_DEPENDS makes Ninja do that automatically).
file(GLOB NEXTTOONZ_CORE_TEST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/core/test_*.cpp
)

# The ones that need no more of tnzcore than its sources that are off Qt
# (TNZCORE_NOQT_SOURCES), and no Qt themselves: with WITH_QT=OFF the core
# test binary is made of these alone.
set(NEXTTOONZ_CORE_NOQT_TEST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/core/test_tfilepath.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/test_tgeometry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/test_tglentries.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/test_tkeyvaluestore.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/test_tsmartpointer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/test_tsound_io.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/test_tstring.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/test_tsystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/test_tthreadmutex.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/test_ttranslatabletext.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/test_timagecache.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/test_tproperty.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/test_vector_noqt.cpp
)

# Vector images, styles, pli files and pattern brushes drawn by Skia: in the
# core tests without Qt, and in the toonzlib tests with Qt (which load the
# image library and colorfx as the programs do), against the same checksums
# (doc/nexttoonz_core_noqt.md, 8d).
set(NEXTTOONZ_VECTOR_NOQT_TEST ${CMAKE_CURRENT_SOURCE_DIR}/core/test_vector_noqt.cpp)
list(REMOVE_ITEM NEXTTOONZ_CORE_TEST_SOURCES ${NEXTTOONZ_VECTOR_NOQT_TEST})

if(WITH_QT)
    # legacy/: the code of tnzcore before a migration step, kept for the
    # differential tests (test_*_qt.cpp) that compare it with the new code.
    # legacy_tsystem.cpp serves test_tsystem_qt.cpp only, which is compiled
    # on macOS and Linux only (#ifndef _WIN32): its Windows branch never
    # matched the UNICODE build (doc/nexttoonz_core_noqt.md, 8.16).
    set(NEXTTOONZ_LEGACY_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/core/legacy/legacy_tfilepath.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/legacy/legacy_tcontenthistory.cpp)
    if(NOT WIN32)
        list(APPEND NEXTTOONZ_LEGACY_SOURCES
            ${CMAKE_CURRENT_SOURCE_DIR}/core/legacy/legacy_tsystem.cpp)
    endif()
    add_executable(nexttoonz_core_tests ${NEXTTOONZ_CORE_TEST_SOURCES}
        ${NEXTTOONZ_LEGACY_SOURCES})
    target_include_directories(nexttoonz_core_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/core)
    target_link_libraries(nexttoonz_core_tests
        PRIVATE
            tnzcore
            tnzbase
            Qt5::Core
            GTest::gtest
            GTest::gtest_main
    )
    nexttoonz_stage_runtime(nexttoonz_core_tests tnzcore tnzbase)

    # Prints the executable's path as the core and Qt see it; the core tests
    # launch it in several ways (step 6c of doc/nexttoonz_core_noqt.md).
    add_executable(nexttoonz_appinfo_probe
        ${CMAKE_CURRENT_SOURCE_DIR}/core/appinfo_probe.cpp)
    target_link_libraries(nexttoonz_appinfo_probe PRIVATE tnzcore Qt5::Core)
    nexttoonz_stage_runtime(nexttoonz_appinfo_probe tnzcore)
    add_dependencies(nexttoonz_core_tests nexttoonz_appinfo_probe)
    target_compile_definitions(nexttoonz_core_tests PRIVATE
        NEXTTOONZ_APPINFO_PROBE="$<TARGET_FILE:nexttoonz_appinfo_probe>")
else()
    add_executable(nexttoonz_core_tests ${NEXTTOONZ_CORE_NOQT_TEST_SOURCES})
    target_link_libraries(nexttoonz_core_tests
        PRIVATE
            tnzcore
            colorfx
            image
            GTest::gtest
            GTest::gtest_main
    )
    target_compile_definitions(nexttoonz_core_tests PRIVATE
        NEXTTOONZ_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/reference_project")
    nexttoonz_stage_runtime(nexttoonz_core_tests tnzcore colorfx image)
endif()

# Registered as "core.<Suite>.<Test>" with the label "core", so that
# `ctest -L core` or `ctest -R core` selects them (the toonzlib tests use
# "toonzlib." the same way).
gtest_discover_tests(nexttoonz_core_tests
    TEST_PREFIX "core."
    PROPERTIES LABELS core
    DISCOVERY_TIMEOUT 60
)

#-----------------------------------------------------------------------------
# Tests of the canvas IR library (no other library, no rasteriser)

add_subdirectory(canvas)

#-----------------------------------------------------------------------------
# Tests against toonzlib and the image library (formats, scene model)

if(WITH_QT AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/toonzlib/CMakeLists.txt)
    add_subdirectory(toonzlib)
endif()

#-----------------------------------------------------------------------------
# Pixel tests of the canvas backends: Skia (tnzskia) and OpenGL (tnzglcanvas),
# and the Phase 1 spike that proved how Skia is linked (off by default, see
# skia_spike/README.md)

if(WITH_SKIA)
    add_subdirectory(canvas_skia)
endif()

option(WITH_CANVAS_GL_TESTS "Build the tests of the OpenGL canvas backend (they need an OpenGL context)" OFF)
if(WITH_CANVAS_GL_TESTS AND WITH_QT)
    add_subdirectory(canvas_gl)
endif()

option(WITH_SKIA_SPIKE "Build the Phase 1 Skia spike test (needs WITH_SKIA)" OFF)
if(WITH_SKIA_SPIKE AND WITH_QT)
    if(NOT WITH_SKIA)
        message(FATAL_ERROR "WITH_SKIA_SPIKE needs WITH_SKIA")
    endif()
    add_subdirectory(skia_spike)
endif()

#-----------------------------------------------------------------------------
# Golden renders (registered by golden/CMakeLists.txt once fixtures exist)

if(WITH_QT)
    add_subdirectory(golden)
endif()

#-----------------------------------------------------------------------------
# Fixture generator (developer tool, not a test)

if(WITH_QT)
    add_subdirectory(fixtures)
endif()

#-----------------------------------------------------------------------------
# The git hooks that guard the fixtures and baselines (.githooks/,
# tools/setup-hooks.sh): a shell test that commits into a throwaway
# repository. It needs git and a POSIX sh, so not on Windows.

find_package(Git QUIET)
find_program(NEXTTOONZ_POSIX_SH sh)
if(GIT_FOUND AND NEXTTOONZ_POSIX_SH AND NOT WIN32)
    add_test(NAME hooks.fixture_guard
        COMMAND ${NEXTTOONZ_POSIX_SH}
            ${CMAKE_CURRENT_SOURCE_DIR}/hooks/test_fixture_guard.sh
    )
    set_tests_properties(hooks.fixture_guard PROPERTIES LABELS hooks)
endif()
