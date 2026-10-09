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
    foreach(_dep ${ARGN})
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
# Tests against toonzlib and the image library (formats, scene model)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/toonzlib/CMakeLists.txt)
    add_subdirectory(toonzlib)
endif()

#-----------------------------------------------------------------------------
# Golden renders (registered by golden/CMakeLists.txt once fixtures exist)

add_subdirectory(golden)

#-----------------------------------------------------------------------------
# Fixture generator (developer tool, not a test)

add_subdirectory(fixtures)
