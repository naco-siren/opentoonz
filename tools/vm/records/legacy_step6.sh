#!/bin/bash
# Runs inside the nt-asan VM: builds tcomposer and the libraries of the tree
# before the canvas migration in toonz/build-legacy, and records with them
# the new scene and the functions of step 6; then puts the clone back on
# the branch.
set -uo pipefail
here=${NT_MAIN:?set NT_MAIN to the main checkout}/toonz/build-phase1/vm
cd ~/opentoonz && git fetch -q origin && git checkout -q --detach origin/nexttoonz/phase0 || exit 1
git log --oneline -1
(cd toonz/build-legacy && ninja tcomposer toonzlib image tnzstdfx colorfx tnzext tnzbase tnzcore > /tmp/build_legacy.log 2>&1; echo "ninja exit $?"; grep -B2 -A8 "error:" /tmp/build_legacy.log | head -20)
B=$HOME/opentoonz/toonz/build-legacy bash $here/scene_linux.sh legacy vector_cmapped_fx
B=$HOME/opentoonz/toonz/build-legacy bash $here/scene_linux.sh legacy_again vector_cmapped_fx
B=$HOME/opentoonz/toonz/build-legacy bash $here/offscreen_linux.sh legacy
cd ~/opentoonz && git checkout -q wip/phase1-skia-spike && git log --oneline -1
diff -r /var/tmp/ab/legacy/scene_vector_cmapped_fx/out /var/tmp/ab/legacy_again/scene_vector_cmapped_fx/out > /dev/null && echo "legacy scene twice: same" || echo "legacy scene twice: DIFFERS"
