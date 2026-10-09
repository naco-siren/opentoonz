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

#-----------------------------------------------------------------------------
# Unit tests against tnzcore

add_executable(nexttoonz_core_tests
    core/test_tgeometry.cpp
    core/test_tfilepath.cpp
    core/test_traster.cpp
)

target_link_libraries(nexttoonz_core_tests
    PRIVATE
        tnzcore
        Qt5::Core
        GTest::gtest
        GTest::gtest_main
)

gtest_discover_tests(nexttoonz_core_tests
    DISCOVERY_TIMEOUT 60
)

#-----------------------------------------------------------------------------
# Golden renders (registered by golden/CMakeLists.txt once fixtures exist)

add_subdirectory(golden)

#-----------------------------------------------------------------------------
# Fixture generator (developer tool, not a test)

add_subdirectory(fixtures)
