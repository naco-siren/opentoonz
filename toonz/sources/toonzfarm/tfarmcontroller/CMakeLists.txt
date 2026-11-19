include_directories(
    ../include
)

add_executable(tfarmcontroller
    tfarmcontroller.cpp
)

target_link_libraries(tfarmcontroller
    Qt5::Core
    tfarm
    # fromStr() on a QString (tconvert.h)
    nexttoonz_qt_interop
)
