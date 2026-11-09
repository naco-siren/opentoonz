#!/bin/bash
# Builds the Skia spike tests for iPadOS and, for the simulator, runs them.
#
#   run_ios.sh simulator <skia dir> <googletest source dir> <work dir> [device]
#   run_ios.sh device    <skia dir> <googletest source dir> <work dir>
#
# <skia dir> is an unpacked JetBrains skia-pack archive for the matching
# target (iosSim-Release-x64 on an Intel Mac, iosSim-Release-arm64 on Apple
# Silicon, ios-Release-arm64 for a real iPad). "device" only compiles and
# links: running on a real iPad needs an app bundle and code signing.
# [device] is the name or UDID of a simulator, by default the first iPad
# that `xcrun simctl list devices available` shows.
#
# The test that draws into a TRaster is left out, because tnzcore links Qt
# and is not built for iPadOS yet.
set -euo pipefail

mode=$1
skia=$2
gtest=$3
work=$4
here=$(cd "$(dirname "$0")" && pwd)

case "$mode" in
  simulator) sdk=iphonesimulator; arch=$(uname -m); min=-mios-simulator-version-min=15.0 ;;
  device)    sdk=iphoneos;        arch=arm64;       min=-miphoneos-version-min=15.0 ;;
  *) echo "first argument must be simulator or device" >&2; exit 2 ;;
esac

lib=$(ls "$skia"/out/*/libskia.a)
mkdir -p "$work"
cxx="xcrun --sdk $sdk clang++ -arch $arch $min -std=c++17 -O2 -DNDEBUG"

$cxx -I"$gtest/googletest/include" -I"$gtest/googletest" \
    -c "$gtest/googletest/src/gtest-all.cc" -o "$work/gtest-all.o"
$cxx -I"$gtest/googletest/include" \
    -c "$gtest/googletest/src/gtest_main.cc" -o "$work/gtest_main.o"
$cxx -DSK_RELEASE -DSK_GANESH -DSK_METAL -DNEXTTOONZ_SKIA_SPIKE_NO_TNZCORE \
    -I"$skia" -I"$gtest/googletest/include" \
    -c "$here/test_skia_raster.cpp" -o "$work/test_skia_raster.o"
$cxx "$work/test_skia_raster.o" "$work/gtest-all.o" "$work/gtest_main.o" "$lib" \
    -framework CoreFoundation -framework CoreGraphics -framework CoreText \
    -framework Foundation -framework Metal -framework UIKit \
    -o "$work/nexttoonz_skia_spike_tests_$mode"
echo "built $work/nexttoonz_skia_spike_tests_$mode"

if [ "$mode" = simulator ]; then
  device=${5:-$(xcrun simctl list devices available | grep -m1 iPad |
                sed -E 's/.*\(([0-9A-F-]{36})\).*/\1/')}
  xcrun simctl boot "$device" 2>/dev/null || true
  xcrun simctl spawn "$device" "$work/nexttoonz_skia_spike_tests_$mode"
fi
