#!/bin/bash
# Runs inside the nt-asan VM: renders scenes/<scene>.tnz of the branch's
# reference project with the tcomposer of build directory $B into
# /var/tmp/ab/<tag>/scene_<scene>.
set -uo pipefail
tag=$1; scene=$2
host=${NT_MAIN:?set NT_MAIN to the main checkout}/toonz/build-phase1/src/toonz/sources
b=${B:-$HOME/opentoonz/toonz/build-asan}
tc=$(find $b -name tcomposer -type f -perm -u+x | head -1)
py=$(grep -m1 "Python3_EXECUTABLE:" $HOME/opentoonz/toonz/build-asan/CMakeCache.txt | cut -d= -f2)
out=/var/tmp/ab/$tag/scene_$scene
rm -rf $out
export ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 QT_QPA_PLATFORM=offscreen
env ${RASTERIZER:+NEXTTOONZ_VECTOR_RASTERIZER=$RASTERIZER} $py $host/tests/golden/run_golden.py --tcomposer $tc --stuff $HOME/opentoonz/stuff \
    --project $host/tests/fixtures/reference_project --scene scenes/$scene.tnz --expected /nonexistent --work $out --xvfb > /tmp/scene_$tag.log 2>&1
echo "scene $scene: $(ls $out/out 2>/dev/null | wc -l) frames ($tc)"
