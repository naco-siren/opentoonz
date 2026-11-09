# Phase 1 spike: a CPU-only Skia from vcpkg, linked into one test binary.
#
# Off by default (WITH_SKIA_SPIKE). Nothing else in the tree links Skia, so
# the existing libraries, tools and tests are built exactly as before.
# See README.md in this directory for how to install Skia and configure.

# The config package the vcpkg port installs. It is found through
# CMAKE_PREFIX_PATH (<vcpkg root>/installed/<triplet>) or unofficial-skia_DIR;
# the vcpkg toolchain file is deliberately not required, because it would
# redirect every other find_package() in this build to vcpkg as well.
find_package(unofficial-skia CONFIG REQUIRED)

add_executable(nexttoonz_skia_spike_tests
    test_skia_raster.cpp
)

# No C++ standard override: with milestone 148 the public headers used here
# compile under the tree's C++17. Skia builds itself as C++20 (gn/skia/
# BUILD.gn), so its headers may start to require it; if they do, only the
# code that includes them has to move, which is one more reason to keep the
# Skia backend in its own library behind the canvas interface.

# Skia is built without RTTI. UBSan's vptr check (part of the asan preset)
# wants the typeinfo of every class whose virtual functions are called, so
# code that calls into Skia cannot be compiled with it: the link fails with
# "undefined reference to typeinfo for SkCanvas". The flag is a no-op when
# the sanitizer is off.
if(NOT MSVC)
    target_compile_options(nexttoonz_skia_spike_tests PRIVATE -fno-sanitize=vptr)
endif()

target_link_libraries(nexttoonz_skia_spike_tests
    PRIVATE
        unofficial::skia::skia
        tnzcore
        Qt5::Core
        GTest::gtest
        GTest::gtest_main
)

nexttoonz_stage_runtime(nexttoonz_skia_spike_tests tnzcore)

gtest_discover_tests(nexttoonz_skia_spike_tests
    TEST_PREFIX "skia_spike."
    PROPERTIES LABELS skia_spike
    DISCOVERY_TIMEOUT 60
)
