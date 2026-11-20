#!/bin/bash
set -uo pipefail
export ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1
python3 ${NT_MAIN:?set NT_MAIN to the main checkout}/toonz/build-phase1/vm/experiment.py || exit 1
cd ~/opentoonz/toonz/build-asan && ninja tnzglcanvas > /tmp/build.log 2>&1; echo "ninja exit $?"; grep -A6 "error:" /tmp/build.log | head -20
for x in NONE X1 X2 X3 X4 X5 "X1 X2" "X1 X2 X3 X4 X5"; do
  rm -rf /tmp/ab_x; mkdir -p /tmp/ab_x
  env $(for v in $x; do printf "%s=1 " $v; done) QT_QPA_PLATFORM=xcb LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a /tmp/abbin/ab_try1 /tmp/ab_x >/dev/null 2>&1
  n=$(cmp -l /tmp/ab_before/production_scaled.raw /tmp/ab_x/production_scaled.raw | wc -l)
  echo "$x: production_scaled differs in $n bytes"
done
cd ~/opentoonz && git checkout -q toonz/sources/tnzglcanvas/tglcanvas.cpp
