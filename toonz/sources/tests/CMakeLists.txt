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

add_executable(nexttoonz_core_tests ${NEXTTOONZ_CORE_TEST_SOURCES})

target_link_libraries(nexttoonz_core_tests
    PRIVATE
        tnzcore
        tnzbase
        Qt5::Core
        GTest::gtest
        GTest::gtest_main
)

nexttoonz_stage_runtime(nexttoonz_core_tests tnzcore tnzbase)

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

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/toonzlib/CMakeLists.txt)
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
if(WITH_CANVAS_GL_TESTS)
    add_subdirectory(canvas_gl)
endif()

option(WITH_SKIA_SPIKE "Build the Phase 1 Skia spike test (needs WITH_SKIA)" OFF)
if(WITH_SKIA_SPIKE)
    if(NOT WITH_SKIA)
        message(FATAL_ERROR "WITH_SKIA_SPIKE needs WITH_SKIA")
    endif()
    add_subdirectory(skia_spike)
endif()

#-----------------------------------------------------------------------------
# Golden renders (registered by golden/CMakeLists.txt once fixtures exist)

add_subdirectory(golden)

#-----------------------------------------------------------------------------
# Fixture generator (developer tool, not a test)

add_subdirectory(fixtures)

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
