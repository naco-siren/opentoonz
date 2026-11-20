#!/bin/bash
# Runs inside the nt-asan VM: sync to the branch, rebuild, and compare with
# what ab_linux.sh recorded "before".
set -uo pipefail
export ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 QT_QPA_PLATFORM=offscreen
cd ~/opentoonz || exit 1
git fetch -q origin && git reset -q --hard origin/wip/phase1-skia-spike || exit 1
git log --oneline -1
cd toonz/build-asan || exit 1
cmake . > /tmp/cmake.log 2>&1 || { tail -20 /tmp/cmake.log; exit 1; }
ninja -k 0 > /tmp/build.log 2>&1
echo "ninja exit $?"
grep -B2 -A12 "error:\|undefined reference" /tmp/build.log | head -80
tail -1 /tmp/build.log | cut -c1-140
bash ${NT_MAIN:?set NT_MAIN to the main checkout}/toonz/build-phase1/vm/ab_linux.sh after golden
for f in /tmp/ab_before/*.raw; do n=$(basename $f); if cmp -s $f /tmp/ab_after/$n; then echo "same     $n"; else echo "DIFFERS  $n $(cmp -l $f /tmp/ab_after/$n | wc -l) bytes"; fi; done
python3 - <<'PY'
import os, hashlib
a, b = '/tmp/golden_before', '/tmp/golden_after'
same = diff = 0
for scene in sorted(os.listdir(a)):
    da, db = os.path.join(a, scene, 'out'), os.path.join(b, scene, 'out')
    if not os.path.isdir(da): continue
    for f in sorted(os.listdir(da)):
        pa, pb = os.path.join(da, f), os.path.join(db, f)
        ha = hashlib.md5(open(pa, 'rb').read()).hexdigest()
        hb = hashlib.md5(open(pb, 'rb').read()).hexdigest() if os.path.exists(pb) else None
        if ha == hb: same += 1
        else: diff += 1; print('golden frame differs:', scene, f)
print('golden frames: %d identical files, %d different' % (same, diff))
PY
export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 LSAN_OPTIONS=suppressions=$HOME/opentoonz/toonz/sources/tests/lsan.supp:print_suppressions=0
ctest -LE "golden|canvas_gl" -j 7 --timeout 600 > /tmp/ctest_unit.log 2>&1; echo "unit ctest exit $?"
grep -E "tests passed|\*\*\*Failed" /tmp/ctest_unit.log | head -10
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 QT_QPA_PLATFORM=xcb LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a ctest -L canvas_gl --timeout 300 > /tmp/ctest_gl.log 2>&1; echo "gl ctest exit $?"
grep -E "tests passed|\*\*\*Failed" /tmp/ctest_gl.log | head -10
