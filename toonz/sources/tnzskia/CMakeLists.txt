# tnzskia: the canvas backend that rasterises on the CPU with Skia
# (doc/nexttoonz_canvas_ir.md, doc/nexttoonz_phase1_skia.md).
#
# The only code of the tree that includes Skia's headers. Like tnzcanvas it
# has to build for iPadOS today: no Qt, no OpenGL, no tnzcore, which
# tests/canvas_skia checks. Built only with WITH_SKIA, because Skia is not
# yet something every build of the tree can be asked to provide.

#-----------------------------------------------------------------------------
# Skia itself, as the target nexttoonz_skia. It comes from one of two places:
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

# The Skia of vcpkg is built without RTTI. UBSan's vptr check (part of the
# asan preset) wants the typeinfo of every class whose virtual functions are
# called, so code that calls into Skia cannot be compiled with it: the link
# fails with "undefined reference to typeinfo for SkCanvas". The flag is a
# no-op when the sanitizer is off.
if(NOT MSVC)
    target_compile_options(nexttoonz_skia INTERFACE -fno-sanitize=vptr)
endif()

#-----------------------------------------------------------------------------

set(HEADERS
    ../include/canvas/tskiacanvas.h
)

set(SOURCES
    tskiacanvas.cpp
)

add_library(tnzskia SHARED ${HEADERS} ${SOURCES})

target_compile_definitions(tnzskia PRIVATE TNZSKIA_EXPORTS)

# No C++ standard override: the Skia headers used here compile under the
# tree's C++17 (milestones 144 and 148). Skia builds itself as C++20, so its
# headers may come to require it; only this library would have to move.

target_link_libraries(tnzskia
    PUBLIC
        tnzcanvas
    PRIVATE
        nexttoonz_skia
)

if(BUILD_ENV_APPLE)
    # Like the other libraries of the tree: found next to the executable.
    set_target_properties(tnzskia PROPERTIES
        INSTALL_NAME_DIR "@executable_path"
        BUILD_WITH_INSTALL_NAME_DIR ON
    )
endif()
