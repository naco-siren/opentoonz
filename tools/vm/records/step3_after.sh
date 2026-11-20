#!/bin/bash
# Runs inside the nt-asan VM: sync to the branch, rebuild, record pictures as
# <tag> and compare them with the ones recorded before step 3.
set -uo pipefail
tag=${1:-after}
export ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 QT_QPA_PLATFORM=offscreen
cd ~/opentoonz && git fetch -q origin && git reset -q --hard origin/wip/phase1-skia-spike || exit 1
git log --oneline -1
cd toonz/build-asan && cmake . > /tmp/cmake.log 2>&1; ninja -k 0 > /tmp/build.log 2>&1; echo "ninja exit $?"; grep -B2 -A10 "error:\|undefined reference" /tmp/build.log | head -60
here=${NT_MAIN:?set NT_MAIN to the main checkout}/toonz/build-phase1/vm
bash $here/ab2_linux.sh $tag golden
mkdir -p /var/tmp/ab/before2/golden_link 2>/dev/null
echo "--- against before2 (pictures)"; bash $here/ab2_compare.sh before2 $tag | grep -v "^golden"
echo "--- against before (golden)"; bash $here/ab2_compare.sh before $tag | grep "golden"
if [ "${2:-}" = tests ]; then
  export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 LSAN_OPTIONS=suppressions=$HOME/opentoonz/toonz/sources/tests/lsan.supp:print_suppressions=0
  ctest -LE "golden|canvas_gl" -j 7 --timeout 600 > /tmp/ctest_unit.log 2>&1; echo "unit ctest exit $?"
  grep -E "tests passed|\*\*\*Failed" /tmp/ctest_unit.log | head -10
  ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 QT_QPA_PLATFORM=xcb LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a ctest -L canvas_gl --timeout 300 > /tmp/ctest_gl.log 2>&1; echo "gl ctest exit $?"
  grep -E "tests passed|\*\*\*Failed" /tmp/ctest_gl.log | head -10
fi
