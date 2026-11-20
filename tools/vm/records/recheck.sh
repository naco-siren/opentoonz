#!/bin/bash
# Runs inside the nt-asan VM: sync, rebuild, and compare the test pictures
# and the golden renders with the ones recorded before the migration.
set -uo pipefail
export ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 QT_QPA_PLATFORM=offscreen
cd ~/opentoonz && git fetch -q origin && git reset -q --hard origin/wip/phase1-skia-spike || exit 1
git log --oneline -1
cd toonz/build-asan && ninja -k 0 > /tmp/build.log 2>&1; echo "ninja exit $?"; grep -A8 "error:" /tmp/build.log | head -30
tag=${1:-again}
bash ${NT_MAIN:?set NT_MAIN to the main checkout}/toonz/build-phase1/vm/ab_linux.sh $tag ${2:-}
for f in /tmp/ab_before/*.raw; do n=$(basename $f); if cmp -s $f /tmp/ab_$tag/$n; then echo "same     $n"; else echo "DIFFERS  $n $(cmp -l $f /tmp/ab_$tag/$n | wc -l) bytes"; fi; done
if [ -d /tmp/golden_$tag ]; then
  d=0; s=0
  for f in $(cd /tmp/golden_before && find . -name "*.png"); do if cmp -s /tmp/golden_before/$f /tmp/golden_$tag/$f; then s=$((s+1)); else d=$((d+1)); echo "golden frame differs: $f"; fi; done
  echo "golden frames: $s identical files, $d different"
fi
