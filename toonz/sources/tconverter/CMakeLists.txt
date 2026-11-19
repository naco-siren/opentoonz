add_executable(tconverter
    tconverter.cpp
)

target_link_libraries(tconverter
    Qt5::Core
    toonzlib
    image
)

# The desktop OpenGL drawing (tgl.h, tvectorgl.h, tofflinegl.h, ...): in
# tnzglview since step 8c of doc/nexttoonz_core_noqt.md.
target_link_libraries(tconverter tnzglview)
