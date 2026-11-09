#!/bin/bash
# Builds tnzcanvas and its tests for iPadOS and, for the simulator, runs
# them (see ../ios_gtest.sh).
#
#   run_ios.sh <simulator|device> <googletest source dir> <work dir>
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
sources=$here/../..

exec "$here/../ios_gtest.sh" "$1" "$2" "$3" nexttoonz_canvas_tests -- \
    -DTNZCANVAS_STATIC -I"$sources/include" \
    "$sources"/tnzcanvas/*.cpp "$here"/test_*.cpp
