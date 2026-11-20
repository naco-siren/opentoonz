#!/bin/sh
# Tests of the fixture guard (.githooks/, tools/setup-hooks.sh): builds a
# throwaway repository with the hooks of this checkout, installs them the way
# a developer does, and commits into it.
#
#   sh toonz/sources/tests/hooks/test_fixture_guard.sh
#
# Needs git; uses git-lfs too when it is installed. Touches nothing outside a
# temporary directory (it gets its own HOME, so no global git configuration
# applies). Registered in CTest as hooks.fixture_guard.

set -u

here=$(cd "$(dirname "$0")" && pwd)
src=$(cd "$here/../../../.." && pwd)
[ -f "$src/.githooks/fixture-guard.sh" ] || {
    echo "cannot find .githooks/ above $here" >&2
    exit 2
}

# Nothing of the caller's repository or configuration may leak in.
unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_OBJECT_DIRECTORY \
    GIT_ALTERNATE_OBJECT_DIRECTORIES GIT_CEILING_DIRECTORIES GIT_PREFIX \
    GIT_CONFIG GIT_CONFIG_PARAMETERS GIT_CONFIG_COUNT 2>/dev/null || true
tmp=$(mktemp -d "${TMPDIR:-/tmp}/fixture-guard-test.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
export HOME="$tmp/home" GIT_CONFIG_NOSYSTEM=1
mkdir -p "$HOME"
git config --global user.name 'Fixture Guard Test'
git config --global user.email 'test@example.invalid'
git config --global init.defaultBranch main
git config --global commit.gpgsign false
git config --global core.autocrlf false

failures=0
passes=0
pass() {
    passes=$((passes + 1))
    echo "ok   $1"
}
fail() {
    failures=$((failures + 1))
    echo "FAIL $1"
    [ -f "$tmp/out" ] && tail -n 30 "$tmp/out" | sed 's/^/     | /'
}

# expect ok|refused <name> <command...>; output goes to $tmp/out.
expect() {
    want=$1
    name=$2
    shift 2
    if "$@" >"$tmp/out" 2>&1; then got=ok; else got=refused; fi
    if [ "$got" = "$want" ]; then pass "$name"; else fail "$name (got $got, wanted $want)"; fi
}
# The last output mentions <text>.
mentions() {
    if grep -F -q -- "$2" "$tmp/out"; then pass "$1"; else fail "$1: output lacks '$2'"; fi
}
head_is() {
    if [ "$(git rev-parse HEAD)" = "$2" ]; then pass "$1"; else fail "$1: HEAD moved"; fi
}

FIX=toonz/sources/tests/fixtures/reference_project
GOLD=toonz/sources/tests/golden/expected
HASHES=toonz/sources/tests/core/test_vector_noqt_hashes_linux.inc

repo=$tmp/repo
git init -q "$repo"
cd "$repo" || exit 2

have_lfs=no
if command -v git-lfs >/dev/null 2>&1; then
    have_lfs=yes
    git lfs install --local >/dev/null
fi

# The repository before the guard: some fixtures, and a commit to put a
# worktree on later.
mkdir -p $FIX/scenes $GOLD/basic toonz/sources/tests/core
echo scene >$FIX/scenes/basic.tnz
echo png >$GOLD/basic/0001.png
echo '{"a", 1},' >$HASHES
echo code >code.cpp
git add -A
git commit -q -m 'Before the guard'
before_guard=$(git rev-parse HEAD)

mkdir -p .githooks tools
cp "$src/.githooks/pre-commit" "$src/.githooks/commit-msg" \
    "$src/.githooks/fixture-guard.sh" .githooks/
cp "$src/tools/setup-hooks.sh" tools/
git add -A
git commit -q -m 'Add the guard'

# --- setup ---------------------------------------------------------------
hooks=$(git rev-parse --git-common-dir)/hooks
lfs_before=$(cat "$hooks/pre-push" "$hooks/post-commit" 2>/dev/null)
expect ok 'setup installs' sh tools/setup-hooks.sh
mentions 'setup reports installing' 'installed'
cp "$hooks/pre-commit" "$tmp/shim1"
expect ok 'setup again' sh tools/setup-hooks.sh
mentions 'setup again changes nothing' 'up to date'
if cmp -s "$tmp/shim1" "$hooks/pre-commit"; then pass 'shim unchanged by rerun'; else fail 'shim changed by rerun'; fi
if [ -z "$(git config --get core.hooksPath)" ]; then pass 'core.hooksPath left unset'; else fail 'core.hooksPath was set'; fi
if [ "$have_lfs" = yes ]; then
    if [ "$(cat "$hooks/pre-push" "$hooks/post-commit")" = "$lfs_before" ]; then
        pass 'Git LFS hooks untouched'
    else
        fail 'Git LFS hooks changed'
    fi
fi

echo foreign >"$hooks/commit-msg.keep"
mv "$hooks/commit-msg" "$hooks/commit-msg.ours"
printf '#!/bin/sh\nexit 0\n' >"$hooks/commit-msg"
expect refused 'setup refuses to overwrite a foreign hook' sh tools/setup-hooks.sh
mv "$hooks/commit-msg.ours" "$hooks/commit-msg"
rm -f "$hooks/commit-msg.keep"

git config core.hooksPath .githooks
expect refused 'setup refuses when core.hooksPath is set' sh tools/setup-hooks.sh
git config --unset core.hooksPath

# --- ordinary commits ------------------------------------------------------
echo more >>code.cpp
git add code.cpp
expect ok 'ordinary commit' git commit -q -m 'Code'
if [ "$have_lfs" = yes ]; then
    echo again >>code.cpp
    git add code.cpp
    GIT_TRACE=1 git commit -q -m 'Code with trace' >"$tmp/out" 2>&1
    mentions 'Git LFS post-commit hook still runs' 'lfs post-commit'
    mentions 'the guard shim runs too' 'hooks/commit-msg'
fi

# --- protected paths -------------------------------------------------------
h=$(git rev-parse HEAD)
echo changed >>$FIX/scenes/basic.tnz
git add $FIX/scenes/basic.tnz
expect refused 'fixture change without trailer' git commit -q -m 'Regenerate'
mentions 'refusal lists the file' "$FIX/scenes/basic.tnz"
mentions 'refusal names the trailer' 'Fixtures-Update: <reason>'
head_is 'nothing committed' "$h"
expect refused 'trailer in the subject does not count' git commit -q -m 'Fixtures-Update: x'
expect refused 'trailer without a reason' git commit -q -m 'Regenerate

Fixtures-Update:'
expect refused 'trailer in a comment does not count' git commit -q --cleanup=strip -m 'Regenerate

# Fixtures-Update: x'
expect ok 'fixture change with trailer' git commit -q -m 'Regenerate basic

The scene gains a column.
Fixtures-Update: the scene gains a column
Co-Authored-By: Someone <someone@example.invalid>'
expect ok 'trailer key in another case' sh -c "echo x >>$FIX/scenes/basic.tnz && git add -A && git commit -q -m 'R

fixtures-update: lower case'"

echo changed >$GOLD/basic/0001.png
git add $GOLD
expect refused 'golden change without trailer' git commit -q -m 'New goldens'
git reset -q --hard
echo '{"b", 2},' >>$HASHES
git add $HASHES
expect refused 'checksum table change without trailer' git commit -q -m 'New sums'
git reset -q --hard

git rm -q $GOLD/basic/0001.png
expect refused 'deleting a golden needs the trailer too' git commit -q -m 'Drop'
git reset -q --hard

echo changed >>$FIX/scenes/basic.tnz
expect refused 'commit -a (temporary index) is checked' git commit -q -a -m 'All'
expect refused 'commit <path> (temporary index) is checked' git commit -q -m 'Path' -- $FIX/scenes/basic.tnz
git reset -q --hard

mkdir -p "$FIX/scenes/sub dir"
echo x >"$FIX/scenes/sub dir/odd name.tnz"
git add -A
expect refused 'file name with spaces is caught' git commit -q -m 'Odd'
mentions 'and listed whole' 'sub dir/odd name.tnz'
git reset -q --hard
rm -rf "$FIX/scenes/sub dir"

# The editor path: the message has the usual comment block.
echo changed >>$FIX/scenes/basic.tnz
git add -A
cat >"$tmp/editor" <<'EOF'
#!/bin/sh
{ printf 'Edited\n\nFixtures-Update: from the editor\n'; cat "$1"; } >"$1.new" && mv "$1.new" "$1"
EOF
chmod +x "$tmp/editor"
expect ok 'trailer typed in the editor (commit -v)' env GIT_EDITOR="$tmp/editor" git commit -q -v
cat >"$tmp/editor2" <<'EOF'
#!/bin/sh
{ printf 'Edited\n\n'; cat "$1"; printf 'Fixtures-Update: below the scissors\n'; } >"$1.new" && mv "$1.new" "$1"
EOF
chmod +x "$tmp/editor2"
echo again >>$FIX/scenes/basic.tnz
git add -A
expect refused 'trailer below the scissors line does not count' env GIT_EDITOR="$tmp/editor2" git commit -q -v
git reset -q --hard

# --- artifacts ---------------------------------------------------------------
for f in "$FIX/scenes/basic.tnz.bak" "$FIX/scenes/basic.tnz.bak2" \
    "$FIX/scenes/sceneIcons/basic .png" "$FIX/.DS_Store" "$GOLD/basic/.DS_Store"; do
    mkdir -p "$(dirname "$f")"
    echo junk >"$f"
    git add -A
    expect refused "artifact refused: ${f#toonz/sources/tests/}" git commit -q -m 'Junk

Fixtures-Update: even with the trailer'
    mentions "artifact listed: ${f#toonz/sources/tests/}" "$f"
    git reset -q --hard
    git clean -q -fd
done
echo junk >.DS_Store
git add -A
expect ok '.DS_Store outside the fixtures is not our business' git commit -q -m 'Root junk'

# --- a worktree whose branch predates the guard ------------------------------
git worktree add -q "$tmp/old" "$before_guard" 2>/dev/null
if (
    cd "$tmp/old" || exit 1
    echo changed >>$FIX/scenes/basic.tnz
    git add -A
    git commit -q -m 'Old branch'
) >"$tmp/out" 2>&1; then pass 'worktree without .githooks commits (shim stays quiet)'; else fail 'worktree without .githooks'; fi
if [ "$have_lfs" = yes ]; then
    (cd "$tmp/old" && echo x >y && git add y && GIT_TRACE=1 git commit -q -m y) >"$tmp/out" 2>&1
    mentions 'Git LFS hooks run in that worktree' 'lfs post-commit'
fi
git worktree remove --force "$tmp/old"

# --- range (CI) ----------------------------------------------------------------
guard() { sh .githooks/fixture-guard.sh "$@"; }
base=$(git rev-parse HEAD)
echo a >>code.cpp && git commit -q -am 'Plain'
echo b >>$FIX/scenes/basic.tnz && git commit -q -am 'Marked

Fixtures-Update: test' 2>/dev/null
expect ok 'range: marked fixture commit passes' guard range HEAD --not "$base"
mentions 'range: reports it as intended' 'marked as intended'
echo c >>$FIX/scenes/basic.tnz && git commit -q --no-verify -am 'Sneaky'
expect refused 'range: --no-verify fixture commit fails' guard range HEAD --not "$base"
mentions 'range: names the commit' 'Sneaky'
mentions 'range: lists the file' "$FIX/scenes/basic.tnz"
expect ok 'range: the label approves it' env FIXTURE_GUARD_APPROVED=1 sh .githooks/fixture-guard.sh range HEAD --not "$base"
base=$(git rev-parse HEAD)
echo junk >$FIX/scenes/basic.tnz.bak && git add -A && git commit -q --no-verify -m 'Junk

Fixtures-Update: no'
expect refused 'range: artifact fails even with trailer and label' env FIXTURE_GUARD_APPROVED=1 sh .githooks/fixture-guard.sh range HEAD --not "$base"
expect refused 'tree: artifact in the tree fails' guard tree HEAD
git rm -q $FIX/scenes/basic.tnz.bak
git commit -q -m 'Remove junk

Fixtures-Update: remove the backup' 2>/dev/null
expect ok 'tree: clean tree passes' guard tree HEAD

# Merges: a trailer on the merge covers what it brings in.
main=$(git rev-parse HEAD)
git checkout -q -b topic
echo d >>$GOLD/basic/0001.png && git commit -q --no-verify -am 'Topic goldens'
git checkout -q main
echo e >>code.cpp && git commit -q -am 'Main moves'
expect refused 'merge without trailer is refused by commit-msg' git merge -q --no-ff topic -m 'Merge topic'
git merge --abort 2>/dev/null || git reset -q --hard
expect ok 'merge with trailer' git merge -q --no-ff topic -m 'Merge topic

Fixtures-Update: goldens of topic'
expect ok 'range: merge with trailer covers the merged commit' guard range HEAD --not "$main"
git reset -q --hard HEAD^
git merge -q --no-ff --no-verify topic -m 'Merge topic'
expect refused 'range: merge without trailer does not' guard range HEAD --not "$main"
mentions 'range: names the merged commit' 'Topic goldens'

# --- uninstall -------------------------------------------------------------
expect ok 'uninstall' sh tools/setup-hooks.sh --uninstall
if [ ! -e "$hooks/pre-commit" ] && [ ! -e "$hooks/commit-msg" ]; then pass 'shims removed'; else fail 'shims left'; fi
if [ "$have_lfs" = yes ] && grep -q 'git lfs' "$hooks/pre-push"; then pass 'LFS hooks kept by uninstall'; fi

echo
echo "$passes passed, $failures failed (git-lfs: $have_lfs)"
[ "$failures" -eq 0 ]
