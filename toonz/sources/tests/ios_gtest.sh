#!/bin/bash
# Builds a GoogleTest binary for iPadOS with Xcode's clang and, for the
# simulator, runs it there. The tree has no CMake build for iPadOS yet; this
# is how the parts that already build for it are tested.
#
#   ios_gtest.sh <simulator|device> <googletest source dir> <work dir> <name> \
#       -- <compiler arguments: flags, sources, libraries>
#
# "simulator" builds for the architecture of this Mac and runs the binary
# with `xcrun simctl spawn` on the simulator named by NEXTTOONZ_IOS_SIMULATOR
# (a name or UDID), by default the first iPad that
# `xcrun simctl list devices available` shows. "device" builds for arm64 and
# stops after linking: running on a real iPad needs an app bundle and code
# signing.
#
# <googletest source dir> can be _deps/googletest-src of a build directory
# that fetched GoogleTest.
set -euo pipefail

mode=$1
gtest=$2
work=$3
name=$4
shift 4
[ "${1:-}" = "--" ] && shift

case "$mode" in
  simulator) sdk=iphonesimulator; arch=$(uname -m); min=-mios-simulator-version-min=15.0 ;;
  device)    sdk=iphoneos;        arch=arm64;       min=-miphoneos-version-min=15.0 ;;
  *) echo "first argument must be simulator or device" >&2; exit 2 ;;
esac

objects=$work/$mode
mkdir -p "$objects"
cxx=(xcrun --sdk "$sdk" clang++ -arch "$arch" "$min" -std=c++17 -O2 -DNDEBUG)

if [ ! -f "$objects/gtest-all.o" ]; then
  "${cxx[@]}" -I"$gtest/googletest/include" -I"$gtest/googletest" \
      -c "$gtest/googletest/src/gtest-all.cc" -o "$objects/gtest-all.o"
  "${cxx[@]}" -I"$gtest/googletest/include" \
      -c "$gtest/googletest/src/gtest_main.cc" -o "$objects/gtest_main.o"
fi

binary=$work/${name}_$mode
"${cxx[@]}" -I"$gtest/googletest/include" "$@" \
    "$objects/gtest-all.o" "$objects/gtest_main.o" -o "$binary"
echo "built $binary"

if [ "$mode" = simulator ]; then
  device=${NEXTTOONZ_IOS_SIMULATOR:-$(xcrun simctl list devices available |
      grep -m1 iPad | sed -E 's/.*\(([0-9A-F-]{36})\).*/\1/')}
  xcrun simctl boot "$device" 2>/dev/null || true
  xcrun simctl spawn "$device" "$binary"
fi
