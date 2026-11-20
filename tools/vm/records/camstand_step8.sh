#!/bin/bash
# Runs inside the nt-asan VM: only the camera-stand part of step8_after.sh.
set -uo pipefail
tag=${1:-after8}
here=${NT_MAIN:?set NT_MAIN to the main checkout}/toonz/build-phase1/vm
UBSAN_HALT=0 B=$HOME/opentoonz/toonz/build-legacy bash $here/camstand_linux.sh legacy 2>&1 | grep -v "runtime error"
UBSAN_HALT=0 B=$HOME/opentoonz/toonz/build-legacy bash $here/camstand_linux.sh legacy_again 2>&1 | grep -v "runtime error"
bash $here/camstand_linux.sh $tag
RASTERIZER=skia bash $here/camstand_linux.sh ${tag}_skia
echo "--- camstand: legacy twice"; diff -r /var/tmp/ab/legacy/camstand /var/tmp/ab/legacy_again/camstand > /dev/null && echo same || echo DIFFERS
echo "--- camstand against legacy"
for f in /var/tmp/ab/legacy/camstand/*.raw; do n=$(basename $f); cmp -s $f /var/tmp/ab/$tag/camstand/$n && echo "same     $n" || echo "DIFFERS  $n"; done
