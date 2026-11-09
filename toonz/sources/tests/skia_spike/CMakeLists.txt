# Phase 1 spike: one test binary that links Skia and nothing of the canvas
# libraries. It proved how Skia is obtained and linked, and holds the
# measurements the canvas design rests on.
#
# Off by default (WITH_SKIA_SPIKE). Skia is the target nexttoonz_skia, which
# tnzskia/CMakeLists.txt defines (hence WITH_SKIA); see README.md here.

add_executable(nexttoonz_skia_spike_tests
    test_skia_raster.cpp
)

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
