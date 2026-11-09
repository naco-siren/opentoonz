#!/bin/bash
# Builds the Skia spike tests for iPadOS and, for the simulator, runs them
# (see ../ios_gtest.sh).
#
#   run_ios.sh <simulator|device> <skia dir> <googletest source dir> <work dir>
#
# <skia dir> is an unpacked JetBrains skia-pack archive for the matching
# target: iosSim-Release-x64 on an Intel Mac, iosSim-Release-arm64 on Apple
# Silicon, ios-Release-arm64 for a real iPad.
#
# The test that draws into a TRaster is left out, because tnzcore links Qt
# and is not built for iPadOS yet.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
skia=$2
lib=$(ls "$skia"/out/*/libskia.a)

exec "$here/../ios_gtest.sh" "$1" "$3" "$4" nexttoonz_skia_spike_tests -- \
    -DSK_RELEASE -DSK_GANESH -DSK_METAL -DNEXTTOONZ_SKIA_SPIKE_NO_TNZCORE \
    -I"$skia" "$here/test_skia_raster.cpp" "$lib" \
    -framework CoreFoundation -framework CoreGraphics -framework CoreText \
    -framework Foundation -framework Metal -framework UIKit
