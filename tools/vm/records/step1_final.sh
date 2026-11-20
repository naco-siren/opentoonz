#!/bin/bash
# Runs inside the nt-asan VM: the final state of step 1, built and tested.
set -uo pipefail
export ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 QT_QPA_PLATFORM=offscreen
cd ~/opentoonz || exit 1
git fetch -q origin && git reset -q --hard origin/wip/phase1-skia-spike || exit 1
git log --oneline -1
cd toonz/build-asan || exit 1
ninja -k 0 nexttoonz_canvas_tests nexttoonz_canvas_skia_tests nexttoonz_canvas_gl_tests nexttoonz_skia_spike_tests nexttoonz_core_tests nexttoonz_toonzlib_tests tcomposer lzocompress lzodecompress > /tmp/build.log 2>&1
echo "ninja exit $?"; tail -1 /tmp/build.log | cut -c1-120
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ctest -R "^canvas\.|^canvas_skia\.|^skia_spike\.|^core\." -j 7 --timeout 300 > /tmp/ctest1.log 2>&1; echo "ctest canvas/skia/core exit $?"
grep -E "tests passed|Failed|\*\*\*" /tmp/ctest1.log | head -10
./bin/nexttoonz_canvas_gl_tests 2>&1 | tail -3
QT_QPA_PLATFORM=xcb LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a ctest -R "^canvas_gl\." --timeout 300 > /tmp/ctest2.log 2>&1; echo "ctest gl under xvfb exit $?"
grep -E "tests passed|Failed|\*\*\*" /tmp/ctest2.log | head -10
