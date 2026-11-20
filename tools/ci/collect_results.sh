#!/usr/bin/env bash
# Gathers a CI job's machine-readable results into one directory, the
# "results bundle" of doc/nexttoonz_cli_ci.md, section 4.3, which the job
# uploads as the artifact results-<platform>-<sha> on every run:
#
#   junit/ctest.xml      written by `ctest --output-junit` directly
#   golden/<scene>.json  run_golden.py's outcome per scene (nexttoonz.golden-scene/1)
#   renders/<scene>/     every golden render of this run (about 6 MB)
#   diff/<scene>/        the diff images of the frames that failed
#   sheets/overview.png  every rendered frame, small, one row per scene
#   sheets/fx/           the fx sweep contact sheets (tests/golden/contact_sheet.py)
#   ccache.txt           `ccache -s` of this run, when the job uses ccache
#
# Usage: collect_results.sh <build dir> <results dir>
# Run from the repository root. Missing inputs are not an error: a job that
# failed before its tests still uploads what there is. The sheets need a
# python3 with numpy and Pillow (the golden tests' own); without one, or if
# drawing fails, the job goes on without them.

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

# The renders, as a tree with one folder of frames per scene, and the diffs
for out in "$build"/golden/*/out; do
    [ -d "$out" ] || continue
    scene="$(basename "$(dirname "$out")")"
    if compgen -G "$out/*.png" >/dev/null; then
        mkdir -p "$results/renders/$scene"
        cp "$out"/*.png "$results/renders/$scene/"
    fi
    if compgen -G "$out/../diff/*.png" >/dev/null; then
        mkdir -p "$results/diff/$scene"
        cp "$out"/../diff/*.png "$results/diff/$scene/"
    fi
done

# Sheets: looked at on every run, not only when something failed (Q3 of
# doc/nexttoonz_cli_ci.md)
if [ -d "$results/renders" ]; then
    mkdir -p "$results/sheets"
    root="$(pwd)"
    tests="$root/toonz/sources/tests"
    [ -d "$root/tests/golden" ] && tests="$root/tests"
    python3 "$root/tools/nt/nexttoonz" sheet "$results/renders" --out "$results/sheets/overview.png" \
        --title "${GITHUB_JOB:-local} ${GITHUB_SHA:-}" >/dev/null ||
        echo "collect_results: no overview sheet" >&2
    python3 "$tests/golden/contact_sheet.py" --project "$tests/fixtures/reference_project" \
        --renders "$results/renders" --out "$results/sheets/fx" > "$results/sheets/fx_summary.txt" ||
        echo "collect_results: no fx contact sheets" >&2
fi

if [ -n "${CCACHE_DIR:-}" ] && command -v ccache >/dev/null; then
    ccache -s > "$results/ccache.txt" 2>&1 || true
fi

echo "results in $results:"
find "$results" -type f | sed "s|^$results/|  |" | sort | head -n 50
exit 0
