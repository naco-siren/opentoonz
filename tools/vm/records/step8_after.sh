#!/bin/bash
# Runs inside the nt-asan VM after step 8: everything step7_after.sh checks
# (and the tests), then camera-stand renders of the tree before the
# migration (toonz/build-legacy, built by legacy_step6.sh), of the branch,
# and of the branch with the CPU rasteriser, compared.
set -uo pipefail
tag=${1:-after8}
here=${NT_MAIN:?set NT_MAIN to the main checkout}/toonz/build-phase1/vm
bash $here/step7_after.sh $tag tests
# The tree before the migration reads an uninitialised Shift & Trace status
# in every camera-stand render (fixed on the branch): reported, not fatal
UBSAN_HALT=0 B=$HOME/opentoonz/toonz/build-legacy bash $here/camstand_linux.sh legacy
UBSAN_HALT=0 B=$HOME/opentoonz/toonz/build-legacy bash $here/camstand_linux.sh legacy_again
bash $here/camstand_linux.sh $tag
RASTERIZER=skia bash $here/camstand_linux.sh ${tag}_skia
echo "--- camstand: legacy twice"; diff -r /var/tmp/ab/legacy/camstand /var/tmp/ab/legacy_again/camstand > /dev/null && echo same || echo DIFFERS
echo "--- camstand against legacy"
for f in /var/tmp/ab/legacy/camstand/*.raw; do n=$(basename $f); cmp -s $f /var/tmp/ab/$tag/camstand/$n && echo "same     $n" || echo "DIFFERS  $n"; done
