add_executable(tcleanup
    tcleanupper.cpp
)

target_link_libraries(tcleanup
    Qt5::Core
    Qt5::Widgets
    tfarm
    image
)

# The desktop OpenGL drawing (tgl.h, tvectorgl.h, tofflinegl.h, ...): in
# tnzglview since step 8c of doc/nexttoonz_core_noqt.md.
target_link_libraries(tcleanup tnzglview)
