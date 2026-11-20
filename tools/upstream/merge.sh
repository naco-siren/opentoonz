#!/usr/bin/env bash
# Merges upstream OpenToonz into the current branch with the rename settings
# fixed by doc/nexttoonz_layout.md (section 3.1 and risk list): no rename limit
# and a 25% rename threshold. Measured on upstream #6919 over a moved tree, the
# default threshold turned 7 files into modify/delete conflicts, 25% left 2.
#
# The merge stops before committing, so the "file location" hints (an upstream
# file added in a directory we moved; git has already put it at the new place)
# can be confirmed first. Output goes to local/runs/upstream-merge/.
#
# Usage: tools/upstream/merge.sh [--dry-run] [<ref>]
#   <ref>      what to merge (default: upstream/master, fetched first)
#   --dry-run  no change to the work tree: git merge-tree with the same
#              settings, and the same summary
set -euo pipefail

dry=0
ref=
for a in "$@"; do
    case "$a" in
        --dry-run) dry=1 ;;
        -h|--help) sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        -*) echo "unknown option: $a" >&2; exit 2 ;;
        *) ref=$a ;;
    esac
done
ref=${ref:-upstream/master}

top=$(git rev-parse --show-toplevel)
cd "$top"
common=$(git rev-parse --path-format=absolute --git-common-dir)
runs=${NEXTTOONZ_LOCAL:-$(dirname "$common")/local}/runs/upstream-merge
out=$runs/$(date +%Y-%m-%d_%H%M%S)_$(echo "$ref" | tr '/' '-')
mkdir -p "$out"

case "$ref" in
    upstream/*) git fetch -q upstream "${ref#upstream/}" ;;
esac
theirs=$(git rev-parse --verify "$ref^{commit}")
ours=$(git rev-parse HEAD)
echo "ours   $(git log -1 --format='%h %s' "$ours")"
echo "theirs $(git log -1 --format='%h %s' "$theirs") ($ref)"
echo "merge base $(git merge-base "$ours" "$theirs" | cut -c1-9), $(git rev-list --count --no-merges "$ours..$theirs") upstream commits"

opts=(-c merge.renameLimit=0 -c diff.renameLimit=0)
if [ "$dry" = 1 ]; then
    git "${opts[@]}" merge-tree --write-tree --name-only -X find-renames=25% "$ours" "$theirs" \
        > "$out/merge.log" 2>&1 && rc=0 || rc=$?
else
    if [ -n "$(git status --porcelain --untracked-files=no)" ]; then
        echo "the work tree has changes; commit or set them aside first" >&2
        exit 1
    fi
    git "${opts[@]}" merge --no-ff --no-commit -X find-renames=25% "$theirs" \
        > "$out/merge.log" 2>&1 && rc=0 || rc=$?
fi

echo
echo "conflicts and hints by kind:"
grep -oE '^CONFLICT \([^)]*\)' "$out/merge.log" | sort | uniq -c | sed 's/^/  /' || echo "  none"
grep -E '^CONFLICT \(file location\)' "$out/merge.log" > "$out/file-location.txt" || true
n=$(wc -l < "$out/file-location.txt" | tr -d ' ')
if [ "$n" != 0 ]; then
    echo
    echo "$n new upstream files were placed under moved directories; confirm each"
    echo "(list: $out/file-location.txt), e.g.:"
    head -5 "$out/file-location.txt" | cut -c1-200 | sed 's/^/  /'
fi
echo
echo "log: $out/merge.log"
if [ "$dry" = 1 ]; then
    echo "dry run: exit $rc (0 = merges cleanly)"
else
    echo "merge stopped before the commit (exit $rc); review, then 'git commit' or 'git merge --abort'"
fi
