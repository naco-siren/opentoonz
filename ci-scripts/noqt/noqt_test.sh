#!/usr/bin/env bash
# Configures, builds and tests the core without Qt (WITH_QT=OFF; see
# doc/nexttoonz_core_noqt.md and toonz/sources/tests/README.md), checks that
# nothing in that build reached Qt, and, given a GoogleTest source tree,
# runs the core tests in the iPad simulator as well.
#
# Usage: noqt_test.sh <build dir> <googletest source dir or -> \
#            <iOS skia dir or -> [cmake args...]
#   Run from the repository root. The third-party libraries the full build
#   needs must be installed (the configuration still finds them), but not Qt.
#   "-" for the GoogleTest sources skips the iPad simulator. The iOS skia dir
#   is an unpacked skia-pack for the simulator (ci-scripts/skia/skia_pack.sh
#   iosSim-x64): since step 8d the core draws vector images with Skia there
#   too.

set -euo pipefail

if [ $# -lt 3 ]; then
    echo "usage: $0 <build dir> <googletest source dir or -> <iOS skia dir or -> [cmake args...]" >&2
    exit 2
fi
build="$1"
gtest_src="$2"
ios_skia="$3"
shift 3
root="$(pwd)"

cmake -S toonz/sources -B "$build" -G Ninja -DWITH_QT=OFF "$@"
cmake --build "$build"

echo "--- nothing reached Qt"
# The compile commands name no Qt directory or macro...
if grep -qiE 'qt5|qt@5|/qt6|QT_[A-Z]+_LIB' "$build/compile_commands.json"; then
    echo "compile_commands.json mentions Qt" >&2
    exit 1
fi
# ...no object file depends on a Qt header, wherever it was found (a Qt in a
# default include directory would not show in the compile commands)...
if ninja -C "$build" -t deps | grep -E '/Qt[A-Za-z]*\.framework/|/qt5/|/qt6/|/include/Qt[A-Z][A-Za-z]*/'; then
    echo "a source of the no-Qt build includes a Qt header" >&2
    exit 1
fi
# ...and no library of it links Qt.
libs=$(find "$build" \( -name 'libtnz*.dylib' -o -name 'libtnz*.so' \
    -o -name 'libcolorfx.*' -o -name 'libimage.*' \) -type f)
for lib in $libs; do
    if { otool -L "$lib" 2>/dev/null || ldd "$lib"; } | grep -E 'Qt[A-Za-z]*\.framework|libQt'; then
        echo "$lib links Qt" >&2
        exit 1
    fi
done
echo "ok: $(echo $libs | wc -w | tr -d ' ') libraries, no Qt"

echo "--- tests"
(cd "$build" && ctest --output-on-failure --no-tests=error --timeout 300)

if [ "$gtest_src" != "-" ]; then
    if [ "$ios_skia" = "-" ]; then
        echo "the iPad simulator needs the iOS skia dir" >&2
        exit 2
    fi
    echo "--- the core tests in the iPad simulator"
    NEXTTOONZ_BOOST_INCLUDE=$(sed -n 's/^Boost_INCLUDE_DIR:PATH=//p' "$build/CMakeCache.txt") \
        "$root/toonz/sources/tests/core/run_ios_noqt.sh" simulator "$gtest_src" "$build/ios" "$ios_skia"
fi
