#!/bin/bash
# Runs on the host: one of the scripts in this directory inside the lima VM.
# The VM sees the main checkout at the same path (create.sh mounts it), so the
# script runs from this checkout; NT_* variables set here are passed through.
# usage: tools/vm/run.sh <script> [args...]     e.g. tools/vm/run.sh build.sh my-branch
#   NT_VM  the lima instance (default nt-asan)
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
script=${1:?usage: run.sh <script> [args...]}; shift
[ -f "$here/$script" ] || script=${script#tools/vm/}
[ -f "$here/$script" ] || { echo "no such script: $here/$script" >&2; exit 2; }
vm=${NT_VM:-nt-asan}
limactl list --format '{{.Status}}' "$vm" 2>/dev/null | grep -qx Running \
    || { echo "VM $vm is not running: limactl start $vm" >&2; exit 1; }
env=()
for v in $(compgen -e | grep '^NT_' | grep -vx NT_VM || true); do env+=("$v=${!v}"); done
exec limactl shell "$vm" env ${env[@]+"${env[@]}"} bash "$here/$script" "$@"
