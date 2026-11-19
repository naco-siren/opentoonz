add_executable(tcomposer
    tcomposer.cpp
)

target_link_libraries(tcomposer
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    toonzlib
    tfarm
    tnzstdfx
    sound
    image
    colorfx
    toonzqt
)

# The desktop OpenGL drawing (tgl.h, tvectorgl.h, tofflinegl.h, ...): in
# tnzglview since step 8c of doc/nexttoonz_core_noqt.md.
target_link_libraries(tcomposer tnzglview)
