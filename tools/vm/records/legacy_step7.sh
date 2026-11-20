#!/bin/bash
# Runs inside the nt-asan VM: renders the scenes of step 7 (the plastic
# deformer and the flow paint brush) with the tcomposer of the tree before
# the migration (toonz/build-legacy, see legacy_step6.sh), twice, into
# /var/tmp/ab/legacy{,_again}/scene_<scene>.
set -uo pipefail
here=${NT_MAIN:?set NT_MAIN to the main checkout}/toonz/build-phase1/vm
for s in plastic_basic fx_sweep_iwa_a; do
  B=$HOME/opentoonz/toonz/build-legacy bash $here/scene_linux.sh legacy $s
  B=$HOME/opentoonz/toonz/build-legacy bash $here/scene_linux.sh legacy_again $s
  diff -r /var/tmp/ab/legacy/scene_$s/out /var/tmp/ab/legacy_again/scene_$s/out > /dev/null && echo "legacy $s twice: same" || echo "legacy $s twice: DIFFERS"
done
