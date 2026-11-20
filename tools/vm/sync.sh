#!/bin/bash
# Runs inside the VM: brings the clone to origin/<branch> and rebuilds the
# configured build directory (after build.sh has configured it once).
# usage: sync.sh <branch>
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/env.sh"
nt_checkout "${1:?usage: sync.sh <branch>}"
# shellcheck disable=SC2086
ninja -C "$NT_BUILD" -k 0 $NT_TARGETS > "$NT_LOGS/build.log" 2>&1 || { tail -30 "$NT_LOGS/build.log"; exit 1; }
tail -1 "$NT_LOGS/build.log" | cut -c1-120
