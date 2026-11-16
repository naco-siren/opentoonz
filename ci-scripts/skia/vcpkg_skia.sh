#!/usr/bin/env bash
# Builds Skia from source through vcpkg, as pinned by vcpkg.json next to this
# script, and installs it under <install root>/<triplet>. Configure the tree
# with -DWITH_SKIA=ON
#      -Dunofficial-skia_DIR=<install root>/<triplet>/share/unofficial-skia
# rather than putting <install root>/<triplet> on CMAKE_PREFIX_PATH: the
# install tree also holds vcpkg's static zlib and expat, which find_package()
# would then prefer to the system's for the rest of the build.
#
# This is the way Linux gets Skia: the prebuilt skia-pack for Linux is built
# with the old libstdc++ ABI (doc/nexttoonz_phase1_skia.md, section 2).
# Only the CPU rasteriser is built (default-features: false). The vcpkg
# toolchain file is not used either: it would send every find_package() of
# the build to vcpkg.
#
# Usage: vcpkg_skia.sh <install root> [triplet]
#   triplet      defaults to x64-linux-release; the -release triplets skip the
#                debug build of every package.
#   NEXTTOONZ_VCPKG_ROOT  where vcpkg is cloned (default: $RUNNER_TEMP/vcpkg, or
#                ~/.cache/nexttoonz-vcpkg outside GitHub Actions). Not VCPKG_ROOT,
#                which runner images may already set to their own vcpkg.
#   CC, CXX      the compilers vcpkg builds with, as for any CMake project.
#
# CI caches the install root (keyed on this directory and the compiler
# version), not vcpkg's binary cache, so that a cache hit needs neither the
# clone nor the bootstrap. Needs git, curl, zip, unzip, tar, pkg-config,
# ninja and Python 3.10 or newer (pkgconf is built with meson).

set -euo pipefail

if [ $# -lt 1 ]; then
    echo "usage: $0 <install root> [triplet]" >&2
    exit 2
fi

here="$(cd "$(dirname "$0")" && pwd)"
install_root="$1"
triplet="${2:-x64-linux-release}"
if [ -n "${NEXTTOONZ_VCPKG_ROOT:-}" ]; then
    vcpkg_root="$NEXTTOONZ_VCPKG_ROOT"
elif [ -n "${RUNNER_TEMP:-}" ]; then
    vcpkg_root="$RUNNER_TEMP/vcpkg"
else
    vcpkg_root="$HOME/.cache/nexttoonz-vcpkg"
fi

baseline="$(sed -n 's/.*"builtin-baseline": *"\([0-9a-f]*\)".*/\1/p' "$here/vcpkg.json")"
if [ -z "$baseline" ]; then
    echo "no builtin-baseline in $here/vcpkg.json" >&2
    exit 1
fi

# vcpkg has to be at the baseline commit itself: the manifest's baseline pins
# the port versions, the checkout pins the port files and vcpkg's own tools.
if [ "$(git -C "$vcpkg_root" rev-parse HEAD 2>/dev/null || true)" != "$baseline" ]; then
    rm -rf "$vcpkg_root"
    git init -q "$vcpkg_root"
    git -C "$vcpkg_root" remote add origin https://github.com/microsoft/vcpkg.git
    git -C "$vcpkg_root" fetch -q --depth 1 origin "$baseline"
    git -C "$vcpkg_root" checkout -q --detach FETCH_HEAD
fi
if [ ! -x "$vcpkg_root/vcpkg" ]; then
    "$vcpkg_root/bootstrap-vcpkg.sh" -disableMetrics
fi

export VCPKG_DISABLE_METRICS=1
"$vcpkg_root/vcpkg" install \
    --triplet "$triplet" \
    --x-manifest-root="$here" \
    --x-install-root="$install_root" \
    --clean-after-build

test -f "$install_root/$triplet/share/unofficial-skia/unofficial-skia-config.cmake"
echo "Skia installed under $install_root/$triplet"
