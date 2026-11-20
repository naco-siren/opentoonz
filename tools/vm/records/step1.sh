#!/bin/bash
# Runs inside the nt-asan VM: build the canvas libraries of Phase 1 step 1
# with the sanitizers and run their tests.
set -uo pipefail
export ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 QT_QPA_PLATFORM=offscreen
cd ~/opentoonz || exit 1
git fetch -q origin && git checkout -q wip/phase1-skia-spike && git reset -q --hard origin/wip/phase1-skia-spike || exit 1
git log --oneline -1
cd toonz/build-asan || exit 1
cmake . -DWITH_SKIA=ON -DWITH_SKIA_SPIKE=ON -DWITH_CANVAS_GL_TESTS=ON > /tmp/cmake.log 2>&1 || { tail -30 /tmp/cmake.log; exit 1; }
ninja -k 0 tnzcanvas tnzskia tnzglcanvas nexttoonz_canvas_tests nexttoonz_canvas_skia_tests nexttoonz_canvas_gl_tests nexttoonz_skia_spike_tests nexttoonz_core_tests > /tmp/build.log 2>&1
echo "ninja exit $?"
grep -B2 -A14 "error:\|FAILED\|undefined reference" /tmp/build.log | head -120
tail -1 /tmp/build.log | cut -c1-160
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ctest -R "^canvas\.|^canvas_skia\.|^skia_spike\." --timeout 300 > /tmp/ctest1.log 2>&1; echo "ctest (canvas, skia; leaks on) exit $?"
grep -E "tests passed|Total Test time|Failed|Not Run|\*\*\*" /tmp/ctest1.log | head -30
ctest -R "^canvas_gl\." --timeout 300 > /tmp/ctest2.log 2>&1; echo "ctest (gl) exit $?"
grep -E "tests passed|Total Test time|Failed|Not Run|\*\*\*" /tmp/ctest2.log | head -40
./tests/canvas_skia/nexttoonz_canvas_skia_tests --gtest_filter='*Repeatable*' 2>&1 | grep hash
./tests/canvas_gl/nexttoonz_canvas_gl_tests --gtest_filter='CanvasBackends.*' 2>&1 | grep -E "mean|PASSED|FAILED|Expected|renderer|error" | head -40
