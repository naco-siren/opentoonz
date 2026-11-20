#!/bin/bash
# Runs inside the nt-asan VM: builds colorfx of the tree as it was before
# the canvas migration (origin/nexttoonz/phase0) in a build directory of its
# own and records the region pictures as /var/tmp/ab/legacy/regions; then
# puts the clone back on the branch.
set -uo pipefail
here=${NT_MAIN:?set NT_MAIN to the main checkout}/toonz/build-phase1/vm
cd ~/opentoonz && git fetch -q origin && git checkout -q --detach origin/nexttoonz/phase0 || exit 1
git log --oneline -1
mkdir -p toonz/build-legacy && cd toonz/build-legacy
S="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake -G Ninja ../sources -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
  "-DCMAKE_C_FLAGS=$S" "-DCMAKE_CXX_FLAGS=$S" -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined -DCMAKE_SHARED_LINKER_FLAGS=-fsanitize=address,undefined \
  -DWITH_CANON=OFF -DWITH_SYSTEM_LZO=ON -DWITH_SYSTEM_SUPERLU=ON -DWITH_TESTS=OFF -DWITH_TRANSLATION=OFF > /tmp/cmake_legacy.log 2>&1
echo "cmake exit $?"; tail -3 /tmp/cmake_legacy.log
ninja colorfx > /tmp/build_legacy.log 2>&1; echo "ninja exit $?"; grep -B2 -A8 "error:" /tmp/build_legacy.log | head -30
B=$HOME/opentoonz/toonz/build-legacy bash $here/regions_linux.sh legacy
cd ~/opentoonz && git checkout -q wip/phase1-skia-spike && git log --oneline -1
for t in before4 after4; do
  echo "--- legacy against $t"; bash $here/ab2_compare.sh legacy $t | grep "^regions"
done
