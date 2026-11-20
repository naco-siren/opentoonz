#!/bin/bash
# Runs inside the nt-asan VM for Phase 2 step 1 (doc/nexttoonz_core_noqt.md):
# syncs the clone to wip/phase2-noqt, builds what the Sanitizers CI job
# builds and runs its Test step (ASan + UBSan + LSan, every test ctest knows),
# then configures and tests the no-Qt build (WITH_QT=OFF) in build-noqt.
set -uo pipefail
cd ~/opentoonz || exit 1
git fetch -q origin && git checkout -q wip/phase2-noqt 2>/dev/null || git checkout -q -b wip/phase2-noqt origin/wip/phase2-noqt
git reset -q --hard origin/wip/phase2-noqt || exit 1
git log --oneline -1

cd toonz/build-asan || exit 1
# Test discovery runs the test binaries at build time; as sync.sh does.
export ASAN_OPTIONS=detect_leaks=0:halt_on_error=1
cmake . > /tmp/cmake.log 2>&1 || { tail -30 /tmp/cmake.log; exit 1; }
targets="nexttoonz_core_tests nexttoonz_toonzlib_tests nexttoonz_canvas_tests nexttoonz_canvas_skia_tests tcomposer lzocompress lzodecompress"
ninja -k 0 > /tmp/build.log 2>&1; echo "ninja (everything) exit $?"
grep -B2 -A10 "error:" /tmp/build.log | head -60

export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1
export LSAN_OPTIONS=suppressions=$HOME/opentoonz/toonz/sources/tests/lsan.supp:print_suppressions=0
export UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1
# canvas_gl is built in this VM but not by CI; it needs a real X server.
ctest --no-tests=error --output-on-failure --timeout 900 -j 7 -LE canvas_gl > /tmp/ctest.log 2>&1; echo "ctest exit $?"
grep -E "tests passed|\(Failed\)|\(SEGFAULT\)|\(Subprocess|Timeout|Not Run" /tmp/ctest.log | head -20
ninja nexttoonz_canvas_gl_tests > /dev/null 2>&1
QT_QPA_PLATFORM=xcb LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a ctest -L canvas_gl --timeout 300 > /tmp/ctest_gl.log 2>&1; echo "ctest canvas_gl under xvfb exit $?"
grep -E "tests passed|\(Failed\)" /tmp/ctest_gl.log

echo "--- no-Qt build"
cd ~/opentoonz/toonz
skia_dir=$(grep -E '^unofficial-skia_DIR' build-asan/CMakeCache.txt | cut -d= -f2)
cmake -S sources -B build-noqt -G Ninja -DWITH_QT=OFF -DWITH_SKIA=ON \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -Dunofficial-skia_DIR="$skia_dir" > /tmp/cmake_noqt.log 2>&1 || { tail -30 /tmp/cmake_noqt.log; exit 1; }
cd build-noqt && ninja > /tmp/build_noqt.log 2>&1; echo "noqt ninja exit $?"
grep -B2 -A10 "error:" /tmp/build_noqt.log | head -40
grep -c 'qt5\|QT_' compile_commands.json
ldd lib/opentoonz/libtnzcore.so 2>/dev/null || ldd $(find . -name 'libtnzcore.so*' | head -1)
ctest --no-tests=error --timeout 300 -j 7 > /tmp/ctest_noqt.log 2>&1; echo "noqt ctest exit $?"
grep -E "tests passed|\(Failed\)" /tmp/ctest_noqt.log
