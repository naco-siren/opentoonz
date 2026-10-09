# Generates tests/fixtures/reference_project, the scene set that the golden
# tests render. The generated project is committed; this tool exists so it can
# be regenerated deterministically and so that scene construction through the
# toonzlib API is itself exercised.
#
#   nexttoonz_gen_fixtures <stuff dir> <output project dir>
#
# Example:
#   ./bin/nexttoonz_gen_fixtures ../../stuff ../sources/tests/fixtures/reference_project

add_executable(nexttoonz_gen_fixtures
    gen_reference_project.cpp
)

target_link_libraries(nexttoonz_gen_fixtures
    PRIVATE
        Qt5::Core
        Qt5::Gui
        toonzlib
        image
        tnzstdfx
        colorfx
        tnzext
        tnzbase
        tnzcore
)
