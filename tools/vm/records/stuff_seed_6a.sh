#!/bin/bash
# Runs inside the nt-asan VM for step 6a of doc/nexttoonz_core_noqt.md: what
# tcomposer leaves in a fresh home when it initializes the stuff folder
# (TEnv::initUserStuffDir, Linux only), with the binaries saved under
# ~/ab6a/<tag>/{bin,lib}. Four runs, each recorded as a listing of the home
# (names, kinds, modes, sizes, contents) plus SystemVar.ini byte for byte:
#   1 first run: no ~/.config/OpenToonz
#   2 second run: everything already there
#   3 the stuff folder removed, the ini left
#   4 portable: started from a folder that holds portablestuff
# Usage: stuff_seed_6a.sh <tag>; then diff -r ~/ab6a/seed_before ~/ab6a/seed_after
set -uo pipefail
tag=$1
base=~/ab6a/$tag
out=~/ab6a/seed_$tag
rm -rf "$out" "$base/prefix"
mkdir -p "$out" "$base/prefix/share/opentoonz"
cp -a "$base/bin" "$base/lib" "$base/prefix/"
cp -a ~/opentoonz/stuff "$base/prefix/share/opentoonz/stuff"
scene_src=~/opentoonz/toonz/sources/tests/fixtures/reference_project
export ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 QT_QPA_PLATFORM=offscreen

snapshot() {  # <dir> <file>
  (cd "$1" && find . -mindepth 1 | LC_ALL=C sort | while read -r p; do
     if [ -L "$p" ]; then echo "$p link $(readlink "$p")"
     elif [ -d "$p" ]; then echo "$p dir $(stat -c %a "$p")"
     else echo "$p file $(stat -c '%a %s' "$p") $(sha256sum < "$p" | cut -c1-16)"; fi
   done) > "$2"
}

run() {  # <case> <cwd>
  local c=$1 cwd=$2
  mkdir -p "$out/$c/frames"
  (cd "$cwd" && HOME=$home "$base/prefix/bin/tcomposer" \
      "$work/project/scenes/vector_basic.tnz" -o "$out/$c/frames/v.png" \
      -nthreads 1 > "$out/$c/stdout.txt" 2>&1; echo "rc=$?" >> "$out/$c/stdout.txt")
  sed -i "s|$home|<home>|g; s|$work|<work>|g; s|$base|<base>|g" "$out/$c/stdout.txt"
  snapshot "$home" "$out/$c/home.txt"
  ini=$home/.config/OpenToonz/SystemVar.ini
  if [ -f "$ini" ]; then cp "$ini" "$out/$c/SystemVar.ini"; fi
  snapshot "$out/$c/frames" "$out/$c/frames.txt"
  rm -rf "$out/$c/frames"
}

# a fixed path, so that the two runs write the same bytes into the ini
work=/var/tmp/seed6a_work
rm -rf "$work"; mkdir -p "$work"
home=$work/home
mkdir -p "$home"
cp -a "$scene_src" "$work/project"
run 1-first "$work"
run 2-second "$work"
rm -rf "$home/.config/OpenToonz/stuff"
run 3-stuff-removed "$work"
# portable: a fresh home, started from a folder holding portablestuff
rm -rf "$home"; mkdir -p "$home" "$work/portable"
cp -a ~/opentoonz/stuff "$work/portable/portablestuff"
run 4-portable "$work/portable"
snapshot "$work/portable/portablestuff" "$out/4-portable/portablestuff.txt"
rm -rf "$work"
echo "recorded $out"
