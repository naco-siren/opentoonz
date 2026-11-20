#!/bin/bash
# Runs inside the nt-asan VM after step 9 (Skia is the default):
# 1. everything step7_after.sh compares, with NEXTTOONZ_VECTOR_RASTERIZER=gl,
#    against the renders from before the migration (the old path must not
#    have changed);
# 2. the unit tests with leak detection and the GL tests;
# 3. the golden tests with the default (Skia) against the new baselines;
# 4. tcomposer with no OpenGL at all (no X server, offscreen platform, no GL
#    vendor library): every scene but the GLSL ones, compared with the
#    golden renders of 3.
set -uo pipefail
here=${NT_MAIN:?set NT_MAIN to the main checkout}/toonz/build-phase1/vm
NEXTTOONZ_VECTOR_RASTERIZER=gl bash $here/step7_after.sh after9gl
cd ~/opentoonz/toonz/build-asan
export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 LSAN_OPTIONS=suppressions=$HOME/opentoonz/toonz/sources/tests/lsan.supp:print_suppressions=0 QT_QPA_PLATFORM=offscreen
ctest -LE "golden|canvas_gl" -j 7 --timeout 600 > /tmp/ctest_unit.log 2>&1; echo "unit ctest exit $?"
grep -E "tests passed|\*\*\*Failed" /tmp/ctest_unit.log | head -10
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 QT_QPA_PLATFORM=xcb LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a ctest -L canvas_gl --timeout 300 > /tmp/ctest_gl.log 2>&1; echo "gl ctest exit $?"
grep -E "tests passed|\*\*\*Failed" /tmp/ctest_gl.log | head -10
export ASAN_OPTIONS=detect_leaks=0:halt_on_error=1
ctest -L golden -j 7 --timeout 900 > /tmp/golden9.log 2>&1; echo "golden (skia) ctest exit $?"
grep -E "tests passed|\(Failed\)" /tmp/golden9.log
echo "--- tcomposer with no OpenGL"
src=$HOME/opentoonz/toonz/sources
rm -rf /var/tmp/ab/s9/nogl; mkdir -p /var/tmp/ab/s9/nogl
for t in $src/tests/fixtures/reference_project/scenes/*.tnz; do
  s=$(basename $t .tnz)
  env -u DISPLAY -u WAYLAND_DISPLAY QT_QPA_PLATFORM=offscreen LIBGL_DRIVERS_PATH=/nonexistent \
      __GLX_VENDOR_LIBRARY_NAME=none __EGL_VENDOR_LIBRARY_FILENAMES=/dev/null \
      python3 $src/tests/golden/run_golden.py --tcomposer bin/tcomposer --stuff $HOME/opentoonz/stuff \
      --project $src/tests/fixtures/reference_project --scene scenes/$s.tnz \
      --expected $src/tests/golden/expected/$s --work /var/tmp/ab/s9/nogl/$s --timeout 900 > /var/tmp/ab/s9/nogl/$s.log 2>&1
  rc=$?
  same=0; differ=0
  for f in /var/tmp/ab/s9/nogl/$s/out/*.png; do
    [ -f "$f" ] || continue
    if cmp -s $f golden/$s/out/$(basename $f); then same=$((same+1)); else differ=$((differ+1)); fi
  done
  echo "$s: run_golden exit $rc, frames same as with GL available $same, different $differ"
done
