# Tests against toonzlib and the image library: file-format round trips
# (pli, tlv, png, tpl, tnz) on the committed reference project, and basic
# scene-model operations (TXsheet, TStageObject, FxDag).
#
# toonzlib_test_main.cpp provides main(): a QCoreApplication must exist
# before the libraries are initialised, so gtest_main is not linked.
#
# The binary keeps the default runtime output directory (bin/): loading and
# saving .tlv levels spawns the lzocompress/lzodecompress helpers found next
# to the executable through QCoreApplication::applicationDirPath().

file(GLOB NEXTTOONZ_TOONZLIB_TEST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp
)

add_executable(nexttoonz_toonzlib_tests
    toonzlib_test_main.cpp
    toonzlib_test_env.h
    ${NEXTTOONZ_TOONZLIB_TEST_SOURCES}
)

target_compile_definitions(nexttoonz_toonzlib_tests
    PRIVATE
        NEXTTOONZ_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures/reference_project"
        NEXTTOONZ_STUFF_DIR="${CMAKE_SOURCE_DIR}/../../stuff"
)

target_link_libraries(nexttoonz_toonzlib_tests
    PRIVATE
        toonzlib
        image
        tnzstdfx
        colorfx
        tnzext
        tnzbase
        tnzcore
        Qt5::Core
        Qt5::Gui
        GTest::gtest
)

# lzocompress/lzodecompress must be built for the tlv tests to run.
foreach(_lzo_helper lzocompress lzodecompress)
    if(TARGET ${_lzo_helper})
        add_dependencies(nexttoonz_toonzlib_tests ${_lzo_helper})
    endif()
endforeach()

# Every test is registered as "toonzlib.<Suite>.<Test>" with the label
# "toonzlib", so `ctest -R toonzlib` or `ctest -L toonzlib` selects them.
gtest_discover_tests(nexttoonz_toonzlib_tests
    TEST_PREFIX "toonzlib."
    PROPERTIES LABELS toonzlib
    DISCOVERY_TIMEOUT 60
)
