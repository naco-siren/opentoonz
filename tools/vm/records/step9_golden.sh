#!/bin/bash
# Runs inside the nt-asan VM for step 9: syncs to the branch, rebuilds, and
# renders every golden scene with the default (Skia) and with
# NEXTTOONZ_VECTOR_RASTERIZER=gl, into /var/tmp/ab/s9/{skia,gl}/<scene>/out.
set -uo pipefail
export ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1
cd ~/opentoonz && git fetch -q origin && git reset -q --hard origin/wip/phase1-skia-spike || exit 1
git log --oneline -1
cd toonz/build-asan && cmake . > /tmp/cmake.log 2>&1; ninja -k 0 > /tmp/build.log 2>&1; echo "ninja exit $?"; grep -B2 -A10 "error:" /tmp/build.log | head -40
for mode in skia gl; do
  if [ $mode = gl ]; then export NEXTTOONZ_VECTOR_RASTERIZER=gl; else unset NEXTTOONZ_VECTOR_RASTERIZER; fi
  QT_QPA_PLATFORM=offscreen ctest -L golden -j 7 --timeout 900 > /tmp/golden_$mode.log 2>&1; echo "golden $mode ctest exit $?"
  grep -E "tests passed|\(Failed\)" /tmp/golden_$mode.log
  rm -rf /var/tmp/ab/s9/$mode; mkdir -p /var/tmp/ab/s9/$mode
  for d in golden/*/; do n=$(basename $d); mkdir -p /var/tmp/ab/s9/$mode/$n; cp -r $d/out /var/tmp/ab/s9/$mode/$n/; done
done
unset NEXTTOONZ_VECTOR_RASTERIZER
echo "--- gl against before (golden, the renders of before step 2)"
for d in /var/tmp/ab/s9/gl/*/; do n=$(basename $d); for f in $d/out/*.png; do
  cmp -s $f /var/tmp/ab/before/golden/$n/out/$(basename $f) && echo "same" || echo "DIFFERS $n"; done; done | sort | uniq -c
