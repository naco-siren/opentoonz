# Phase 1 spike: a CPU-only Skia from vcpkg, linked into one test binary.
#
# Off by default (WITH_SKIA_SPIKE). Nothing else in the tree links Skia, so
# the existing libraries, tools and tests are built exactly as before.
# See README.md in this directory for how to install Skia and configure.

# Skia comes from one of two places, wrapped in the target nexttoonz_skia:
#
# - NEXTTOONZ_SKIA_PACK_DIR: an unpacked JetBrains skia-pack release (prebuilt
#   static libraries with the headers next to them).
# - otherwise the config package of the vcpkg port, found through
#   CMAKE_PREFIX_PATH (<vcpkg root>/installed/<triplet>) or
#   unofficial-skia_DIR. The vcpkg toolchain file is deliberately not
#   required, because it would redirect every other find_package() in this
#   build to vcpkg as well.
set(NEXTTOONZ_SKIA_PACK_DIR "" CACHE PATH
    "Unpacked JetBrains skia-pack archive to link instead of vcpkg's Skia")

add_library(nexttoonz_skia INTERFACE)
if(NEXTTOONZ_SKIA_PACK_DIR)
    file(GLOB _skia_pack_lib "${NEXTTOONZ_SKIA_PACK_DIR}/out/*/libskia.a")
    if(NOT _skia_pack_lib)
        message(FATAL_ERROR "No out/*/libskia.a under ${NEXTTOONZ_SKIA_PACK_DIR}")
    endif()
    # The archive has no list of the macros Skia was built with. SK_GANESH and
    # SK_METAL are the ones its build script implies (script/build.py).
    target_compile_definitions(nexttoonz_skia INTERFACE SK_GANESH SK_METAL)
    target_include_directories(nexttoonz_skia INTERFACE "${NEXTTOONZ_SKIA_PACK_DIR}")
    target_link_libraries(nexttoonz_skia INTERFACE
        "${_skia_pack_lib}"
        "-framework CoreFoundation"
        "-framework CoreGraphics"
        "-framework CoreText"
        "-framework Foundation"
        "-framework Metal"
    )
else()
    find_package(unofficial-skia CONFIG REQUIRED)
    target_link_libraries(nexttoonz_skia INTERFACE unofficial::skia::skia)
endif()
# Both are release builds. Skia's headers switch to their debug layout when
# NDEBUG is missing, so say which one the library is.
target_compile_definitions(nexttoonz_skia INTERFACE SK_RELEASE)

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
        nexttoonz_skia
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
