#!/bin/bash
# Runs inside the nt-asan VM: builds ab_strokes.cpp against the libraries of
# ~/opentoonz/toonz/build-asan as they are now and writes its pictures to
# /var/tmp/ab/<tag>/regions.
set -uo pipefail
tag=$1
here=${NT_MAIN:?set NT_MAIN to the main checkout}/toonz/build-phase1/vm
src=~/opentoonz/toonz/sources
b=${B:-$HOME/opentoonz/toonz/build-asan}
out=/var/tmp/ab/$tag/strokes
export ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1
libdir=$(dirname $(find $b -name "libtnzcore.so" | head -1))
cxx=$(grep -m1 "CMAKE_CXX_COMPILER:" $b/CMakeCache.txt | cut -d= -f2)
rm -rf $out; mkdir -p $out /var/tmp/ab/bin
common="-std=gnu++17 -O1 -fsanitize=address,undefined -fno-sanitize=vptr -DLINUX -Dx64 -DTNZ_LITTLE_ENDIAN=1 -fPIC $(pkg-config --cflags Qt5Gui Qt5Core Qt5OpenGL) -I $src/include"
$cxx $common $here/ab_strokes.cpp -L$libdir -lcolorfx -ltnzcore $(pkg-config --libs Qt5Gui Qt5Core) -lGL -Wl,-rpath,$libdir -o /var/tmp/ab/bin/ab_strokes_$tag 2>&1 | grep -E "error|undefined" -A3 | head -20
QT_QPA_PLATFORM=xcb LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a /var/tmp/ab/bin/ab_strokes_$tag $out 2>&1 | grep -E "pictures|ERROR|runtime error|no style" | tail -4
echo "strokes: $(ls $out | wc -l) pictures"
