#!/usr/bin/env bash
# Fetches the prebuilt Skia of JetBrains skia-pack for macOS (or the iPad
# simulator), checks its SHA-256 and unpacks it. Configure the tree with -DWITH_SKIA=ON
# -DNEXTTOONZ_SKIA_PACK_DIR=<the directory this prints on its last line>.
#
# This is the way macOS gets Skia (doc/nexttoonz_phase1_skia.md, sections 1
# and 7): no build, static libskia.a with RTTI, no OpenGL. The golden
# baselines of the vector scenes were rendered with the x64 package below.
#
# Usage: skia_pack.sh <arm64|x64|iosSim-x64> <directory>
#   iosSim-x64 is for the core tests without Qt in the iPad simulator on an
#   Intel Mac (tests/core/run_ios_noqt.sh, doc/nexttoonz_core_noqt.md 8d).
#   The zip is kept in <directory> and downloaded only when missing, so CI
#   caches <directory> (keyed on this file). The zip is checked against the
#   pinned SHA-256 every time, cached or not.

set -euo pipefail

if [ $# -ne 2 ]; then
    echo "usage: $0 <arm64|x64|iosSim-x64> <directory>" >&2
    exit 2
fi
arch="$1"
dir="$2"

release=m144-e2e6623374-4
# The digests GitHub records for the release assets (iosSim-x64: the digest
# of the zip downloaded with the x64 one on 2026-11-08, whose digest is the
# one GitHub records; not compared with GitHub's record of its own).
case "$arch" in
    x64)   sha256=077513011356f9d96c596def3c5b292e43c705dc6902812daa82bc147303a9f4
           name="Skia-$release-macos-Release-x64" ;;
    arm64) sha256=5ec2f81f28bf79ce4b9fd744dd183b3f718cb72a742a8465ae44f395a7bfb411
           name="Skia-$release-macos-Release-arm64" ;;
    iosSim-x64)
           sha256=fe4a092491720c1c8332c0e96aaec7c12174ed191fb32341a4d0926d325b045b
           name="Skia-$release-iosSim-Release-x64" ;;
    *) echo "unknown arch '$arch' (arm64, x64 or iosSim-x64)" >&2; exit 2 ;;
esac
url="https://github.com/JetBrains/skia-pack/releases/download/$release/$name.zip"

mkdir -p "$dir"
zip="$dir/$name.zip"
if [ ! -f "$zip" ]; then
    curl -fsSL --retry 3 -o "$zip.part" "$url"
    mv "$zip.part" "$zip"
fi
echo "$sha256  $zip" | shasum -a 256 -c -

unpacked="$dir/$name"
if [ ! -f "$unpacked/.unpacked" ]; then
    rm -rf "$unpacked"
    # Only libskia.a is linked; the other twenty-odd libraries of the
    # package (icu, harfbuzz, skottie...) are left in the zip.
    unzip -q "$zip" -x 'out/*' -d "$unpacked"
    unzip -q "$zip" 'out/*/libskia.a' -d "$unpacked"
    touch "$unpacked/.unpacked"
fi
ls "$unpacked"/out/*/libskia.a >/dev/null
echo "$unpacked"
