#!/bin/bash
# Runs inside the nt-asan VM: records the stroke pictures of the tree before
# the canvas migration (headers and libraries of origin/nexttoonz/phase0,
# built by legacy_regions.sh in toonz/build-legacy) as /var/tmp/ab/legacy/strokes,
# then puts the clone back on the branch.
set -uo pipefail
here=${NT_MAIN:?set NT_MAIN to the main checkout}/toonz/build-phase1/vm
cd ~/opentoonz && git fetch -q origin && git checkout -q --detach origin/nexttoonz/phase0 || exit 1
git log --oneline -1
(cd toonz/build-legacy && ninja colorfx > /tmp/build_legacy.log 2>&1; echo "ninja exit $?")
B=$HOME/opentoonz/toonz/build-legacy bash $here/strokes_linux.sh legacy
B=$HOME/opentoonz/toonz/build-legacy bash $here/strokes_linux.sh legacy_again > /dev/null 2>&1
cd ~/opentoonz && git checkout -q wip/phase1-skia-spike && git log --oneline -1
echo "--- legacy twice"; bash $here/ab2_compare.sh legacy legacy_again | grep "^strokes"
