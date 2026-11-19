#!/bin/bash
# Builds the part of the core that is off Qt, with the core tests that need
# no more, for iPadOS and, for the simulator, runs them (see ../ios_gtest.sh).
# The file lists are read from the CMake lists the WITH_QT=OFF build uses
# (TNZCORE_NOQT_SOURCES in tnzcore/CMakeLists.txt, IMAGE_NOQT_SOURCES in
# image/CMakeLists.txt and NEXTTOONZ_CORE_NOQT_TEST_SOURCES in
# tests/CMakeLists.txt), so this builds what that build does. Since step 8d
# of doc/nexttoonz_core_noqt.md that includes the vector images, drawn by
# Skia: tnzcanvas, tnzskia, colorfx, the pli reader and LZ4 come along.
#
#   run_ios_noqt.sh <simulator|device> <googletest source dir> <work dir> \
#       <skia dir>
#
# <skia dir> is an unpacked JetBrains skia-pack archive for the matching
# target (iosSim-Release-x64 on an Intel Mac, iosSim-Release-arm64 on Apple
# Silicon, ios-Release-arm64 for a real iPad; ci-scripts/skia/skia_pack.sh).
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
sources=$here/../..
mode=$1
gtest=$2
work=$3
skia=$4
lib=$(ls "$skia"/out/*/libskia.a)
# Boost's headers (header only here): where the build found them, given by
# noqt_test.sh, or Homebrew's. Searched last, as the folder may hold others.
boost=${NEXTTOONZ_BOOST_INCLUDE:-$(brew --prefix boost 2>/dev/null)/include}

# The entries of a set(<name> ...) block of a CMakeLists.txt, one per line.
cmake_list() {
  sed -n "/^set($2\$/,/^)/p" "$1" | sed '1d;$d' | sed 's/^ *//'
}

files=()
while read -r f; do files+=("$sources/tnzcore/$f"); done \
  < <(cmake_list "$sources/tnzcore/CMakeLists.txt" TNZCORE_NOQT_SOURCES)
while read -r f; do files+=("$sources/image/$f"); done \
  < <(cmake_list "$sources/image/CMakeLists.txt" IMAGE_NOQT_SOURCES)
while read -r f; do files+=("${f/\$\{CMAKE_CURRENT_SOURCE_DIR\}/$sources/tests}"); done \
  < <(cmake_list "$sources/tests/CMakeLists.txt" NEXTTOONZ_CORE_NOQT_TEST_SOURCES)
files+=("$sources"/colorfx/colorfx.cpp "$sources"/colorfx/colorfxutils.cpp
        "$sources"/colorfx/rasterstyles.cpp "$sources"/colorfx/regionstyles.cpp
        "$sources"/colorfx/strokestyles.cpp
        "$sources"/colorfx/flowlinestrokestyle.cpp
        "$sources"/tnzcanvas/*.cpp "$sources"/tnzskia/*.cpp)

# LZ4 (the image cache's codec) from the tree, compiled as C.
case "$mode" in
  simulator) sdk=iphonesimulator; arch=$(uname -m); min=-mios-simulator-version-min=15.0 ;;
  device)    sdk=iphoneos;        arch=arm64;       min=-miphoneos-version-min=15.0 ;;
esac
lz4=$sources/../../thirdparty/Lz4/Lz4_131/lib
mkdir -p "$work/$mode/lz4"
for c in lz4 lz4frame lz4hc xxhash; do
  xcrun --sdk "$sdk" clang -arch "$arch" "$min" -O2 -c "$lz4/$c.c" \
      -o "$work/$mode/lz4/$c.o"
done

# The macros of the Apple desktop build (toonz/sources/CMakeLists.txt):
# tnztypes.h picks the integer types by platform, and tmachine.h the order
# of a pixel's channels (BGRM, by i386), which is what Skia draws in.
exec "$here/../ios_gtest.sh" "$mode" "$gtest" "$work" nexttoonz_core_noqt_tests -- \
    -DMACOSX -Di386 -D__MACOS__ -DTNZ_LITTLE_ENDIAN=1 -Dx64 \
    -DTNZCORE_EXPORTS -DTCOLOR_EXPORTS -DTGEOMETRY_EXPORTS -DTVECTORIMAGE_EXPORTS -DTSOUND_EXPORTS -DTSYSTEM_EXPORTS \
    -DTNZCANVAS_STATIC -DTNZSKIA_STATIC -DSK_RELEASE -DSK_GANESH -DSK_METAL \
    -DNEXTTOONZ_WITH_SKIA \
    -DNEXTTOONZ_FIXTURE_DIR="\"$sources/tests/fixtures/reference_project\"" \
    -I"$sources/include" -I"$sources/common/flash" -I"$lz4" -I"$skia" \
    -idirafter "$boost" \
    "${files[@]}" "$work/$mode"/lz4/*.o "$lib" -lz \
    -framework CoreFoundation -framework CoreGraphics -framework CoreText \
    -framework Foundation -framework Metal -framework UIKit
