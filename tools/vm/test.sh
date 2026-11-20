#!/bin/bash
# Runs inside the VM: the Test step of workflow_sanitizers.yml.
# usage: test.sh [<ASAN_OPTIONS> [<LSAN_OPTIONS or -> [<ctest args>...]]]
#   defaults: the job's own (leaks on, the repository's lsan.supp), all tests, -j 7
set -uo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/env.sh"
export ASAN_OPTIONS=${1:-detect_leaks=1:halt_on_error=1}
lsan=${2:-suppressions=$NT_CLONE/toonz/sources/tests/lsan.supp:print_suppressions=0}
if [ "$lsan" != "-" ]; then export LSAN_OPTIONS=$lsan; fi
shift 2 2>/dev/null || shift $#
[ $# -gt 0 ] || set -- -j 7
cd "$NT_BUILD" || exit 1
ctest --no-tests=error --timeout 900 "$@" > "$NT_LOGS/ctest.log" 2>&1
rc=$?
echo "ctest exit $rc"
grep -E "tests passed|Total Test time|\(Failed\)|\(SEGFAULT\)|Timeout" "$NT_LOGS/ctest.log" | head -20
exit $rc
