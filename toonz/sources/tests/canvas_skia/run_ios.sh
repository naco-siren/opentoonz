#!/bin/bash
# Builds tnzcanvas, tnzskia and the pixel tests of the Skia canvas for
# iPadOS and, for the simulator, runs them (see ../ios_gtest.sh).
#
#   run_ios.sh <simulator|device> <skia dir> <googletest source dir> <work dir>
#
# <skia dir> is an unpacked JetBrains skia-pack archive for the matching
# target: iosSim-Release-x64 on an Intel Mac, iosSim-Release-arm64 on Apple
# Silicon, ios-Release-arm64 for a real iPad.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
sources=$here/../..
skia=$2
lib=$(ls "$skia"/out/*/libskia.a)

exec "$here/../ios_gtest.sh" "$1" "$3" "$4" nexttoonz_canvas_skia_tests -- \
    -DTNZCANVAS_STATIC -DTNZSKIA_STATIC -DSK_RELEASE -DSK_GANESH -DSK_METAL \
    -I"$sources/include" -I"$skia" \
    "$sources"/tnzcanvas/*.cpp "$sources"/tnzskia/*.cpp "$here"/test_*.cpp \
    "$lib" \
    -framework CoreFoundation -framework CoreGraphics -framework CoreText \
    -framework Foundation -framework Metal -framework UIKit
