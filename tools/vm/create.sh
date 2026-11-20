#!/bin/bash
# Runs on the host: creates the lima VM the scripts here expect (Ubuntu 24.04,
# like the Sanitizers job's runner), with the main checkout mounted read-only
# at the same path, and the long-lived worktrees' directory too when it exists
# (doc/nexttoonz_layout.md 4.10). Then: limactl start "$NT_VM" and
# tools/vm/run.sh provision.sh <branch>.
# usage: tools/vm/create.sh       NT_VM (nt-asan), NT_CPUS (host CPUs - 2),
#                                 NT_MEMORY in GiB (32), NT_DISK in GiB (40)
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
main=$(dirname "$(git -C "$here" rev-parse --path-format=absolute --git-common-dir)")
mounts=(--mount-only "$main")
wt="$(dirname "$main")/opentoonz-wt"
[ -d "$wt" ] && mounts+=(--mount-only "$wt")
cpus=${NT_CPUS:-$(( $(sysctl -n hw.ncpu 2>/dev/null || nproc) - 2 ))}
limactl create --tty=false --name="${NT_VM:-nt-asan}" --vm-type=vz \
    --cpus="$cpus" --memory="${NT_MEMORY:-32}" --disk="${NT_DISK:-40}" \
    "${mounts[@]}" template:ubuntu-24.04
