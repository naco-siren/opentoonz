#!/bin/bash
# Runs inside the nt-asan VM: builds ab_offscreen.cpp against the libraries
# and headers of the build directory $B (default build-asan) and the
# sources $SRC, and writes its pictures to /var/tmp/ab/<tag>/offscreen.
# With RASTERIZER=skia in the environment the program draws on the CPU.
set -uo pipefail
tag=$1
here=${NT_MAIN:?set NT_MAIN to the main checkout}/toonz/build-phase1/vm
src=${SRC:-$HOME/opentoonz/toonz/sources}
b=${B:-$HOME/opentoonz/toonz/build-asan}
out=/var/tmp/ab/$tag/offscreen
export ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1
libdir=$(dirname $(find $b -name "libtnzcore.so" | head -1))
cxx=$(grep -m1 "CMAKE_CXX_COMPILER:" $b/CMakeCache.txt | cut -d= -f2)
gtest=$(dirname $(dirname $(find $HOME/opentoonz/toonz/build-asan/_deps /usr/src -name gtest-all.cc 2>/dev/null | head -1)))
rm -rf $out; mkdir -p $out /var/tmp/ab/bin
common="-std=gnu++17 -O1 -fsanitize=address,undefined -fno-sanitize=vptr -DLINUX -Dx64 -DTNZ_LITTLE_ENDIAN=1 -fPIC $(pkg-config --cflags Qt5Gui Qt5Core Qt5OpenGL) -I $src/include"
$cxx $common -DNEXTTOONZ_FIXTURE_DIR=\"$src/tests/fixtures/reference_project\" -DNEXTTOONZ_STUFF_DIR=\"$HOME/opentoonz/stuff\" \
    -I $here/toonzlib_main -I $gtest/include -I $gtest $here/ab_offscreen.cpp $gtest/src/gtest-all.cc \
    -L$libdir -ltoonzlib -limage -ltnzstdfx -lcolorfx -ltnzext -ltnzbase -ltnzcore $(pkg-config --libs Qt5Gui Qt5Core) -lGL -lpthread \
    -Wl,-rpath,$libdir -o /var/tmp/ab/bin/ab_offscreen_$tag 2>&1 | grep -E "error|undefined" -A3 | head -20
env ${RASTERIZER:+NEXTTOONZ_VECTOR_RASTERIZER=$RASTERIZER} QT_QPA_PLATFORM=xcb LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a /var/tmp/ab/bin/ab_offscreen_$tag $out 2>&1 | grep -E "done|ERROR|runtime error|no raster" | tail -4
echo "offscreen: $(ls $out | wc -l) pictures"
