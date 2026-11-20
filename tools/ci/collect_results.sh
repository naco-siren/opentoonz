#!/usr/bin/env bash
# Gathers a CI job's machine-readable results into one directory, the
# "results bundle" of doc/nexttoonz_cli_ci.md, section 4.3, which the job
# uploads as the artifact results-<platform>-<sha> on every run:
#
#   junit/ctest.xml      written by `ctest --output-junit` directly
#   golden/<scene>.json  run_golden.py's outcome per scene (nexttoonz.golden-scene/1)
#
# Usage: collect_results.sh <build dir> <results dir>
# Run from the repository root. Missing inputs are not an error: a job that
# failed before its tests still uploads what there is.

set -uo pipefail

if [ $# -ne 2 ]; then
    echo "usage: $0 <build dir> <results dir>" >&2
    exit 2
fi
build="$1"
results="$2"
mkdir -p "$results/junit" "$results/golden"

if compgen -G "$build/golden/*.json" >/dev/null; then
    cp "$build"/golden/*.json "$results/golden/"
fi

echo "results in $results:"
find "$results" -type f | sed "s|^$results/|  |" | sort | head -n 50
exit 0
