#!/bin/bash
# Runs inside the VM: the Configure and Build steps of workflow_sanitizers.yml
# (the asan preset with clang and ccache, Skia from vcpkg) on origin/<branch>.
# usage: build.sh <branch>
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/env.sh"
nt_checkout "${1:?usage: build.sh <branch>}"
skia_cmake=$NT_SKIA/x64-linux-release/share/unofficial-skia
if [ ! -d "$skia_cmake" ]; then
    echo "building Skia with vcpkg into $NT_SKIA (once; several minutes)"
    ci-scripts/skia/vcpkg_skia.sh "$NT_SKIA" > "$NT_LOGS/skia.log" 2>&1 || { tail -30 "$NT_LOGS/skia.log"; exit 1; }
fi
cmake -S toonz/sources --preset asan -B "$NT_BUILD" \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    -DWITH_SKIA=ON -Dunofficial-skia_DIR="$skia_cmake" > "$NT_LOGS/configure.log" 2>&1 \
    || { tail -30 "$NT_LOGS/configure.log"; exit 1; }
# shellcheck disable=SC2086
ninja -C "$NT_BUILD" -k 0 $NT_TARGETS > "$NT_LOGS/build.log" 2>&1 || { tail -40 "$NT_LOGS/build.log"; exit 1; }
tail -2 "$NT_LOGS/build.log"
echo "built in $NT_BUILD"
