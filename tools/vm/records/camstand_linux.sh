#!/bin/bash
# Runs inside the nt-asan VM: builds ../ab/ab_camstand.cpp (camera-stand
# renders, ToonzScene::renderFrame) against the libraries of the build
# directory $B (default build-asan) and writes its pictures to
# /var/tmp/ab/<tag>/camstand. With RASTERIZER=skia in the environment the
# frames are drawn on the CPU.
set -uo pipefail
tag=$1
here=${NT_MAIN:?set NT_MAIN to the main checkout}/toonz/build-phase1/vm
src=${SRC:-$HOME/opentoonz/toonz/sources}
b=${B:-$HOME/opentoonz/toonz/build-asan}
out=/var/tmp/ab/$tag/camstand
export ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=${UBSAN_HALT:-1}
libdir=$(dirname $(find $b -name "libtnzcore.so" | head -1))
cxx=$(grep -m1 "CMAKE_CXX_COMPILER:" $b/CMakeCache.txt | cut -d= -f2)
gtest=$(dirname $(dirname $(find $HOME/opentoonz/toonz/build-asan/_deps /usr/src -name gtest-all.cc 2>/dev/null | head -1)))
rm -rf $out; mkdir -p $out /var/tmp/ab/bin
# .tlv levels are read by the lzo helpers next to the executable
for h in lzocompress lzodecompress; do ln -sf $(find $b $HOME/opentoonz/toonz/build-asan -name $h -type f | head -1) /var/tmp/ab/bin/$h; done
common="-std=gnu++17 -O1 -fsanitize=address,undefined -fno-sanitize=vptr -DLINUX -Dx64 -DTNZ_LITTLE_ENDIAN=1 -fPIC $(pkg-config --cflags Qt5Gui Qt5Core Qt5OpenGL) -I $src/include"
$cxx $common -DNEXTTOONZ_FIXTURE_DIR=\"$src/tests/fixtures/reference_project\" -DNEXTTOONZ_STUFF_DIR=\"$HOME/opentoonz/stuff\" \
    -I $here/toonzlib_main -I $gtest/include -I $gtest $here/../ab/ab_camstand.cpp $gtest/src/gtest-all.cc \
    -L$libdir -ltoonzlib -limage -ltnzstdfx -lcolorfx -ltnzext -ltnzbase -ltnzcore $(pkg-config --libs Qt5Gui Qt5Core) -lGL -lpthread \
    -Wl,-rpath,$libdir -o /var/tmp/ab/bin/ab_camstand_$tag 2>&1 | grep -E "error|undefined" -A3 | head -20
env ${RASTERIZER:+NEXTTOONZ_VECTOR_RASTERIZER=$RASTERIZER} QT_QPA_PLATFORM=xcb LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a /var/tmp/ab/bin/ab_camstand_$tag $out 2>&1 | grep -E "pixels drawn|ERROR|runtime error" | tail -30
echo "camstand: $(ls $out | wc -l) pictures"
