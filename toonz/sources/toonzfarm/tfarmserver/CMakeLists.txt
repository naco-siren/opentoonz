include_directories(
    ../include
)

add_executable(tfarmserver
    tfarmserver.cpp
)

target_link_libraries(tfarmserver
    Qt5::Core
    tfarm
    # fromStr() on a QString (tconvert.h)
    nexttoonz_qt_interop
)
