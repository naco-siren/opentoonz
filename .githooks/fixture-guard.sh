#!/bin/sh
# Guard for the test fixtures and baselines of NextToonz.
#
# The reference project and the expected results of the tests are the source
# of truth for every refactor, and opening the project in OpenToonz writes
# backups and scene icons into it. This script refuses changes to them that
# are not marked as intended, and refuses those artifacts outright.
#
#   fixture-guard.sh pre-commit          staged changes: refuse artifacts,
#                                        warn about protected files
#   fixture-guard.sh commit-msg <file>   staged changes: refuse protected files
#                                        unless <file> has the trailer
#   fixture-guard.sh range <rev-list args...>
#                                        every commit rev-list selects (CI)
#   fixture-guard.sh tree <rev>          the whole tree of <rev>: artifacts
#   fixture-guard.sh ci                  range + tree from GitHub Actions'
#                                        environment (see the workflow)
#
# A change to a protected path is intended when the commit message has a line
#     Fixtures-Update: <reason>
# below its subject. In `range`, a merge commit with that line also covers
# the commits it brings in, and FIXTURE_GUARD_APPROVED=1 (the PR label in CI)
# covers everything. Nothing covers an artifact.
#
# POSIX sh; git is the only tool it needs besides sed and grep.

set -u

TRAILER_KEY='Fixtures-Update'

# Paths whose changes need the trailer. Keep toonz/sources/tests/README.md
# ("Protected fixtures") in step with this list.
is_protected() {
    case $1 in
    toonz/sources/tests/fixtures/reference_project/*) return 0 ;;
    toonz/sources/tests/golden/expected/*) return 0 ;;
    # Checksum tables of tests (test_vector_noqt_hashes_<platform>.inc)
    toonz/sources/tests/*_hashes*.inc) return 0 ;;
    esac
    return 1
}

# Files OpenToonz (or the file manager) leaves in a project it has opened:
# scene and level backups (<file>.bak, <file>.bak<N>, toonzscene.cpp), scene
# icons, Finder's folder settings. Refused under the protected folders.
is_artifact() {
    case $1 in
    toonz/sources/tests/fixtures/reference_project/* | \
        toonz/sources/tests/golden/expected/*) ;;
    *) return 1 ;;
    esac
    case $1 in
    *.bak | *.bak[0-9]* | */sceneIcons/* | */.DS_Store) return 0 ;;
    esac
    return 1
}

# git quotes unusual file names ("a\tb"); the leading quote is all that is in
# the way of matching their prefix.
unquote() {
    case $1 in
    \"*) printf '%s\n' "${1#\"}" ;;
    *) printf '%s\n' "$1" ;;
    esac
}

# Does the message on stdin (without comments) carry the trailer with a
# reason? Its subject does not count.
has_trailer() {
    sed '1d' |
        grep -i -E "^${TRAILER_KEY}:[[:space:]]*[^[:space:]]" >/dev/null
}

# What the staged changes are compared with: HEAD, or the empty tree before
# the first commit.
staged_base() {
    if git rev-parse --verify -q HEAD >/dev/null; then
        echo HEAD
    else
        git hash-object -t tree /dev/null
    fi
}

# Staged files; a deletion is listed with $1 = all, left out with $1 = kept.
staged_files() {
    if [ "$1" = kept ]; then
        filter=--diff-filter=d
    else
        filter=
    fi
    git -c core.quotePath=false diff --cached --name-only --no-renames \
        $filter "$(staged_base)" --
}

# Read file names on stdin, print those $1 (a predicate above) accepts.
select_files() {
    while IFS= read -r f; do
        p=$(unquote "$f")
        if "$1" "$p"; then
            printf '%s\n' "$f"
        fi
    done
}

list() {
    sed 's/^/    /' >&2
}

report_artifacts() {
    {
        echo
        echo "fixture-guard: these files are artifacts of opening the reference"
        echo "project in OpenToonz (backups, scene icons, .DS_Store). They never"
        echo "belong in the repository, and no trailer or label lets them in:"
    } >&2
    printf '%s\n' "$1" | list
    {
        echo
        echo "Unstage them (git restore --staged <file>) and delete them. Work on"
        echo "a copy of the project instead (toonz/sources/tests/README.md)."
        echo
    } >&2
}

cmd_pre_commit() {
    artifacts=$(staged_files kept | select_files is_artifact)
    if [ -n "$artifacts" ]; then
        report_artifacts "$artifacts"
        return 1
    fi
    protected=$(staged_files all | select_files is_protected)
    if [ -n "$protected" ]; then
        {
            echo "fixture-guard: this commit changes test fixtures or baselines:"
            printf '%s\n' "$protected" | list
            echo "The message needs a line '${TRAILER_KEY}: <reason>' below the"
            echo "subject, or the commit will be refused."
        } >&2
    fi
    return 0
}

cmd_commit_msg() {
    msgfile=$1
    # pre-commit does not run for merges; check the artifacts again here.
    artifacts=$(staged_files kept | select_files is_artifact)
    if [ -n "$artifacts" ]; then
        report_artifacts "$artifacts"
        return 1
    fi
    protected=$(staged_files all | select_files is_protected)
    [ -n "$protected" ] || return 0
    # What follows a scissors line (commit -v) is not part of the message.
    if sed '/^. -* >8 -*$/,$d' "$msgfile" | git stripspace --strip-comments |
        has_trailer; then
        return 0
    fi
    {
        echo
        echo "fixture-guard: commit refused. It changes the test fixtures or"
        echo "baselines, which the tests take as the truth:"
    } >&2
    printf '%s\n' "$protected" | list
    {
        echo
        echo "If this is an accident (OpenToonz opened the reference project, the"
        echo "generator was run over it), unstage the files:"
        echo "    git restore --staged <file>    # and git restore <file>"
        echo
        echo "If it is intended, say why in the message, below the subject:"
        echo "    ${TRAILER_KEY}: <reason>"
        echo "Your message was kept in .git/COMMIT_EDITMSG; reuse it with"
        echo "    git commit -e -F \"\$(git rev-parse --git-path COMMIT_EDITMSG)\""
        echo
    } >&2
    return 1
}

# Files a commit changes. For a merge, the files that differ from every parent
# (what the merge itself did); the merged commits are checked on their own.
commit_files() {
    # $1 = commit, $2 = all | kept
    if [ "$2" = kept ]; then
        filter=--diff-filter=d
    else
        filter=
    fi
    if git rev-parse -q --verify "$1^2" >/dev/null; then
        git -c core.quotePath=false diff-tree -r -c --no-commit-id \
            --name-only $filter "$1"
    else
        git -c core.quotePath=false diff-tree -r --root --no-commit-id \
            --name-only --no-renames $filter "$1"
    fi
}

commit_has_trailer() {
    git log -1 --format=%B "$1" | has_trailer
}

describe() {
    git log -1 --format='%h %s' "$1"
}

cmd_range() {
    tmp=$(mktemp -d "${TMPDIR:-/tmp}/fixture-guard.XXXXXX") || return 2
    trap 'rm -rf "$tmp"' EXIT
    if ! git rev-list "$@" >"$tmp/commits"; then
        echo "fixture-guard: git rev-list $* failed" >&2
        return 2
    fi
    # Commits a merge with the trailer brings in are covered by it.
    : >"$tmp/covered"
    git rev-list --merges "$@" | while IFS= read -r m; do
        if commit_has_trailer "$m"; then
            git rev-list "$m" --not "$m^1" >>"$tmp/covered"
        fi
    done

    status=0
    n=0
    while IFS= read -r c; do
        n=$((n + 1))
        artifacts=$(commit_files "$c" kept | select_files is_artifact)
        if [ -n "$artifacts" ]; then
            {
                echo "fixture-guard: $(describe "$c") adds artifacts:"
                printf '%s\n' "$artifacts" | list
            } >&2
            status=1
        fi
        protected=$(commit_files "$c" all | select_files is_protected)
        [ -n "$protected" ] || continue
        if [ "${FIXTURE_GUARD_APPROVED:-0}" = 1 ] ||
            commit_has_trailer "$c" ||
            grep -q "^$c\$" "$tmp/covered"; then
            {
                echo "fixture-guard: $(describe "$c") updates fixtures (marked as intended):"
                printf '%s\n' "$protected" | list
            } >&2
            continue
        fi
        {
            echo "fixture-guard: $(describe "$c") changes fixtures without '${TRAILER_KEY}:':"
            printf '%s\n' "$protected" | list
        } >&2
        status=1
    done <"$tmp/commits"

    if [ "$status" -ne 0 ]; then
        {
            echo
            echo "Each commit that changes the files above on purpose needs a line"
            echo "'${TRAILER_KEY}: <reason>' in its message (reword it, or put the"
            echo "line in the merge commit that brings it in); on a pull request,"
            echo "the label 'fixtures-update' covers all of its commits. Artifacts"
            echo "are refused either way: remove them from the commits."
        } >&2
    else
        echo "fixture-guard: $n commit(s) checked, no unmarked fixture change, no artifact."
    fi
    return $status
}

cmd_tree() {
    artifacts=$(git -c core.quotePath=false ls-tree -r --name-only "$1" -- \
        toonz/sources/tests/fixtures/reference_project \
        toonz/sources/tests/golden/expected | select_files is_artifact)
    if [ -n "$artifacts" ]; then
        {
            echo "fixture-guard: the tree of $1 contains artifacts:"
            printf '%s\n' "$artifacts" | list
        } >&2
        return 1
    fi
    echo "fixture-guard: no artifact in the tree of $1."
}

is_null_sha() {
    case $1 in
    '' | *[!0]*) return 1 ;;
    esac
    return 0
}

# GitHub Actions: GITHUB_EVENT_NAME, and from the workflow PR_BASE_SHA,
# PR_HEAD_SHA, PR_APPROVED (label present), PUSH_BEFORE, PUSH_AFTER,
# PUSH_REF (refs/heads/...).
cmd_ci() {
    case ${GITHUB_EVENT_NAME:-} in
    pull_request | pull_request_target)
        head=$PR_HEAD_SHA
        base=$(git merge-base "$PR_BASE_SHA" "$PR_HEAD_SHA") || return 2
        [ "${PR_APPROVED:-false}" = true ] && FIXTURE_GUARD_APPROVED=1
        export FIXTURE_GUARD_APPROVED
        set -- "$head" --not "$base"
        ;;
    push)
        head=$PUSH_AFTER
        if ! is_null_sha "$PUSH_BEFORE" &&
            git cat-file -e "$PUSH_BEFORE^{commit}" 2>/dev/null &&
            git merge-base --is-ancestor "$PUSH_BEFORE" "$head"; then
            set -- "$head" --not "$PUSH_BEFORE"
        else
            # A new branch, or a forced push: the commits no other branch has.
            branch=${PUSH_REF#refs/heads/}
            set -- "$head" --not "--exclude=refs/remotes/origin/$branch" \
                --remotes=origin
        fi
        ;;
    *)
        echo "fixture-guard: unknown event '${GITHUB_EVENT_NAME:-}'" >&2
        return 2
        ;;
    esac
    echo "fixture-guard: checking git rev-list $*"
    status=0
    cmd_range "$@" || status=1
    cmd_tree "$head" || status=1
    return $status
}

case ${1:-} in
pre-commit) cmd_pre_commit ;;
commit-msg) cmd_commit_msg "$2" ;;
range)
    shift
    cmd_range "$@"
    ;;
tree) cmd_tree "$2" ;;
ci) cmd_ci ;;
*)
    echo "usage: $0 pre-commit | commit-msg <file> | range <rev-list args> | tree <rev> | ci" >&2
    exit 2
    ;;
esac
