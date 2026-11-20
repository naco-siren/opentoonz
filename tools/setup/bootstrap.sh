#!/usr/bin/env bash
# Creates the machine-local root local/ (doc/nexttoonz_layout.md 4.8) and
# fills local/cache with what a macOS build and the test scripts need:
#
#   cache/venv/                         Python venv with numpy and Pillow (golden scripts, A/B tools)
#   cache/skia-pack/<release>/<arch>/   JetBrains skia-pack, via ci-scripts/skia/skia_pack.sh
#   cache/vcpkg/  cache/vcpkg-bincache/ vcpkg at the pinned commit (Linux-style Skia builds)
#   cache/opencv/4.10-static-x64/       static OpenCV 4.10 (only with --opencv: about 4 minutes)
#
# With --tiff it also builds the bundled libtiff inside this checkout
# (thirdparty/tiff-4.0.3, as CI does), which every new worktree needs before
# its first configure until layout step 12.
#
# Everything already present is left alone, so it is safe to run again; an
# entry that is a symlink (a transitional name pointing at an old place) is
# also left alone.
#
# Usage: tools/setup/bootstrap.sh [--root DIR] [--skia ARCHS] [--no-vcpkg] [--opencv] [--tiff] [--check]
#   --root DIR    the local root (default: $NEXTTOONZ_LOCAL, else local/ in the main checkout,
#                 found through git's common dir, so every worktree uses the same one)
#   --skia ARCHS  comma-separated skia_pack.sh archs (default: the host's, plus iosSim-x64
#                 on an Intel Mac); "none" to skip
#   --no-vcpkg    skip vcpkg
#   --opencv      also build the static OpenCV
#   --tiff        also build the bundled libtiff in this checkout
#   --check       only report what is there and what is missing
#
# At the end it prints the CMake arguments that point a build at the cache.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
src=$(cd "$here/../.." && pwd)

skia_release=m144-e2e6623374-4
vcpkg_commit=ec48c11eae9448c8c6e7739d93e06fa2433aa95f
opencv_version=4.10.0
opencv_sha256=b2171af5be6b26f7a06b1229948bbb2bdaa74fcf5cd097e0af6378fce50a6eb9
venv_packages="numpy==2.5.4 pillow==13.0.0"

root=${NEXTTOONZ_LOCAL:-}
skia_archs=default
want_vcpkg=1
want_opencv=0
want_tiff=0
check_only=0
while [ $# -gt 0 ]; do
    case "$1" in
        --root) root=$2; shift 2 ;;
        --skia) skia_archs=$2; shift 2 ;;
        --no-vcpkg) want_vcpkg=0; shift ;;
        --opencv) want_opencv=1; shift ;;
        --tiff) want_tiff=1; shift ;;
        --check) check_only=1; shift ;;
        -h|--help) sed -n '2,28p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

if [ -z "$root" ]; then
    common=$(git -C "$src" rev-parse --path-format=absolute --git-common-dir)
    root="$(dirname "$common")/local"
fi
mkdir -p "$root"
root=$(cd "$root" && pwd)
cache=$root/cache
echo "local root: $root"

if [ "$skia_archs" = default ]; then
    case "$(uname -s)-$(uname -m)" in
        Darwin-x86_64) skia_archs=x64,iosSim-x64 ;;
        Darwin-arm64)  skia_archs=arm64 ;;
        *)             skia_archs=none ;;   # Linux gets Skia from vcpkg (ci-scripts/skia/vcpkg_skia.sh)
    esac
fi

status() { printf '  %-34s %s\n' "$1" "$2"; }
present() { [ -e "$1" ] || [ -L "$1" ]; }
skia_old_name() { case "$1" in x64) echo macos-Release-x64 ;; arm64) echo macos-Release-arm64 ;; iosSim-x64) echo iosSim-Release-x64 ;; esac; }

# --- tools on PATH ------------------------------------------------------------
echo "tools:"
# The venv in use since Phase 0 was made with Homebrew's Python 3.14; the pinned
# numpy needs 3.11 or later, so Apple's /usr/bin/python3 (3.9) does not do.
python=${PYTHON:-}
if [ -z "$python" ]; then
    for p in python3.14 python3.13 python3.12 python3.11 python3; do
        if command -v "$p" >/dev/null && "$p" -c 'import sys; sys.exit(sys.version_info < (3, 11))'; then
            python=$p; break
        fi
    done
fi
[ -n "$python" ] || { echo "no Python 3.11 or later on PATH (set PYTHON)" >&2; exit 1; }
status "python3" "$(command -v "$python" || echo missing) $("$python" --version 2>&1 || true)"
status "clang" "$(command -v clang || echo missing)"
case "$(command -v clang || true)" in
    */llvm*/bin/clang) echo "  warning: Homebrew llvm's clang is first on PATH; the macOS build expects Apple clang (/usr/bin first)" ;;
esac
for t in cmake ninja git curl unzip shasum; do
    command -v "$t" >/dev/null || echo "  missing: $t"
done

# --- report only ----------------------------------------------------------------
if [ "$check_only" = 1 ]; then
    echo "cache:"
    for e in venv/bin/python vcpkg/vcpkg vcpkg-bincache "opencv/4.10-static-x64/lib/cmake/opencv4"; do
        if present "$cache/$e"; then status "$e" ok; else status "$e" missing; fi
    done
    for a in ${skia_archs//,/ }; do
        [ "$a" = none ] && continue
        if ls "$cache/skia-pack/$skia_release/$a"/*/.unpacked >/dev/null 2>&1 \
           || ls "$cache/skia-pack/$skia_release/$(skia_old_name "$a")"/out/*/libskia.a >/dev/null 2>&1; then
            status "skia-pack $a" ok
        else
            status "skia-pack $a" missing
        fi
    done
    exit 0
fi

mkdir -p "$cache" "$root/snapshots" "$root/runs" "$root/scratch"

# --- venv -------------------------------------------------------------------------
echo "venv:"
if present "$cache/venv"; then
    status venv "present"
else
    # A venv records its own path, so it is made in place; a failed install
    # removes it rather than leave a half-made one that counts as present.
    "$python" -m venv "$cache/venv"
    "$cache/venv/bin/python" -m pip install -q --disable-pip-version-check $venv_packages \
        || { rm -rf "$cache/venv"; exit 1; }
    status venv "created ($venv_packages)"
fi

# --- skia-pack --------------------------------------------------------------------
echo "skia-pack $skia_release:"
skia_script=$src/ci-scripts/skia/skia_pack.sh
[ -f "$skia_script" ] || skia_script=$src/tools/ci/skia/skia_pack.sh
skia_dirs=()
for a in ${skia_archs//,/ }; do
    [ "$a" = none ] && continue
    d=$cache/skia-pack/$skia_release/$a
    if [ -L "$cache/skia-pack/$skia_release" ]; then
        # The hand-made layout before layout step 0: <platform>-Release-<cpu>/ directly.
        old=$cache/skia-pack/$skia_release/$(skia_old_name "$a")
        if [ -d "$old" ]; then skia_dirs+=("$a=$old"); status "$a" "$old (transitional link)"
        else status "$a" "missing under the transitional link"; fi
        continue
    fi
    out=$(bash "$skia_script" "$a" "$d" | tail -1)
    skia_dirs+=("$a=$out")
    status "$a" "$out"
done

# --- vcpkg ------------------------------------------------------------------------
if [ "$want_vcpkg" = 1 ]; then
    echo "vcpkg:"
    if present "$cache/vcpkg"; then
        status vcpkg "present"
    else
        git init -q "$cache/vcpkg"
        git -C "$cache/vcpkg" remote add origin https://github.com/microsoft/vcpkg.git
        git -C "$cache/vcpkg" fetch -q --depth 1 origin "$vcpkg_commit"
        git -C "$cache/vcpkg" checkout -q --detach FETCH_HEAD
        (cd "$cache/vcpkg" && ./bootstrap-vcpkg.sh -disableMetrics > "$cache/vcpkg-bootstrap.log" 2>&1)
        status vcpkg "checked out at ${vcpkg_commit:0:8}, bootstrapped"
    fi
    present "$cache/vcpkg-bincache" || mkdir -p "$cache/vcpkg-bincache"
    status vcpkg-bincache "$cache/vcpkg-bincache (export VCPKG_DEFAULT_BINARY_CACHE)"
fi

# --- OpenCV -----------------------------------------------------------------------
# The configuration of the build made by hand on 2026-11-04 (its CMakeCache), so
# the result links the same eight static libraries.
opencv_dir=$cache/opencv/4.10-static-x64
if [ "$want_opencv" = 1 ]; then
    echo "opencv:"
    if present "$opencv_dir"; then
        status opencv "present"
    else
        work=$root/scratch/opencv-$opencv_version-build
        mkdir -p "$work"
        tgz=$work/opencv-$opencv_version.tar.gz
        [ -f "$tgz" ] || curl -fsSL --retry 3 -o "$tgz" \
            "https://github.com/opencv/opencv/archive/refs/tags/$opencv_version.tar.gz"
        echo "$opencv_sha256  $tgz" | shasum -a 256 -c -
        tar -xzf "$tgz" -C "$work"
        cmake -S "$work/opencv-$opencv_version" -B "$work/build" -G Ninja \
            -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
            -DCMAKE_INSTALL_PREFIX="$opencv_dir" \
            -DBUILD_LIST=core,imgproc,imgcodecs,videoio,calib3d,features2d,flann,video \
            -DBUILD_TESTS=OFF -DBUILD_PERF_TESTS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_DOCS=OFF \
            -DBUILD_opencv_apps=OFF -DBUILD_JAVA=OFF -DBUILD_opencv_python2=OFF -DBUILD_opencv_python3=OFF \
            -DWITH_FFMPEG=OFF -DWITH_OPENEXR=OFF -DWITH_TIFF=OFF -DWITH_WEBP=OFF -DWITH_OPENJPEG=OFF \
            -DWITH_JASPER=OFF -DWITH_IPP=OFF -DWITH_OPENCL=OFF -DWITH_PROTOBUF=OFF -DWITH_ADE=OFF \
            -DWITH_EIGEN=OFF -DWITH_LAPACK=OFF -DWITH_1394=OFF -DWITH_GSTREAMER=OFF -DWITH_ITT=OFF \
            -DWITH_AVIF=OFF -DWITH_QUIRC=OFF -DOPENCV_GENERATE_PKGCONFIG=OFF \
            > "$work/configure.log" 2>&1
        cmake --build "$work/build" > "$work/build.log" 2>&1
        cmake --install "$work/build" > "$work/install.log" 2>&1
        rm -rf "$work"   # only the install is kept (4.8)
        status opencv "built and installed"
    fi
fi

# --- bundled libtiff, in this checkout -----------------------------------------------
if [ "$want_tiff" = 1 ]; then
    echo "libtiff:"
    tiff=$src/thirdparty/tiff-4.0.3
    if [ -f "$tiff/libtiff/.libs/libtiff.a" ]; then
        status libtiff "present in $tiff"
    else
        (cd "$tiff" && CFLAGS='-fPIC' CXXFLAGS='-fPIC' ./configure --disable-jbig > config.log.bootstrap 2>&1 \
            && make -j "$(sysctl -n hw.ncpu 2>/dev/null || nproc)" > make.log.bootstrap 2>&1) \
            || { echo "libtiff build failed; see $tiff/*.log.bootstrap" >&2; exit 1; }
        status libtiff "built in $tiff"
    fi
fi

# --- what to pass to CMake ------------------------------------------------------------
echo
echo "CMake arguments for a build using this cache:"
echo "  -DPython3_EXECUTABLE=$cache/venv/bin/python"
present "$opencv_dir" && echo "  -DOpenCV_DIR=$opencv_dir/lib/cmake/opencv4"
for e in ${skia_dirs[@]+"${skia_dirs[@]}"}; do
    case "$e" in
        x64=*|arm64=*) echo "  -DWITH_SKIA=ON -DNEXTTOONZ_SKIA_PACK_DIR=${e#*=}" ;;
        iosSim-x64=*)  echo "  (iPad simulator tests: tests/core/run_ios_noqt.sh with ${e#*=})" ;;
    esac
done
