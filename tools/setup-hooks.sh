#!/bin/sh
# Installs the git hooks of NextToonz for this clone, once: every worktree of
# the clone gets them. Running it again changes nothing.
#
#   tools/setup-hooks.sh              install (or refresh) the hooks
#   tools/setup-hooks.sh --uninstall  remove them
#
# The hooks themselves are versioned in .githooks/. What goes into the
# clone's hooks directory ($(git rev-parse --git-common-dir)/hooks) is one
# small shim per hook that runs the versioned hook of the worktree being
# committed in, when its branch has one.
#
# core.hooksPath is deliberately left alone: pointing it at .githooks would
# (1) stop git from running the Git LFS hooks in .git/hooks, and (2) be
# resolved inside each worktree, so a worktree on a branch without
# .githooks/ would run no hook at all, LFS included. The shims use hook
# names Git LFS does not use (pre-commit, commit-msg), so its hooks
# (pre-push, post-checkout, post-commit, post-merge) stay as they are.

set -eu

MARKER='nexttoonz-hook-shim'
HOOKS='pre-commit commit-msg'

die() {
    echo "setup-hooks: $*" >&2
    exit 1
}

git rev-parse --git-dir >/dev/null 2>&1 || die "not inside a git repository"

if hooks_path=$(git config --get core.hooksPath); then
    die "core.hooksPath is set to '$hooks_path', so git does not run the hooks
in .git/hooks (the Git LFS ones included). This setup does not use it;
remove it first (git config --unset core.hooksPath, with --global if it is
set there) or install the hooks there by hand."
fi

common=$(git rev-parse --git-common-dir)
dir=$(cd "$common" && pwd)/hooks
mkdir -p "$dir"

shim() {
    cat <<EOF
#!/bin/sh
# $MARKER: installed by tools/setup-hooks.sh, do not edit.
# Runs the versioned hook .githooks/$1 of the worktree being committed in
# (git runs hooks at its top level). A worktree whose branch has no such
# hook commits without it.
[ -f .githooks/$1 ] || exit 0
exec sh .githooks/$1 "\$@"
EOF
}

is_ours() {
    [ -f "$1" ] && grep -q "$MARKER" "$1"
}

if [ "${1:-}" = --uninstall ]; then
    for h in $HOOKS; do
        if is_ours "$dir/$h"; then
            rm -f "$dir/$h"
            echo "setup-hooks: removed $dir/$h"
        fi
    done
    exit 0
fi
[ $# -eq 0 ] || die "usage: $0 [--uninstall]"

# Refuse before writing anything.
for h in $HOOKS; do
    if [ -e "$dir/$h" ] && ! is_ours "$dir/$h"; then
        die "$dir/$h exists and was not installed by this script. Move it
away, or make it also run 'sh .githooks/$h \"\$@\"', and run this again."
    fi
done

for h in $HOOKS; do
    new=$(shim "$h")
    if [ -f "$dir/$h" ] && [ "$(cat "$dir/$h")" = "$new" ]; then
        state='up to date'
    else
        printf '%s\n' "$new" >"$dir/$h"
        state='installed'
    fi
    chmod +x "$dir/$h"
    echo "setup-hooks: $dir/$h $state"
done

# The Git LFS hooks are not ours to install, but say if they are missing.
if command -v git-lfs >/dev/null 2>&1; then
    for h in pre-push post-checkout post-commit post-merge; do
        if ! grep -q 'git lfs' "$dir/$h" 2>/dev/null; then
            echo "setup-hooks: note: no Git LFS hook $h; 'git lfs install' adds it"
        fi
    done
fi
